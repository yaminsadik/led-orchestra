# CLAUDE.md

Guidance for working in this repo. Keep durable design in `docs/`; this file is
the quick orientation + the rules that are easy to violate.

## What This Is

LED Orchestra: a distributed, offline addressable-LED show. Up to ~20 ESP32-C6
LED nodes each render one WS2812B segment of a single virtual strip, driven over
Matter-over-Thread by a controller/hub. The product is **all-C6** and **offline**
(rendering needs no venue Wi-Fi, cloud, or internet). `main` is C++ ESP-IDF/
ESP-Matter only; the Rust Phase 1/2 proof is archived on `archive/rust-phase-2`.

## Locked Architecture

The controller/border-router topology is a **validation-gated** decision. Read,
in order: [docs/architecture.md](docs/architecture.md),
[docs/controller-topology-adr.md](docs/controller-topology-adr.md),
[docs/controller-topology-validation.md](docs/controller-topology-validation.md).

- A single infra-less C6 cannot self-resolve operational Matter nodes, so a real
  **OpenThread Border Router is required** (confirmed on hardware; see
  [docs/debugging-journal.md](docs/debugging-journal.md)).
- Production target: **Option 2 — Hub C6 (Matter controller + esp-thread-br host
  + thin Kubernetes gateway) + RCP C6.** Fallbacks: Option 3 (all-C6 split),
  Option 4 (Pi `ot-br-posix`).
- **Kubernetes** is the off-board control plane (authoring/validation/scheduling
  of declarative program bundles); the hub stays thin and only caches/relays.
- **Next implementation work:** Phase 4 border-router validation (Stage 0 first —
  a *separate* Thread client resolving a node through the BR).

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

cd matter-prototype/led-node && idf.py set-target esp32c6 && idf.py build
cd ../controller-node && idf.py set-target esp32c6 && idf.py build
```

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
