# CLAUDE.md

Guidance for working in this repo. Keep durable design in `docs/`; this file is
the quick orientation + the rules that are easy to violate.

## What This Is

LED Orchestra: a distributed, offline addressable-LED show. Up to ~20 ESP32-C6
LED nodes each render one WS2812B segment of a single virtual strip, driven over
Matter-over-Thread by a split hub/control stack. The **LED nodes are ESP32-C6**;
the selected architecture is **S3+H2 BR-only + separate ESP32-C6 controller**.
The product is **all-Espressif** and **offline** (rendering needs no venue
Wi-Fi, cloud, or internet). `main` is C++ ESP-IDF/ESP-Matter only; the Rust
Phase 1/2 proof is archived on `archive/rust-phase-2`.

## Locked Architecture

The controller/border-router topology is a **validation-gated** decision. Read,
in order: [docs/architecture.md](docs/architecture.md),
[docs/controller-topology-adr.md](docs/controller-topology-adr.md),
[docs/controller-topology-validation.md](docs/controller-topology-validation.md).

- A single infra-less C6 cannot self-resolve operational Matter nodes, so a real
  **OpenThread Border Router is required** (confirmed on hardware; see
  [docs/debugging-journal.md](docs/debugging-journal.md)).
- **Resolved 2026-06-10:** the S3+H2 **one-board hub failed** the offline
  co-located Stage C gate, so the selected architecture is the **split topology**:
  **S3+H2 board as BR-only** plus a **separate ESP32-C6 controller** and
  Thread-only C6 LED nodes. The older all-C6 split remains historical evidence;
  Pi `ot-br-posix` stays the last-resort fallback.
- **Kubernetes** is the off-board control plane (authoring/validation/scheduling
  of declarative program bundles); the hub stays thin and only caches/relays.
- **Current implementation work:** the selected split topology has passed recovery
  (Stage D), real multi-node Matter group control (Phase 5), and the Phase 6
  hardware gate. **Offline OTA (Phase 7) is implemented**: LED-node requestor +
  app rollback + Thread-attach health gate, and an offline provider fork
  (`controller-node/components/lo_ota_provider/`, DCL/TLS removed; local candidate
  + plain-HTTP control-LAN image source). The provider-on controller builds and
  the provider cluster boots on hardware, but the co-located build (server +
  commissioner = two full CHIP stacks on one C6) is **RAM-bound**: `controller.init()`
  crash-looped with `CHIP_ERROR_NO_MEMORY` until the overlay dropped OpenThread BR
  (S3+H2 is the BR) and BLE (this build never BLE-commissions) and shrank the Wi-Fi
  softAP buffers — see the 2026-06-28 OOM journal entry. **What remains:** confirm
  the slimmed build boots past Wi-Fi init (blocked on bench reconnect), the
  end-to-end OTA transfer proof (a commissioned node downloads + applies an image
  over Thread), the bad-image rollback proof, then the field-security layer
  (secure boot / flash encryption / signed images). Note: the provider-on
  controller can't BLE-commission (single-role BLE, and BLE is now off for heap) —
  commission with the commissioner-only build, then add the provider; OTA rides Thread.
  Runbooks + committed config:
  [matter-prototype/s3-h2-hub-validation/](matter-prototype/s3-h2-hub-validation/)
  (see `phase-7-offline-ota.md`).
  See [docs/controller-topology-validation.md](docs/controller-topology-validation.md),
  the 2026-06-09/10 entries, and the 2026-06-27/28 OTA entries in
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

# Historical S3+H2 validation builds. The helper layers our overlays onto the
# stock esp-matter examples (S3 controller+OTBR hub, H2 ot_rcp); see the runbooks:
#   matter-prototype/s3-h2-hub-validation/build-s3-hub.sh build
```

Pinned toolchain: **ESP-IDF v5.4.1 + esp-matter release/v1.4.2** (do not upgrade
without explicit approval; see Stage A). The S3 validation builds and H2 RCP
build on this same pinned toolchain.

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

## Firmware Build Discipline

- Treat **LED-node firmware** as a **paired mixed-fleet deliverable**. If a
  change affects LED-node behavior, features, rendering, effects, palettes,
  cluster handling, persisted config, OTA-visible software, or anything else
  that changes the LED-node binary, update **both** LED-node builds before
  calling the work done:
  - the default **N8 / 8 MB** build
  - the **N4 / 4 MB** build using
    `sdkconfig.4mb.defaults`
- Keep the N4 and N8 LED releases on the same feature set unless the user
  explicitly approves a temporary divergence.
- A **controller-only** change does **not** require rebuilding LED-node
  firmware.
- A **controller OTA-provider** image is an **8 MB controller** target; the
  commissioner-only controller build may still be used on 4 MB or 8 MB
  controller boards.

## Repo Hygiene

- `managed_components/`, `build/`, `sdkconfig*`, and `dependencies.lock` are
  per-project gitignored build state — do not commit them.
- There is no `firmware/` (Rust) tree on `main`; that work is on
  `archive/rust-phase-2`.
- Record failures worth remembering in
  [docs/debugging-journal.md](docs/debugging-journal.md) using its template.
