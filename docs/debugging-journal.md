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
  _(Superseded 2026-06-06: the co-located hub is now the S3+H2 board — controller
  + esp-thread-br host on the ESP32-S3, radio on the ESP32-H2 RCP — see below.)_

## 2026-06-06: Architecture Pivot To The S3+H2 Hub; S3 Toolchain Gap (Stage A)

### Symptom

Standing up the S3+H2 hub validation, the H2 RCP built fine for `esp32h2`, but
every `esp32s3` build failed immediately at CMake configure:

```text
The CMAKE_ASM_COMPILER:  xtensa-esp32s3-elf-gcc
is not a full path and was not found in the PATH.
HINT: Try to reinstall the toolchain for the chip that you trying to use.
```

### Architecture At The Time

2026-06-06 pivot: the preferred hub is no longer an all-C6 co-located Hub but the
**Espressif ESP Thread BR board** — ESP32-S3 (Matter controller + esp-thread-br
host) + ESP32-H2 RCP — driving Thread-only C6 LED nodes. The all-C6 split (Stage
0) is now the proven fallback. Rationale: least wiring/role sprawl, and Wi-Fi/BLE
(S3) + 802.15.4 (H2) on separate SoCs structurally removes the 2026-06-04
single-C6 three-radio contention. See
[`controller-topology-adr.md`](controller-topology-adr.md) (2026-06-06 amendment).

### What Worked

- ESP-IDF v5.4.1 + esp-matter v1.4.2 (pinned, unchanged) built `ot_rcp` for
  `esp32h2`: `esp_ot_rcp.bin` 0x31a00 (~203 KB), 81% free in the 1 MB app partition.
- The board's S3↔H2 pins match the stock examples (UART rx17/tx18; RCP update
  reset7/boot8; bundled `/rcp_fw/ot_rcp`), so no SDK patching is needed — the
  committed overlays layer onto stock `thread_border_router` / `controller`.

### What Failed

- All `esp32s3` builds (`thread_border_router`, `controller`+OTBR) — at configure,
  before compiling anything.

### Hypotheses And Experiments

| Hypothesis | Experiment | Result |
| --- | --- | --- |
| ESP-IDF env not sourced | `idf.py --version` → v5.4.1; the H2 build worked | Rejected |
| esp32s3 (Xtensa) toolchain not installed | `ls ~/.espressif/tools` → only `riscv32-esp-elf*` | **Confirmed** |

### Decisive Evidence

`~/.espressif/tools` held `riscv32-esp-elf`, `riscv32-esp-elf-gdb`, `cmake`,
`ninja`, `openocd-esp32`, `esp-rom-elfs` — **no `xtensa-*`**. The repo had only
ever targeted RISC-V chips (C6/H2), so the Xtensa toolchain was never installed.

### Resolution

Installed the S3 target compiler for the **same** pinned IDF (NOT a version
upgrade), operator-approved 2026-06-06: `"$IDF_PATH/install.sh" esp32s3`, then
re-sourced `export.sh`. `xtensa-esp32s3-elf-gcc` 14.2.0 then resolved under
`~/.espressif/tools/xtensa-esp-elf/...` and `idf.py --version` stayed v5.4.1. Both
S3 builds passed: `thread_border_router.bin` ~1.88 MB (19% app-partition free,
`rcp_fw` bundled) and the `controller`+OTBR hub `controller.bin` ~2.29 MB (24%
free). Finding F-A1 in
[`../matter-prototype/s3-h2-hub-validation/stage-a-inventory.md`](../matter-prototype/s3-h2-hub-validation/stage-a-inventory.md).

A second, smaller gotcha (**F-A2**) surfaced on the first S3 BR build: the
`thread_border_router` (`CONFIG_AUTO_UPDATE_RCP=y`) generates its `rcp_fw` image
from `CONFIG_RCP_SRC_DIR`, which defaults to the **in-tree**
`$IDF_PATH/examples/openthread/ot_rcp/build`. We build the RCP out-of-tree, so the
build died with `FileNotFoundError: .../ot_rcp/build/rcp_version`. Fixed by having
`build-s3-hub.sh build_br` build the RCP first and override `CONFIG_RCP_SRC_DIR` to
our `build/rcp-h2` via a gitignored defaults fragment.

### Lessons

- "All-C6" was load-bearing in the *toolchain*, not just the BOM: moving the hub
  to an Xtensa S3 needs `install.sh esp32s3` once. A RISC-V-only install builds C6
  and H2 but silently lacks the S3 compiler until you try.
- A green H2 (RISC-V) build does not imply an S3 (Xtensa) build will even
  configure — different architectures, different toolchains.
- Out-of-tree example builds can still depend on an **in-tree default path**: the
  BR's `esp_rcp_update` reads `CONFIG_RCP_SRC_DIR` (defaulting to the stock
  `ot_rcp/build`), so an out-of-tree RCP build must be pointed at explicitly (F-A2).

### Follow-Up

- [done 2026-06-06] Operator approved + ran `install.sh esp32s3`; host-verified
  `build-br` + `build-hub` on the pinned toolchain (sizes in Stage A). F-A1
  resolved; F-A2 (RCP path) fixed in `build-s3-hub.sh`.
- [todo] Run the S3+H2 Stages B/C on hardware; record discovery/CASE/SetScene
  evidence — especially whether co-located discovery works **offline** or needs a
  local Wi-Fi backbone — in
  [`controller-topology-validation.md`](controller-topology-validation.md).
- [todo] Re-measure flash headroom on the 8 MB board (the host build shows the
  S3 app partition at 19%/24% free, at/under the 25% gate target).

## 2026-06-06 — S3+H2 Stage B First Flash: S3 OK, RCP Path Blocked

### Context

First hardware pass on `feat/s3-h2-hub` with the ESP Thread BR-Zigbee GW board,
using the Stage B `thread_border_router` build from
[`../matter-prototype/s3-h2-hub-validation/`](../matter-prototype/s3-h2-hub-validation/).

### What Worked

- Branch was already on `feat/s3-h2-hub`.
- S3 bench identity passed on `/dev/cu.usbmodem1101`: ESP32-S3 rev v0.2, 8 MB
  flash, 2 MB embedded PSRAM detected at boot.
- H2 bench identity passed on the board's USB1 side: ESP32-H2 rev v1.2, MAC
  `10:51:db:ff:fe:67:93:95`.
- `build-s3-hub.sh build` passed for H2 RCP, S3 BR, and S3 hub after exporting
  `ESP_MATTER_PATH` before sourcing `esp-matter/export.sh`.
- `flash-br /dev/cu.usbmodem1101` erased and flashed the Stage B BR image plus
  bundled `rcp_fw` partition.
- Direct H2 flash succeeded at 115200 baud using the built `ot_rcp` artifacts.

### What Failed

The Stage B BR did not reach the `rcp version` gate. On boot it entered an
RCP-update reboot loop:

```text
OPENTHREAD: spinel UART interface initialization completed
P-SpinelDrive-: Wait for response timeout
OpenThread: Internal RCP Version: openthread-esp32/4c2820d377-005c5cefc; esp32h2; 2026-06-06 23:36:41 UTC
RCP_UPDATE: Failed to connect to RCP
RCP firmware not found in storage, will reboot to try next image
```

`Internal RCP Version` is the bundled image version from S3 flash, not proof that
the S3 read the H2. The preceding spinel timeout means the live S3-to-H2 RCP link
did not answer.

### Current Read

The generated `rcp_fw` SPIFFS image contains only `/rcp_fw/ot_rcp_0/rcp_image`.
When the updater marks that failed, it flips to sequence 1; because no
`ot_rcp_1` image exists, the next boot reports "firmware not found" and flips
back. That explains the alternating loop, but not the root cause.

The official schematic shows two USB paths: USB1 for ESP32-H2 and USB2 for
ESP32-S3. USB1 direct flash is now proven, but `idf.py`/esptool at 460800 baud
dropped with `Device not configured` during erase. Manual esptool at 115200 baud
erased and wrote bootloader, partition table, and `esp_ot_rcp.bin` successfully.
`build-s3-hub.sh flash-rcp` now defaults direct-H2 flashing to 115200
(`RCP_FLASH_BAUD` can override it).

Next, reconnect USB2/S3 and reflash the Stage B BR to clear the old RCP updater
NVS state. If the direct-flashed H2 gives a healthy `rcp version` but host-side
auto-update still fails from a clean S3 flash, the remaining suspect is the
S3-controlled H2 reset/boot update path.

### Follow-Up

- [done] Plug the H2 USB1 side and identify the H2 port with `esptool chip_id`.
- [done] Flash `ot_rcp` directly to the H2 once.
- [next] Reconnect USB2/S3, erase/reflash Stage B BR, then retry `rcp version`.
- [next] If direct-flashed RCP works but `CONFIG_AUTO_UPDATE_RCP` still loops,
  add a no-auto-update Stage B diagnostic overlay and record host-side RCP update
  as the failing subpath.

## 2026-06-07 — S3+H2 Stage B Diagnostic: Auto-Update Is Not The Only Failure

### Context

After direct-flashing the H2 `ot_rcp`, we unplugged H2 USB1 and left only S3 USB2
present (`/dev/cu.usbmodem101`). The goal was to isolate the live S3 UART -> H2
RCP link from host-side `esp_rcp_update`.

### Diagnostic Added

Added a reproducible no-auto-update BR build:

- `matter-prototype/s3-h2-hub-validation/sdkconfig.s3-br-direct-rcp.defaults`
- `build-s3-hub.sh build-br-direct`
- `build-s3-hub.sh flash-br-direct`
- `build-s3-hub.sh monitor br-direct`

The generated config was verified to contain:

```text
# CONFIG_AUTO_UPDATE_RCP is not set
CONFIG_OPENTHREAD_RADIO_SPINEL_UART=y
```

The S3 diagnostic flash succeeded. App size was `0x1d9b00`; smallest app
partition is `0x250000`, leaving `0x76500` bytes (20%) free.

### Result

The no-auto-update diagnostic still failed before the `rcp version` gate. This
removed the RCP-update SPIFFS sequence as the only cause:

```text
OPENTHREAD: spinel UART interface initialization completed
P-SpinelDrive-: Wait for response timeout
P-SpinelDrive-: Wait for response timeout
P-SpinelDrive-: Failed to reset co-processor!
Platform------: ResetCoprocessor() at spinel_driver.cpp:160: Failure
```

The S3 then aborted and rebooted. Unlike the auto-update build, there was no
`RCP_UPDATE` loop and no repeated GPIO18 reuse warning; the failure is simply
"S3 cannot get a spinel response from H2."

### Current Read

The direct H2 flash path is proven, and both the H2 `ot_rcp` and S3 spinel host
are configured at 460800 baud. The remaining unresolved question is whether the
H2 is actually booting the RCP app when the S3 tries to talk to it, or whether
the on-board UART/reset/boot path is not in the expected state.

One important clue: monitoring H2 USB1 earlier showed the H2 in ROM download
mode:

```text
boot:0x4 (DOWNLOAD(USB/UART0))
waiting for download
```

Unplugging H2 USB1 did not fix the S3 link, so USB1 alone is not sufficient as an
explanation. Resume by proving H2 app boot in isolation: plug H2 USB1 only, reset
with BOOT released, and verify it does not stay in ROM download mode. If H2 boots
normally but S3 still times out, focus on S3 GPIO17/18 UART reachability and the
S3 GPIO7/GPIO8 reset/boot control path.

## 2026-06-08 — S3+H2 br-direct Retest Passed The Spinel Boundary

### Context

Retested after plugging in S3 USB2 only. macOS exposed the single visible modem as
`/dev/cu.usbmodem1101`, but `esptool.py -p /dev/cu.usbmodem1101 chip_id`
identified it as the S3:

```text
Chip is ESP32-S3 (QFN56) (revision v0.2)
MAC: 9c:13:9e:0a:46:88
```

### Result

The current S3 `br-direct` diagnostic image no longer failed at
`ResetCoprocessor()`. It reached the previous failure boundary and continued:

```text
OPENTHREAD: spinel UART interface initialization completed
P-SpinelDrive-: co-processor reset: RESET_POWER_ON
P-SpinelDrive-: Software reset co-processor successfully
OpenThread attached to netif
OpenThread started: OK
Server initialization complete
```

This proves the live S3 UART/reset path can talk to the direct-flashed H2 RCP
under the current physical state. The earlier direct-diagnostic failure is now
most likely stateful or physical (for example H2 BOOT/reset/download-mode state),
not a hard proof that GPIO17/18 or GPIO7/8 are unusable.

### Console Notes

The prompt exposed by this image is the Matter console, not a raw OpenThread CLI.
Direct commands such as `rcp version`, `state`, and `ifconfig` returned
`Error: -1`. The runbook's Matter-wrapped forms also failed in this image:

```text
matter esp ot_cli rcp version
Error: 83886338
matter esp ot_cli state
Error: 83886338
matter esp ot_cli srp server state
Error: 83886338
```

`help` showed the top-level Matter command, and `matter stat peak` worked:

```text
Packet Buffers: 2
Timers: 3
TCP endpoints: 0
UDP endpoints: 2
Exchange contexts: 0
Unsolicited message handlers: 5
Platform events: 0
```

### Current Read

The direct-flashed-H2 hardware path is alive, but Stage B still needs a usable BR
control surface to create/export a Thread dataset, start Thread, and inspect SRP.
The earlier Stage B `CONFIG_AUTO_UPDATE_RCP` path remains an unresolved separate
subpath: reflash/monitor `br` only if host-side RCP update is the next thing being
tested.

## 2026-06-08 — S3+H2 br-direct OT CLI Bridge Restored

### Symptom

The S3 BR image could boot, reset the H2 RCP, and attach OpenThread, but the
Matter console did not expose the OpenThread CLI bridge. The wrapped commands
needed by Stage B returned `Error: 83886338`.

### Fix

Created `matter-prototype/s3-h2-hub-validation/thread_border_router_otcli/`, a
local copy of Espressif's `thread_border_router` app shell, and registered the
OT CLI bridge before console init:

```cpp
esp_matter::console::otcli_register_commands();
```

The SDK checkout remains unpatched. `build-s3-hub.sh` now points Stage B BR
builds at this local app shell while still using the stock esp-matter and
ESP-IDF components.

### Evidence

Static build evidence showed the final image compiled
`thread_border_router_otcli/main/app_main.cpp` and linked
`esp_matter_console_otcli.cpp.obj`:

```text
app_main.cpp.obj -c .../thread_border_router_otcli/main/app_main.cpp
esp_matter_console_otcli.cpp.obj ... (_ZN10esp_matter7console23otcli_register_commandsEv)
```

After `flash-br-direct /dev/cu.usbmodem1101`, the S3 booted, reset the H2, and
the console exposed `ot_cli`:

```text
matter esp help
    ot_cli: Openthread cli commands. Usage: matter esp ot_cli <command>.

matter esp ot_cli rcp version
openthread-esp32/4c2820d377-005c5cefc; esp32h2; 2026-06-07 01:01:49 UTC

matter esp ot_cli state
disabled

matter esp ot_cli srp server state
disabled
```

`disabled` is expected before B.2 dataset/thread bring-up. The important result
is that Stage B now has a usable BR control surface on the direct-flashed-H2
path.

### Follow-Up

Stage B B.2 passed immediately after this fix. The BR created and committed a new
dataset, exported active TLVs, raised the OpenThread netif, started Thread, became
`leader`, and reached `srp server state = running` after `srp server enable`.

Active dataset TLVs:

```text
0e08000000000001000000030000154a0300001035060004001fffe00208a4722531cc1f59020708fd783f10c811bc78051056e95233105cc18d2419a9f1839e5364030f4f70656e5468726561642d643431610102d41a04108ab87ae955aa7c39249a334e01c5beea0c0402a0f7f8
```

Continue Stage B B.3/B.4: bring the separate C6 controller and LED back into the
DNS/CASE/SetScene gate. The host-side `CONFIG_AUTO_UPDATE_RCP` path remains
unresolved separately.

## 2026-06-08 — S3+H2 BR Answers a Separate C6 Client

### Result

Stage B progressed past the S3/H2 bring-up boundary. The separate ESP32-C6
controller was reflashed with the Stage 0 client overlay using
`EXTRA_SDKCONFIG_DEFAULTS` and the Stage 0 client build directory. The generated
config confirmed the intended pure-client shape:

```text
# CONFIG_OPENTHREAD_BORDER_ROUTER is not set
CONFIG_LED_ORCHESTRA_OPERATOR_WIFI_MODE_DISABLED=y
CONFIG_OPENTHREAD_NUM_MESSAGE_BUFFERS=128
```

The C6 controller joined the S3+H2 BR dataset and reached `router`. After the S3
BR monitor reset the S3, `srp server enable` had to be rerun on the BR; once SRP
was running again, the C6 saw the SRP service in Thread network data.

### Evidence

The C6 registered a throwaway SRP `probe` service and browsed the BR's DNS-SD
responder:

```text
matter esp ot_cli dns browse _matter._tcp.default.service.arpa
DNS browse response for _matter._tcp.default.service.arpa.
49F59A617842C60B-000000000001B669

probe

Done
```

After removing the probe and rebooting/rechecking the C6, a later browse still
returned the controller's `_matter._tcp` record through the S3+H2 BR:

```text
49F59A617842C60B-000000000001B669
    Port:5580, Priority:0, Weight:0, TTL:7175
    Host:F6B47AAAFC8975AE.default.service.arpa.
    HostAddress:fd78:3f10:c811:bc78:ad94:fe8:844b:8320 TTL:7175
```

Buffer headroom at the checkpoint:

```text
C6 client bufferinfo: total 128, free 126, max-used 8
S3 BR bufferinfo:     total 65, free 63, max-used 14
```

### Notes

The Stage B runbook's original `-D SDKCONFIG_DEFAULTS=...` controller recipe did
not apply the Stage 0 client overlay because `controller-node/CMakeLists.txt`
intentionally sets its own `SDKCONFIG_DEFAULTS`. Use
`EXTRA_SDKCONFIG_DEFAULTS="$S0/sdkconfig.client.defaults"` plus
`-D SDKCONFIG="$S0/build/client/sdkconfig"` instead.

One transient C6 OpenThread timer panic occurred after SRP probe cleanup while
closing the monitor:

```text
Guru Meditation Error: Core 0 panic'ed (Instruction access fault)
ot::Timer::Scheduler::ProcessTimers(...)
```

It did not repeat on the next monitor close. The C6 rebooted, came back as
`router`, and DNS browse still passed. Treat it as a residual observation unless
it recurs during LED commissioning.

Next: plug/power the C6 LED node, pair it from the C6 controller with the S3 BR
dataset, then run the real LED `_matter._tcp` browse and CASE/`SetScene` gate.

## 2026-06-08 — Stage B End-to-End PASS: Commission → Resolve → CASE → Render

### Symptom

This is the entry for the *successful* Stage B gate, plus the three things that
fought us on the way to a clean physical render. Final result: a separate C6
controller commissioned a C6 LED node over BLE→Thread, resolved it **through the
S3+H2 BR**, established operational CASE, and `SetScene` rendered the correct
colors on a physical strip. No Error 28; no Error 32 on the render runs.

### Architecture At The Time

| Device | Role |
| --- | --- |
| S3+H2 board | Thread Border Router only (`thread_border_router_otcli`, direct-flashed H2); owns SRP/DNS-SD |
| C6 controller | Matter commissioner/resolver (Stage 0 client overlay: BR off, operator Wi-Fi off) |
| C6 LED node | Matter accessory, cluster `0xFFF1FC00`, WS2812-family strip on GPIO2 |

### What Worked

- **Commissioning attempt 1 succeeded when the controller was rebooted
  immediately before pairing** (clean BLE stack): PASE → `AddNOC` → Thread attach
  → SRP register → CASE → `CommissioningComplete`; the LED logged
  `lo_led_node: commissioning complete`.
- `dns browse _matter._tcp.default.service.arpa` on the controller returned the
  LED's operational instance **through the S3+H2 BR**:
  `49F59A617842C60B-0000000000000001` (alongside the controller's `…1B669`).
- After a fresh controller reset, CASE established in ~1 s and `SetScene`
  rendered each color (renderer logged `scene seq=… effect=1 rgb=…`).

### What Failed

### Hypotheses And Experiments

| Hypothesis | Experiment | Result |
| --- | --- | --- |
| BLE commissioning is flaky (~50%) from radio contention | Reboot the C6 controller right before each `pairing ble-thread` | Reboot → clean BLE stack; attempt-1-after-reboot completed every time. Retrying *without* a reboot hit `Disabling CHIPoBLE service due to error: ac` and crashed |
| `operational discovery failed: 32` during commissioning is fatal | Let it retry instead of aborting | Benign race: the commissioner's first discovery outran the LED's SRP registration; the retry resolved and CASE established |
| `SetScene` silently does nothing after a node reboot | Send to a healthy/registered node with SRP running; watch node + controller logs | **No** node-side `Received command`, **no** discovery logs — the controller reused a **stale CASE session** to the node's pre-reboot address and never re-resolved |
| Resetting the controller clears the stale session | Reset controller, resend `SetScene` | Fresh resolve + CASE in ~1 s; render landed. (An intermediate run with a passive controller took ~110 s as it slowly worked through stale state) |
| Commanded red rendering green is a wiring/level fault | Compare commanded vs observed across R/G/B | Blue was correct, red↔green were swapped → **color-order** mismatch, not wiring |

### Decisive Evidence

Operator footgun first — `lo-set-scene <node> <endpoint> <effect> <rrggbb>
<speed> <brightness> [seq] [start-ms]`: **arg 2 is the endpoint (must be `1`)**,
not a sequence. Commands sent with `1 21 1 …` were accepted by the controller
(`invoke destination=0x1 endpoint=21 …`) but silently no-op at the node — there
is no endpoint 21. Correct form renders:

```text
invoke destination=0x1 endpoint=1 cluster=0xFFF1FC00 command=0x0 {…}
CASE Session established to peer: <…1B669, 1>
lo_renderer: scene seq=201 effect=1 rgb=255,0,0 speed=0 brightness=255 start=0
```

Color order — the bench strip is a **12 V WS2815 wired RGB**, but the bundled
`espressif__led_strip` component only defines `LED_PIXEL_FORMAT_GRB` / `GRBW`
(no RGB). With GRB on the wire, the RGB strip reads byte0/byte1 swapped, so a
commanded red `(255,0,0)` emits `[g,r,b]=[0,255,0]` and the strip shows green;
blue is identical in both orders. Verified by the symptom matrix above.

### Resolution

- **Render fix:** swap the R/G channel values at the `led_strip_set_pixel` call
  in `led-node/main/led_orchestra_renderer.cpp` (renderer still configures
  `LED_PIXEL_FORMAT_GRB`; the swap makes the *wire* order RGB). Reflashed with a
  plain `idf.py flash` (no NVS erase) so the LED stayed commissioned; red/green/
  blue then rendered correctly on the physical strip (operator-confirmed).
- **CASE fix:** reset the commissioner to drop stale operational sessions after a
  node reboots; do not rely on it to re-resolve a dead session on its own.
- **Commissioning workflow:** reboot the C6 commissioner between BLE pairing
  attempts.

Stage B gate: **PASS** end-to-end.

### Lessons

- A stale CASE session is invisible: `SetScene` is acknowledged by the controller
  yet never reaches the node. If a node was power-cycled, reset the commissioner
  before chasing "discovery" ghosts.
- Always confirm the strip's actual **wire color order**; `led_strip` only emits
  GRB, so an RGB strip needs an R/G swap (or a GRBW/SK6812 strip a different one).
- `lo-set-scene` arg 2 is the **endpoint**, not a sequence — a wrong endpoint
  fails *silently* at the node.
- The C6 commissioner still single-radios BLE + 802.15.4; the reboot-between-
  attempts crutch goes away on the real hub, where the **S3 owns BLE/Wi-Fi and
  the H2 owns 802.15.4**.

### Follow-Up

- Decide the committed form of the color-order swap: the documented strip target
  is WS2812B (GRB); the bench strip is WS2815 (RGB). Candidate: a Kconfig strip
  color-order option (default GRB) instead of the current hardcoded bench swap.
- Proceed to Stage C (co-located one-board hub) — the decisive gate.

## 2026-06-09 — Node 2 Commissioned; Next-Node Runbook Locked In

### Result

Commissioned a second C6 LED node on the existing Stage B bench without any
firmware rebuild:

| Device | Port / role |
| --- | --- |
| S3+H2 board | `/dev/cu.usbmodem1301`; `thread_border_router_otcli`; Thread router; SRP/DNS-SD owner |
| C6 controller | `/dev/cu.usbmodem4`; separate commissioner/resolver; fabric intact |
| C6 LED node 2 | `/dev/cu.usbmodem1101`; commissioned as Matter node id `2` |

The successful pairing command was the same dataset TLV from Stage B:

```text
matter esp controller pairing ble-thread 2 0e08000000000001000000030000154a0300001035060004001fffe00208a4722531cc1f59020708fd783f10c811bc78051056e95233105cc18d2419a9f1839e5364030f4f70656e5468726561642d643431610102d41a04108ab87ae955aa7c39249a334e01c5beea0c0402a0f7f8 20202021 3840
```

### What Actually Matters Next Time

Do **not** re-debug commissioning from scratch when adding the next LED node.
The failure mode was already known: long USB-Serial/JTAG console writes can be
silently truncated. Use `sercap.py` or an equivalent paced writer; confirm the
controller echo includes the final `20202021 3840` before trusting the attempt.
Do not rebuild for `CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH`; that was a red
herring for this path.

Known-good sequence for the next node:

```text
# BR: SRP is lost if the S3 monitor/open reset the BR.
matter esp ot_cli srp server state
matter esp ot_cli srp server enable      # if disabled/stopped
matter esp ot_cli srp server state       # expect running
matter esp ot_cli dataset active -x

# Controller: reset immediately before BLE pairing.
# Node: reset/new NVS node and confirm "commissioning window opened".

# Controller: send through sercap.py/chunked writes, not one pyserial dump.
matter esp controller pairing ble-thread <node-id-k> <dataset_tlvs> 20202021 3840

# Verdict.
matter esp ot_cli dns browse _matter._tcp.default.service.arpa
lo-set-scene <node-id-k> 1 3 000000 10 60 <seq>
```

Successful node 2 evidence:

- Node 2 logged Thread attach, detected the BR SRP server, advertised
  `49F59A617842C60B-0000000000000002._matter._tcp`, established CASE, then
  logged `Received CommissioningComplete`, `Commissioning completed successfully`,
  and `lo_led_node: commissioning complete`.
- Controller `dns browse _matter._tcp.default.service.arpa` returned node 2
  (`49F59A617842C60B-0000000000000002`) alongside the controller record
  (`...000000000001B669`). After node 1 was powered back up, the browse returned
  node 1, node 2, and the controller.
- Direct Fibonacci scene control worked for both nodes:

  ```text
  lo-set-scene 1 1 3 000000 10 60 31
  lo-set-scene 2 1 3 000000 10 60 32
  ```

  `effect=3` is Fibonacci. `brightness=60` (about 24% of 255) is the runtime
  power-shortage workaround that stopped the bench blink/recover cycle. Do not
  confuse this with 70% brightness (`179`), which is too high for the workaround.

### Open Caveat

The helper command `lo-set-scene 0x0001 ...` logged as `invoke destination=0x1`,
so on this bench it behaved like node id `1`, not a proven multicast groupcast.
Use direct per-node commands for power-limited bench testing until the real group
path is configured and verified via the Stage E group `--is-group-cmd` flow.
