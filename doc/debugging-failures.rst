.. This file is included by debugging.rst.

COMMON FAILURE PATTERNS
========================

This section catalogs frequently encountered Bluetooth failure patterns
with their btmon trace signatures. Each pattern includes the symptom,
the trace evidence to look for, the root cause, and recommended actions.

Use this as a reference when triaging issues -- match the symptoms you
observe against these patterns to quickly narrow down the problem.

Pairing and Security Failures
------------------------------

Pattern: Pairing Failed -- Authentication Requirements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: Pairing attempt fails immediately after feature exchange.

**Trace signature**::

    > SMP: Pairing Request (0x01) len 6
          Authentication requirement: Bonding, MITM, SC (0x2d)

    < SMP: Pairing Response (0x02) len 6
          Authentication requirement: Bonding, SC (0x29)

    < SMP: Pairing Failed (0x05) len 1
          Reason: Authentication Requirements (0x03)

**Root cause**: The devices cannot agree on an association model. Common
when one side requires MITM protection but the IO capabilities only
support Just Works (e.g., both sides are ``NoInputNoOutput``).

**Action**: Check IO capabilities in the Pairing Request/Response. If
MITM is required, at least one device must have ``KeyboardDisplay``,
``DisplayYesNo``, or ``KeyboardOnly`` capability.

Pattern: Encryption Failed -- Key Missing or Stale
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: Reconnection to a previously bonded device fails.

**Trace signature**::

    < HCI Event: LE Enhanced Connection Complete (0x0a)
          Status: Success (0x00)
          Handle: 42

    > HCI Command: LE Start Encryption (0x08|0x0019)
          Handle: 42
          Long Term Key: 1a2b3c4d...

    < HCI Event: Encryption Change (0x08)
          Status: PIN or Key Missing (0x06)
          Handle: 42

**Root cause**: The stored Long Term Key (LTK) does not match what the
remote device has. This happens when one side has been factory reset or
re-paired with another device, invalidating the bond.

**Action**: Remove the bond on both sides and re-pair. Check
``/var/lib/bluetooth/<adapter>/<device>/info`` for stored key data.

Pattern: Secure Connections Required
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: Connection or service access rejected.

**Trace signature**::

    < L2CAP: Connection Response (0x03)
          Result: Connection refused - security block (0x0003)

Or at the ATT layer::

    < ATT: Error Response (0x01)
          Error: Insufficient Encryption (0x0f)

Followed by a pairing attempt that uses Legacy Pairing (no ``SC`` flag)
when the remote requires Secure Connections.

**Root cause**: The service requires Secure Connections but one or both
devices do not support it, or the pairing fell back to Legacy.

**Action**: Verify both devices support Secure Connections (check the
``SC`` flag in SMP Pairing Request/Response). Check the controller's
LE features for SC support.

Connection Failures
--------------------

Pattern: Connection Timeout
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: Connection attempt hangs and eventually fails.

**Trace signature**::

    > HCI Command: LE Create Connection (0x08|0x000d)
          Peer Address: 00:11:22:33:44:55

    ... (long delay, typically 5-30 seconds)

    < HCI Event: LE Enhanced Connection Complete (0x0a)
          Status: Connection Failed to be Established (0x3e)

Or for established connections that drop::

    < HCI Event: Disconnect Complete (0x05)
          Status: Success (0x00)
          Handle: 42
          Reason: Connection Timeout (0x08)

**Root cause (connection setup)**: The remote device is not advertising,
is out of range, or is using a different address type than expected
(public vs random).

**Root cause (established connection)**: The remote device stopped
responding. Could be out of range, crashed, or the connection interval
is too aggressive for the radio environment.

**Action**: For setup failures, verify the remote is advertising (use
``bluetoothctl scan on``). For established connection drops, check the
connection parameters (interval, supervision timeout) in the
``LE Connection Update`` events.

Pattern: Connection Rejected -- Limited Resources
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: Connection attempt rejected by the controller.

**Trace signature**::

    < HCI Event: LE Enhanced Connection Complete (0x0a)
          Status: Connection Rejected due to Limited Resources (0x0d)

Or::

    < HCI Event: LE Enhanced Connection Complete (0x0a)
          Status: Memory Capacity Exceeded (0x07)

**Root cause**: The controller has reached its maximum number of
simultaneous connections. This limit varies by controller (typically
5-16 for LE).

**Action**: Check how many connections are currently active. Disconnect
unused connections. Check controller documentation for maximum
supported connections.

Pattern: Connection Parameter Rejection
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: Connection works initially but one side rejects parameter
updates.

**Trace signature**::

    > HCI Command: LE Connection Update (0x08|0x0013)
          Handle: 42
          Min interval: 6
          Max interval: 6
          Latency: 0
          Timeout: 500

    < HCI Event: Command Status (0x0f)
          Status: Invalid HCI Command Parameters (0x12)

Or at the L2CAP level::

    > L2CAP: Connection Parameter Update Request (0x12)
          Min interval: 6
          Max interval: 6
          Latency: 99
          Timeout: 200

    < L2CAP: Connection Parameter Update Response (0x13)
          Result: Connection Parameters rejected (0x0001)

**Root cause**: The requested parameters are outside the acceptable
range. Common issues: interval too small (< 6 = 7.5ms), latency too
high relative to timeout, or timeout too short.

**Action**: Check the parameter values against Bluetooth specification
limits. The supervision timeout must be greater than
``(1 + latency) * interval * 2``.

GATT Failures
--------------

Pattern: Service Discovery Returns Empty
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: GATT service discovery completes but finds no services.

**Trace signature**::

    > ATT: Read By Group Type Request (0x10) len 6
          Handle range: 0x0001-0xffff
          Attribute group type: Primary Service (0x2800)

    < ATT: Error Response (0x01) len 4
          Error: Attribute Not Found (0x0a)
          Handle: 0x0001

**Root cause**: The remote device has no GATT services, or the
connection is not encrypted and all services require encryption. Also
occurs if the remote device's GATT database is not yet initialized
(e.g., device still booting).

**Action**: Check if encryption is established before discovery. Try
pairing first, then re-discover. If using a custom peripheral, verify
its GATT database is correctly configured.

Pattern: Characteristic Write Rejected
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: Writing to a characteristic fails.

**Trace signature**::

    > ATT: Write Request (0x12) len 5
          Handle: 0x0025
          Data: 0100

    < ATT: Error Response (0x01) len 4
          Error: Write Not Permitted (0x03)
          Handle: 0x0025

Common error codes in write failures:

.. list-table::
   :header-rows: 1
   :widths: 10 30 60

   * - Code
     - Error
     - Meaning
   * - 0x03
     - Write Not Permitted
     - Characteristic does not support writes, or wrong write type
       (Write Request vs Write Command vs Write Without Response)
   * - 0x05
     - Insufficient Authentication
     - Link must be authenticated (paired with MITM protection)
   * - 0x06
     - Request Not Supported
     - The server does not support this ATT operation
   * - 0x0f
     - Insufficient Encryption
     - Link must be encrypted. Usually triggers automatic encryption
   * - 0x0d
     - Invalid Attribute Length
     - Written value is too long or too short for the characteristic

Audio Streaming Failures
--------------------------

Pattern: A2DP Stream Setup Rejected
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: Audio does not start playing over A2DP.

**Trace signature**::

    < AVDTP: Set Configuration (0x03) Response Reject (0x03)
          Error Code: SEP In Use (0x0c)

Or::

    < AVDTP: Set Configuration (0x03) Response Reject (0x03)
          Error Code: Unsupported Configuration (0x29)

**Root cause (SEP In Use)**: The remote endpoint is already streaming
to another device. Multi-point devices have limited endpoints.

**Root cause (Unsupported Configuration)**: The codec parameters
selected are not supported by the remote device. Check the capabilities
exchanged during ``Get Capabilities`` / ``Get All Capabilities``.

**Action**: For SEP In Use, disconnect the other audio source first.
For Unsupported Configuration, compare the requested codec config
against the capabilities returned by the remote.

Pattern: LE Audio CIS Establishment Failure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: LE Audio unicast audio does not start streaming.

**Trace signature**::

    > HCI Command: LE Create CIS (0x08|0x0064)
          Number of CIS: 1
          CIS Handle: 257
          ACL Handle: 42

    < HCI Event: LE CIS Established (0x19)
          Status: Connection Failed to be Established (0x3e)
          Connection Handle: 257

Or the ASCS state machine stalls::

    > ATT: Write Request (0x12) len N
          Handle: 0x0025
          Data: 04...    (ASE Control Point: Enable)

    < ATT: Handle Value Notification (0x1b) len N
          Handle: 0x0022
          Data: 03...    (ASE State: Releasing)

**Root cause**: CIS failure can be a radio issue or controller
limitation. ASCS state regression (e.g., jumping to Releasing instead
of Streaming) indicates the remote rejected the operation.

**Action**: Check the ASE state transitions against the expected flow
(Idle → Codec Configured → QoS Configured → Enabling → Streaming).
Any deviation indicates where the remote rejected the configuration.
See the LE Audio protocol flow documentation for the full state machine.

Pattern: Audio Glitches During Streaming
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: Audio plays but with dropouts, clicks, or stuttering.

**Trace signature**: Look for gaps in the isochronous data flow::

    < ISO Data: Handle 257 ...   #5000 [hci0] 10.000123
    < ISO Data: Handle 257 ...   #5001 [hci0] 10.010045
    ... (missing packets -- gap in sequence)
    < ISO Data: Handle 257 ...   #5010 [hci0] 10.100234

Also check for ``Number of Completed Packets`` backing up (controller
buffer exhaustion)::

    > ACL Data TX: Handle 42 [6/6] flags 0x00 dlen 27
                                   ^^^^ all buffers used

**Root cause**: Radio interference, aggressive connection parameters
(interval too long for the data rate), or host-side scheduling delays
causing late data delivery to the controller.

**Action**: Check connection parameters. Shorter intervals improve
reliability but increase power consumption. Check if other
Bluetooth or Wi-Fi activity correlates with the dropouts (co-existence
issues).

L2CAP Failures
---------------

Pattern: L2CAP Connection Refused
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: A profile-level connection fails.

**Trace signature**::

    < L2CAP: Connection Request (0x02) ident 3 len 4
          PSM: 25 (0x0019)
          Source CID: 64

    > L2CAP: Connection Response (0x03) ident 3 len 8
          Destination CID: 0
          Source CID: 64
          Result: Connection refused - no resources (0x0004)

Common L2CAP connection results:

.. list-table::
   :header-rows: 1
   :widths: 10 35 55

   * - Code
     - Result
     - Meaning
   * - 0x0002
     - PSM not supported
     - No service registered on that PSM. The profile is not available.
   * - 0x0003
     - Security block
     - Encryption or authentication required. Usually triggers pairing.
   * - 0x0004
     - No resources
     - Internal resource limit. Controller or host ran out of channels.
   * - 0x0006
     - Invalid Source CID
     - Protocol error -- the requested CID is already in use.
   * - 0x0007
     - Source CID Already Allocated
     - Duplicate channel identifier.

Pattern: LE Credit-Based Connection Stall
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: LE CoC (Connection-Oriented Channel) connects but data
transfer stalls.

**Trace signature**::

    < L2CAP: LE Credits (0x16) ident 0 len 4
          CID: 64
          Credits: 0

No further credits are issued and the sending side cannot transmit.

**Root cause**: The receiving application is not consuming data fast
enough, so it stops issuing credits. This is flow-control working as
designed, but it indicates a performance bottleneck on the receiver.

**Action**: Check the receiver's data consumption rate. Increase the
initial credit count if possible. Monitor the credit flow in the trace
to identify when credits stop being issued.

Management Interface Failures
-------------------------------

Pattern: Command Rejected by Kernel
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Symptom**: A bluetoothd operation fails, visible in btmon as a
management error.

**Trace signature**::

    @ MGMT Command: Pair Device (0x0019) plen 8
          BR/EDR Address: 00:11:22:33:44:55
          Capability: NoInputNoOutput
          Address type: BR/EDR (0x00)

    @ MGMT Event: Command Status (0x0002) plen 3
          Pair Device (0x0019)
            Status: Already Paired (0x09)

Or::

    @ MGMT Event: Command Status (0x0002) plen 3
          Pair Device (0x0019)
            Status: Connect Failed (0x04)

**Root cause**: These are kernel-level rejections. ``Already Paired``
means the bond exists and re-pairing was not forced. ``Connect Failed``
means the underlying HCI connection could not be established.

**Action**: For ``Already Paired``, remove the existing bond first.
For ``Connect Failed``, check the HCI-level events around the same
timestamp for the specific connection failure reason.
