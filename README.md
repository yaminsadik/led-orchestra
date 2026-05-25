# LED Orchestra

Distributed addressable-LED light show. Up to 20 × ESP32-C6 nodes each drive
a WS2812B (NeoPixel) strip segment; together the segments form one virtual
strip. The current Rust WiFi/UDP path is the known-good fallback. The next
prototype track adds ESP-Matter over Thread so the installation can run as an
offline local mesh with a dedicated controller node.

## Layout

| Crate         | What it is                                                            | Build target                          |
|---------------|-----------------------------------------------------------------------|---------------------------------------|
| `shared/`     | `no_std` protocol types, `Effect` trait, effect implementations       | host + ESP RISC-V targets             |
| `firmware/`   | esp-hal + esp-hal-smartled firmware for one ESP32-C6 node             | `riscv32imac-unknown-none-elf`        |
| `controller/` | clap CLI (`loctl`) for sending commands and inspecting state          | host                                  |
| `matter-prototype/` | ESP-IDF/ESP-Matter prototype apps for Thread LED/controller nodes | `esp32c6` via ESP-IDF |

`shared/` is pulled in by both `firmware/` and `controller/` so effects, the
effect-name registry, and the `NodeConfig` struct are defined exactly once.

There is intentionally **no top-level Cargo workspace** — the firmware and
controller compile for different targets and benefit from independent lock
files.

## Documentation

- [Architecture](docs/architecture.md) — crate responsibilities, runtime flow,
  and system invariants.
- [Wire protocol](docs/protocol.md) — the Phase 2 UDP packet format shared by
  the controller and firmware.
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
5. **Adding a new effect** = new file in `shared/src/effects/`, add the
   variant in `EffectId`, register it in `EffectRegistry`. Later OTA work will
   let new effect code reach nodes without a USB cable.

## Hardware

- ESP32-C6 dev board per node (RISC-V, stock nightly/stable Rust)
- WS2812B / NeoPixel strip per node (5 V, addressable; pads: GND, DIN, +5 V)
- **External 5 V power supply for the strip** — do not power it from the ESP32
- ESP32 GND and PSU GND must be tied together
- ESP32 GPIO → strip DIN (default: GPIO2; configurable per node)

## Build

### One-time setup

```bash
# 1. Install Rust if it isn't already (puts cargo, rustc, rustup on PATH)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"

# 2. Add the ESP32-C6 target
rustup target add riscv32imac-unknown-none-elf

# 3. Install the flasher
cargo install espflash
```

### Rust WiFi Firmware Fallback (per board)

Create a local ignored credentials file:

```bash
cp firmware/.env.example firmware/.env
# Edit firmware/.env with your WiFi SSID and password.
```

```bash
cd firmware

cargo build --release
cargo run --release
```

The `.cargo/config.toml` runner pipes the build through
`espflash flash --monitor` so `cargo run` reflashes the connected board. If
multiple ESP boards are plugged in, pass the port explicitly through espflash
or unplug the boards you are not flashing.

Environment variables still override `firmware/.env` when both are set. If
neither is set, the firmware renders the Phase 1 fallback rainbow locally, but
it does not start the Phase 2 UDP receiver.

### ESP-Matter/Thread Prototype

The Matter/Thread path is intentionally separate from the working Rust firmware
until it proves out on hardware. Start with `matter-prototype/README.md`; the
initial LED-node and controller-node ESP-IDF app skeletons already exist there,
but they still need first `idf.py` build validation and hardware testing.

Prototype goals:

- LED node: ESP32-C6 Matter-over-Thread device with a custom LED Orchestra
  cluster and WS2812 output.
- Controller node: ESP32-C6 Matter controller/commissioner with USB serial
  operator ingress, local scene state, and controller-hosted OTA images.
- First fabric: private development Matter fabric with generated per-device
  factory data and test/dev credentials.

Expected first-build commands once ESP-IDF is installed and exported:

```bash
. "$IDF_PATH/export.sh"
cd matter-prototype/led-node
idf.py set-target esp32c6
idf.py build

cd ../controller-node
idf.py set-target esp32c6
idf.py build
```

### Controller

```bash
cd controller
cargo run -- effects             # lists effects via shared/ — already works
cargo run -- --help
```

Phase 2 now has a shared packet format plus controller-side UDP send support:

```bash
# Default bus is udp://239.42.0.1:4242 and target-node 0 means broadcast.
cargo run -- all effect rainbow --brightness 40 --speed 128
cargo run -- all solid ff8800 --brightness 40
cargo run -- all off

# For a single board, send unicast to its WiFi address and node id.
cargo run -- --bus udp://192.168.1.42:4242 --target-node 1 all solid 00ff44

# If the router blocks WiFi client-to-client unicast, send LAN broadcast.
cargo run -- --bus udp://192.168.1.255:4242 --target-node 1 all effect rainbow
```

`shared::SetScenePacket` is the fixed-width `no_std` wire contract used by
the controller and firmware UDP receiver. The node joins WiFi, listens on UDP
port `4242`, decodes `SetScenePacket`, and swaps the active scene when the
packet targets this node.

## Phase status

- [x] **Phase 1** — One ESP32-C6 drives one strip with one effect
- [x] **Phase 2** — Controller sends commands to one node over WiFi
  - Confirmed on ESP32-C6 hardware: WiFi join, UDP `4242`, controller command,
    and visible LED response.
- [ ] Phase 3 — ESP-Matter/Thread feasibility prototype
  - Initial ESP-IDF LED-node and controller-node apps are scaffolded under
    `matter-prototype/`; first `idf.py` build and hardware validation remain.
- [ ] Phase 4 — Multi-node offline Thread mesh
- [ ] Phase 5 — Segment config and synchronized effects over Matter
- [ ] Phase 6 — Offline OTA through the controller node
- [ ] Phase 7 — Operator UX beyond USB serial
