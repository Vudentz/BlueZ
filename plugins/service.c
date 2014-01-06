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

#include "lib/bluetooth.h"
#include "lib/sdp.h"
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
	char *path;
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
	return btd_error_not_available(msg);
}

static DBusMessage *service_connect(DBusConnection *conn, DBusMessage *msg,
								void *user_data)
{
	return btd_error_not_available(msg);
}

static const char *data_get_state(struct service_data *data)
{
	btd_service_state_t state = btd_service_get_state(data->service);
	int err;

	switch (state) {
	case BTD_SERVICE_STATE_UNAVAILABLE:
		return "unavailable";
	case BTD_SERVICE_STATE_DISCONNECTED:
		err = btd_service_get_error(data->service);
		return err < 0 ? "error" : "disconnected";
	case BTD_SERVICE_STATE_CONNECTING:
		return "connecting";
	case BTD_SERVICE_STATE_CONNECTED:
		return "connected";
	case BTD_SERVICE_STATE_DISCONNECTING:
		return "disconnecting";
	}

	return "unknown";
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

static const GDBusPropertyTable service_properties[] = {
	{ "State", "s", get_state, NULL, NULL },
	{ "RemoteUUID", "s", get_remote_uuid, NULL, remote_uuid_exists },
	{ "LocalUUID", "s", get_local_uuid, NULL, local_uuid_exists },
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

static void service_cb(struct btd_service *service,
						btd_service_state_t old_state,
						btd_service_state_t new_state,
						void *user_data)
{
	struct service_data *data;

	data = service_get_data(service);
	if (!data)
		return;

	if (new_state == BTD_SERVICE_STATE_UNAVAILABLE) {
		data_remove(data);
		return;
	}

	g_dbus_emit_property_changed(btd_get_dbus_connection(), data->path,
						SERVICE_INTERFACE, "State");
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

	while (services)
		data_remove(services->data);
}

BLUETOOTH_PLUGIN_DEFINE(service, VERSION, BLUETOOTH_PLUGIN_PRIORITY_DEFAULT,
					service_init, service_exit)
