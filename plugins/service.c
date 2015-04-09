/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2014  Intel Corporation.
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include <glib.h>
#include <gdbus/gdbus.h>

#include "lib/uuid.h"
#include "src/log.h"
#include "src/plugin.h"
#include "src/dbus-common.h"
#include "src/error.h"
#include "src/adapter.h"
#include "src/device.h"
#include "src/service.h"
#include "src/profile.h"

#define SERVICE_INTERFACE "org.bluez.Service1"

static unsigned int service_id = 0;
static GSList *services = NULL;

struct service_data {
	struct btd_service *service;
	btd_service_state_t state;
	char *path;
	DBusMessage *connect;
	DBusMessage *disconnect;
};

static struct service_data *find_data(struct btd_service *service)
{
	GSList *l;

	for (l = services; l; l = l->next) {
		struct service_data *data = l->data;

		if (data->service == service)
			return data;
	}

	return NULL;
}

static void data_free(void *user_data)
{
	struct service_data *data = user_data;

	if (data->connect)
		dbus_message_unref(data->connect);

	if (data->disconnect)
		dbus_message_unref(data->disconnect);

	g_free(data->path);
	g_free(data);
}

static void data_remove(struct service_data *data)
{
	services = g_slist_remove(services, data);
	g_dbus_unregister_interface(btd_get_dbus_connection(), data->path,
							SERVICE_INTERFACE);
}

static DBusMessage *service_disconnect(DBusConnection *conn, DBusMessage *msg,
								void *user_data)
{
	struct service_data *data = user_data;
	int err;

	if (data->disconnect)
		return btd_error_in_progress(msg);

	data->disconnect = dbus_message_ref(msg);

	err = btd_service_disconnect(data->service);
	if (err == 0)
		return NULL;

	dbus_message_unref(data->disconnect);
	data->disconnect = NULL;

	return btd_error_failed(msg, strerror(-err));
}

static DBusMessage *service_connect(DBusConnection *conn, DBusMessage *msg,
								void *user_data)
{
	struct service_data *data = user_data;
	int err;

	if (data->connect)
		return btd_error_in_progress(msg);

	err = btd_service_connect(data->service);
	if (err < 0)
		return btd_error_failed(msg, strerror(-err));

	data->connect = dbus_message_ref(msg);

	return NULL;
}

static const char *data_get_state(struct service_data *data)
{
	struct btd_service *service = data->service;
	int err;

	data->state = btd_service_get_state(service);

	switch (data->state) {
	case BTD_SERVICE_STATE_UNAVAILABLE:
		return "unavailable";
	case BTD_SERVICE_STATE_DISCONNECTED:
		if (btd_service_is_reconnecting(service))
			return "reconnecting";
		err = btd_service_get_error(data->service);
		return err < 0 ? "error" : "disconnected";
	case BTD_SERVICE_STATE_CONNECTING:
		return btd_service_is_reconnecting(service) ? "reconnecting" :
								"connecting";
	case BTD_SERVICE_STATE_CONNECTED:
		return "connected";
	case BTD_SERVICE_STATE_DISCONNECTING:
		return "disconnecting";
	}

	return "unknown";
}

static gboolean get_device(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct service_data *data = user_data;
	struct btd_device *dev = btd_service_get_device(data->service);
	const char *path = btd_device_get_path(dev);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);

	return TRUE;
}

static gboolean get_state(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct service_data *data = user_data;
	const char *state;

	state = data_get_state(data);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &state);

	return TRUE;
}

static gboolean remote_uuid_exists(const GDBusPropertyTable *property,
								void *user_data)
{
	struct service_data *data = user_data;
	struct btd_profile *p = btd_service_get_profile(data->service);

	return p->remote_uuid != NULL;
}

static gboolean get_remote_uuid(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct service_data *data = user_data;
	struct btd_profile *p = btd_service_get_profile(data->service);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &p->remote_uuid);

	return TRUE;
}


static gboolean local_uuid_exists(const GDBusPropertyTable *property,
								void *user_data)
{
	struct service_data *data = user_data;
	struct btd_profile *p = btd_service_get_profile(data->service);

	return p->local_uuid != NULL;
}

static gboolean get_local_uuid(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct service_data *data = user_data;
	struct btd_profile *p = btd_service_get_profile(data->service);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &p->local_uuid);

	return TRUE;
}

static gboolean version_exists(const GDBusPropertyTable *property,
								void *user_data)
{
	struct service_data *data = user_data;
	uint16_t version = btd_service_get_version(data->service);

	return version != 0x0000;
}

static gboolean get_version(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct service_data *data = user_data;
	uint16_t version = btd_service_get_version(data->service);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT16, &version);

	return TRUE;
}

static gboolean get_auto_connect(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct service_data *data = user_data;
	dbus_bool_t value = btd_service_get_auto_connect(data->service);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &value);

	return TRUE;
}

static void set_auto_connect(const GDBusPropertyTable *property,
						DBusMessageIter *value,
						GDBusPendingPropertySet id,
						void *user_data)
{
	struct service_data *data = user_data;
	dbus_bool_t b;

	if (dbus_message_iter_get_arg_type(value) != DBUS_TYPE_BOOLEAN) {
		g_dbus_pending_property_error(id,
					ERROR_INTERFACE ".InvalidArguments",
					"Invalid arguments in method call");
		return;
	}

	dbus_message_iter_get_basic(value, &b);

	btd_service_set_auto_connect(data->service, b);

	g_dbus_pending_property_success(id);
}

static gboolean get_blocked(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct service_data *data = user_data;
	dbus_bool_t value = btd_service_is_blocked(data->service);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &value);

	return TRUE;
}

static void set_blocked(const GDBusPropertyTable *property,
						DBusMessageIter *value,
						GDBusPendingPropertySet id,
						void *user_data)
{
	struct service_data *data = user_data;
	dbus_bool_t b;

	if (dbus_message_iter_get_arg_type(value) != DBUS_TYPE_BOOLEAN) {
		g_dbus_pending_property_error(id,
					ERROR_INTERFACE ".InvalidArguments",
					"Invalid arguments in method call");
		return;
	}

	dbus_message_iter_get_basic(value, &b);

	btd_service_set_blocked(data->service, b);

	g_dbus_pending_property_success(id);
}

static const GDBusPropertyTable service_properties[] = {
	{ "Device", "o", get_device, NULL, NULL },
	{ "State", "s", get_state, NULL, NULL },
	{ "RemoteUUID", "s", get_remote_uuid, NULL, remote_uuid_exists },
	{ "LocalUUID", "s", get_local_uuid, NULL, local_uuid_exists },
	{ "Version", "q", get_version, NULL, version_exists },
	{ "AutoConnect", "b", get_auto_connect, set_auto_connect, NULL },
	{ "Blocked", "b", get_blocked, set_blocked, NULL },
	{ }
};

static const GDBusMethodTable service_methods[] = {
	{ GDBUS_ASYNC_METHOD("Disconnect", NULL, NULL, service_disconnect) },
	{ GDBUS_ASYNC_METHOD("Connect", NULL, NULL, service_connect) },
	{}
};

static struct service_data *service_get_data(struct btd_service *service)
{
	struct btd_device *dev = btd_service_get_device(service);
	struct btd_profile *p = btd_service_get_profile(service);
	struct service_data *data;

	data = find_data(service);
	if (data != NULL)
		return data;

	data = g_new0(struct service_data, 1);
	data->path = g_strdup_printf("%s/%s", btd_device_get_path(dev),
								p->remote_uuid);
	data->path = g_strdelimit(data->path, "-", '_');
	data->service = service;
	if (g_dbus_register_interface(btd_get_dbus_connection(),
					data->path, SERVICE_INTERFACE,
					service_methods, NULL,
					service_properties, data,
					data_free) == FALSE) {
		error("Unable to register service interface for %s",
								data->path);
		data_free(data);
		return NULL;
	}

	services = g_slist_prepend(services, data);

	DBG("%s", data->path);

	return data;
}

static void service_connected(struct service_data *data)
{
	DBusMessage *reply;

	if (!data->connect)
		return;

	reply = dbus_message_new_method_return(data->connect);
	g_dbus_send_message(btd_get_dbus_connection(), reply);
	dbus_message_unref(data->connect);
	data->connect = NULL;
}

static void service_disconnected(struct service_data *data)
{
	DBusMessage *reply;
	int err;

	if (data->disconnect) {
		reply = dbus_message_new_method_return(data->disconnect);
		g_dbus_send_message(btd_get_dbus_connection(), reply);
		dbus_message_unref(data->disconnect);
		data->connect = NULL;
	}

	if (!data->connect)
		return;

	err = btd_service_get_error(data->service);

	reply = btd_error_failed(data->connect, strerror(-err));
	g_dbus_send_message(btd_get_dbus_connection(), reply);
	dbus_message_unref(data->connect);
	data->connect = NULL;
}

static void service_cb(struct btd_service *service,
						btd_service_state_t old_state,
						btd_service_state_t new_state,
						void *user_data)
{
	struct service_data *data;

	data = service_get_data(service);
	if (!data)
		return;

	switch (new_state) {
	case BTD_SERVICE_STATE_UNAVAILABLE:
		data_remove(data);
		return;
	case BTD_SERVICE_STATE_CONNECTED:
		service_connected(data);
		break;
	case BTD_SERVICE_STATE_DISCONNECTED:
		service_disconnected(data);
		break;
	default:
		break;
	}

	if (data->state != btd_service_get_state(service))
		g_dbus_emit_property_changed(btd_get_dbus_connection(),
						data->path, SERVICE_INTERFACE,
						"State");
}

static int service_init(void)
{
	DBG("");

	service_id = btd_service_add_state_cb(service_cb, NULL);

	return 0;
}

static void service_exit(void)
{
	DBG("");

	btd_service_remove_state_cb(service_id);

	g_slist_free_full(services, data_free);
}

BLUETOOTH_PLUGIN_DEFINE(service, VERSION, BLUETOOTH_PLUGIN_PRIORITY_DEFAULT,
					service_init, service_exit)
