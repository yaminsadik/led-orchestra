# LED Orchestra

Distributed addressable-LED light show. Up to 20 ESP32-C6 nodes each drive a
WS2812B/NeoPixel strip segment; together the segments form one virtual strip.
The active implementation path is C++ on ESP-IDF/ESP-Matter over Thread so the
installation can run as a fully offline local mesh with a dedicated controller
node.

The first prototype needs no venue Wi-Fi, cloud, or internet. USB is the
baseline operator path, and the controller node hosts a private Wi-Fi AP by
default for laptop/mobile convenience:

```text
USB laptop OR controller-local Wi-Fi laptop/mobile
  -> ESP32-C6 controller node
     (Matter controller + commissioner, scene source of truth)
  -> Thread/Matter mesh
  -> ESP32-C6 LED nodes
     (Matter-over-Thread devices, LED renderers)
```

The laptop/mobile is operator input only; the controller node is the Matter
controller/commissioner. LED nodes are operated and controlled through Thread by
the controller node. See
[Architecture](docs/architecture.md#roles-and-responsibilities) for the full
role glossary and diagram.

The completed Rust WiFi/UDP Phase 1/2 implementation is archived on the
`archive/rust-phase-2` branch. Keep `main` focused on the C++ Matter/Thread
prototype and later production path.

## Layout

| Path | What it is | Build target |
| --- | --- | --- |
| `matter-prototype/led-node/` | ESP-IDF/ESP-Matter LED node for one physical strip segment | `esp32c6` via ESP-IDF |
| `matter-prototype/controller-node/` | ESP-IDF/ESP-Matter controller/commissioner with USB serial ingress and controller-local Wi-Fi ingress | `esp32c6` via ESP-IDF |
| `matter-prototype/common/` | Shared C++ constants for cluster ids, command ids, tags, and effect ids | included by both apps |
| `matter-prototype/cluster/` | Human-readable LED Orchestra custom cluster contract | docs |

## Documentation

- [Architecture](docs/architecture.md) — crate responsibilities, runtime flow,
  and system invariants.
- [Requirements](docs/requirements.md) — ESP-IDF, ESP-Matter, FastLED direction,
  and local shell setup.
- [Roadmap](docs/roadmap.md) — phase-by-phase acceptance criteria and the
  recommended next implementation slice.
- [Matter/Thread plan](docs/matter-thread.md) — ESP-Matter over Thread tradeoffs,
  prototype roles, custom cluster contract, and OTA direction.
- [Handoff](docs/handoff.md) — current project state, key decisions, and
  commands for a new Codex chat.

## Architectural rules

1. **One virtual strip.** Every physical strip is a contiguous slice of the
   global LED index space. Effects are calculated as
   `f(global_index, time_ms) -> Rgb`.
2. **Effects are pure functions.** No per-node state. Identical inputs ⇒
   identical pixels everywhere — this is how nodes stay visually in sync
   without sample-accurate clocks (just a shared time origin).
3. **Nodes are independent.** A node that loses network contact keeps running
   its last valid scene locally. When it reconnects, the controller resends
   scene + segment metadata + sync clock. Missing nodes are marked offline;
   the show keeps going on the rest of the strip.
4. **Override priority** (highest wins):
   `emergency > segment > group > global`.
5. **Adding a new effect** = add a stable effect id in the C++ registry, render
   it in the LED-node effect engine, and keep ids append-only. Later OTA work
   will let new compiled effect code reach nodes without a USB cable.

## Hardware

- ESP32-C6 dev board per node
- WS2812B / NeoPixel strip per node (5 V, addressable; pads: GND, DIN, +5 V)
- **External 5 V power supply for the strip** — do not power it from the ESP32
- ESP32 GND and PSU GND must be tied together
- ESP32 GPIO → strip DIN (default: GPIO2; configurable per node)

## Build

### One-Time Setup

See [Requirements](docs/requirements.md) for the full ESP-IDF/ESP-Matter setup.

```bash
# Install host prerequisites, then clone/install ESP-IDF and ESP-Matter as
# described in docs/requirements.md.
. "$HOME/esp/esp-idf/export.sh"
. "$HOME/esp/esp-matter/export.sh"
export IDF_CCACHE_ENABLE=1
```

### ESP-Matter/Thread Prototype

The required ESP-IDF/ESP-Matter install and per-shell export steps are in
[Requirements](docs/requirements.md).

Prototype goals:

- LED node: ESP32-C6 Matter-over-Thread device with a custom LED Orchestra
  cluster and WS2812 output. Later acts as a Matter OTA Requestor.
- Controller node: ESP32-C6 Matter controller/commissioner and local source of
  truth for scenes, node inventory, and groups. It takes operator intent (and
  later OTA image bytes) over USB serial or the controller's private Wi-Fi AP.
  Laptop/mobile clients are UI/operator ingress only — they are not the Matter
  controller and hold no fabric credentials.
- First fabric: private development Matter fabric with generated per-device
  factory data and test/dev credentials.
- Rendering: start from the working ESP-IDF `led_strip` renderer, then validate
  FastLED as the C++ effect/rendering layer before larger effect refactors.

Build commands:

```bash
. "$HOME/esp/esp-idf/export.sh"
. "$HOME/esp/esp-matter/export.sh"

cd matter-prototype/led-node
idf.py set-target esp32c6
idf.py build

cd ../controller-node
idf.py set-target esp32c6
idf.py build
```

## Phase status

- [x] **Phase 1/2** — Rust WiFi/UDP proof archived on `archive/rust-phase-2`
- [ ] Phase 3 — C++ ESP-Matter/Thread feasibility prototype
  - LED-node and controller-node apps build; LED-node has been flashed once.
    Controller-node has been flashed with USB serial plus private Wi-Fi AP
    ingress and booted to its shell. Commissioning and physical Matter/Thread
    LED control remain.
- [ ] Phase 4 — Multi-node offline Thread mesh
- [ ] Phase 5 — Segment config and synchronized FastLED effects over Matter
- [ ] Phase 6 — Offline OTA through the controller node
- [ ] Phase 7 — Operator UX beyond USB serial
