# CLAUDE.md

Guidance for working in this repo. Keep durable design in `docs/`; this file is
the quick orientation + the rules that are easy to violate.

## What This Is

LED Orchestra: a distributed, offline addressable-LED show. Up to ~20 ESP32-C6
LED nodes each render one WS2812B segment of a single virtual strip, driven over
Matter-over-Thread by a hub. The **LED nodes are ESP32-C6**; the **hub is the
Espressif ESP Thread BR board (ESP32-S3 host + ESP32-H2 RCP)**. The product is
**all-Espressif** and **offline** (rendering needs no venue Wi-Fi, cloud, or
internet). `main` is C++ ESP-IDF/ESP-Matter only; the Rust Phase 1/2 proof is
archived on `archive/rust-phase-2`.

## Locked Architecture

The controller/border-router topology is a **validation-gated** decision. Read,
in order: [docs/architecture.md](docs/architecture.md),
[docs/controller-topology-adr.md](docs/controller-topology-adr.md),
[docs/controller-topology-validation.md](docs/controller-topology-validation.md).

- A single infra-less C6 cannot self-resolve operational Matter nodes, so a real
  **OpenThread Border Router is required** (confirmed on hardware; see
  [docs/debugging-journal.md](docs/debugging-journal.md)).
- Production target (**amended 2026-06-06**): the **S3+H2 one-board hub** —
  ESP32-S3 (Matter controller + esp-thread-br host + thin Kubernetes gateway) +
  ESP32-H2 RCP. Fallbacks: the proven **all-C6 split** (the Stage 0 config), then
  a Pi `ot-br-posix`. The former all-C6 co-located Hub C6 + RCP C6 is superseded
  by the S3+H2 board (same one-board goal, radios on physically separate SoCs).
- **One-board sufficiency is a HYPOTHESIS** until the S3+H2 hardware gates pass.
  Do not claim the board is the hub until the one-node end-to-end gate
  (commission → resolve through the BR → CASE → `SetScene` → render) passes.
- **Kubernetes** is the off-board control plane (authoring/validation/scheduling
  of declarative program bundles); the hub stays thin and only caches/relays.
- **Next implementation work:** Phase 4 **Stage 0 (all-C6) PASSED on hardware
  (2026-06-04)** — a *separate* client resolved an LED node's `_matter._tcp`
  through the C6 BR (host + RCP) and an operational-CASE `SetScene` rendered,
  once the commissioner's Wi-Fi softAP was dropped to clear single-C6 three-radio
  (Wi-Fi + BLE + 802.15.4) contention. The **S3+H2 board removes that contention
  by construction** (Wi-Fi/BLE on the S3, 802.15.4 on the H2). Next are the
  **S3+H2 hub Stages A-F**: inventory/toolchain (A), S3+H2 BR-only baseline (B),
  the co-located one-board hub gate (C), recovery (D), scale+soak (E), thin
  ingress (F). Runbooks + committed config:
  [matter-prototype/s3-h2-hub-validation/](matter-prototype/s3-h2-hub-validation/).
  See [docs/controller-topology-validation.md](docs/controller-topology-validation.md)
  and the 2026-06-04 entry in
  [docs/debugging-journal.md](docs/debugging-journal.md).

## Invariants (do not break)

- **LED nodes are Thread-only.** They never join Wi-Fi or expose an API.
- **LED control never rides Wi-Fi.** Wi-Fi/IP carries only Kubernetes ingress and
  telemetry; controller→node is always Matter-over-Thread.
- **Effects are pure functions** of `(global_index, time_ms, params, context)`;
  no per-node state needed to stay in sync.
- **Effect ids and cluster wire ids are append-only.** Never reorder or reuse.
- **Program bundles are data, not code.** New effect *behavior* ships as compiled
  firmware via Matter OTA.
- **Keep-last-valid:** a node keeps rendering its last valid scene/bundle if a
  bad command arrives or contact drops.

## Build

```bash
. "$HOME/esp/esp-idf/export.sh"
. "$HOME/esp/esp-matter/export.sh"
export IDF_CCACHE_ENABLE=1

# LED node + (all-C6 fallback) controller node — ESP32-C6
cd matter-prototype/led-node && idf.py set-target esp32c6 && idf.py build
cd ../controller-node && idf.py set-target esp32c6 && idf.py build

# S3+H2 one-board hub (primary target). The helper layers our overlays onto the
# stock esp-matter examples (S3 controller+OTBR hub, H2 ot_rcp); see the runbooks:
#   matter-prototype/s3-h2-hub-validation/build-s3-hub.sh build
```

Pinned toolchain: **ESP-IDF v5.4.1 + esp-matter release/v1.4.2** (do not upgrade
without explicit approval; see Stage A). The S3 hub and H2 RCP build on this same
pinned toolchain.

Operator AP credentials live in the gitignored
`matter-prototype/controller-node/sdkconfig.defaults.local` (copy from the
`.example`). Never commit SSID/password or other secrets.

## Documentation Discipline

When you add, rename, remove, or change a controller console/terminal command,
or a cluster command/attribute/effect id, **update the docs in the same change**:

- [docs/console.md](docs/console.md) — operator command reference (authoritative).
- [matter-prototype/cluster/led-orchestra.md](matter-prototype/cluster/led-orchestra.md)
  — the field/tag contract.
- [matter-prototype/controller-node/README.md](matter-prototype/controller-node/README.md)
  — the implemented-slice list.

(This policy is mirrored for Cursor users in `.cursor/rules/console-commands.mdc`.)
Phase *numbering* lives only in [docs/roadmap.md](docs/roadmap.md) and the README;
other docs refer to phases by name (e.g. "the OTA phase") to avoid drift.

## Repo Hygiene

- `managed_components/`, `build/`, `sdkconfig*`, and `dependencies.lock` are
  per-project gitignored build state — do not commit them.
- There is no `firmware/` (Rust) tree on `main`; that work is on
  `archive/rust-phase-2`.
- Record failures worth remembering in
  [docs/debugging-journal.md](docs/debugging-journal.md) using its template.
