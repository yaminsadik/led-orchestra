# LED Orchestra

Distributed addressable-LED light show. Up to 20 × ESP32-C3/C6 nodes each drive
a WS2812B (NeoPixel) strip segment; together the segments form one virtual
strip. A Rust CLI controller is the source of truth for layout, scene, mode,
groups, and overrides.

## Layout

| Crate         | What it is                                                            | Build target                          |
|---------------|-----------------------------------------------------------------------|---------------------------------------|
| `shared/`     | `no_std` protocol types, `Effect` trait, effect implementations       | host + ESP RISC-V targets             |
| `firmware/`   | esp-hal + esp-hal-smartled firmware for one ESP32-C3/C6 node          | C3: `riscv32imc`; C6: `riscv32imac`   |
| `controller/` | clap CLI (`loctl`) for sending commands and inspecting state          | host                                  |

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

## Architectural rules

1. **One virtual strip.** Every physical strip is a contiguous slice of the
   global LED index space. Effects are calculated as
   `f(global_index, time_ms) -> Rgb`.
2. **Effects are pure functions.** No per-node state. Identical inputs ⇒
   identical pixels everywhere — this is how nodes stay visually in sync
   without sample-accurate clocks (just a shared time origin).
3. **Nodes are independent.** A node that loses WiFi keeps running its last
   valid scene locally. When it reconnects, the controller resends scene +
   segment metadata + sync clock. Missing nodes are marked offline; the show
   keeps going on the rest of the strip.
4. **Override priority** (highest wins):
   `emergency > segment > group > global`.
5. **Adding a new effect** = new file in `shared/src/effects/`, add the
   variant in `EffectId`, register it in `EffectRegistry`. Phase 7 (OTA) will
   let new effect code reach nodes without a USB cable.

## Hardware

- ESP32-C3 or ESP32-C6 dev board per node (RISC-V, stock nightly/stable Rust)
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

# 2. Add the ESP32-C3 and ESP32-C6 targets
rustup target add riscv32imc-unknown-none-elf
rustup target add riscv32imac-unknown-none-elf

# 3. Install the flasher
cargo install espflash
```

### Firmware (per board)

Create a local ignored credentials file:

```bash
cp firmware/.env.example firmware/.env
# Edit firmware/.env with your WiFi SSID and password.
```

```bash
cd firmware

# ESP32-C3, the default firmware feature.
cargo build --release
cargo run --release

# ESP32-C6.
cargo build --release \
  --no-default-features \
  --features chip-esp32c6 \
  --target riscv32imac-unknown-none-elf
cargo run --release \
  --no-default-features \
  --features chip-esp32c6 \
  --target riscv32imac-unknown-none-elf
```

The `.cargo/config.toml` runner pipes the build through
`espflash flash --monitor` so `cargo run` reflashes the connected board. If
multiple ESP boards are plugged in, pass the port explicitly through espflash
or unplug the boards you are not flashing.

Environment variables still override `firmware/.env` when both are set. If
neither is set, the firmware renders the Phase 1 fallback rainbow locally, but
it does not start the Phase 2 UDP receiver.

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

- [x] **Phase 1** — One ESP32-C3 drives one strip with one effect
- [ ] Phase 2 — Controller sends commands to one node over WiFi
  - Controller UDP send path and shared packet protocol are in place.
  - Firmware WiFi join + UDP receive loop builds.
  - Real-board flash + UDP packet acceptance works via LAN broadcast.
  - Firmware has separate build features for ESP32-C3 and ESP32-C6.
  - Physical LED response confirmation remains.
  - See [Roadmap](docs/roadmap.md#phase-2-controller-to-one-node-over-wifi).
- [ ] Phase 3 — Per-node segment config for 20 boards
- [ ] Phase 4 — Global synced effects (time sync across nodes)
- [ ] Phase 5 — Modes, groups, scenes, override priority
- [ ] Phase 6 — Terminal control panel (ratatui)
- [ ] Phase 7 — OTA firmware updates
