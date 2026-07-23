# LED Orchestra

LED Orchestra is an offline, distributed addressable-LED show controller. Each
ESP32-C6 LED node drives one WS2812B/NeoPixel strip segment; together the nodes
render one virtual strip over Matter-over-Thread.

The active path is C++ on ESP-IDF and ESP-Matter. The selected hardware topology
is:

```text
operator / Kubernetes ingress
  -> separate ESP32-C6 controller / Matter commissioner
  -> Espressif S3+H2 board used as Thread Border Router only
       ESP32-S3: esp-thread-br host
       ESP32-H2: 802.15.4 RCP radio
  -> Thread / Matter mesh
  -> ESP32-C6 LED nodes
  -> WS2812B / NeoPixel strips
```

Rendering does not require venue Wi-Fi, cloud access, or internet. USB serial and
the controller-local Wi-Fi AP are operator ingress to the controller. Kubernetes
is a future/off-board authoring and scheduling plane that pushes validated show
bundles to the controller; it does not talk directly to LED nodes.

## Current Status

- **Active development path:** `matter-prototype/`
- **Selected topology:** S3+H2 board as BR-only plus a separate ESP32-C6
  controller.
- **Rejected topology:** the all-in-one offline S3+H2 hub failed validation; the
  board stays in the project as the BR-only companion board.
- **Archived prototype:** the earlier Rust Wi-Fi/UDP Phase 1/2 proof lives on
  `archive/rust-phase-2`.

What works today:

- ESP32-C6 LED-node and controller-node apps build with ESP-IDF/ESP-Matter.
- The controller boots with USB shell and private operator AP ingress.
- Matter commissioning, Thread bring-up, group commands, durable node config,
  scheduled scene support, and OTA requestor/provider scaffolding are present.
- The LED-node scene library now covers ambient ocean looks plus party and
  occasion scenes for venue use, including wave/surf, reveal, celebration, and
  upscale shimmer cues.
- Multi-node synchronized scene and offline OTA still need the remaining
  hardware gates before they are field-ready.

See [Roadmap](docs/roadmap.md) for phase-by-phase acceptance criteria and
remaining work.

## Repository Map

| Path | Purpose |
| --- | --- |
| `matter-prototype/` | Active ESP-IDF/ESP-Matter firmware lane. Start here for builds. |
| `matter-prototype/led-node/` | ESP32-C6 Matter-over-Thread LED node. Renders one strip segment. |
| `matter-prototype/controller-node/` | Separate ESP32-C6 Matter controller/commissioner. |
| `matter-prototype/common/` | Shared C++ cluster, command, tag, and effect constants. |
| `matter-prototype/cluster/` | Human-readable LED Orchestra custom cluster contract. |
| `matter-prototype/s3-h2-hub-validation/` | S3+H2 validation record and retained BR-only split-topology support. |
| `matter-prototype/stage0-br-validation/` | Historical all-C6 border-router validation and fallback runbook. |
| `docs/` | Architecture, roadmap, console commands, validation record, and hardware notes. |

## Start Here

1. Read [Architecture](docs/architecture.md) for the role split between
   Kubernetes, operator ingress, the Matter controller, the Thread Border Router,
   and LED nodes.
2. Read [Topology ADR](docs/controller-topology-adr.md) if you need to understand
   why the project selected a separate controller plus BR-only S3+H2 board.
3. Read [Requirements](docs/requirements.md) before building; ESP-IDF and
   ESP-Matter must both be installed and exported in each shell.
4. Build the active apps from [matter-prototype](matter-prototype/README.md).
5. Use [Console Operation](docs/console.md) for monitor setup and command
   references.

## Build

One-time shell setup, assuming ESP-IDF and ESP-Matter are already installed:

```bash
. "$HOME/esp/esp-idf/export.sh"
. "$HOME/esp/esp-matter/export.sh"
export IDF_CCACHE_ENABLE=1
```

Build the LED node:

```bash
cd matter-prototype/led-node
idf.py set-target esp32c6
idf.py build
```

If your LED-node fleet mixes **4 MB** and **8 MB** ESP32-C6 boards, also build a
second LED image for the N4 units:

```bash
cd matter-prototype/led-node
idf.py -B build-4mb \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.4mb.defaults" \
  -D SDKCONFIG=build-4mb/sdkconfig set-target esp32c6 build
```

Release rule for mixed LED fleets:

- Any change that alters the **LED-node firmware** means shipping **two**
  LED-node images for that release: one **N8** image and one **N4** image.
- Do not treat one image as the long-term universal image for both hardware
  classes. A 4 MB image may boot on an 8 MB node, but it keeps the node at the
  tighter 4 MB limit.

Build the controller node:

```bash
cd ../controller-node
idf.py set-target esp32c6
idf.py build
```

Controller flash-size rule:

- The default commissioner build fits the proven **4 MB** layout and can also be
  flashed to an **8 MB** controller board.
- If you want the controller to host the offline OTA Provider build, use an
  **8 MB controller board** and the `sdkconfig.ota-provider.defaults` overlay.

Before flashing the controller AP image, create local AP credentials from the
committed template:

```bash
cd matter-prototype/controller-node
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
```

Then edit `sdkconfig.defaults.local`. Do not commit real SSID/password values.

## Hardware

- Espressif ESP Thread Border Router / Zigbee Gateway board, used as BR-only:
  ESP32-S3-WROOM-1 host plus ESP32-H2-MINI-1 RCP.
- One ESP32-C6 dev board for the controller node.
- One ESP32-C6 dev board per LED node. A mixed fleet of **4 MB** and **8 MB**
  LED-node boards is supported, but it requires separate N4 and N8 LED images.
- WS2812B / NeoPixel strip per LED node.
- External 5 V strip power supply. Do not power strips from the ESP32 board.
- Shared ground between ESP32 and LED power supply.
- Default LED data pin: GPIO2, configurable per node.

## Design Rules

- The installation is one virtual strip. Every physical strip owns a contiguous
  range of the global LED index space.
- Effects are pure functions of `global_index` and time, so adjacent segments
  stay visually aligned.
- Nodes keep rendering their last valid scene or bundle if the controller or
  border router is unavailable.
- Override priority is `emergency > segment > group > global`.
- Effect ids are append-only. New effect code ships as firmware through OTA;
  program bundles are declarative data.

## Key Docs

- [Architecture](docs/architecture.md)
- [Roadmap](docs/roadmap.md)
- [Console Operation](docs/console.md)
- [Matter/Thread Plan](docs/matter-thread.md)
- [Mesh Network](docs/mesh-network.md)
- [Topology ADR](docs/controller-topology-adr.md)
- [Topology Validation](docs/controller-topology-validation.md)
- [Debugging Journal](docs/debugging-journal.md)
- [LED Strip Power Bring-Up](docs/led-strip-power-bringup.md)
- [LED Node Flash Sizing Decision](docs/led-node-flash-sizing-decision.md)
