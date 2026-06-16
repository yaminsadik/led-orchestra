# ADR: Controller / Border-Router Topology

- **Status:** Accepted — validation complete, **resolved 2026-06-10.** The
  original decision preferred an all-C6 hub; the 2026-06-06 amendment made the
  **S3+H2 one-board hub the primary candidate**; the 2026-06-10 resolution closes
  the gate after the one-board offline Stage C failure and selects the **split
  topology**: **S3+H2 board as BR-only + separate ESP32-C6 controller**.
- **Date:** 2026-06-02 (original); 2026-06-06 (amendment); 2026-06-10 (resolution)
- **Deciders:** project owner + supervisor

## 2026-06-10 Resolution — Split Topology Selected

The one-board S3+H2 hub experiment is closed. **Stage B passed** (S3+H2 as
BR-only, separate C6 controller resolves and controls through it) but the
decisive **offline Stage C failed**: the co-located S3 controller could not
complete operational discovery through its own BR path
(`Commissioning complete ... Error CHIP:0x00000032`). That is enough to reject
the one-board hub as the active architecture.

**Selected architecture:**

```text
Kubernetes / operator / USB / Wi-Fi ingress
  -> separate ESP32-C6 Matter controller / commissioner
  -> S3+H2 board as BR-only (ESP32-S3 esp-thread-br host + ESP32-H2 RCP)
  -> Matter-over-Thread
  -> ESP32-C6 LED nodes
```

This selected split topology is stronger than a paper fallback: it has now
passed Stage B BR resolution, Stage D recovery work, and real multi-node Matter
group control. The older all-C6 split remains historical proof and a deeper
fallback if the S3+H2 BR-only path ever proves unstable. Pi `ot-br-posix`
remains the last resort.

## 2026-06-06 Amendment — S3+H2 One-Board Hub Is The Primary Target

The original decision (below) preferred an **all-C6** hub — former Option 2: a
Hub C6 co-locating the Matter controller + esp-thread-br host, plus an RCP C6.
That "all-C6" preference is **no longer a hard constraint.** The new priority is
the system that works robustly with the **least wiring, least role sprawl, and
minimum operational overhead.**

**New primary hub candidate:** the Espressif **ESP Thread Border Router / Zigbee
Gateway board** (also listed as *ESP Thread BR-Zigbee GW*), which integrates two
SoCs on one board:

- **ESP32-S3-WROOM-1** — hub host: Wi-Fi/BLE, Matter commissioner/controller,
  Thread Border Router host, and the future thin K8s/USB/Wi-Fi ingress.
- **ESP32-H2-MINI-1** — the 802.15.4 Thread RCP radio.
- (Zigbee is ignored entirely.)

LED nodes are unchanged: **ESP32-C6, Thread-only**, never on Wi-Fi, never with a
direct Wi-Fi/API surface. The target product shape is:

```text
Kubernetes / operator / USB / Wi-Fi ingress
  -> S3+H2 hub board (Matter controller + esp-thread-br host + H2 RCP)
  -> Matter-over-Thread
  -> ESP32-C6 LED nodes
```

**Why this supersedes the all-C6 Hub (former Option 2):**

- **Least wiring.** The S3↔H2 link (UART + RESET/BOOT for RCP update) is routed
  on the PCB. There is no breadboard spinel link to hand-wire as in the Stage 0
  all-C6 BR (the `D6/D7` side-pin crossover).
- **Least role sprawl.** One commercial board is the whole hub, versus two or
  three separate C6 boards (controller C6 + BR-host C6 + RCP C6).
- **Radios split by construction.** Wi-Fi/BLE live on the S3; 802.15.4 lives on
  the H2. This structurally removes the **2026-06-04 single-C6 three-radio
  contention** (Wi-Fi softAP + BLE + native 802.15.4 on one 2.4 GHz PHY) that
  caused the operational `error 32` (`CHIP_ERROR_TIMEOUT`) in Stage 0. See
  [`debugging-journal.md`](debugging-journal.md).
- **Supported toolchain path.** esp-matter ships `examples/controller` +
  `sdkconfig.defaults.otbr` (Matter controller + OTBR on `esp32s3`) and
  `examples/thread_border_router` for this exact board, on the pinned
  ESP-IDF v5.4.1 / esp-matter v1.4.2 (no toolchain upgrade needed).

**New locked ladder (each rung taken only if the one above fails its gate):**

| Rung | Topology | Role |
| --- | --- | --- |
| **Primary target** | **S3+H2 one-board hub** (ESP32-S3 controller + esp-thread-br host + thin ingress; ESP32-H2 RCP) + Thread-only C6 LED nodes | Preferred production target **if** it passes the end-to-end hardware gates. |
| **Fallback 1 — all-C6 split** | Controller C6 + BR-host C6 + RCP C6 (the former Option 3, and exactly the **Stage 0** config) | Proven for discovery + operational CASE on 2026-06-04. Use if the S3+H2 hub fails its gate but the C6 BR path is sound. |
| **Fallback 2 — Pi** | Pi/Linux `ot-br-posix` + RCP/dongle (former Option 4) | Final fallback only if the C6/H2 esp-thread-br path itself is not stable. |

**Status of one-board sufficiency: historical failed hypothesis.** That a *single* S3+H2 board can
host the Matter controller **and** the BR host **and** (later) the thin ingress
within heap/flash headroom was **not proven.** It was gated by the staged S3+H2
experiment in
[`controller-topology-validation.md`](controller-topology-validation.md) and the
runbooks in
[`../matter-prototype/s3-h2-hub-validation/`](../matter-prototype/s3-h2-hub-validation/).
The decisive offline Stage C gate failed, so the board is retained only as the
**BR-only** half of the selected split topology.

**What is preserved.** The Stage 0 all-C6 evidence stays as historical proof and
as the Fallback-1 runbook
([`../matter-prototype/stage0-br-validation/`](../matter-prototype/stage0-br-validation/)).
The validation gate stays **evidence-based and quantitative.** The original
all-C6 decision is retained verbatim below for history.

---

## Context (original, 2026-06-02)

LED Orchestra runs a fully offline Matter-over-Thread mesh: an ESP32-C6
controller commissions and drives Thread-only ESP32-C6 LED nodes. See
[`architecture.md`](architecture.md#roles-and-responsibilities) for the role
glossary.

Two forces drive this decision:

1. **A proven bug.** Hardware bring-up on 2026-06-02 showed a single ESP32-C6
   acting as Matter commissioner **and** its own infra-less SRP/DNS-SD owner
   cannot resolve its own operational nodes — operational discovery times out
   (`dns browse` → `Error 28: ResponseTimeout`) even though the SRP record
   exists and the mesh carries traffic. Full evidence in
   [`debugging-journal.md`](debugging-journal.md). The supported fix is a real
   OpenThread Border Router that owns SRP/DNS-SD, per Espressif's host+RCP BR
   shape.

2. **A product constraint and a new ingress.** The product was originally
   intended to be all-C6 (ESP32-C6 ≈ $6 vs. a Pi ≈ $60; single SKU, no OS to
   maintain, fast and predictable recovery after a power-yank). Separately, a
   Kubernetes control plane is being added to author, validate, schedule, and
   store program bundles, talking only to the controller (LED nodes stay
   Thread-only). _(The all-C6 product constraint was relaxed by the 2026-06-06
   amendment above.)_

The open question was **how much of the controller can be co-located with the
border router on one C6**, given the C6's limited RAM/flash.

## Decision (original 2026-06-02 — superseded by the 2026-06-06 amendment above)

> Superseded: the all-C6-first ladder below is retained for history. The
> forward-looking ladder was in the 2026-06-06 amendment — S3+H2 one-board hub
> (candidate) → all-C6 split (Stage 0 config) → Pi. Former Option 2 (all-C6
> co-located Hub C6 + RCP) is superseded by the S3+H2 board, which achieves the
> same one-board hub with Wi-Fi/BLE and 802.15.4 on physically separate SoCs.

Adopt a **validation-gated** topology with an all-C6-first fallback ladder.

| Option | Topology | Role |
| --- | --- | --- |
| **2** | Hub C6 (Matter controller + esp-thread-br host + *thin* K8s bundle gateway) + RCP C6 + Thread-only LED C6s | **Preferred production target** if the hub validates within measured headroom. |
| **3** | Controller C6 + BR-host C6 + RCP C6 (all-C6) | Fallback if the hub is resource-constrained but the C6 BR path is sound. |
| **4** | Pi/Linux `ot-br-posix` + RCP/dongle | Final fallback if the C6 BR path itself is not stable. |

The **locked production ladder is 2 (target) → 3 (all-C6 fallback) → 4 (Pi
fallback)**; each rung is chosen only when the rung above it fails its gate.
Option 1 — a single C6 doing everything — sits **outside** this ladder; see
[Alternatives Considered](#alternatives-considered).

Cross-cutting rules (still in force under the amended ladder):

- **Kubernetes does the heavy lifting** (authoring, validation, scheduling,
  libraries). The hub is a *thin* gateway: cache already-approved bundles and
  relay Matter/Thread commands. Heavy logic is never ported onto the hub.
- **LED control never rides Wi-Fi.** In the all-C6 split fallback the separate
  controller C6 joins the BR-owned Thread mesh over its **own 802.15.4 radio**
  and uses the BR only for SRP/DNS-SD and border routing; controller→LED control
  stays Thread/Matter. Wi-Fi carries only K8s/operator ingress. On the S3+H2 hub,
  the S3's Wi-Fi is ingress/backbone only and the H2 carries all Thread traffic.
- **Size the hub for its job.** The S3+H2 board is 8 MB flash + 2 MB PSRAM
  (4 MB on early samples); LED nodes stay 4 MB C6. The former all-C6 hub was to
  be planned on an 8/16 MB C6.

## Validation Gate

The **S3+H2 one-board hub** is not treated as proven until the staged experiment
in [`controller-topology-validation.md`](controller-topology-validation.md)
passes on the same quantitative metrics (min free heap, largest free block, heap
drift, soak uptime, reboot recovery, discovery success, 20-node scale,
stored-bundle footprint, flash headroom).

History: the decisive first step for the all-C6 path was **Stage 0** — prove a
*separate* Thread client resolves an LED node's `_matter._tcp` record through the
BR (host+RCP), not the BR resolving its own record. **Stage 0 PASSED on
2026-06-04** (discovery + operational CASE), retiring the colocated
self-resolution failure for the all-C6 split. The decisive test for the S3+H2
hub was the **one-node end-to-end gate** (Stage C: the co-located S3 controller
commissions a C6 LED, resolves it through its own H2-backed BR, establishes
CASE, and renders `SetScene`). That gate failed offline. Decision mapping:

```text
Stage B (S3+H2 BR-only + separate C6 controller) PASSED 2026-06-08
Stage C (offline co-located S3+H2 one-board hub) FAILED 2026-06-09
Selected topology 2026-06-10 -> S3+H2 BR-only + separate C6 controller
C6/H2 esp-thread-br path itself not stable -> Pi / ot-br-posix
```

## Consequences

**Positive**

- Fixes the discovery bug on Espressif's supported BR shape (host + RCP), now on
  a board where the RCP radio (H2) is physically separate from the host's
  Wi-Fi/BLE (S3).
- Minimizes wiring and role sprawl: the S3+H2 hub is one board with an on-PCB
  S3↔H2 link, versus two/three hand-wired C6 boards.
- Keeps the live show offline: rendering and already-loaded scheduled programs
  need no Wi-Fi/K8s; only new programs, live overrides, and telemetry use the
  K8s↔hub link.
- Decision is evidence-based, not a guess: the gate selects the rung. The all-C6
  split remains a proven (Stage 0) fallback.

**Negative / risks**

- The S3+H2 one-board hub co-locates the Matter controller + esp-thread-br host
  (+ later the thin gateway) on the S3 — the **unproven** part and the reason for
  the gate. Likely failure mode is heap exhaustion / fragmentation under load
  (PSRAM on the S3 mitigates), which is why the gate is quantitative.
- The stock controller+OTBR build resolves operationally via a Wi-Fi/Ethernet
  backbone + platform mDNS; **offline (backbone-less) co-located discovery is the
  key open risk** — gate it offline-first with a Wi-Fi-backbone diagnostic
  fallback. A hub Wi-Fi backbone does not violate the invariants: LED control
  still rides Thread, and nodes keep rendering their last valid scene offline.
- The BR (S3+H2, or a Pi equivalent) is the main mesh-infrastructure SPOF for
  *new* commissioning and CASE re-resolution. Already-running nodes keep
  rendering their last valid scene; node-stored scheduled programs further reduce
  this exposure. See [`mesh-network.md`](mesh-network.md#resilience-properties).
- The Pi fallback abandons the all-Espressif shape; adopted only if the C6/H2 BR
  path is not stable.

## Alternatives Considered

- **All-C6 co-located Hub (former Option 2) as the primary target.** Superseded
  on 2026-06-06 by the S3+H2 board. It pursued the same one-board hub goal but on
  a single C6 that time-shares one 2.4 GHz PHY across Wi-Fi, BLE, and 802.15.4 —
  exactly the contention that produced the 2026-06-04 operational `error 32`. The
  S3+H2 board reaches the one-board goal with the radios on separate SoCs, so the
  all-C6 co-located Hub is no longer pursued; the all-C6 *split* (below) is the
  retained all-C6 fallback.
- **Option 1 — single C6 for everything.** Explored first; sits outside the
  production ladder. The infra-less form (1a, the original prototype) is rejected:
  proven to fail at operational discovery in
  [`debugging-journal.md`](debugging-journal.md). The esp-thread-br-on-one-C6 form
  (1b, native radio, no RCP) survives only as a low-confidence curiosity; the RCP
  (now the H2) is the safe default. Not a planned target.
- **All-C6 split (Fallback 1 / former Option 3) as the default.** Not chosen as
  default: it adds boards and a SPOF for separation that the S3+H2 hub does not
  need. Retained as the **proven** all-C6 fallback (it is exactly the Stage 0
  configuration) because it relieves hub resource contention without an OS to
  maintain.
- **Pi/Linux as the default brain.** Not chosen as default: adds an OS to
  maintain and a power-loss filesystem-corruption risk, and is unnecessary at
  ~20-node scale where esp-thread-br on the S3+H2 board is sufficient. Retained as
  the final fallback (`ot-br-posix` is the reference OTBR if the C6/H2 BR path
  proves unstable).

## Related Docs

- [`controller-topology-validation.md`](controller-topology-validation.md) — the
  gating experiment and pass/fail metrics (S3+H2 stages + retained Stage 0).
- [`../matter-prototype/s3-h2-hub-validation/`](../matter-prototype/s3-h2-hub-validation/)
  — S3+H2 hub runbooks and committed config.
- [`../matter-prototype/stage0-br-validation/`](../matter-prototype/stage0-br-validation/)
  — the Stage 0 all-C6 evidence (now the Fallback-1 runbook).
- [`debugging-journal.md`](debugging-journal.md) — the bug that motivates this and
  the single-radio contention finding.
- [`mesh-network.md`](mesh-network.md) — confirmed border-router split.
- [`architecture.md`](architecture.md) — role glossary and offline invariants.
- [`matter-thread.md`](matter-thread.md) — custom cluster contract and BR
  decision.
