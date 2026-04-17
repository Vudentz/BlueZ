.. This file is included by btmon.rst.

CROSS-LAYER CORRELATION
========================

Bluetooth uses a layered protocol stack where errors at one layer
frequently originate from a different layer. Effective debugging
requires tracing errors across layers to find the true root cause.

This section provides a systematic approach to cross-layer analysis
and documents the most common inter-layer error cascades.

The Bluetooth Protocol Stack
-----------------------------

Understanding the layer relationships is essential for correlation::

    ┌─────────────────────────────────────────┐
    │  Profiles (A2DP, HFP, LE Audio, etc.)   │
    ├─────────────────────────────────────────┤
    │  GATT / ATT            │  SDP           │
    ├────────────────────────┤                │
    │  SMP                   │                │
    ├────────────────────────┴────────────────┤
    │  L2CAP                                  │
    ├─────────────────────────────────────────┤
    │  HCI (Host Controller Interface)        │
    ├─────────────────────────────────────────┤
    │  Controller (firmware / radio)          │
    └─────────────────────────────────────────┘

In btmon output, each layer has visible indicators:

- **HCI**: ``HCI Command:``, ``HCI Event:``, ``ACL Data``
- **L2CAP**: ``L2CAP:`` within ACL data frames
- **ATT/GATT**: ``ATT:`` within L2CAP frames on CID 0x0004
- **SMP**: ``SMP:`` within L2CAP frames on CID 0x0006 (LE) or 0x0007 (BR/EDR)
- **AVDTP/A2DP**: ``AVDTP:`` within L2CAP frames on PSM 25
- **AVCTP/AVRCP**: ``AVCTP:`` within L2CAP frames on PSM 23 or 27
- **Management**: ``@`` prefixed lines (kernel ↔ userspace)

Correlation Method
-------------------

When you find an error at any layer, use this three-step process:

**Step 1: Note the connection handle and timestamp**

Every error occurs on a specific connection handle at a specific time.
Record both::

    < ATT: Error Response (0x01)         #287 [hci0] 2.340012
          Handle: 0x0025
          Error: Insufficient Authentication (0x05)

Here: handle embedded in the ACL frame carrying this ATT PDU, frame
#287, timestamp 2.340012.

**Step 2: Check the layer below**

Look at the next lower layer around the same timestamp and handle.
Was the underlying transport healthy?

For ATT errors, check L2CAP and HCI:

- Is the L2CAP channel on CID 0x0004 still connected?
- Was the HCI link encrypted? (Look for prior ``Encryption Change``
  event on this handle.)

For L2CAP errors, check HCI:

- Did the HCI connection exist? (Look for ``Connection Complete``
  with this handle.)
- Was the connection still alive? (No ``Disconnect Complete`` before
  this point.)

**Step 3: Check the layer above**

What operation triggered the error? Look at what happened just before:

- Was a profile-level operation (e.g., GATT read, A2DP configure)
  in progress?
- Did the error trigger a recovery action (e.g., pairing, encryption)?
- Did the higher layer retry after recovery succeeded?

Security-Triggered Cascades
-----------------------------

The most common cross-layer cascades involve security. When a
higher-layer operation requires encryption or authentication that
is not yet established, a multi-layer cascade occurs.

Cascade 1: ATT → Encryption → Retry
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An ATT operation fails because the link is not encrypted. The host
automatically starts encryption, and then retries the ATT operation.

**Expected trace sequence**::

    > ATT: Read Request (0x0a) len 2                    #100
          Handle: 0x0025

    < ATT: Error Response (0x01) len 4                  #102
          Error: Insufficient Encryption (0x0f)
          Handle: 0x0025

    > HCI Command: LE Start Encryption (0x08|0x0019)    #104
          Handle: 42
          Long Term Key: ...

    < HCI Event: Encryption Change (0x08)               #106
          Status: Success (0x00)
          Handle: 42
          Encryption: Enabled

    > ATT: Read Request (0x0a) len 2                    #108
          Handle: 0x0025

    < ATT: Read Response (0x0b) len N                   #110
          Handle: 0x0025
          Value: ...

**What to check when this fails**:

- If ``Encryption Change`` returns a non-zero status, check the HCI
  error code. ``PIN or Key Missing (0x06)`` means the bond is stale.
- If the retry ATT operation fails again with a different error, the
  problem is at the GATT layer, not security.
- If ``LE Start Encryption`` never appears after the ATT error, the
  host is not triggering automatic encryption (possible bluetoothd
  configuration issue).

Cascade 2: ATT → SMP Pairing → Encryption → Retry
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When the link has no bond at all, an ATT authentication error triggers
a full SMP pairing flow.

**Expected trace sequence**::

    > ATT: Read Request (0x0a) len 2                    #100
          Handle: 0x0025

    < ATT: Error Response (0x01) len 4                  #102
          Error: Insufficient Authentication (0x05)
          Handle: 0x0025

    > SMP: Pairing Request (0x01) len 6                 #104
          ...

    < SMP: Pairing Response (0x02) len 6                #106
          ...

    (... SMP exchange: public keys, confirm, random, DHKey check ...)

    < HCI Event: Encryption Change (0x08)               #150
          Status: Success (0x00)
          Encryption: Enabled

    > ATT: Read Request (0x0a) len 2                    #152
          Handle: 0x0025

    < ATT: Read Response (0x0b) len N                   #154
          ...

**What to check when this fails**:

- If SMP sends ``Pairing Failed``, check the reason code (see SMP
  section). The ATT operation will not be retried.
- The ``Disconnect Complete`` that often follows ``Pairing Failed``
  is the consequence, not the cause. Look at the SMP failure reason.

Cascade 3: L2CAP → SMP → Encryption → Retry
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An L2CAP connection request is refused due to security, triggering
pairing.

**Expected trace sequence**::

    < L2CAP: Connection Request (0x02) ident 1 len 4   #200
          PSM: 25 (0x0019)

    > L2CAP: Connection Response (0x03) ident 1 len 8   #202
          Result: Connection refused - security block (0x0003)

    > SMP: Security Request (0x0b) len 1                #204
          Authentication: Bonding, SC (0x09)

    (... SMP pairing flow ...)

    < L2CAP: Connection Request (0x02) ident 2 len 4   #280
          PSM: 25 (0x0019)

    > L2CAP: Connection Response (0x03) ident 2 len 8   #282
          Result: Connection successful (0x0000)

**What to check when this fails**:

- If the retry L2CAP connection is also refused, the security level
  achieved by pairing may be insufficient (e.g., Legacy Pairing when
  Secure Connections is required).

Audio Cross-Layer Issues
--------------------------

Audio protocols add additional layers above L2CAP, creating longer
correlation chains.

A2DP: AVDTP → L2CAP → HCI
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A2DP streaming involves two L2CAP channels (signaling + media) on PSM
25. Issues at any layer affect audio.

**Audio dropout correlation**:

1. **Symptom**: Audio glitch at timestamp T
2. **Check ISO/ACL data**: Is there a gap in media packets around T?
3. **Check HCI flow control**: Are controller buffers full (``[N/N]``
   in ACL output)?
4. **Check connection events**: Was there a connection parameter update
   or role switch around T?
5. **Check co-existence**: Was there Wi-Fi or other Bluetooth activity
   around T? (Look for ``Inquiry`` or ``LE Set Scan Enable`` near T.)

**AVDTP abort correlation**::

    < AVDTP: Abort (0x0a) Command
          ACP SEID: 1

An AVDTP Abort typically means the remote device detected an
inconsistency. Check the events immediately before the Abort:

- Was a ``Set Configuration`` or ``Open`` rejected?
- Did the signaling L2CAP channel have errors?
- Was there a disconnect on the media transport channel?

LE Audio: ASCS → ATT → L2CAP → CIS → HCI
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

LE Audio has the deepest protocol stack and the longest correlation
chains.

**State machine correlation**: The ASE (Audio Stream Endpoint) state
machine is controlled via ATT writes to the ASE Control Point. Each
write changes the ASE state, which is reported via ATT notifications.
CIS setup happens between QoS Configured and Streaming states.

Cross-layer checkpoints:

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - ASE State Transition
     - Lower-Layer Dependency
     - What to Check
   * - Codec Configured → QoS Configured
     - ATT write to Control Point
     - Was the write acknowledged? Check for ATT errors.
   * - QoS Configured → Enabling
     - ATT write + CIS creation begins
     - Did ``LE Create CIS`` command succeed?
   * - Enabling → Streaming
     - CIS established
     - Did ``LE CIS Established`` event show Success?
   * - Streaming → (audio playing)
     - ISO data flowing on CIS handle
     - Are ISO data packets present and regular?
   * - Any → Releasing
     - Unexpected state regression
     - What ATT notification triggered the regression?
       Check the ASE state notification for the reason.

**CIS failure root cause at HCI**:

When ``LE CIS Established`` fails, correlate with the ACL link::

    - Is the ACL connection still alive? (Check handle.)
    - Were the CIG/CIS parameters valid? (Check ``LE Set CIG Parameters``
      command status.)
    - Did the remote accept the CIS request? (The remote can reject via
      ``LE Reject CIS Request``.)

Timing-Based Correlation
--------------------------

Some cross-layer issues are only visible through timing analysis.

**Supervision Timeout**

The HCI supervision timeout defines how long a link survives without
receiving a packet from the remote. When the timeout expires, the
controller sends ``Disconnect Complete`` with reason ``Connection
Timeout (0x08)``.

Correlate with higher layers:

- Was a long-running operation in progress? (e.g., SMP has a 30-second
  timeout; AVDTP has a configurable signal timeout.)
- Did the remote stop responding at a specific protocol step?
- Were connection parameter updates negotiated that changed the
  supervision timeout?

**Protocol Timeouts**

Each protocol layer has its own timeout behavior:

.. list-table::
   :header-rows: 1
   :widths: 20 15 65

   * - Protocol
     - Timeout
     - Behavior
   * - ATT
     - 30s
     - Transaction timeout. If no response in 30s, the client may
       disconnect.
   * - SMP
     - 30s
     - If any SMP PDU is not received within 30s, pairing fails.
   * - AVDTP
     - Configurable
     - Signaling response timeout. Typically 3-10 seconds.
   * - L2CAP
     - RTX: 1-60s
     - Signaling request timeout. Retransmitted then channel closed.
   * - HCI
     - Supervision
     - Controller-level. Default 20s LE, 32s BR/EDR.

When you see a timeout at one layer, check whether it was caused by
a timeout at a lower layer or by the remote device simply not
responding.

Constructing a Cross-Layer Timeline
-------------------------------------

For complex issues, build a timeline that includes events from all
relevant layers on the same connection handle::

    Time        Layer    Event                           Frame
    ─────────── ──────── ─────────────────────────────── ─────
    0.000000    HCI      LE Connection Complete (h=42)   #100
    0.045231    ATT      Exchange MTU Request            #105
    0.047002    ATT      Exchange MTU Response (247)     #107
    0.050000    ATT      Read By Group Type Request      #109
    0.052341    ATT      Read By Group Type Response     #111
    ...
    0.200000    ATT      Write Request (CCCD enable)     #150
    0.201000    ATT      Error: Insuff. Authentication   #152
    0.202000    SMP      Pairing Request                 #154
    0.250000    SMP      Pairing Failed (reason 0x03)    #170
    0.251000    HCI      Disconnect Complete (0x05)      #172

This timeline makes it immediately clear that the disconnect was caused
by a pairing failure, which was triggered by a CCCD write that required
authentication. The root cause is at the SMP layer (incompatible IO
capabilities), not at HCI or ATT.
