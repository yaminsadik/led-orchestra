# Stage F — Thin Ingress (only after hub proof)

**Goal:** add the **thin** K8s/USB/Wi-Fi ingress + bundle cache to the S3+H2 hub
**only after** the core Matter/Thread hub behavior passes (Stages C–E). The hub
stays thin: it **receives an already-validated program bundle, stores it, and
relays/activates it over Matter** — nothing more.

**Topology:** the [`stage-e-scale-soak.md`](stage-e-scale-soak.md) hub + nodes,
plus an off-board control plane (Kubernetes / operator / USB / Wi-Fi) talking to
the **S3** over Wi-Fi/IP for ingress only.

```text
K8s / operator / USB / Wi-Fi  --(ingress: bundle push, telemetry)-->  S3 hub
S3 hub  --(Matter-over-Thread, via H2 RCP)-->  C6 LED nodes
```

**Gate:** all Stage D/E metrics **still pass** with the thin ingress live and a
**resident bundle cache** present — i.e. adding ingress must not regress heap,
flash headroom, discovery, recovery, or latency, and the stored bundle must not
push min free heap below the floor.

> Prereq: **Stage E PASS**. This stage is deliberately last: ingress is worthless
> if the hub cannot reliably render, and it competes with Matter/Thread for the
> S3's RAM/flash — so it is gated on the hub already being proven.

---

## F.1 Invariants this stage must not break

- **LED control never rides Wi-Fi.** Wi-Fi/IP carries only ingress (bundle push)
  and telemetry; controller→node is always Matter-over-Thread via the H2. Verify
  the ingress netif and the Thread path stay separate.
- **Program bundles are data, not code.** The hub stores/relays a bundle; new
  effect *behavior* still ships as firmware via Matter OTA, never as runtime code.
- **Keep-last-valid + offline.** Already-rendering nodes keep their last valid
  scene through an ingress/backbone outage; rendering a loaded scene needs no
  cloud/internet.
- **Hub stays thin.** **No** scheduler, bundle authoring, validation, or heavy
  libraries on the S3 — that logic stays in Kubernetes (see
  [`../../docs/architecture.md`](../../docs/architecture.md)). The hub only
  caches/relays.

## F.2 Bring up the thin ingress

```text
# S3 ingress netif up (the hub may host its own AP / join a local backbone):
matter esp wifi connect <ingress_ssid> <ingress_password>
# confirm the Thread path is unaffected:
matter esp ot_cli state                 # leader/router, unchanged
matter esp ot_cli rcp version           # H2 link healthy under ingress load
```

## F.3 Push + activate a bundle, then re-measure

1. Push **one already-validated** bundle from the control plane to the hub over
   IP; confirm it is **stored** (resident cache) and the heap stays above the
   floor with the bundle resident.
2. **Activate** it: the hub relays/activates the scene over Matter (group
   `SetScene` / the bundle's scenes) — nodes render.
3. **Re-run the Stage D recovery checks and the Stage E metrics** with ingress +
   the resident bundle live. Pull the ingress backbone mid-render and confirm the
   nodes **keep their last valid scene** (offline invariant).

## Evidence (fill at the bench)

```text
Date / operator:
Ingress path (own AP | local backbone) + telemetry on?:
Bundle stored (size) / resident cache heap cost:
Min free heap WITH ingress + bundle resident (>= floor?):
Flash headroom WITH bundle cache (>=25%?):
Activate: nodes rendered the bundle scene? latency:
Stage D recovery re-run WITH ingress: pass/total:
Stage E metrics re-check WITH ingress (heap/discovery/latency): pass?:
Ingress/backbone outage mid-render -> keep-last-valid held? (y/n):
LED control confirmed Thread-only (never Wi-Fi)? (y/n):
Pass/Fail vs gate:
Notes:
```

## Decision

- **PASS** → the S3+H2 one-board hub is validated end-to-end **as the LED
  Orchestra hub**: it commissions, discovers through its own BR, renders at scale
  through a soak, and carries a thin ingress + resident bundle cache without
  regression. Record the result in
  [`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md)
  and update [`../../docs/roadmap.md`](../../docs/roadmap.md) / the READMEs to
  reflect the hub as **proven** (not just a candidate).
- **FAIL — ingress regresses the hub** → keep the hub thin or thinner (drop
  ingress features), or move ingress off the S3 entirely (a separate thin gateway
  in front of the hub). Journal the regression in
  [`debugging-journal.md`](../../docs/debugging-journal.md); the Matter/Thread hub
  proof from Stages C–E still stands.
