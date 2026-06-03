# Debugging Journal

This journal records failures that taught us something important about the
system. Keep setup instructions and the happy path in the README and focused
reference docs. Use this file for symptoms, hypotheses, experiments, evidence,
incorrect assumptions, fixes, and lessons worth revisiting later.

Prefer a small number of decisive log lines over full serial dumps. Record what
worked as carefully as what failed: narrowing the boundary of a bug is often
the most valuable part of the investigation.

## Incident Template

```md
## YYYY-MM-DD: Short Incident Title

### Symptom

### Architecture At The Time

### What Worked

### What Failed

### Hypotheses And Experiments

| Hypothesis | Experiment | Result |
| --- | --- | --- |

### Decisive Evidence

### Resolution

### Lessons

### Follow-Up
```

## 2026-06-02: Matter Pairing Times Out After AddNOC

### Symptom

Pairing an ESP32-C6 LED node with the ESP32-C6 controller node over Matter BLE
commissioning appeared to make progress, then stalled. The controller retried
operational discovery for roughly 60 seconds. The LED node eventually expired
its fail-safe timer, removed the provisional Matter fabric, reverted its Thread
dataset, and reopened its commissioning window.

The visible controller-side error was:

```text
OperationalSessionSetup[1:0000000000000002]: operational discovery failed: 32
Session establishment failed for <0000000000000002, 1>, error: 32
```

`CHIP:0x00000032` is a timeout. It was the final symptom, not the root cause.

### Architecture At The Time

The prototype deliberately collapsed several responsibilities onto one
ESP32-C6 controller node:

| Device | Responsibilities |
| --- | --- |
| Controller ESP32-C6 | Matter commissioner/controller, Thread Leader, SRP server, DNS-SD server, DNS client, USB operator ingress |
| LED ESP32-C6 | Matter accessory, Thread node, SRP client, WS2812 renderer |

The controller used an infra-less Thread mesh. There was no separate Linux
OpenThread Border Router, host-side `esp-thread-br`, or independent DNS-SD
owner.

### What Worked

- Both serial monitors ran concurrently, which exposed the LED-side half of the
  timeout instead of leaving the investigation controller-only.
- BLE commissioning established a connection and completed PASE.
- The LED node accepted AddNOC and provisionally joined the Matter fabric.
- The LED node applied the Thread operational dataset and attached to the mesh.
- The controller saw the LED as a Thread child.
- The controller pinged the LED mesh-local IPv6 address successfully in `44 ms`.
- The LED registered its operational Matter service in the controller SRP
  registry:

```text
011ECA4195A1887C-0000000000000002._matter._tcp.default.service.arpa.
    port: 5540
    host: 4A73774C5F4D8410.default.service.arpa.
    addresses: [fd86:2be7:3e1f:7755:ea21:24fa:27e0:9dad]
```

### What Failed

- The controller could not resolve the LED node's operational Matter service
  through DNS-SD.
- The controller's colocated OpenThread DNS client did not receive responses
  from its own DNS-SD server.
- Matter commissioning could not progress from provisional AddNOC state to an
  operational CASE session.
- The LED node correctly rolled the provisional configuration back when its
  fail-safe timer expired.

### Hypotheses And Experiments

| Hypothesis | Experiment | Result |
| --- | --- | --- |
| LED-side BLE/Thread coexistence is failing | Monitor the LED serial port during pairing and ping its Thread address | Ruled out as the primary cause. BLE commissioning, Thread attach, SRP registration, and IPv6 ping all worked. |
| The Thread mesh itself is broken | Inspect child table and ping the LED from the controller | Ruled out. The LED attached and answered ping in `44 ms`. |
| The LED never registers its operational Matter service | Query `srp server host` and `srp server service` on the controller | Ruled out. The SRP registry contained the expected `_matter._tcp` service and LED IPv6 address. |
| Missing OMR prefix alone explains the timeout | Verify mesh-local connectivity and SRP storage | Not sufficient by itself. The same Thread mesh could carry LED traffic and registration. |
| SRP unicast address mode advertises the wrong local endpoint | Switch the live SRP server from unicast to anycast and rerun DNS probes | Ruled out as a complete fix. Anycast changed the advertised resolver endpoint, but DNS still timed out. |
| `CONFIG_ENABLE_ROUTE_HOOK=n` contributes to the local resolver failure | Rebuild + flash the controller with `=y`, rerun direct DNS probes before pairing | **Ruled out (2026-06-02).** With Thread `leader`, SRP `running`, and a real `_matter._tcp` record in `srp server service`, `dns browse` still returned `Error 28: ResponseTimeout`. Route hook adds an IPv6 route, not a DNS-SD responder. |
| A single ESP32-C6 cannot reliably act as commissioner and its own infra-less SRP/DNS-SD owner in this integration | Probe DNS directly through the controller's published ML-EID, anycast locator, and RLOC | Strongly supported. Every DNS path timed out even though SRP storage and remote Thread connectivity worked. |

### Decisive Evidence

The controller SRP server was manually enabled for the experiment:

```text
matter esp ot_cli srp server enable
matter esp ot_cli srp server state
```

It reached:

```text
running
```

The controller SRP registry contained the LED's operational record, but direct
DNS probes failed:

```text
matter esp ot_cli dns browse _matter._tcp.default.service.arpa.
matter esp ot_cli dns servicehost 011ECA4195A1887C-0000000000000002 _matter._tcp.default.service.arpa.
```

Both returned:

```text
Error 28: ResponseTimeout
```

The controller could reach the LED over Thread:

```text
16 bytes from fd86:2be7:3e1f:7755:ea21:24fa:27e0:9dad:
icmp_seq=2 hlim=255 time=44ms
```

The controller could also ping its own RLOC:

```text
16 bytes from fd86:2be7:3e1f:7755:0:ff:fe00:9000:
icmp_seq=3 hlim=64 time=1ms
```

However, DNS browse still timed out when explicitly sent through the
controller's ML-EID, anycast locator, and RLOC. Changing the SRP server to
anycast mode changed the advertised DNS server endpoint but did not restore DNS
responses.

The LED-side monitor then showed the expected rollback:

```text
chip[FS]: Fail-safe timer expired
chip[SVR]: Commissioning failed (attempt 1): 32
chip[NP]: Reverting Thread operational dataset
chip[FP]: Fabric (0x1) deleted.
lo_led_node: commissioning window opened
```

### Current Conclusion

This was not primarily a CPU, memory, BLE, radio coexistence, or basic Thread
mesh failure. The boundary is narrower: in the tested infra-less configuration,
the controller's local DNS client could not obtain DNS-SD answers from the
DNS-SD server colocated on the same ESP32-C6, even though SRP registration
storage worked.

The next cheap, reversible experiment is to rebuild only the controller with:

```text
CONFIG_ENABLE_ROUTE_HOOK=y
```

After flashing, run direct DNS browse and service-resolution probes before
retrying full pairing. If `Error 28: ResponseTimeout` remains, revert the
experiment and move to the supported split architecture: a separate OpenThread
Border Router and SRP/DNS-SD owner, with the Matter controller joining that
Thread network.

### Route Hook Experiment Result (2026-06-02)

Tried the cheap experiment above and ruled it out. Rebuilt **only** the
controller with `CONFIG_ENABLE_ROUTE_HOOK=y` (verified in
`build/config/sdkconfig.h`: `#define CONFIG_ENABLE_ROUTE_HOOK 1`), flashed it,
left the LED node untouched, and reran direct DNS probes before any pairing.

Controller state during the probe was healthy: `state` = `leader`, `srp server
state` = `running`, and `srp server service` listed a real operational record —
the controller's *own* `_matter._tcp` service (node `...1B669`, port 5580, AAAA
`fd86:2be7:3e1f:7755:df53:8605:83ab:967e`). Even browsing for a record the node
itself published:

```text
matter esp ot_cli dns browse _matter._tcp.default.service.arpa
> DNS browse response for _matter._tcp.default.service.arpa.
Error 28: ResponseTimeout
```

So the route hook changed nothing: the colocated DNS client still gets no answer
from the colocated DNS-SD server, even for a record sitting in its own SRP
storage. `CONFIG_ENABLE_ROUTE_HOOK` only installs an IPv6 route; it is not a
DNS-SD responder. The experiment was reverted (`sdkconfig.defaults` back to
`CONFIG_ENABLE_ROUTE_HOOK=n`). The controller flash currently still carries the
experimental build; reflashing baseline is deferred because the controller's
role changes under the split architecture anyway.

Aside (new lesson): pasting the ~213-char `dataset set active <tlvs>` line over
USB-Serial/JTAG raced with the next command and panicked the console
(`Guru Meditation Error: Store access fault`). Do not re-send the dataset during
bring-up — the controller restores it from NVS and boots straight to `leader`.

### Resolution

Route hook ruled out (see above). Resolving this requires the supported split
architecture: a separate OpenThread Border Router / SRP-DNS-SD owner, with the
Matter controller joining that Thread network and resolving operational nodes
through it. That is now the active direction.

### Lessons

- Monitor both ends of a distributed failure before changing code.
- A successful BLE commissioning exchange does not prove Matter commissioning
  completed. Operational DNS-SD discovery and CASE session establishment still
  have to succeed.
- An SRP record existing in server storage does not prove a DNS client can
  resolve it.
- A ping proves IP reachability, not that UDP service dispatch or DNS-SD
  resolution works.
- When one device owns several infrastructure roles, test each boundary
  independently before blaming resource pressure.

### Follow-Up

- [done] Rebuilt the controller with `CONFIG_ENABLE_ROUTE_HOOK=y`, flashed only
  the controller, reran direct DNS probes — still `Error 28: ResponseTimeout`.
- [done] Reverted the route-hook experiment in `sdkconfig.defaults`.
- Scope a separate OTBR/SRP-DNS owner and decide the hardware first: host-side
  `esp-thread-br` (host SoC + 802.15.4 RCP) or a Linux/Pi `ot-br-posix`, with the
  Matter controller joining that Thread network.
- Optional: reflash the controller baseline (route hook off) if a pristine
  device is wanted before the architecture change.
