# Controller / Border-Router Topology Validation

This is the experiment that decides the production controller/border-router
topology. It turns the validation-gated decision in
[`controller-topology-adr.md`](controller-topology-adr.md) into a concrete,
**quantitative** pass/fail test. The result confirms (or rejects) the primary
hub candidate — it is not run to ratify a foregone conclusion.

**Amended 2026-06-06.** The primary path under test is now the **S3+H2 one-board
hub** (ESP32-S3 Matter controller + esp-thread-br host; ESP32-H2 RCP). The
**all-C6 path** below is retained: its **Stage 0 PASSED on 2026-06-04**
(discovery + operational CASE on a real C6 BR), and it is the proven **Fallback 1**
if the S3+H2 hub fails its gate. The original failure that motivates the whole
gate is in [`debugging-journal.md`](debugging-journal.md): a single ESP32-C6
acting as Matter commissioner **and** its own infra-less SRP/DNS-SD owner could
not resolve its own operational nodes (`dns browse` → `Error 28:
ResponseTimeout`). The fix is a real border router that owns SRP/DNS-SD. What is
*not yet proven* is whether a single S3+H2 board can host the controller **and**
the BR host within headroom.

## Decision Ladder Under Test

| Rung | Topology | Role in the ladder |
| --- | --- | --- |
| **Primary target** | S3+H2 one-board hub (ESP32-S3 controller + esp-thread-br host + thin ingress; ESP32-H2 RCP) | Preferred production target **if** the one board validates within headroom |
| **Fallback 1 — all-C6 split** | Controller C6 + BR-host C6 + RCP C6 (= the **Stage 0** config) | Proven for discovery + operational CASE (2026-06-04). Used if the S3+H2 hub fails its gate but the C6 BR path is sound |
| **Fallback 2 — Pi** | Pi/Linux `ot-br-posix` + RCP/dongle | Final fallback only if the C6/H2 esp-thread-br path itself is not stable |

The **locked ladder is S3+H2 hub → all-C6 split → Pi**; each rung is chosen only
when the rung above it fails its gate. The former all-C6 *co-located* Hub (Hub C6
+ RCP C6) is **superseded** by the S3+H2 board (same one-board goal, radios on
separate SoCs); see the ADR. Option 1 (a single C6 doing everything) sits outside
this ladder: its infra-less form is ruled out.

Wiring rule (load-bearing): on the S3+H2 hub, the S3's Wi-Fi is ingress/backbone
only and the **H2 carries all Thread traffic**; controller→LED control stays on
Thread/Matter and must **not** ride Wi-Fi. In the all-C6 split fallback, the
separate controller C6 joins the BR's Thread mesh over its **own 802.15.4 radio**
and uses the BR only for SRP/DNS-SD and border routing. See
[`architecture.md`](architecture.md#decision-led-nodes-stay-thread-only).

## Pass/Fail Metrics (Quantitative Gate)

Record these for every hardware stage (they apply to the S3+H2 hub and to any
fallback). Targets are **initial** — baseline them on the first clean run, then
ratify the thresholds. "Vibes" do not pass the gate; a number does.

| Metric | How measured | Initial target | Notes |
| --- | --- | --- | --- |
| Min free heap | `esp_get_minimum_free_heap_size()` sampled over the run | ≥ 48 KB sustained | Hard floor for stability headroom; baseline then set. On the S3, internal-RAM heap is the constraint even with PSRAM. |
| Largest free block | `heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)` | ≥ 24 KB sustained | Guards against fragmentation even when total free looks OK. |
| Heap drift | Min free heap trend over the soak | No monotonic downward drift over 72 h | Slow leaks/fragmentation are the silent field failure. |
| Soak uptime | Continuous run without crash/watchdog/reboot | ≥ 72 h | Stage E. |
| Reboot recovery | Power-yank → rejoin → rediscover → resume control | 100% over ≥ 20 cycles, first scene ≤ 60 s | Unclean power-down is the real deployment event. Stage D. |
| Operational discovery | `dns browse` / CASE resolution success rate | 100% over ≥ 50 attempts | Includes re-resolution after node drop/rejoin. |
| Scale | Nodes commissioned and group-controllable | 20 nodes; group `SetScene` reaches all within latency budget | Stage E. |
| Stored bundles | Representative bundle cache resident | Present without pushing heap below floor | Stage F. |
| Flash usage | App + NVS + bundle cache + placeholder OTA image + bundled RCP image vs. partition | ≥ 25% partition headroom | The S3+H2 board is 8 MB; confirm the `partitions_br.csv` layout leaves headroom. |
| RCP health | `rcp version` / RCP reset count over the run | Version returns; no repeated RCP resets | S3↔H2 spinel link stability. |

## Primary Experiment — S3+H2 One-Board Hub (Stages A–F)

Each stage has a runbook in
[`../matter-prototype/s3-h2-hub-validation/`](../matter-prototype/s3-h2-hub-validation/)
with exact build/flash commands, bring-up sequence, expected healthy logs, and a
pass/fail evidence template. Work the stages in order; a failure falls back per
the [Decision Rules](#decision-rules).

| Stage | Goal | Boards | Decisive output |
| --- | --- | --- | --- |
| **A** | Hardware inventory + toolchain compatibility | S3+H2 board (host-only) | A written hardware/toolchain matrix; the board flashing path is understood; the relevant examples build on the pinned toolchain |
| **B** | S3+H2 as a **BR-only** baseline | S3+H2 (BR only) + separate C6 controller (BR off) + 1 C6 LED | A *separate* C6 client resolves the LED's `_matter._tcp` through the S3+H2 BR; CASE + `SetScene` render; no Error 28/32 |
| **C** | **Co-located one-board hub** (the decisive gate) | S3 (controller + BR host) + H2 RCP + 1 C6 LED | The S3 commissions a C6 LED, resolves it through its **own** BR, CASE succeeds, `SetScene` renders |
| **D** | Repeatability + recovery | Stage C set | 100% recovery over the agreed cycle count; no heap drift in the short run |
| **E** | Scale + soak | Stage C set scaled toward ~20 LED nodes | Group `SetScene` reaches all nodes; 72 h soak within the metric targets |
| **F** | Thin K8s/USB/Wi-Fi ingress | Stage E set + thin ingress | All Stage D/E metrics still pass with the thin ingress + a resident bundle cache live |

**Evidence discipline (every hardware gate):** fill the runbook's **Evidence**
block at the bench (decisive log lines, not full dumps), then record the
pass/fail + the metrics here against the [quantitative
gate](#passfail-metrics-quantitative-gate). Anything that fails in a way future
work should remember goes in [`debugging-journal.md`](debugging-journal.md); a
workflow change updates the README / `CLAUDE.md`. "Vibes" do not pass a gate; a
number does. Stage A's host-build portion **PASSED** (2026-06-06): on the pinned
toolchain (ESP-IDF v5.4.1 / esp-matter v1.4.2) the H2 `ot_rcp`, the S3
`thread_border_router`, and the S3 `controller`+OTBR hub all build — after
resolving **Finding F-A1** (the Xtensa S3 toolchain install) and **F-A2** (the BR's
out-of-tree RCP image path). S3 app-partition headroom is **19% (BR) / 24% (hub)**,
at/under the 25% target — re-measure on the board. The bench inventory and Stages
B–F still need hardware. See the 2026-06-06 entry in
[`debugging-journal.md`](debugging-journal.md) and
[`stage-a-inventory.md`](../matter-prototype/s3-h2-hub-validation/stage-a-inventory.md).

### Stage A — Hardware Inventory + Toolchain Compatibility (host-only)

Prove we know the board and build environment before flashing or refactoring.
Confirm: which USB port is the S3 host vs. the H2 RCP; the H2 RCP flashing method
(host-side `AUTO_UPDATE_RCP` vs. direct H2 USB); S3 and H2 flash/module
identities, boot logs, and partition tables; whether the board is the 8 MB flash
+ 2 MB PSRAM part or an early 4 MB sample; and that **ESP-IDF v5.4.1 + the pinned
esp-matter** build the relevant examples (`thread_border_router` for `esp32s3`,
`ot_rcp` for `esp32h2`). **Do not casually upgrade ESP-IDF or esp-matter**; an
upgrade requires written justification, a breakage assessment, and explicit
approval.

**Gate:** a written hardware/toolchain matrix exists and the board flashing path
is understood; the example builds succeed on the pinned toolchain. Runbook:
[`stage-a-inventory.md`](../matter-prototype/s3-h2-hub-validation/stage-a-inventory.md).

### Stage B — S3+H2 As BR-Only Baseline (reuse the Stage 0 client) — **PASSED 2026-06-08**

Replace the hand-wired C6 BR-host + C6 RCP with the official S3+H2 board, while
keeping the existing separate C6 `controller-node` (with the Stage 0
`sdkconfig.client.defaults`: border router **off**, operator Wi-Fi **off**) as
the commissioner/resolver, and a C6 LED node as the accessory. Bring up Thread,
the SRP server, and DNS-SD on the S3+H2 BR; from the separate C6 controller,
resolve the LED's `_matter._tcp` **through the S3+H2 BR**, establish CASE, and
send `SetScene` (`0xFFF1FC00`).

**Gate:** `rcp version` succeeds (S3↔H2 spinel link healthy); BR reaches a
healthy Thread state (`leader`) with `srp server` `running`; the separate C6
controller's `dns browse` returns the LED record **through the S3+H2 BR**; CASE
succeeds; `SetScene` renders on the physical LED; **no Error 28, no Error 32.**
Runbook:
[`stage-b-br-baseline.md`](../matter-prototype/s3-h2-hub-validation/stage-b-br-baseline.md).

**Result — PASSED 2026-06-08.** The separate C6 controller commissioned a C6 LED
node over BLE→Thread, resolved it **through the S3+H2 BR** (`dns browse` →
`49F59A617842C60B-0000000000000001`), established operational CASE, and
`SetScene` rendered solid red/green/blue on a physical strip — no Error 28/32.
Three bench gotchas (a stale operational CASE session needs a commissioner reset;
`lo-set-scene` arg 2 is the **endpoint**; the bench WS2815 is wired RGB while
`led_strip` emits GRB, needing an R/G swap) are recorded in the 2026-06-08
end-to-end [`debugging-journal.md`](debugging-journal.md) entry. Next: **Stage C**
(co-located one-board hub — the decisive gate).

### Stage C — Co-Located One-Board Hub Proof (decisive) — **OFFLINE FAIL 2026-06-09**

Prove the S3 can host **both** the Thread BR host **and** the Matter
commissioner/controller while the H2 is the RCP. Start from esp-matter's
`controller` example built with `sdkconfig.defaults.otbr` for `esp32s3`
(co-located controller + OTBR), and integrate only the minimum LED Orchestra
behavior needed for the gate: commission a C6 LED over BLE→Thread, operational
discovery, CASE, and invoke the custom cluster `0xFFF1FC00` command `SetScene`.
Do **not** port the K8s gateway, bundles, scheduling, or OTA yet. Keep Zigbee
disabled. Preserve the existing custom-cluster contract.

Per the offline product shape, test **backbone-less first** if feasible. The
stock controller+OTBR build resolves operationally via a Wi-Fi/Ethernet backbone
+ platform mDNS (it disables the OT SRP/DNS *clients*), so if a Wi-Fi backbone is
required by the example, document exactly why and confirm LED control still does
**not** ride Wi-Fi (commissioning is BLE; control is Matter-over-Thread; the
backbone is ingress/mDNS only).

**Gate:** the S3 commissions one C6 LED over BLE→Thread; the LED registers
`_matter._tcp` through the board's BR/SRP path; the S3 resolves the operational
node through **its own** BR path; CASE succeeds; the S3 sends `SetScene`; the
physical LED renders. Capture heap, largest free block, flash usage, RCP health,
Thread state, SRP/DNS-SD evidence, and serial logs. Runbook:
[`stage-c-onehub.md`](../matter-prototype/s3-h2-hub-validation/stage-c-onehub.md).

**Result — OFFLINE FAIL 2026-06-09.** The S3+H2 one-board hub formed Thread,
the H2 RCP responded, SRP was running, and the C6 LED joined Thread and
advertised its `_matter._tcp` service. The long commissioning command was sent
with paced writes and echoed the final `20202021 3840`, so this was not the
known USB-serial truncation failure. The gate failed at the co-located
controller's operational discovery step:

```text
Commissioning complete for node ID 0x0000000000000001: Error CHIP:0x00000032
```

That fails the offline Stage C gate. The proven fallback is the Stage B split
topology: S3+H2 BR-only plus a separate C6 controller. The fallback was restored
and revalidated the same day with a fresh C6 LED commissioned as node `3`; DNS
browse returned `...0003` and the controller, and
`lo-set-scene 3 1 3 000000 10 60 301` reached the LED. Details are in
[`debugging-journal.md`](debugging-journal.md) and
[`checkpoints/2026-06-09-stage-b-split-known-good.md`](checkpoints/2026-06-09-stage-b-split-known-good.md).

### Stage D — Repeatability + Recovery

Move from "worked once" to "credible hub." Repeat
commissioning/discovery/CASE/`SetScene` several times; power-cycle the hub and
the LED; verify rediscovery and the first successful scene after reboot; track
min free heap and largest free block over repeated operations.

**Gate:** 100% recovery over the agreed initial cycle count; no monotonic heap
drift in the short run; no DNS-SD regression; no CASE-timeout regression. Runbook:
[`stage-d-recovery.md`](../matter-prototype/s3-h2-hub-validation/stage-d-recovery.md).

### Stage E — Scale + Soak

Scale toward the target node count (eventually ~20 C6 LED nodes); validate group
`SetScene` (group `0x0001`) once at least two nodes are commissioned; run a longer
soak, eventually 72 h. Track min free heap, largest free block, flash headroom,
discovery success rate, RCP resets, reboot recovery, and command latency.

**Gate:** the quantitative thresholds above hold at 20-node scale through the
soak. Runbook:
[`stage-e-scale-soak.md`](../matter-prototype/s3-h2-hub-validation/stage-e-scale-soak.md).

### Stage F — Thin Ingress (only after hub proof)

Add the thin K8s/USB/Wi-Fi ingress + bundle cache **only after** the core
Matter/Thread hub behavior passes (Stages C–E). Receive an already-validated
bundle, store it, relay/activate over Matter. **Do not** port scheduling,
validation, authoring, or libraries onto the S3 — that logic stays in Kubernetes.

**Gate:** all Stage D/E metrics still pass with the thin ingress + a resident
bundle cache live. Runbook:
[`stage-f-ingress.md`](../matter-prototype/s3-h2-hub-validation/stage-f-ingress.md).

## Decision Rules

```text
Stage A FAIL (toolchain can't build / board unknown) -> resolve before proceeding (do not upgrade toolchain without approval)
Stage B FAIL (separate client can't resolve through the S3+H2 BR) -> the C6/H2 esp-thread-br path is suspect -> investigate, then Fallback 2 (Pi / ot-br-posix)
Stage C FAIL (co-located one-board discovery OR heap/stability) -> Fallback 1 (all-C6 split = the proven Stage 0 config)
Stages A-F PASS within headroom -> S3+H2 one-board hub  [primary target]
```

The all-C6 split fallback reuses the **Stage 0** configuration (a separate
controller C6 joining the BR's Thread network over its native radio), which is
already proven for discovery + operational CASE.

## Retained All-C6 Path (historical + Fallback 1)

### Stage 0 — BR Fixes Discovery, Proven From a Separate Client — **PASSED 2026-06-04**

This was the original primary go/no-go for the all-C6 ladder, and the cleanest
mapping back to the 2026-06-02 bug. A **separate** C6 client resolved the real LED
`_matter._tcp` record **through** a real C6 BR-host + RCP, and — after a
radio-contention fix — established an operational **CASE** session and drove a
custom-cluster `SetScene` that rendered on the LED.

**Status (2026-06-04):** Stage 0 is a **full PASS** (discovery + operational
CASE). The BR-host reached `leader`, the SRP server reached `running`, `rcp
version` succeeded, the separate client joined the BR mesh and resolved the LED's
`_matter._tcp` through the BR:

```text
525E53F22D34B3AE-0000000000000002
Port:5540
Host:120633A3E1E984B0.default.service.arpa.
HostAddress:fd42:957b:9cb6:4fbc:d25c:e012:d49c:57d6
```

The follow-on operational `error 32` (`0x32` = `CHIP_ERROR_TIMEOUT`) was
root-caused to **single-C6 radio contention**: the commissioner ran its operator
Wi-Fi softAP **and** BLE **and** native 802.15.4 on one 2.4 GHz PHY. Disabling the
operator Wi-Fi AP on the commissioner build cleared it; the controller resolved
the node through the BR, established CASE (`read-attr`, no timeout), and a
`SetScene` rendered solid red. The OT message pool peaked at `14/128`, so removing
the Wi-Fi PHY — not the buffer bump — was the operative fix. **This is exactly the
contention the S3+H2 board removes by construction** (Wi-Fi/BLE on the S3,
802.15.4 on the H2). Full runbook + decisive evidence:
[`../matter-prototype/stage0-br-validation/README.md`](../matter-prototype/stage0-br-validation/README.md)
and the 2026-06-04 entry in [`debugging-journal.md`](debugging-journal.md).

### All-C6 Co-Located Hub Stages 1–4 — superseded by the S3+H2 stages

The original plan continued Stage 0 into an all-C6 *co-located* Hub (Stage 1:
minimal controller co-located onto the BR host; Stage 2: scale to ~20; Stage 3:
soak + power-yank; Stage 4: add the thin K8s gateway). Those stages are
**superseded** by the S3+H2 Stages B–F above, which pursue the same co-located
hub goal on a board whose radios are physically split. They remain the reference
for the all-C6 split fallback only if Stage C fails: in that case the controller
moves to its own C6 and joins the BR's Thread network over its native radio (the
Stage 0 split), which is expected to restore the headroom a co-located single SoC
lacked.

## Flash Sizing

The S3+H2 board is **8 MB flash + 2 MB PSRAM** (4 MB on early samples). The
controller + esp-thread-br + Wi-Fi + a served OTA image + the bundled RCP image
fit the 8 MB part using `partitions_br.csv` (factory app + `rcp_fw` SPIFFS for
host-side RCP update); the Stage E/F flash-headroom metric confirms ≥ 25%
headroom. PSRAM on the S3 absorbs Matter/BLE/mbedTLS allocations
(`CONFIG_SPIRAM_MODE_QUAD=y` for the board's 2 MB PSRAM), relieving the internal
RAM that constrained the all-C6 hub. LED nodes stay 4 MB C6.

## Non-Goals

- No heavy scheduling, validation, authoring, or library logic on the hub.
- No live LED control over Wi-Fi in any rung (the S3 backbone is ingress/mDNS
  only; the H2 carries Thread).
- No Zigbee.
- No internet/cloud dependency for rendering already-loaded scenes.
- No runtime effect-code upload.

## Related Docs

- [`controller-topology-adr.md`](controller-topology-adr.md) — the decision this
  experiment gates, with the 2026-06-06 amendment.
- [`../matter-prototype/s3-h2-hub-validation/`](../matter-prototype/s3-h2-hub-validation/)
  — the S3+H2 hub runbooks and committed config (Stages A–F).
- [`../matter-prototype/stage0-br-validation/README.md`](../matter-prototype/stage0-br-validation/README.md)
  — the Stage 0 all-C6 runbook + evidence (Fallback 1).
- [`debugging-journal.md`](debugging-journal.md) — the discovery-timeout bug and
  the single-radio contention finding this design removes.
- [`mesh-network.md`](mesh-network.md) — the confirmed border-router split and
  join/control sequence.
- [`matter-thread.md`](matter-thread.md) — custom cluster contract and BR
  decision.
- [`console.md`](console.md) — `ot_cli` Thread/SRP/DNS bring-up and `lo-*` scene
  commands used in these stages.
