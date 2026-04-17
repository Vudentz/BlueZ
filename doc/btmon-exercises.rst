.. This file is included by btmon.rst.

DEBUGGING EXERCISES
====================

This section provides guided exercises using synthetic btmon trace
excerpts. Each exercise presents a scenario, shows the relevant trace
output, and walks through the diagnosis process. Work through the
exercises in order -- they build on concepts from earlier exercises.

Exercise 1: LE Connection Failure
----------------------------------

**Scenario**: A central device attempts to connect to a peripheral.
The user reports "connection failed."

**Trace excerpt**::

    > HCI Command: LE Create Connection (0x08|0x000d) plen 25  #10 [hci0] 0.000000
          Scan interval: 96 (60.00 msec)
          Scan window: 48 (30.00 msec)
          Filter policy: White list is not used (0x00)
          Peer address type: Public (0x00)
          Peer address: 00:11:22:33:44:55
          Own address type: Random (0x01)
          Min connection interval: 24 (30.00 msec)
          Max connection interval: 40 (50.00 msec)
          Connection latency: 0 (0x0000)
          Supervision timeout: 72 (720 msec)
          Min connection length: 0 (0.00 msec)
          Max connection length: 0 (0.00 msec)

    < HCI Event: Command Status (0x0f) plen 4                  #11 [hci0] 0.001234
          LE Create Connection (0x08|0x000d) ncmd 1
            Status: Success (0x00)

    ... (5 seconds pass with no further events)

    < HCI Event: LE Enhanced Connection Complete (0x0a) plen 31 #12 [hci0] 5.123456
          Status: Connection Failed to be Established (0x3e)
          Handle: 0
          Role: Central (0x00)
          Peer address type: Public (0x00)
          Peer address: 00:11:22:33:44:55

**Questions to answer**:

1. Did the HCI command itself succeed?
2. What does the final status ``0x3e`` mean?
3. What are the possible causes?
4. What would you check next?

**Walkthrough**:

1. **Command Status** shows ``Success (0x00)`` -- the controller accepted
   the command. This only means the connection attempt started, not that
   it succeeded.

2. Status ``0x3e`` is ``Connection Failed to be Established``. This
   means the controller tried to connect but never received a response
   from the peripheral within the allotted time.

3. Possible causes:

   - The peripheral is not advertising
   - The peripheral is advertising with a different address type
     (random instead of public, or vice versa)
   - The peripheral is out of range
   - The peripheral is using a Resolvable Private Address (RPA) and
     the central does not have its IRK to resolve it

4. Next steps:

   - Check if the peripheral is advertising: look for scan results
     earlier in the trace (``LE Advertising Report``) to confirm the
     address and address type
   - Verify the address type matches: the ``LE Create Connection``
     command uses ``Peer address type: Public (0x00)`` -- if the
     peripheral uses a random address, this will never connect
   - Check the supervision timeout: 720ms is very short. If the
     peripheral's advertising interval is long (e.g., 1 second), the
     central might not see an advertisement within this window

Exercise 2: Pairing Failure Diagnosis
--------------------------------------

**Scenario**: Two LE devices attempt to pair. The user reports "pairing
keeps failing."

**Trace excerpt**::

    > SMP: Pairing Request (0x01) len 6                        #200 [hci0] 1.000000
          IO capability: NoInputNoOutput (0x03)
          OOB data: Authentication data not present (0x00)
          Authentication requirement: Bonding, MITM, SC (0x2d)
          Max encryption key size: 16
          Initiator key distribution: EncKey IdKey (0x03)
          Responder key distribution: EncKey IdKey (0x03)

    < SMP: Pairing Response (0x02) len 6                       #202 [hci0] 1.023456
          IO capability: NoInputNoOutput (0x03)
          OOB data: Authentication data not present (0x00)
          Authentication requirement: Bonding, MITM, SC (0x29)
          Max encryption key size: 16
          Initiator key distribution: IdKey (0x02)
          Responder key distribution: IdKey (0x02)

    > SMP: Pairing Failed (0x05) len 1                         #204 [hci0] 1.024000
          Reason: Authentication Requirements (0x03)

    < HCI Event: Disconnect Complete (0x05) plen 4             #206 [hci0] 1.025123
          Status: Success (0x00)
          Handle: 42
          Reason: Remote User Terminated Connection (0x13)

**Questions to answer**:

1. What association model would be selected given the IO capabilities?
2. Why did pairing fail with reason ``0x03``?
3. Who initiated the failure (which side sent ``Pairing Failed``)?
4. What is the fix?

**Walkthrough**:

1. Both devices have IO capability ``NoInputNoOutput (0x03)``. Per the
   Bluetooth specification's IO capability mapping table, this results
   in the **Just Works** association model.

2. Both sides request ``MITM`` (Man-In-The-Middle protection) in their
   authentication requirements. However, Just Works **does not provide
   MITM protection** -- it only protects against passive eavesdropping.
   Since MITM was required but cannot be achieved, pairing fails with
   ``Authentication Requirements (0x03)``.

3. The ``>`` prefix on ``Pairing Failed`` means it was sent by the
   **host to the controller** (outgoing). This is the local device
   aborting the pairing. The local SMP implementation detected that
   the negotiated method cannot satisfy the MITM requirement.

4. Either:

   - Remove the MITM requirement from the authentication requirement
     if Just Works security is acceptable for the use case
   - Change the IO capabilities on at least one device to enable
     Passkey Entry or Numeric Comparison (e.g., ``DisplayYesNo`` or
     ``KeyboardDisplay``)

Exercise 3: GATT Service Discovery
------------------------------------

**Scenario**: A central device connects and attempts to discover GATT
services. The application reports "no services found" even though the
peripheral should have services.

**Trace excerpt**::

    < HCI Event: LE Enhanced Connection Complete (0x0a) plen 31  #50 [hci0] 0.000000
          Status: Success (0x00)
          Handle: 42

    > ATT: Exchange MTU Request (0x02) len 2                    #55 [hci0] 0.010000
          Client RX MTU: 517

    < ATT: Exchange MTU Response (0x03) len 2                   #57 [hci0] 0.012345
          Server RX MTU: 247

    > ATT: Read By Group Type Request (0x10) len 6              #59 [hci0] 0.015000
          Handle range: 0x0001-0xffff
          Attribute group type: Primary Service (0x2800)

    < ATT: Error Response (0x01) len 4                          #61 [hci0] 0.016234
          Request opcode: Read By Group Type Request (0x10)
          Handle: 0x0001
          Error: Insufficient Encryption (0x0f)

    ... (no further ATT or SMP activity for 30 seconds)

    < HCI Event: Disconnect Complete (0x05) plen 4              #65 [hci0] 30.016500
          Status: Success (0x00)
          Handle: 42
          Reason: Remote User Terminated Connection (0x13)

**Questions to answer**:

1. What happened during service discovery?
2. Why was it ``Insufficient Encryption`` and not ``Attribute Not Found``?
3. Why did the connection eventually drop?
4. What should have happened after frame #61?

**Walkthrough**:

1. The central sent ``Read By Group Type Request`` for Primary Services.
   The peripheral responded with ``Insufficient Encryption (0x0f)``
   instead of returning service data. This means **all** services on
   the peripheral require encryption.

2. ``Attribute Not Found (0x0a)`` would mean there are no services.
   ``Insufficient Encryption (0x0f)`` means services exist but the
   link is not encrypted. The peripheral's GATT database requires an
   encrypted link for even reading the service list.

3. After 30 seconds of no activity, the remote device terminated the
   connection. This is likely the peripheral's ATT timeout or
   application timeout firing because the central never completed the
   expected pairing/encryption flow.

4. After receiving ``Insufficient Encryption``, the host should have
   automatically triggered ``LE Start Encryption`` (if bonded) or
   ``SMP Pairing Request`` (if not bonded). The absence of either
   indicates the host stack did not handle the security elevation
   automatically. This is the bug -- the central's Bluetooth stack
   should initiate encryption or pairing in response to this error.

Exercise 4: A2DP Codec Negotiation
------------------------------------

**Scenario**: A phone connects to a Bluetooth speaker. The user
reports "no audio" even though the connection appears successful.

**Trace excerpt**::

    < AVDTP: Discover (0x01) Response Accept (0x02)            #300 [hci0] 2.000000
          ACP SEID: 1
            Media Type: Audio (0x00)
            SEP Type: SNK (0x01)
            In use: No

    > AVDTP: Get Capabilities (0x02) Command (0x00)            #302 [hci0] 2.001234
          ACP SEID: 1

    < AVDTP: Get Capabilities (0x02) Response Accept (0x02)    #304 [hci0] 2.003456
          Service Category: Media Transport (0x01)
          Service Category: Media Codec (0x07) - Audio SBC (0x00)
            Sampling Frequency: 44100 48000
            Channel Mode: Joint Stereo
            Block Length: 16
            Subbands: 8
            Allocation Method: Loudness
            Min Bitpool: 2
            Max Bitpool: 53

    > AVDTP: Set Configuration (0x03) Command (0x00)           #306 [hci0] 2.010000
          ACP SEID: 1
          INT SEID: 1
          Service Category: Media Transport (0x01)
          Service Category: Media Codec (0x07) - Audio SBC (0x00)
            Sampling Frequency: 44100
            Channel Mode: Joint Stereo
            Block Length: 16
            Subbands: 8
            Allocation Method: Loudness
            Min Bitpool: 2
            Max Bitpool: 250

    < AVDTP: Set Configuration (0x03) Response Reject (0x03)   #308 [hci0] 2.012345
          Service Category: Media Codec (0x07)
          Error Code: Bad Media Transport Format (0x23)

**Questions to answer**:

1. What codec was negotiated?
2. Why was ``Set Configuration`` rejected?
3. Which specific parameter caused the rejection?
4. What is the fix?

**Walkthrough**:

1. SBC (Sub-Band Codec) -- the mandatory baseline codec for A2DP.

2. The ``Set Configuration`` was rejected with ``Bad Media Transport
   Format (0x23)``. This means a codec parameter was invalid.

3. Compare the ``Get Capabilities`` response with the ``Set
   Configuration`` request:

   - Capabilities say ``Max Bitpool: 53``
   - Configuration requests ``Max Bitpool: 250``
   - 250 exceeds the remote's maximum supported bitpool of 53

   The initiator requested a bitpool value that the sink does not
   support.

4. The initiator must set ``Max Bitpool`` to a value within the range
   advertised in capabilities (2-53). The correct configuration should
   use ``Max Bitpool: 53`` or less.

Exercise 5: LE Audio State Machine Stall
------------------------------------------

**Scenario**: LE Audio unicast setup begins but audio never starts
streaming.

**Trace excerpt**::

    > ATT: Write Request (0x12) len 10                         #400 [hci0] 3.000000
          Handle: 0x0025
          Data: 01 01 01 06 00 00 00 00    (Config Codec, ASE ID 1)

    < ATT: Write Response (0x13) len 0                         #402 [hci0] 3.001000

    < ATT: Handle Value Notification (0x1b) len 8              #404 [hci0] 3.002000
          Handle: 0x0022
          Data: 01 01 ...    (ASE ID 1, State: Codec Configured)

    > ATT: Write Request (0x12) len 16                         #406 [hci0] 3.050000
          Handle: 0x0025
          Data: 02 01 ...    (Config QoS, ASE ID 1)

    < ATT: Write Response (0x13) len 0                         #408 [hci0] 3.051000

    < ATT: Handle Value Notification (0x1b) len 8              #410 [hci0] 3.052000
          Handle: 0x0022
          Data: 01 02 ...    (ASE ID 1, State: QoS Configured)

    > ATT: Write Request (0x12) len 4                          #412 [hci0] 3.100000
          Handle: 0x0025
          Data: 03 01 ...    (Enable, ASE ID 1)

    < ATT: Write Response (0x13) len 0                         #414 [hci0] 3.101000

    < ATT: Handle Value Notification (0x1b) len 8              #416 [hci0] 3.102000
          Handle: 0x0022
          Data: 01 03 ...    (ASE ID 1, State: Enabling)

    > HCI Command: LE Create CIS (0x08|0x0064) plen 9         #418 [hci0] 3.150000
          Number of CIS: 1
          CIS Handle: 257
          ACL Handle: 42

    < HCI Event: Command Status (0x0f) plen 4                  #419 [hci0] 3.151000
          LE Create CIS (0x08|0x0064) ncmd 1
            Status: Success (0x00)

    ... (2 seconds pass)

    < HCI Event: LE CIS Established (0x19) plen 30             #420 [hci0] 5.200000
          Status: Connection Failed to be Established (0x3e)
          Connection Handle: 257

    < ATT: Handle Value Notification (0x1b) len 8              #422 [hci0] 5.210000
          Handle: 0x0022
          Data: 01 00 ...    (ASE ID 1, State: Idle)

**Questions to answer**:

1. How far did the ASE state machine progress?
2. At which layer did the failure occur?
3. What does the ASE regression to Idle indicate?
4. What would you investigate next?

**Walkthrough**:

1. The ASE progressed through: **Idle → Codec Configured → QoS
   Configured → Enabling**. It never reached **Streaming**.

2. The failure occurred at the **HCI layer**. ``LE Create CIS`` was
   accepted (Command Status: Success) but ``LE CIS Established``
   returned ``Connection Failed to be Established (0x3e)``. The ASCS
   layer (ATT writes) worked correctly up to this point.

3. After the CIS failure, the ASE state regressed from Enabling back
   to **Idle** (not just to QoS Configured or Codec Configured). This
   full regression indicates the remote device released the ASE,
   likely because it detected the CIS failure and cleaned up.

4. Investigation steps:

   - Check if ``LE Set CIG Parameters`` was sent before
     ``LE Create CIS`` and whether its parameters were valid
   - Check if the remote device supports the CIS parameters (PHY,
     interval, latency) that were configured
   - Check the ACL connection stability -- was handle 42 still healthy
     at frame #420?
   - Look for any ``LE Reject CIS Request`` from the remote device
     between frames #418 and #420

Tips for Self-Study
--------------------

**Practice with real traces**: The best way to build debugging skill is
to analyze real btsnoop traces. Capture traces of successful operations
first to understand what "normal" looks like, then compare against
failing traces.

**Use btmon's analyze mode**: Run ``btmon -a trace.log`` on any trace
to get a quick summary. This is a good starting point before diving
into packet-level analysis.

**Build a personal pattern library**: As you encounter new failure
patterns, document them with the trace signature. Over time this
becomes your fastest diagnostic tool.

**Cross-reference the specification**: When a trace shows unexpected
behavior, check the Bluetooth Core Specification for the relevant
protocol's state machine and error handling rules. btmon decodes
everything, but understanding *why* a certain response is valid or
invalid requires specification knowledge.
