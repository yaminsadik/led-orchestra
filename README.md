# LED Orchestra

Distributed addressable-LED light show. Up to 20 ESP32-C6 nodes each drive a
WS2812B/NeoPixel strip segment; together the segments form one virtual strip.
The active implementation path is C++ on ESP-IDF/ESP-Matter over Thread so the
installation can run as a fully offline local mesh with a dedicated controller
node.

Rendering scenes needs no venue Wi-Fi, cloud, or internet. A Kubernetes control
plane authors and schedules shows and pushes them to the hub over IP, but the
live show keeps running without it. USB and a controller-local Wi-Fi AP are
operator ingress to the hub:

```text
Kubernetes control plane (authors/validates/schedules program bundles)
  + USB / Wi-Fi (operator ingress)
  -> separate ESP32-C6 controller / commissioner
  -> S3+H2 board as BR-only:
       ESP32-S3 (esp-thread-br host)
       ESP32-H2 (802.15.4 RCP radio, on-PCB UART link)
  -> Thread/Matter mesh
  -> ESP32-C6 LED nodes (Thread-only renderers; store + render bundles)
```

Kubernetes and the laptop/mobile are ingress only; the C6 controller is the
Matter controller/commissioner. LED nodes are controlled over Thread and never
join Wi-Fi. A real OpenThread Border Router is required (confirmed on hardware).
The offline **S3+H2 one-board hub failed** its validation gate, so the selected
architecture is the current split topology above. See
[Architecture](docs/architecture.md#roles-and-responsibilities) for the role
glossary and [the topology ADR](docs/controller-topology-adr.md) for the
decision and its retained fallbacks.

The completed Rust WiFi/UDP Phase 1/2 implementation is archived on the
`archive/rust-phase-2` branch. Keep `main` focused on the C++ Matter/Thread
prototype and later production path.

## Layout

| Path | What it is | Build target |
| --- | --- | --- |
| `matter-prototype/led-node/` | ESP-IDF/ESP-Matter LED node for one physical strip segment | `esp32c6` via ESP-IDF |
| `matter-prototype/controller-node/` | ESP-IDF/ESP-Matter controller/commissioner for the selected split topology | `esp32c6` |
| `matter-prototype/common/` | Shared C++ constants for cluster ids, command ids, tags, and effect ids | included by both apps |
| `matter-prototype/cluster/` | Human-readable LED Orchestra custom cluster contract | docs |
| `matter-prototype/s3-h2-hub-validation/` | Historical S3+H2 one-board validation runbooks + retained BR-only split-topology support | host + `esp32s3`/`esp32h2` |
| `matter-prototype/stage0-br-validation/` | Stage 0 all-C6 BR runbook + evidence (Fallback-1) | host + `esp32c6` |

## Documentation

- [Architecture](docs/architecture.md) — roles, topology, runtime flow, program
  distribution, and system invariants.
- [Topology ADR](docs/controller-topology-adr.md) — the resolved
  controller/border-router decision (selected split topology, with retained
  fallbacks).
- [Topology validation](docs/controller-topology-validation.md) — the staged
  experiment record that rejected the one-board hub and selected the split
  topology.
- [Mesh network](docs/mesh-network.md) — Thread mesh topology, protocol stack,
  and the join/control sequence over 802.15.4.
- [Debugging journal](docs/debugging-journal.md) — the discovery-timeout
  investigation that established the border-router requirement.
- [Matter/Thread plan](docs/matter-thread.md) — ESP-Matter over Thread tradeoffs,
  roles, custom cluster contract, and OTA direction.
- [Console operation](docs/console.md) — how to open the controller monitor, log
  verbosity, and the full console/terminal command reference.
- [Requirements](docs/requirements.md) — ESP-IDF, ESP-Matter, FastLED direction,
  and local shell setup.
- [Roadmap](docs/roadmap.md) — phase-by-phase acceptance criteria and the next
  implementation slice.

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

- **Hub:** Espressif ESP Thread BR / Zigbee GW board (ESP32-S3-WROOM-1 host +
  ESP32-H2-MINI-1 RCP; 8 MB flash + 2 MB PSRAM, 4 MB on early samples). Zigbee
  unused.
- ESP32-C6 dev board per LED node
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

The historical **S3+H2 one-board validation** builds from the stock esp-matter
examples with our committed overlays; see
[matter-prototype/s3-h2-hub-validation/](matter-prototype/s3-h2-hub-validation/)
(`build-s3-hub.sh`).

## Phase status

- [x] **Phase 1/2** — Rust Wi-Fi/UDP proof archived on `archive/rust-phase-2`
- [~] Phase 3 — C++ ESP-Matter/Thread feasibility
  - Apps build; controller boots and runs its operator AP; BLE commissioning
    completes PASE + `AddNOC`. Bring-up established that a single infra-less C6
    cannot self-resolve operational nodes → a real border router is required.
- [x] **Phase 4 — Border-router topology validation** — the offline one-board
  S3+H2 hub failed; the selected architecture is **S3+H2 BR-only + separate C6
  controller**.
- [~] Phase 5 — Multi-node offline Thread mesh
- [ ] Phase 6 — Segment config, synchronized FastLED effects, and program bundles
- [ ] Phase 7 — Offline OTA through the hub
- [ ] Phase 8 — Operator UX and Kubernetes control plane
