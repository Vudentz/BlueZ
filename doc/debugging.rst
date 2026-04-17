=========
Debugging
=========

-------------------------------
BlueZ Bluetooth Debugging Guide
-------------------------------

:Authors: - Luiz Augusto von Dentz <luiz.dentz@gmail.com>
:Copyright: Free use of this software is granted under the terms of the GNU
            Lesser General Public Licenses (LGPL).
:Version: BlueZ
:Date: April 2025

OVERVIEW
========

This document covers debugging Bluetooth issues across the full BlueZ
stack: from HCI traces (``btmon``) through the userspace daemon
(``bluetoothd``) to the Linux kernel Bluetooth subsystem and controller
drivers.

Bluetooth debugging follows a five-phase process:

1. **Capture** -- Collect traces and logs covering the problem scenario
2. **Triage** -- Identify the time window, layer, and protocol area
3. **Trace the Flow** -- Follow the relevant protocol exchange step by step
4. **Isolate the Error** -- Find the specific failure point
5. **Root-Cause** -- Determine why the failure occurred

Each phase narrows the scope. Resist the urge to jump to phase 4
before completing phases 2 and 3 -- many debugging dead ends come from
investigating the wrong part of the trace.

CAPTURING DEBUG INFORMATION
===========================

HCI Traces with btmon
----------------------

``btmon`` captures all traffic between the host and controller. This is
the primary debugging tool for any Bluetooth issue.

**Starting a live capture**::

    # Capture all HCI traffic to a file
    btmon -w /tmp/trace.log

    # Capture with timestamps displayed
    btmon -T -w /tmp/trace.log

    # Capture from a specific controller
    btmon -i hci1 -w /tmp/trace.log

**Reading saved traces**::

    # Basic decode
    btmon -r /tmp/trace.log

    # With timestamps
    btmon -T -r /tmp/trace.log

    # With ISO data (required for LE Audio)
    btmon -I -r /tmp/trace.log

    # Summary statistics
    btmon -a /tmp/trace.log

**Capture guidelines**:

- Start ``btmon`` **before** reproducing the problem. Starting
  mid-scenario loses the connection setup, which is often where the
  root cause lies.
- Include the **full lifecycle**: adapter power-on, scanning/advertising,
  connection, the operation that fails, and the disconnection.
- If the problem is intermittent, use ``btmon-logger`` for persistent
  background capture with automatic log rotation.
- Note the **approximate wall-clock time** when the failure occurs.
- For LE Audio issues, use the ``-I`` flag to include isochronous data.

For detailed btmon output format, protocol flows, and error code
tables, see ``btmon(1)`` and the protocol-specific sections:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Document
     - Coverage
   * - ``btmon-hci-init.rst``
     - HCI initialization and controller setup
   * - ``btmon-connections.rst``
     - Connection tracking, handle mapping, disconnect reason codes
   * - ``btmon-gatt.rst``
     - GATT database reconstruction from discovery traces
   * - ``btmon-smp.rst``
     - SMP pairing flows, Secure Connections, failure codes
   * - ``btmon-l2cap.rst``
     - L2CAP channel tracking, signaling, LE CoC
   * - ``btmon-le-audio.rst``
     - LE Audio: PACS, ASCS state machine, CIS/BIG, BASE
   * - ``btmon-classic-audio.rst``
     - A2DP (AVDTP) and HFP (RFCOMM/SCO) analysis
   * - ``btmon-advertising.rst``
     - Advertising and scanning patterns, AD types
   * - ``btmon-mgmt.rst``
     - Management interface traffic
   * - ``btmon-cs.rst``
     - Channel Sounding protocol flow

**Android and other platforms**: Android's Developer Options include
"Enable Bluetooth HCI snoop log" which produces a btsnoop file.
``btmon`` can read these with ``btmon -r <file>``.

bluetoothd Daemon Logging
--------------------------

``bluetoothd`` has a built-in debug logging system that outputs to both
syslog and the btmon monitor channel simultaneously.

**Enabling debug output**::

    # Run in foreground with full debug
    bluetoothd -n -d

    # Debug specific subsystems using file glob patterns
    bluetoothd -n -d "src/adapter.c:src/device.c"

    # Debug all profile plugins
    bluetoothd -n -d "profiles/*"

    # Multiple patterns (colon, comma, or space separated)
    bluetoothd -n -d "src/adapter.c:src/device.c:plugins/*"

The ``-n`` flag runs in foreground and copies log output to stderr.
The ``-d`` flag enables debug-level messages. Without an argument,
all debug output is enabled; with a pattern, only matching source
files produce debug output.

**Debug output in btmon**: When ``bluetoothd`` runs with ``-d``,
its log messages appear in ``btmon`` output as note lines::

    = Note: bluetoothd: src/adapter.c:start_discovery() ...

This interleaves daemon logic with HCI traffic, making it possible
to correlate daemon decisions with the protocol exchanges they
trigger.

**systemd service override**: To enable debug in a production
system using systemd::

    sudo systemctl edit bluetooth

Add::

    [Service]
    ExecStart=
    ExecStart=/usr/libexec/bluetooth/bluetoothd -n -d

Then restart the service. Note that ``ProtectKernelTunables=true``
is set by default in the service file, which blocks writes to
``/sys/kernel/debug/``. Override this if you need access to kernel
debugfs knobs.

**Log levels**: ``bluetoothd`` uses four log levels:

.. list-table::
   :header-rows: 1
   :widths: 15 15 70

   * - Macro
     - Level
     - Notes
   * - ``error()``
     - LOG_ERR
     - Always active. Includes ``file:function()`` prefix.
   * - ``warn()``
     - LOG_WARNING
     - Always active. Includes ``file:function()`` prefix.
   * - ``info()``
     - LOG_INFO
     - Always active. No file/function prefix.
   * - ``DBG()``
     - LOG_DEBUG
     - Only active when ``-d`` pattern matches the source file.
       Includes ``file:function()`` prefix.

Kernel Bluetooth Subsystem
---------------------------

The Linux kernel Bluetooth subsystem provides debugging through
debugfs, kernel config options, and dynamic debug.

**debugfs interface**: When the kernel is built with ``CONFIG_DEBUG_FS``
and the Bluetooth subsystem is loaded, debug information is available
under ``/sys/kernel/debug/bluetooth/``::

    # Show controller identity address
    cat /sys/kernel/debug/bluetooth/hci0/identity

    # Set minimum encryption key size (for testing)
    echo 7 > /sys/kernel/debug/bluetooth/hci0/min_encrypt_key_size

    # Check kernel self-test results
    cat /sys/kernel/debug/bluetooth/selftest_ecdh
    cat /sys/kernel/debug/bluetooth/selftest_smp

**Kernel config options** relevant to debugging:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Option
     - Purpose
   * - ``CONFIG_BT_FEATURE_DEBUG``
     - Enables Bluetooth feature debug support in the kernel
   * - ``CONFIG_BT_HCIVHCI``
     - Virtual HCI driver for testing without hardware
   * - ``CONFIG_DEBUG_FS``
     - Required for the debugfs interface above

**Dynamic debug**: The kernel's dynamic debug facility can enable
per-function or per-module debug messages at runtime::

    # Enable all Bluetooth subsystem debug messages
    echo 'module bluetooth +p' > /sys/kernel/debug/dynamic_debug/control

    # Enable debug for a specific file
    echo 'file net/bluetooth/hci_core.c +p' > /sys/kernel/debug/dynamic_debug/control

    # Enable debug for L2CAP
    echo 'file net/bluetooth/l2cap_core.c +p' > /sys/kernel/debug/dynamic_debug/control

Output goes to the kernel log (``dmesg`` or ``journalctl -k``).

**Note**: If running under systemd with the default bluetooth.service,
``ProtectKernelTunables=true`` blocks writes to ``/sys/kernel/debug/``.
Either override the service or write to these paths before starting
the service.

Driver-Level Debugging
-----------------------

Bluetooth controller drivers (USB, UART, SDIO, etc.) have their own
debug facilities.

**USB controllers** (btusb): Use the ``usbmon`` kernel facility to
capture raw USB traffic::

    # Load usbmon
    modprobe usbmon

    # Find the USB bus number for the Bluetooth controller
    lsusb | grep -i bluetooth

    # Capture USB traffic (bus 1 example)
    cat /sys/kernel/debug/usb/usbmon/1u > /tmp/usb-trace.txt

**UART controllers** (hci_uart, btattach): Use ``btattach`` to bind
a serial port to an HCI device::

    # Attach a UART controller
    btattach -B /dev/ttyS0 -S 115200

    # With protocol specification
    btattach -B /dev/ttyS0 -P h4

Serial-level issues (flow control, baud rate mismatches) show up as
HCI framing errors in btmon or as device attach failures.

**Firmware issues**: BlueZ includes firmware tools for specific
controllers:

.. list-table::
   :header-rows: 1
   :widths: 15 85

   * - Tool
     - Purpose
   * - ``btmgmt info``
     - Shows controller firmware version, manufacturer, and supported
       features
   * - ``bluemoon``
     - Intel controller configuration and firmware management
   * - ``hex2hcd``
     - Convert Broadcom firmware from hex to hcd format
   * - ``btinfo``
     - Display detailed controller information

**Common driver-level symptoms**:

- Controller not appearing as ``hciX``: Check ``dmesg`` for driver
  probe errors. Verify firmware is installed (``/lib/firmware/``).
- Controller resets during operation: Check for firmware crashes in
  ``dmesg``. May appear as ``Hardware Error`` events in btmon.
- Intermittent failures: Check USB autosuspend (disable with
  ``echo -1 > /sys/bus/usb/devices/.../power/autosuspend``).

TRIAGE
======

Before diving into packet details, orient yourself across layers.

**Step 1: Get HCI summary statistics**::

    btmon -a /tmp/trace.log

This shows device addresses, packet counts by type, and connection
handles.

**Step 2: Scan for errors across all layers**

In the btmon output (which includes daemon logs when ``-d`` is
enabled)::

    btmon -T -r /tmp/trace.log 2>&1 | grep -i -E "error|fail|reject|refused|timeout"

In the kernel log::

    dmesg | grep -i -E "bluetooth|bt|hci" | grep -i -E "error|fail|warn"

**Step 3: Identify the layer**

Use the symptoms to determine where to focus:

.. list-table::
   :header-rows: 1
   :widths: 35 25 40

   * - Symptom
     - Layer
     - Where to Look
   * - Controller not found
     - Driver
     - ``dmesg``, ``lsusb``/``lspci``
   * - Controller found but not usable
     - Kernel/Firmware
     - ``dmesg``, ``btmgmt info``
   * - Cannot power on adapter
     - Management
     - btmon ``@`` lines, ``rfkill list``
   * - Cannot scan or advertise
     - HCI/Management
     - btmon HCI commands and events
   * - Cannot connect
     - HCI
     - btmon connection events
   * - Cannot pair
     - SMP
     - btmon SMP exchange
   * - Cannot discover services
     - GATT
     - btmon ATT PDUs
   * - Audio not working
     - Profile (A2DP/LE Audio/HFP)
     - btmon AVDTP/ASCS/SCO
   * - Connection drops
     - HCI
     - btmon disconnect reason code
   * - Daemon crash
     - bluetoothd
     - Core dump, ``coredumpctl``

**Step 4: Determine the time window**

If you noted the wall-clock time of the failure, find it in the
trace using timestamps::

    btmon -T -r /tmp/trace.log 2>&1 | grep -n "Disconnect\|Error\|Failed"

DEBUGGING BLUETOOTHD
====================

Profile and State Machine Issues
----------------------------------

When the HCI trace looks correct but behavior is wrong, the issue
may be in ``bluetoothd``'s profile logic or state management.

**D-Bus interface inspection**: Use ``busctl`` or ``dbus-monitor`` to
inspect the BlueZ D-Bus state::

    # List all BlueZ managed objects
    busctl tree org.bluez

    # Inspect a specific device
    busctl introspect org.bluez /org/bluez/hci0/dev_00_11_22_33_44_55

    # Monitor D-Bus signals in real time
    dbus-monitor --system "sender='org.bluez'"

**Stored device state**: BlueZ stores bonding keys, device properties,
and cached service data under ``/var/lib/bluetooth/<adapter>/<device>/``:

- ``info`` -- Bond keys, address type, device class, name
- ``attributes`` -- Cached GATT database
- ``cache/`` -- Service cache

Stale or corrupted state files are a common cause of reconnection
and service discovery issues. Removing the device directory and
re-pairing often resolves these problems.

**Test utilities**: The ``test/`` directory contains Python D-Bus
scripts useful for isolating daemon behavior::

    # Test scanning
    test/test-discovery

    # Test device connection
    test/test-device connect 00:11:22:33:44:55

    # Test GATT operations
    test/test-gatt-profile

DEBUGGING THE KERNEL
====================

Using Virtual Controllers
--------------------------

The kernel's virtual HCI driver (``CONFIG_BT_HCIVHCI``) allows testing
Bluetooth behavior without physical hardware.

**BlueZ test suites** use this extensively via ``test-runner``::

    # Run the management interface test suite
    make TESTS=tools/mgmt-tester check

    # Run with btmon monitoring inside the VM
    tools/test-runner -m -- tools/mgmt-tester

    # Run a specific test by name
    tools/test-runner -- tools/mgmt-tester -t "Pair Device - SSP Just Works"

Available test suites:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Test Suite
     - Coverage
   * - ``mgmt-tester``
     - Management interface commands and events
   * - ``l2cap-tester``
     - L2CAP connection, configuration, and data transfer
   * - ``smp-tester``
     - SMP pairing flows and key generation
   * - ``sco-tester``
     - SCO/eSCO connection setup and parameters
   * - ``iso-tester``
     - CIS and BIS establishment and data transfer
   * - ``gap-tester``
     - GAP procedures (discovery, connection, bonding)
   * - ``hci-tester``
     - Low-level HCI command/event handling

Valgrind Testing
-----------------

BlueZ supports running tests under Valgrind for memory error
detection::

    make check-valgrind

A suppression file is provided at ``tools/valgrind.supp`` for known
false positives.

PROTOCOL-SPECIFIC DEBUGGING
============================

.. include:: debugging-tools.rst

.. include:: debugging-failures.rst

.. include:: debugging-cross-layer.rst

REPORTING ISSUES
================

When filing a bug report, include:

1. **Summary** -- One sentence describing the issue and verdict
2. **Environment** -- BlueZ version (``bluetoothd -v``), kernel version
   (``uname -r``), controller info (``btmgmt info``), remote device
   model if relevant
3. **btmon trace** -- Captured with ``btmon -T -w`` covering the full
   scenario. Anonymize MAC addresses (use ``00:11:22:33:44:55`` format)
   unless the report is private.
4. **bluetoothd log** -- If daemon behavior is suspect, include output
   from ``bluetoothd -n -d`` covering the same time window
5. **Kernel log** -- If driver or kernel issues are suspected, include
   relevant ``dmesg`` output
6. **Steps to reproduce** -- Minimal sequence to trigger the issue
