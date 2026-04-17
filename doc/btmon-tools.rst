.. This file is included by btmon.rst.

BLUEZ DEBUGGING TOOLS
=====================

BlueZ provides several command-line tools for Bluetooth debugging and
device management. This section covers when and how to use each tool
in a debugging workflow.

btmon -- The Primary Debugging Tool
-------------------------------------

``btmon`` is the Bluetooth monitor. It captures and decodes all HCI
traffic between the host and controller(s). This is the single most
important debugging tool -- nearly all Bluetooth issues can be
diagnosed from a btmon trace.

**Common usage patterns**:

Live monitoring::

    # Monitor all controllers with timestamps
    btmon -T

    # Monitor a specific controller
    btmon -T -i hci1

Capture to file for later analysis::

    # Write raw btsnoop format (can be re-decoded later)
    btmon -w /tmp/trace.log

    # Write while also displaying live output
    btmon -T -w /tmp/trace.log

Read and decode a saved trace::

    # Basic decode
    btmon -r /tmp/trace.log

    # With timestamps
    btmon -T -r /tmp/trace.log

    # With ISO data (required for LE Audio debugging)
    btmon -I -r /tmp/trace.log

    # Pipe to file for searching
    btmon -T -r /tmp/trace.log 2>&1 | tee /tmp/decoded.txt

Summary statistics::

    # Analyze mode -- device list, packet counts, latency plots
    btmon -a /tmp/trace.log

**Key flags reference**:

.. list-table::
   :header-rows: 1
   :widths: 15 85

   * - Flag
     - Purpose
   * - ``-T``
     - Show timestamps on each packet
   * - ``-I``
     - Include ISO (isochronous) data display. Required for LE Audio
   * - ``-r FILE``
     - Read and decode a btsnoop trace file
   * - ``-w FILE``
     - Write captured traffic to a btsnoop file
   * - ``-a FILE``
     - Analyze mode: summary statistics and latency plots
   * - ``-i hciX``
     - Monitor only the specified controller
   * - ``-p PRIO``
     - Filter by log priority (3=Error, 4=Warning, 6=Info, 7=Debug)
   * - ``-E``
     - Show Ellisys timestamps (for synchronized external captures)

btmon-logger -- Persistent Background Capture
-----------------------------------------------

``btmon-logger`` is a daemon that continuously captures btsnoop traces
in the background with automatic log rotation. Use this for problems
that are intermittent or hard to reproduce on demand.

**Typical setup**::

    # Start logging with 100MB max file size, rotating 5 files
    btmon-logger -p /var/log/bluetooth/

Logs are stored as timestamped btsnoop files and can be decoded later
with ``btmon -r``.

**When to use btmon-logger instead of btmon**:

- The problem is intermittent and you cannot predict when it will occur
- You need to capture over hours or days
- You want capture to survive system sleep/wake cycles
- You are collecting traces on a device that will be returned to you
  later

bluetoothctl -- Device Management and Live Testing
----------------------------------------------------

``bluetoothctl`` is the interactive command-line interface for managing
Bluetooth devices through the BlueZ daemon (``bluetoothd``). It
operates via D-Bus and controls the high-level Bluetooth stack.

**Use bluetoothctl for**:

- Scanning for and connecting to devices
- Pairing and bond management
- GATT service browsing and characteristic read/write
- Audio endpoint configuration and transport control
- Advertising setup and management
- Reproducing issues interactively while btmon captures the trace

**Common debugging sequences**:

Scan and connect::

    [bluetooth]# scan on
    [bluetooth]# scan off
    [bluetooth]# connect 00:11:22:33:44:55

Inspect pairing state::

    [bluetooth]# paired-devices
    [bluetooth]# info 00:11:22:33:44:55

Browse GATT services::

    [bluetooth]# menu gatt
    [bluetooth]# list-attributes

Audio endpoint management::

    [bluetooth]# menu endpoint
    [bluetooth]# list
    [bluetooth]# show /org/bluez/hci0/dev_.../sep1

LE advertising::

    [bluetooth]# menu advertise
    [bluetooth]# uuids 0x1234
    [bluetooth]# advertise on

**Subcommand reference**: Each ``bluetoothctl`` submenu has its own man
page. See ``bluetoothctl-gatt(1)``, ``bluetoothctl-advertise(1)``,
``bluetoothctl-endpoint(1)``, ``bluetoothctl-player(1)``, etc.

btmgmt -- Management Interface Access
---------------------------------------

``btmgmt`` provides direct access to the kernel's Bluetooth Management
interface. This is lower-level than ``bluetoothctl`` and useful for
operations that bypass the BlueZ daemon.

**Use btmgmt for**:

- Checking controller capabilities and supported features
- Reading/writing controller settings when bluetoothd is not running
- Debugging management-layer issues visible in ``btmon`` as ``@``
  prefixed lines
- Power cycling the controller
- Adding/removing devices from kernel allow/block lists

**Common commands**::

    # Show controller info (address, features, settings)
    btmgmt info

    # List all controllers
    btmgmt index

    # Power cycle
    btmgmt power off
    btmgmt power on

    # Read supported features
    btmgmt features

    # Check current settings
    btmgmt setting

btmgmt traffic appears in btmon output with the ``@`` prefix::

    @ MGMT Command: Set Powered (0x0005) plen 1
            Powered: Enabled
    @ MGMT Event: Command Complete (0x0001) plen 7
            Set Powered (0x0005) plen 4
              Status: Success (0x00)
              Current settings: 0x00012eff

Protocol-Specific Test Tools
------------------------------

BlueZ includes test tools for exercising specific protocols directly,
bypassing the high-level stack. These are useful for isolating whether
an issue is in the protocol implementation or in the higher-level
profile logic.

.. list-table::
   :header-rows: 1
   :widths: 15 25 60

   * - Tool
     - Protocol
     - Use Case
   * - ``l2test``
     - L2CAP
     - Test L2CAP connections, modes (ERTM, streaming), and
       throughput. Useful for verifying the data transport layer
       independently of higher profiles.
   * - ``rctest``
     - RFCOMM
     - Test RFCOMM serial connections. Useful for HFP and SPP
       debugging at the transport level.
   * - ``scotest``
     - SCO/eSCO
     - Test synchronous voice connections. Useful for isolating
       HFP audio path issues from the call control logic.
   * - ``isotest``
     - ISO
     - Test isochronous channels (CIS and BIS). Essential for
       LE Audio transport debugging.
   * - ``btgatt-client``
     - GATT
     - Standalone GATT client that connects and performs discovery
       and read/write operations without bluetoothd.
   * - ``btgatt-server``
     - GATT
     - Standalone GATT server for testing remote device behavior.
   * - ``l2ping``
     - L2CAP
     - Bluetooth-level ping. Tests basic connectivity and round-trip
       latency at the L2CAP layer.

**Example: Testing L2CAP independently**::

    # Server side
    l2test -r

    # Client side (connect to specific address, PSM 25)
    l2test -s 00:11:22:33:44:55 -P 25

**Example: Testing ISO channels**::

    # CIS Central
    isotest -s 00:11:22:33:44:55

    # CIS Peripheral
    isotest -d

All protocol test tools produce HCI traffic visible in ``btmon``.

Kernel-Level Test Suites
--------------------------

BlueZ includes comprehensive test suites that exercise the kernel's
Bluetooth subsystem using virtual controllers (no hardware needed).
These run via the ``test-runner`` infrastructure.

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

Run a specific tester::

    make TESTS=tools/mgmt-tester check

These are primarily for verifying kernel changes but can also help
confirm whether an issue exists at the kernel level or higher.

Tool Selection Guide
---------------------

Use this decision tree to pick the right tool:

1. **"I need to see what's happening on the HCI bus"** → ``btmon``
2. **"I need to capture traces over a long period"** → ``btmon-logger``
3. **"I need to connect/pair/manage a device"** → ``bluetoothctl``
4. **"I need to check controller capabilities"** → ``btmgmt``
5. **"I need to test a specific protocol in isolation"** → ``l2test``,
   ``rctest``, ``scotest``, ``isotest``
6. **"I need to test GATT without bluetoothd"** → ``btgatt-client``
   / ``btgatt-server``
7. **"I need to verify kernel Bluetooth behavior"** → ``*-tester``
   suites

In most debugging sessions, you will use ``btmon`` for capture and
``bluetoothctl`` to drive the scenario, with occasional use of
``btmgmt`` or protocol test tools for isolation.
