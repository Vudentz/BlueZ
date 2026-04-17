---
name: btsnoop-analyzer
description: Analyze Bluetooth btsnoop/HCI traces using btmon. Covers output parsing, GATT discovery, SMP pairing, L2CAP channels, LE Audio, error diagnosis, and connection lifecycle analysis.
license: LGPL-2.1
compatibility: opencode
metadata:
  audience: bluetooth-developers
  workflow: debugging
---

## What I do

I help analyze Bluetooth btsnoop trace files captured via `btmon` or Android
HCI snoop logging. I can:

- Decode btmon output and explain protocol exchanges
- Reconstruct GATT databases from discovery traces
- Diagnose SMP pairing failures
- Track L2CAP channel establishment and teardown
- Analyze LE Audio streaming (ASCS, PACS, BASS, CIS, BIG)
- Correlate errors across protocol layers (HCI, ATT, L2CAP, SMP)
- Identify connection lifecycle issues (setup, parameter negotiation, drops)
- Analyze advertising and scanning patterns

## When to use me

Use this skill when:
- You have a btsnoop log file and need to understand what happened
- A Bluetooth connection is failing and you need to diagnose why
- You need to verify GATT service/characteristic discovery
- Audio streaming (LE Audio or A2DP) is not working correctly
- Pairing or bonding is failing
- You need to understand the sequence of HCI events in a trace

## Knowledge base

Read `doc/btmon.rst` in this repository before starting analysis. It contains
detailed documentation organized in two parts:

### Protocol flows (reference)

- **READING THE OUTPUT**: Line prefixes (`<` `>` `@` `=` `#`), right-side
  metadata, indented detail lines, timestamps, frame numbers
- **CONNECTION TRACKING**: Handle-to-address mapping, connection lifecycle
- **HCI ERROR AND DISCONNECT REASON CODES**: Full error code table
- **ANALYZE MODE**: `btmon -a` summary statistics
- **RECONSTRUCTING A GATT DATABASE**: 6-phase discovery protocol with examples
- **SMP PAIRING FLOW**: Phases 1-3, Secure Connections vs Legacy, failure codes
- **L2CAP CHANNEL TRACKING**: Fixed CIDs, dynamic channels, PSM table, LE CoC
- **LE AUDIO PROTOCOL FLOW**: PACS, ASCS state machine, CIS/BIG, BASE
- **PROTOCOL ERROR CODES**: ATT errors, L2CAP results, cross-layer correlation
- **ADVERTISING AND SCANNING**: AD types, extended/periodic advertising

### Debugging guide (methodology)

- **DEBUGGING METHODOLOGY** (`doc/btmon-debugging.rst`): Five-phase systematic
  approach — capture, triage, trace the flow, isolate the error, root-cause.
  Use this to structure your analysis workflow.
- **BLUEZ DEBUGGING TOOLS** (`doc/btmon-tools.rst`): When and how to use btmon,
  bluetoothctl, btmgmt, protocol test tools (l2test, isotest, etc.), and
  kernel-level test suites. Includes a tool selection decision tree.
- **COMMON FAILURE PATTERNS** (`doc/btmon-failures.rst`): Catalog of frequent
  failures with trace signatures — pairing/security, connection setup/drops,
  GATT, A2DP, LE Audio, L2CAP, management. Match observed symptoms against
  these patterns for fast triage.
- **CROSS-LAYER CORRELATION** (`doc/btmon-cross-layer.rst`): How to trace
  errors across protocol layers. Covers security-triggered cascades (ATT →
  encryption, ATT → SMP, L2CAP → SMP), audio cross-layer issues, timing-based
  correlation, and cross-layer timeline construction.
- **DEBUGGING EXERCISES** (`doc/btmon-exercises.rst`): Five guided exercises
  with synthetic trace excerpts: LE connection failure, pairing failure, GATT
  discovery with security, A2DP codec negotiation, LE Audio state machine stall.

## Workflow

### Step 1: Load the trace

Run btmon to decode the trace file. Use appropriate flags:

```sh
# Basic decode
btmon -r trace.log

# With timestamps
btmon -T -r trace.log

# With ISO data (needed for LE Audio)
btmon -I -r trace.log

# Analyze mode for summary statistics
btmon -a trace.log
```

Capture the output to a file for searching:

```sh
btmon -T -r trace.log 2>&1 | tee /tmp/btmon-output.txt
```

### Step 2: Identify the scenario

Look at the first few hundred lines to determine:
1. **Controller initialization** — HCI Reset, Read Local commands
2. **Advertising or scanning** — LE Set Advertising / LE Set Scan
3. **Connection establishment** — LE Create Connection / Connection Complete
4. **Which direction** — Who initiated (central vs peripheral)

### Step 3: Trace the connection lifecycle

For each connection handle, track:
1. Connection Complete event (get the handle number)
2. LE Connection Update events (parameter changes)
3. All traffic on that handle (filter by handle in output)
4. Disconnect Complete event (check the reason code)

### Step 4: Protocol-specific analysis

**For GATT issues**: Look for ATT Read By Group Type (0x10), Read By Type
(0x08), Find Information (0x04), and Error Response (0x01) PDUs. Reconstruct
the attribute table using the 6-phase discovery process documented in
`doc/btmon.rst` section "RECONSTRUCTING A GATT DATABASE FROM SNOOP TRACES".

**For pairing issues**: Look for SMP Pairing Request/Response, then check for
Pairing Failed with a reason code. Cross-reference with the SMP failure reason
table in `doc/btmon.rst` section "SMP PAIRING FLOW".

**For LE Audio issues**: Follow the 8-step diagnosis in `doc/btmon.rst` section
"LE AUDIO PROTOCOL FLOW": PACS read, ASE discovery, codec config, QoS config,
enable, CIS setup, streaming data, release/disable.

**For L2CAP issues**: Track Connection Request/Response pairs and check the
result codes. For LE credit-based channels, track LE Credit Based Connection
Request and monitor credit flow.

### Step 5: Error correlation

When you find an error at one layer, check adjacent layers:
- ATT Error Response → check if L2CAP channel was healthy
- HCI Disconnect → check the reason code → correlate with higher-layer state
- SMP Pairing Failed → check if it was preceded by an HCI error
- L2CAP Connection Response with non-zero result → check HCI link status

### Step 6: Report findings

Structure your analysis as:
1. **Summary**: One-line description of what happened
2. **Timeline**: Key events in chronological order with frame numbers
3. **Root cause**: The specific error or protocol violation
4. **Evidence**: Relevant btmon output snippets
5. **Recommendation**: What to fix or investigate further

## Output format reference

Quick reference for reading btmon output:

| Prefix | Meaning |
|--------|---------|
| `>` | Host → Controller (commands, ACL/SCO/ISO data out) |
| `<` | Controller → Host (events, ACL/SCO/ISO data in) |
| `@` | Management interface traffic (kernel ↔ userspace) |
| `=` | System notes (open/close/index events) |
| `#` | D-Bus debug messages (bluetoothd with `-d`) |

Right-side metadata format:
```
#N [hciX] HH:MM:SS.UUUUUU
```
Where N = frame number, hciX = controller index, timestamp = microsecond
precision.

## Common patterns to look for

### Successful LE connection
```
> HCI LE Create Connection
< HCI Event: LE Meta Event (0x3e) - LE Connection Complete
```

### Pairing failure
```
> SMP: Pairing Request (0x01)
< SMP: Pairing Response (0x02)
...
< SMP: Pairing Failed (0x05)
        Reason: <check the code>
```

### GATT discovery boundary
```
< ATT: Error Response (0x01)
        Error: Attribute Not Found (0x0a)
```
This is normal — it signals the end of a discovery phase.

### Disconnection
```
< HCI Event: Disconnect Complete (0x05)
        Reason: <check the code — 0x13 is normal, 0x08 is timeout>
```

## Important notes

- Always anonymize MAC addresses in reports (use `00:11:22:33:44:55` format)
- `Attribute Not Found (0x0a)` during discovery is **normal**, not an error
- Frame numbers (`#N`) are stable identifiers; use them when referencing events
- The `btmon -a` analyze mode gives quick statistics but not protocol details
- For LE Audio, you **must** use `-I` flag to see isochronous data
