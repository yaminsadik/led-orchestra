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

## 2026-06-04: Stage 0 — Separate Client Resolves Through a Real C6 Border Router

> **RESULT: STAGE 0 PASS (discovery + operational CASE).** The separate client
> resolved the real LED `_matter._tcp` record through the C6 BR-host + RCP, and
> — after the radio-contention fix below — the controller established an
> operational **CASE** session to the node and drove a custom-cluster `SetScene`
> that rendered on the LED. The original `Error 28: ResponseTimeout` (DNS-SD) and
> the follow-on operational `error 32` (`0x32` = `CHIP_ERROR_TIMEOUT`) are both
> retired for this topology. See **Resolution** for the root cause and the
> decisive evidence. The full runbook (wiring, build/flash, bring-up, expected
> logs, pass/fail) is
> [`../matter-prototype/stage0-br-validation/README.md`](../matter-prototype/stage0-br-validation/README.md).

### Symptom (the failure this retires)

The 2026-06-02 incident above: a single infra-less ESP32-C6 acting as commissioner
**and** its own SRP/DNS-SD owner could not resolve its own operational nodes
(`dns browse` → `Error 28: ResponseTimeout`). Stage 0 tests whether moving
SRP/DNS-SD onto a **real OpenThread Border Router** (host + RCP), and resolving
from a **separate** node, fixes it.

### Architecture Under Test

| Device | Firmware | Role |
| --- | --- | --- |
| BR-host C6 | `ot_br` side-pin build from `rebuild-sidepins.sh` (backbone-less) | OTBR host: Thread Leader, SRP server, DNS-SD owner |
| RCP C6 | `ot_rcp` + `sdkconfig.rcp-sidepins.defaults` | 802.15.4 radio for the BR host (spinel/UART on XIAO side pins D6/D7) |
| Client C6 | `controller-node` + `sdkconfig.client.defaults` (`OPENTHREAD_BORDER_ROUTER=n`) | Separate Thread client / controller stand-in; commissions the LED node and runs `dns browse` — **not** a BR itself |
| LED node C6 | existing `led-node` (unchanged) | Matter accessory; registers `_matter._tcp` via SRP after commissioning |

Key difference vs. 2026-06-02: the SRP/DNS-SD owner (BR host) is a **separate
device** from the commissioner/resolver (client). The client's own border router
/ SRP server is compiled **off**, so the BR host is the sole SRP/DNS-SD owner —
the colocated role-stacking that failed is gone.

### What Worked

- All three Stage 0 images build for `esp32c6` from the committed overlays
  (ESP-IDF v5.4.1): `ot_rcp` (229 KB), `ot_br` backbone-less (1.77 MB), and the
  client commissioner with `OPENTHREAD_BORDER_ROUTER=n`.
- Backbone-less `ot_br` compiles cleanly: with `OPENTHREAD_BR_AUTO_START=n` the
  example's "No backbone netif!" hard error is not reached, confirming an offline
  BR build is viable before any Wi-Fi fallback.
- The first physical BR/RCP attempt exposed a XIAO pin-map issue: ESP-IDF's
  `GPIO4`/`GPIO5` defaults are tiny back pads (`MTMS`/`MTDI`) on this board.
  Rebuilt/reflashed only BR-host and RCP to use accessible side pins:
  `BR D6/GPIO16 TX -> RCP D7/GPIO17 RX`,
  `BR D7/GPIO17 RX <- RCP D6/GPIO16 TX`, plus common GND.

### Hypotheses And Experiments

| Hypothesis | Experiment | Result |
| --- | --- | --- |
| A real BR (host+RCP) that owns SRP/DNS-SD lets a *separate* client resolve `_matter._tcp` where the colocated single C6 could not | Bring up the offline BR; from the client run `dns browse _matter._tcp.default.service.arpa` for the LED's record | PASS for DNS-SD: LED node `...0000000000000002` resolved through the BR |
| The offline (backbone-less) BR is sufficient — no Wi-Fi/advertising-proxy/OMR needed for on-mesh DNS-SD | Run §7/§8 of the runbook with no backbone | PASS for DNS-SD: Wi-Fi-backbone diagnostic not needed |
| If offline fails, the missing piece is the border-router init (advertising proxy / OMR), not the C6 BR path itself | Re-run with the Wi-Fi-backbone diagnostic (`sdkconfig.br-host-wifi.defaults`, auto-start) and compare | Not run; offline DNS-SD path succeeded |

### Decisive Evidence

- BR/RCP link passed:
  `rcp version` returned
  `openthread-esp32/4c2820d377-005c5cefc; esp32c6; 2026-06-04 16:55:38 UTC`.
- BR Thread/SRP passed:
  BR `state` was `leader`; `srp server state` was `running`.
- Active BR dataset used by the client:
  `0e08000000000001000000030000104a0300001935060004001fffe00208de82414c1383cf2b0708fd42957b9cb64fbc0510278feaba96b30f1356cb36a6e1b350f3030f4f70656e5468726561642d35383037010258070410e9d7bcb235adc0f8dfe51bd200963e680c0402a0f7f8`.
- Separate client attached to the BR mesh:
  `state` became `child`, then `router` after LED reset/retry.
- Fast triage passed:
  client `dns browse _matter._tcp.default.service.arpa` returned a response that
  included the throwaway `probe` SRP record.
- Real LED DNS-SD passed after LED power-cycle:

  ```text
  DNS service resolution response for
  525E53F22D34B3AE-0000000000000002 for service
  _matter._tcp.default.service.arpa.
  Port:5540, Priority:0, Weight:0, TTL:7127
  Host:120633A3E1E984B0.default.service.arpa.
  HostAddress:fd42:957b:9cb6:4fbc:d25c:e012:d49c:57d6 TTL:7127
  TXT:[SII=32303030, SAI=32303030, SAT=34303030] TTL:7127
  ```

- Commissioning caveat:
  the client still logged
  `OperationalSessionSetup[1:0000000000000002]: operational discovery failed: 32`
  and `Session establishment failed ... error: 32`. This is no longer the
  original DNS-SD `Error 28: ResponseTimeout`; it is the next Matter session
  debugging target.

### Resolution (operational `error 32` — radio contention)

The C6 BR-host + RCP DNS-SD path is viable for the split topology, **and** the
follow-on operational `error 32` is now resolved.

**What `error 32` was.** Decoded against the SDK: this build prints errors as hex
(`CHIP_CONFIG_ERROR_FORMAT_AS_STRING = 0`, `CHIP_ERROR_INTEGER_FORMAT = PRIx32`),
so the logged `32` is `0x32` = `CHIP_ERROR_TIMEOUT`. It is raised from
`OperationalSessionSetup::OnNodeAddressResolutionFailed` — CHIP's **internal**
operational address resolution / CASE setup timed out, even though the manual
`dns resolve` in the OT CLI succeeded (different code paths; the CLI is not proof
the CHIP resolver can resolve).

**Root cause — three radios on one PHY.** The commissioner C6 has a single
2.4 GHz radio time-shared across Wi-Fi, BLE, and 802.15.4. During commissioning
it was driving **all three at once**: the operator Wi-Fi softAP (started
unconditionally at boot by `controller_wifi_ingress_start()` →
`esp_wifi_start()`, even with no SSID), BLE (PASE), and native 802.15.4 (Thread,
`OPENTHREAD_RADIO_NATIVE=y`). The LED *commissionee* — a single C6 running only
BLE+Thread — joined fine, so the differentiator was the commissioner's extra
Wi-Fi load, not radio count.

**Fix** (in `stage0-br-validation/sdkconfig.client.defaults`, layered on the
controller build):

- `CONFIG_LED_ORCHESTRA_OPERATOR_WIFI_MODE_DISABLED=y` — selects the `DISABLED`
  member of the operator-Wi-Fi `choice` so `controller_wifi_ingress_start()`
  takes its no-op branch and the Wi-Fi PHY never powers on. Confirmed live at
  runtime: boot logs `operator Wi-Fi ingress disabled; USB serial remains
  available`.
- `CONFIG_OPENTHREAD_NUM_MESSAGE_BUFFERS=128` — doubled from the default 65.

**Which change was operative.** In the fixed run the OT message pool peaked at
`max-used: 14` of 128 — nowhere near the prior contention-run exhaustion
(`free: 1, max-used: 65`). So the **buffer bump was *not* what fixed it**
(buffers were never the constraint once contention dropped); **removing the
Wi-Fi softAP — the third radio — was the operative change.** The larger pool
stays as headroom. (Caveat: both knobs changed in one pass, so this is inferred
from the buffer telemetry, not an isolated A/B.)

**Decisive evidence (operational, no re-commissioning needed).** `error 32`
fires at `kFindOperationalForStayActive`, *after* the device is committed to the
fabric, so the LED never rolled back — it stayed commissioned on the controller's
fabric (`525E53F22D34B3AE`). We therefore validated the operational path directly
rather than re-pairing:

- `dns browse` from the controller returned both operational records on the
  fabric: the controller's own (`…000000000001B669`, node 112233) and the LED's
  (`…0000000000000002`).
- `controller read-attr 2 0 0x28 0` drove the controller through CASE Sigma2
  (`chip[SC]` processing the LED's response — the peer-cert/`Last Known Good
  UTC Time` path) with **no `error 32`, no timeout** (the original bug logged a
  *visible* ERROR here).
- `controller invoke-cmd 2 1 0xFFF1FC00 0 {SetScene solid red}` returned `Done`
  and the **LED rendered solid red** — end-to-end proof: discovery → CASE →
  custom-cluster command → render.

### Side findings (this session)

- **`lo-set-scene` console helper was broken (now fixed).** Its handler read
  `argv[0]` as the node-id, but `esp_console` passes the command name as
  `argv[0]`, so every call failed `ESP_ERR_INVALID_ARG` (258). Fixed by dropping
  the command name (`argc--; argv++;`) at the top of `set_scene_handler`,
  `set_node_config_handler`, and `sync_clock_handler` in
  `controller-node/main/led_orchestra_console.cpp`. (`matter esp controller
  invoke-cmd <node> <ep> 0xFFF1FC00 0 <json>` remains the lower-level equivalent.)
- **Driving the controller console over USB-Serial/JTAG:** lines longer than
  ~235 chars (e.g. the 198-char `pairing ble-thread` dataset arg) overflow the
  console RX buffer and silently truncate → the commissioner gets a malformed
  command and no-ops with no error. Send long lines in paced chunks. Also,
  commissioner INFO logs are suppressed at the default WARN level and
  `log level * verbose` is **not** a valid command in this build, so a successful
  read/commission is *quiet*; rely on absence of the ERROR-level `error 32` plus
  the rendered `SetScene` for the verdict.

### Dead-ends and false signals (what we tried that didn't pan out)

- **Buffer exhaustion was the wrong primary hypothesis.** The contention run had
  shown the OT pool drained (`free: 1, max-used: 65`), so we bumped
  `OPENTHREAD_NUM_MESSAGE_BUFFERS` to 128. But the *fixed* run peaked at
  `max-used: 14` — buffers were a *symptom* of contention, not the cause. We kept
  the bump as headroom but it is not what fixed `error 32`. Lesson: a drained
  pool under contention does not mean the pool size is the bottleneck.
- **The manual `dns resolve`/`dns browse` PASS masked the real failure.** Those
  OT-CLI commands succeeded throughout, which made the discovery gate look fully
  green and sent us hunting elsewhere. CHIP's internal `AddressResolve`/CASE is a
  *different* code path and was the thing timing out. A green CLI resolve is not
  proof the CHIP resolver works.
- **"No commissioning logs" had three different causes at different times**, all
  looking like the same silence: (1) the `pairing` command line was truncated
  over serial so the commissioner got garbage; (2) commissioner INFO logs are
  suppressed at WARN; (3) a stale `There is already a pairing process` lock from a
  prior failed attempt silently rejected new pairings. Each needed a different
  fix (chunked send / read ERROR-only + observe effect / controller reset).
- **Planning a fresh BLE re-commission was unnecessary churn.** We power-cycled
  the LED repeatedly to "reopen its window." Once we realized `error 32` fires
  *post-commit* (node never left the fabric), the right move was to exercise CASE
  on the already-commissioned node (`read-attr`/`invoke-cmd`) — no BLE, no LED
  reset, no dataset re-paste.
- **Is it a hardware wall? No.** The LED commissionee is itself a single-radio C6
  doing BLE+Thread concurrently and it joined fine, which proves one radio can do
  BLE+Thread. The wall was the *third* radio (Wi-Fi AP) on the commissioner, and
  production Option 2 (Hub+RCP) offloads 802.15.4 to the RCP, so it is friendlier
  to commissioning than this native-radio client, not harder.

### Follow-Up

- [done] Implement + build-verify the Stage 0 firmware/config set (offline BR,
  RCP, client commissioner).
- [done] Run Stage 0 hardware DNS-SD gate; record BR/RCP, SRP, client browse,
  and LED service resolution evidence.
- [done] Debug + resolve Matter operational `error 32`: root-caused to single-C6
  radio contention (Wi-Fi softAP + BLE + native 802.15.4); fixed by disabling the
  operator Wi-Fi AP on the commissioner; validated via operational CASE +
  `SetScene` render.
- [done] Fix the `lo-set-scene` `argv[0]` bug — also fixed `lo-set-node-config`
  and `lo-sync-clock`, which shared the pattern (`argc--; argv++;`).
- [todo] Stage 1: the *co-located* Hub commissioner (radio on the RCP) is the
  production path and removes this native-radio contention by construction; the
  operator Wi-Fi AP can be re-enabled there and load-tested alongside BLE.
