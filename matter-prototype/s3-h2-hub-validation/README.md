# S3+H2 One-Board Hub Validation

This package proves (or disproves) the **2026-06-06 architecture pivot**: that one
**Espressif ESP Thread Border Router / Zigbee Gateway board** — an
**ESP32-S3-WROOM-1** host + an **ESP32-H2-MINI-1** RCP — can be the complete LED
Orchestra hub (Matter controller/commissioner **and** esp-thread-br host), driving
Thread-only **ESP32-C6** LED nodes over Matter-over-Thread.

One-board sufficiency is a **hypothesis until the hardware gates below pass.** Do
not claim the board is the hub until the **Stage C** one-node end-to-end gate
renders a scene on a physical LED. The proven all-C6 split
([`../stage0-br-validation/`](../stage0-br-validation/)) is the fallback and stays
intact as historical evidence.

Decision context: [`../../docs/controller-topology-adr.md`](../../docs/controller-topology-adr.md)
(the amended ladder) and
[`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md)
(the quantitative gate + the Stage A-F experiment this package runs).

## Why one S3+H2 board (vs. the all-C6 hub)

- **Least wiring.** The S3↔H2 link is on the PCB — no breadboard spinel crossover
  like the Stage 0 all-C6 BR.
- **Least role sprawl.** One commercial board is the whole hub.
- **Radios split by construction.** Wi-Fi/BLE live on the S3; 802.15.4 lives on the
  H2. This removes the **2026-06-04 single-C6 three-radio contention** (Wi-Fi softAP
  + BLE + native 802.15.4 on one PHY) that caused the operational `error 32`
  ([`../../docs/debugging-journal.md`](../../docs/debugging-journal.md)).

## Hardware facts (ESP Thread BR board)

| Item | Value | Source |
| --- | --- | --- |
| Host SoC | ESP32-S3-WROOM-1 (Wi-Fi + BLE) — Matter controller + esp-thread-br host + thin ingress | board spec |
| Radio SoC | ESP32-H2-MINI-1 — 802.15.4 Thread RCP (Zigbee ignored) | board spec |
| Flash | 8 MB (4 MB on early samples) | board spec / **confirm in Stage A** |
| PSRAM | 2 MB, quad-line (`CONFIG_SPIRAM_MODE_QUAD=y`) | board spec / **confirm in Stage A** |
| S3↔H2 spinel UART | rx=GPIO17, tx=GPIO18 @ 460800 (on-PCB) | `controller`/`thread_border_router` `main/esp_ot_config.h` |
| RCP update pins | reset=GPIO7, boot=GPIO8 (on-PCB) | same `esp_ot_config.h` |
| RCP update path | host-side `CONFIG_AUTO_UPDATE_RCP` from bundled `/rcp_fw/ot_rcp` | `thread_border_router` defaults |
| USB | USB-C to the S3 (USB-Serial/JTAG console + flash) | board spec / **confirm port in Stage A** |

**Open hardware questions for the operator (cannot be answered from the host):**
which `/dev/cu.*` enumerates as the S3; whether the board exposes a **direct-to-H2
USB/UART** path or **only** host-side RCP update; any button/jumper/boot-strap
actions. See [`stage-a-inventory.md`](stage-a-inventory.md).

## Toolchain matrix (pinned — do not upgrade without approval)

| Component | Pinned | Path | Status |
| --- | --- | --- | --- |
| ESP-IDF | v5.4.1 | `~/esp/esp-idf` | OK (`idf.py --version` = ESP-IDF v5.4.1) |
| esp-matter | release/v1.4.2 | `~/esp/esp-matter` | OK |
| RISC-V toolchain (`riscv32-esp-elf`) — C6 + H2 | installed | `~/.espressif/tools` | OK |
| Xtensa toolchain (`xtensa-esp-elf` 14.2.0) — **S3** | installed 2026-06-06 | `~/.espressif/tools` | OK (resolved finding F-A1) |

**Finding F-A1 (2026-06-06): the S3 (Xtensa) target compiler was not installed —
RESOLVED.** This repo had been all-RISC-V (C6/H2), so only `riscv32-esp-elf` was
present; any `esp32s3` build failed at CMake configure with `xtensa-esp32s3-elf-gcc
... not found in the PATH`. **Resolved 2026-06-06** by installing the S3 target
toolchain for the SAME pinned IDF (NOT a version upgrade): `"$IDF_PATH/install.sh"
esp32s3`, then re-sourcing `export.sh`. `xtensa-esp-elf` 14.2.0 is now on PATH and
`idf.py --version` is still ESP-IDF v5.4.1. Both S3 example builds now succeed
(below).

**Finding F-A2 (2026-06-06): the BR bundles the RCP image from a fixed path.** The
`thread_border_router` builds with `CONFIG_AUTO_UPDATE_RCP=y`, whose
`esp_rcp_update` component generates the `rcp_fw` SPIFFS image from
`CONFIG_RCP_SRC_DIR` (Kconfig default `$IDF_PATH/examples/openthread/ot_rcp/build`).
Because we build the RCP out-of-tree under `build/rcp-h2`, `build-s3-hub.sh
build-br` now builds the RCP first and overrides `CONFIG_RCP_SRC_DIR` to point at it
via a machine-local `build/rcp-src-dir.defaults` fragment (gitignored; the path is
absolute + host-specific). The stock `controller`+OTBR (Stage C) build leaves
`AUTO_UPDATE_RCP` **off**, so the hub build does not bundle the RCP — the H2 must be
provisioned separately on hardware (see [`stage-c-onehub.md`](stage-c-onehub.md)).

## Host build verification (Stage A, on the pinned toolchain)

| Build | Target | Command (`build-s3-hub.sh`) | Result (2026-06-06, after F-A1 fix) |
| --- | --- | --- | --- |
| `ot_rcp` (H2 radio) | esp32h2 | `build-rcp` | **OK** — `esp_ot_rcp.bin` 0x31a00 (~203 KB), 81% free in the 1 MB app partition |
| `thread_border_router` + `s3-br-host` overlay (Stage B BR) | esp32s3 | `build-br` | **OK** — `thread_border_router.bin` 0x1e23d0 (~1.88 MB), **19% free** in the 2368K app partition; `rcp_fw` bundled from `build/rcp-h2` |
| `controller` + `s3-otbr-controller` overlay (Stage C hub) | esp32s3 | `build-hub` | **OK** — `controller.bin` 0x23bb10 (~2.29 MB), **24% free** in the 3072K app partition |

All builds are out-of-tree under `build/` (gitignored). The SDK is not patched
(the board's pins already match the stock examples). Stage B BR builds use
[`thread_border_router_otcli/`](thread_border_router_otcli/) — a local copy of
the stock `thread_border_router` app shell with
`esp_matter::console::otcli_register_commands()` added before console init, so
the running BR exposes `matter esp ot_cli ...`.

> **Host-build only.** These confirm the pinned toolchain compiles + links the S3
> firmware; they are not a hardware gate. The app-partition headroom (BR **19%**,
> hub **24%**) is at/under the **25%** flash-headroom gate target — a watch item to
> re-measure on the board (the 8 MB part has ample space outside the app partition,
> but the OTA-slotted app partition itself is the tight dimension). Real
> heap/flash/discovery evidence still comes from Stages B–F on hardware.

## What's in this package

| File | Purpose |
| --- | --- |
| [`build-s3-hub.sh`](build-s3-hub.sh) | Build/flash helper: `build-rcp` (H2), `build-br` (S3 Stage B), `build-hub` (S3 Stage C), `flash-*`, `monitor`, `clean`. Sources both export scripts; out-of-tree builds. |
| [`thread_border_router_otcli/`](thread_border_router_otcli/) | Local Stage B copy of Espressif's `thread_border_router` app shell; registers the Matter OT CLI bridge while reusing stock esp-matter/ESP-IDF components. |
| `sdkconfig.s3-br-host.defaults` | Stage B overlay on `thread_border_router` (offline BR: `BR_AUTO_START=n`, UART RCP, NOTE logs). |
| `sdkconfig.s3-otbr-controller.defaults` | Stage C overlay on the `controller` example's `sdkconfig.defaults.otbr` (8 MB flash, UART RCP, PSRAM quad, NOTE logs). |
| `sdkconfig.controller-node.s3-otbr.defaults` | **Scaffold (not yet gated):** S3+OTBR overlay for our own `controller-node` app — the post-gate fold-in path. |
| `stage-a-inventory.md` … `stage-f-ingress.md` | The per-stage runbooks (below). |

Serial capture: reuse [`../stage0-br-validation/tools/serlog.py`](../stage0-br-validation/tools/serlog.py)
and [`../stage0-br-validation/tools/sercap.py`](../stage0-br-validation/tools/sercap.py).

## Stages (gated; work in order)

| Stage | Runbook | Gate (one line) |
| --- | --- | --- |
| **A** | [`stage-a-inventory.md`](stage-a-inventory.md) | Hardware/toolchain matrix written; flashing path understood; examples build on the pinned toolchain |
| **B** | [`stage-b-br-baseline.md`](stage-b-br-baseline.md) | A *separate* C6 client resolves the LED through the S3+H2 BR; CASE + `SetScene` render; no Error 28/32 |
| **C** | [`stage-c-onehub.md`](stage-c-onehub.md) | The S3 commissions a C6 LED, resolves it through **its own** BR, CASE, `SetScene` renders (the decisive one-board gate) |
| **D** | [`stage-d-recovery.md`](stage-d-recovery.md) | 100% recovery over the agreed cycle count; no short-run heap drift |
| **E** | [`stage-e-scale-soak.md`](stage-e-scale-soak.md) | Group `SetScene` to ~20 nodes; 72 h soak within the metric targets |
| **F** | [`stage-f-ingress.md`](stage-f-ingress.md) | Thin K8s/USB/Wi-Fi ingress added; all Stage D/E metrics still pass |

Application-firmware runbooks (run on the **split topology**, independent of the
one-board hub gate):

| Runbook | Purpose |
| --- | --- |
| [`phase-5-6-7-bench-runbook.md`](phase-5-6-7-bench-runbook.md) | Bench walkthrough: commission 3→20 nodes, verify real **group control**, durable NVS config, and synchronized **scheduled group** scenes. |
| [`phase-7-offline-ota.md`](phase-7-offline-ota.md) | Offline Matter OTA: requestor (present) + provider scaffold (build-gated), the remaining offline image plumbing, and the field-security layer. |

## Working split (until the board is proven)

The host author prepares docs, runbooks, committed overlays/scripts, and verifies
host builds (`idf.py build` for S3/H2). The **operator flashes hardware and runs
the gates**, pasting serial logs + observed LED results to be recorded as evidence
in the per-stage runbook and in
[`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md).
Anything that fails in a way future work should remember goes in
[`../../docs/debugging-journal.md`](../../docs/debugging-journal.md).
