.. This file is included by btmon.rst.

DEBUGGING METHODOLOGY
=====================

This section presents a systematic approach to Bluetooth debugging using
btsnoop traces. The methodology applies regardless of the protocol area
(LE Audio, A2DP, HFP, pairing, etc.) and provides a repeatable framework
for diagnosing issues from initial symptom to root cause.

Overview
--------

Bluetooth debugging follows a five-phase process:

1. **Capture** -- Collect a btsnoop trace covering the problem scenario
2. **Triage** -- Identify the time window and protocol area of interest
3. **Trace the Flow** -- Follow the relevant protocol exchange step by step
4. **Isolate the Error** -- Find the specific failure point
5. **Root-Cause** -- Determine why the failure occurred

Each phase narrows the scope. Resist the urge to jump to phase 4
before completing phases 2 and 3 -- many debugging dead ends come from
investigating the wrong part of the trace.

Phase 1: Capture
-----------------

A good trace is the foundation of all Bluetooth debugging. Poor capture
practices waste more debugging time than any other factor.

**Starting a live capture**::

    # Capture all HCI traffic to a file
    btmon -w /tmp/trace.log

    # Capture with timestamps displayed
    btmon -T -w /tmp/trace.log

    # Capture from a specific controller
    btmon -i hci1 -w /tmp/trace.log

**Capture guidelines**:

- Start ``btmon`` **before** reproducing the problem. Starting mid-scenario
  loses the connection setup, which is often where the root cause lies.
- Include the **full lifecycle**: adapter power-on, scanning/advertising,
  connection, the operation that fails, and the disconnection.
- If the problem is intermittent, use ``btmon-logger`` for persistent
  background capture with automatic log rotation.
- Note the **approximate wall-clock time** when the failure occurs so
  you can locate it in the trace.
- For LE Audio issues, use the ``-I`` flag to include isochronous data.

**Android and other platforms**: Android's Developer Options include
"Enable Bluetooth HCI snoop log" which produces a btsnoop file.
btmon can read these with ``btmon -r <file>``.

Phase 2: Triage
----------------

Before diving into packet details, orient yourself in the trace.

**Step 1: Get summary statistics**::

    btmon -a /tmp/trace.log

This shows device addresses, packet counts by type, and connection
handles. Use this to identify which devices and connections are
involved.

**Step 2: Scan for errors**

Search the decoded output for common error indicators::

    btmon -T -r /tmp/trace.log 2>&1 | grep -i -E "error|fail|reject|refused|timeout"

This gives a quick list of all explicit errors in the trace.

**Step 3: Identify the time window**

If you noted the wall-clock time of the failure, find it in the trace
using timestamps. The failure point is usually preceded by normal
operation, so look for the transition from success to failure::

    btmon -T -r /tmp/trace.log 2>&1 | grep -n "Disconnect\|Error\|Failed"

**Step 4: Determine the protocol area**

Based on the error messages and the scenario description, identify
which protocol layer to focus on:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Symptom
     - Likely Protocol Area
   * - Cannot pair / bond
     - SMP (see :ref:`SMP PAIRING FLOW`)
   * - Cannot discover services
     - GATT (see :ref:`RECONSTRUCTING A GATT DATABASE`)
   * - Audio not playing (classic)
     - A2DP / AVDTP (see :ref:`A2DP: Advanced Audio Distribution`)
   * - Audio not playing (LE)
     - LE Audio / ASCS / CIS (see :ref:`LE AUDIO PROTOCOL FLOW`)
   * - Call audio issues
     - HFP / SCO (see :ref:`HFP: Hands-Free Profile`)
   * - Connection drops
     - HCI disconnect reasons (see :ref:`CONNECTION TRACKING`)
   * - Connection refused
     - L2CAP (see :ref:`L2CAP CHANNEL TRACKING`)

Phase 3: Trace the Flow
-------------------------

Once you know the protocol area, follow the expected sequence and
compare it against what actually happened in the trace.

**Use the protocol flow documentation** from the relevant ``btmon-*.rst``
section to know what packets to expect. For each step in the expected
flow:

1. Find the corresponding packet in the trace
2. Verify its parameters are correct
3. Check the response status
4. Note the timestamp for latency analysis

**Track by connection handle**: Filter the trace output to a single
connection handle to reduce noise. Multiple connections produce
interleaved traffic that is difficult to follow::

    btmon -T -r /tmp/trace.log 2>&1 | grep "Handle 42\|Handle 0x002a"

**Build a timeline**: For complex issues, write down the key events
with their frame numbers and timestamps::

    #123 [0.000000] LE Create Connection
    #125 [0.045231] LE Connection Complete (handle 42)
    #130 [0.089441] ATT: Exchange MTU Request
    #132 [0.091002] ATT: Exchange MTU Response
    ...
    #287 [2.340012] Disconnect Complete (reason 0x08)

This timeline becomes the backbone of your analysis.

Phase 4: Isolate the Error
----------------------------

With the timeline established, identify exactly where things diverge
from the expected flow.

**Common divergence patterns**:

- **Missing packet**: An expected request or response never appears.
  Check if the connection was still alive. Check if a prerequisite
  step (e.g., encryption) was completed first.

- **Error response**: A request gets an explicit error. The error code
  usually points directly to the cause. See the error code tables
  in the relevant protocol section.

- **Timeout**: No response arrives and eventually a timeout or
  disconnect occurs. Check the supervision timeout value from
  the connection parameters. Check if the remote device was still
  sending any traffic.

- **Wrong parameters**: A response arrives but with unexpected values.
  Compare capability fields, MTU sizes, codec parameters, etc.
  against what was requested.

- **Out-of-order**: Packets arrive in an unexpected sequence. This
  often indicates a state machine violation on one side.

**Check both directions**: Many issues manifest as a failure in one
direction but are caused by something in the other direction. For
example, a GATT read failure might be caused by the remote device
rejecting encryption that was triggered by the read.

Phase 5: Root-Cause
---------------------

Once the specific failure point is identified, determine the
underlying cause. This often requires cross-layer analysis.

**Ask these questions**:

1. **Is this a local or remote issue?** Check which side sent the
   error. If the remote device sent it, the fix may be on the remote
   side, or the local device may be sending something the remote
   doesn't support.

2. **Is this a configuration issue?** Compare the capabilities
   exchanged during connection setup. Mismatched IO capabilities,
   unsupported codec parameters, or incompatible feature bits are
   common causes.

3. **Is this a timing issue?** Check timestamps between request and
   response. Supervision timeouts (default 20s for LE, 32s for
   BR/EDR) and protocol-specific timeouts (30s for SMP, 30s for
   AVDTP) can cause disconnections if operations are too slow.

4. **Is this a security issue?** Many operations require encryption
   or authentication. If the link is not encrypted, operations on
   protected attributes will fail with ``Insufficient Encryption``
   or ``Insufficient Authentication``.

5. **Is this a resource issue?** Controllers have limited buffers
   and connection slots. Check for ``Connection Rejected due to
   Limited Resources`` or ``Memory Capacity Exceeded`` errors.

Reporting Findings
-------------------

Structure your analysis for clarity:

1. **Summary** -- One sentence describing the issue and verdict
2. **Environment** -- Devices involved, roles (central/peripheral),
   controller type if relevant
3. **Timeline** -- Key events with frame numbers and timestamps
4. **Root cause** -- The specific error and why it occurred
5. **Evidence** -- Relevant btmon output snippets (anonymize MAC
   addresses: use ``00:11:22:33:44:55`` format)
6. **Recommendation** -- What to fix or investigate further

This structure works for bug reports, internal documentation, and
automated analysis tools alike.
