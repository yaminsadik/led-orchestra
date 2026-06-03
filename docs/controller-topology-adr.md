# ADR: Controller / Border-Router Topology

- **Status:** Accepted — validation-gated. The direction and fallback ladder are
  accepted; selecting Option 2 specifically is gated on the experiment in
  [`controller-topology-validation.md`](controller-topology-validation.md).
- **Date:** 2026-06-02
- **Deciders:** project owner + supervisor (all-C6 preference)

## Context

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

2. **A product constraint and a new ingress.** The product is intended to be
   all-C6 (ESP32-C6 ≈ $6 vs. a Pi ≈ $60; single SKU, no OS to maintain, fast and
   predictable recovery after a power-yank). Separately, a Kubernetes control
   plane is being added to author, validate, schedule, and store program
   bundles, talking only to the controller (LED nodes stay Thread-only).

The open question is **how much of the controller can be co-located with the
border router on one C6**, given the C6's limited RAM/flash.

## Decision

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

Cross-cutting rules for all options:

- **Kubernetes does the heavy lifting** (authoring, validation, scheduling,
  libraries). The C6 hub is a *thin* gateway: cache already-approved bundles and
  relay Matter/Thread commands. Heavy logic is never ported onto the C6.
- **LED control never rides Wi-Fi.** In Option 3 the separate controller C6 joins
  the BR-owned Thread mesh over its **own 802.15.4 radio** and uses the BR only
  for SRP/DNS-SD and border routing; controller→LED control stays Thread/Matter.
  Wi-Fi carries only K8s ingress.
- **The hub C6 is sized up.** Plan it on an 8/16 MB C6; LED nodes stay 4 MB.
  "All-C6" means one chip family and toolchain, not one flash size.

## Validation Gate

Option 2 is not treated as proven until the staged experiment in
[`controller-topology-validation.md`](controller-topology-validation.md) passes
on quantitative metrics (min free heap, largest free block, heap drift, soak
uptime, reboot recovery, discovery success, 20-node scale, stored-bundle
footprint, flash headroom).

The decisive first step is **Stage 0**: prove a *separate* Thread client can
resolve an LED node's `_matter._tcp` record through the BR (host+RCP) — not the
BR resolving its own record. This directly retires the colocated self-resolution
failure. Decision mapping:

```text
Stage 0 FAIL (separate client can't resolve through the BR) -> Option 4
Stage 1+ co-located Hub FAILS (on-board discovery OR headroom) -> Option 3
Stages 0-4 PASS within headroom -> Option 2  [target]
```

## Consequences

**Positive**

- Fixes the discovery bug on Espressif's supported BR shape (host + RCP).
- Keeps the on-site hardware all-C6 in Options 2 and 3 (supervisor constraint).
- Keeps the live show offline: rendering and already-loaded scheduled programs
  need no Wi-Fi/K8s; only new programs, live overrides, and telemetry use the
  K8s↔controller link.
- Decision is evidence-based, not a guess: the gate selects the option.

**Negative / risks**

- Option 2 co-locates controller + BR host + gateway on one C6 — the unproven
  part, and the reason for the gate. Likely failure mode is heap exhaustion /
  fragmentation under sustained load, which is why the gate is quantitative.
- The BR (host + RCP, or its Pi equivalent) is the main mesh-infrastructure SPOF
  for *new* commissioning and CASE re-resolution. Already-running nodes keep
  rendering their last valid scene; node-stored scheduled programs further
  reduce this exposure. See
  [`mesh-network.md`](mesh-network.md#resilience-properties).
- Option 4 abandons all-C6; adopted only if the C6 BR path is not stable.

## Alternatives Considered

- **Option 1 — single C6 for everything.** Explored first; sits outside the
  production ladder. The infra-less form (1a, the original prototype) is rejected:
  proven to fail at operational discovery in
  [`debugging-journal.md`](debugging-journal.md). The esp-thread-br-on-one-C6 form
  (1b, native radio, no RCP) survives only as an **optional "drop-the-RCP"
  optimization attempted after Option 2 validates** — its DNS-SD path is identical
  to Option 2, so it only tests whether one C6 has the headroom without offloading
  the radio. Low-confidence; the RCP is the safe default; not the planned target.
- **Option 3 as the default.** Not chosen as default: it adds a board and a SPOF
  for separation that Option 2 may not need. Retained as the all-C6 fallback
  rung because it relieves hub resource contention without leaving the C6 family.
- **Pi/Linux as the default brain.** Not chosen as default: contradicts the
  all-C6 product constraint, adds an OS to maintain and a power-loss
  filesystem-corruption risk, and is unnecessary at ~20-node scale where
  esp-thread-br is sufficient. Retained as the final fallback (`ot-br-posix` is
  the reference OTBR if the C6 BR path proves unstable).

## Related Docs

- [`controller-topology-validation.md`](controller-topology-validation.md) — the
  gating experiment and pass/fail metrics.
- [`debugging-journal.md`](debugging-journal.md) — the bug that motivates this.
- [`mesh-network.md`](mesh-network.md) — confirmed border-router split.
- [`architecture.md`](architecture.md) — role glossary and offline invariants.
- [`matter-thread.md`](matter-thread.md) — custom cluster contract and BR
  decision.
