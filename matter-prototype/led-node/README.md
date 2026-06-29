# LED Node Prototype

This is the active ESP32-C6 LED-node firmware. Each node joins the private
Matter-over-Thread fabric, owns one physical WS2812B/WS2815 strip segment, and
exposes the LED Orchestra custom cluster.

The controller assigns each node a virtual segment range. The node renders only
that range, but effects use global LED indexes so a scene can flow across
multiple physical strips.

## Quick Start

```bash
. "$HOME/esp/esp-idf/export.sh"
. "$HOME/esp/esp-matter/export.sh"

idf.py set-target esp32c6
idf.py build
```

Flash one physical board at a time while using the default development Matter
credentials. After commissioning, assign segment metadata from the controller
with `lo-set-node-config`.

## Current Status

- Builds for `esp32c6`.
- Runs as a Thread Matter device, not as a Wi-Fi station.
- Renders `off`, `solid`, `rainbow`, `fibonacci`, `aurora-breathe`, `comet`,
  `theater-chase`, `palette-cycle`, and `twinkle`.
- Persists accepted node config in NVS with magic/version/CRC validation.
- Supports scheduled `SetScene` promotion using `SyncClock` time offsets.
- Includes Matter OTA Requestor support and OTA app partitions.
- Flash headroom is tight on 4 MB C6 boards; check size before adding larger
  features.

## Acceptance Criteria

- Builds with ESP-IDF/ESP-Matter for target `esp32c6`.
- Runs in Thread mode, not WiFi station mode.
- Commissions into the private development fabric.
- Receives `SetScene` over Matter and changes the physical LEDs.
- Stores accepted program bundles (declarative playlists/schedules) and renders
  the active scene/timeline locally, keeping the last valid one when the hub is
  unreachable.
- Later (OTA phase): acts as a Matter OTA Requestor — downloads a signed and
  encrypted image from the hub OTA Provider over the offline fabric,
  verifies/decrypts it, and applies it. Matter fabric credentials and image
  signing/encryption are separate security layers.

## Implemented Slice

- ESP-IDF app skeleton for `esp32c6`.
- Matter on/off light endpoint (its standard Groups cluster makes group
  enrollment work) with the LED Orchestra custom cluster attached.
- Thread-oriented `sdkconfig.defaults`.
- WS2812/WS2815 renderer using ESP-IDF `led_strip` (RMT). This stays the physical
  output path.
- **Effect/color engine** ([`main/led_color.h`](main/led_color.h)) shaped like
  FastLED (`CRGB`/`CHSV`, palettes/gradients, `blend`/`scale8`/`sin8`/easing) plus
  an output-policy hook (per-strip color correction/temperature, master
  brightness, future power budget). The command → renderer-state → engine math →
  `led_strip` path is the production-intended boundary. The engine has two
  backends: a built-in FastLED-compatible fallback (default) and real FastLED
  (`CONFIG_LED_ORCHESTRA_USE_FASTLED`, a hardware-gated spike — see
  [`../../docs/matter-thread.md`](../../docs/matter-thread.md) "FastLED Engine").
- **Effect metadata registry** ([`main/led_orchestra_effects.cpp`](main/led_orchestra_effects.cpp)):
  append-only effect ids with name/description, which params each effect uses,
  and palette references (palettes are data). `SetScene` rejects unknown effect
  ids via the registry and keeps the last valid scene. No runtime-uploaded effect
  code — new effect behavior ships as compiled firmware via OTA.
- **Palette registry**: append-only palette refs currently include `0` aurora,
  `1` ember, `2` ocean, `3` coral, `4` jungle, `5` ice, `6` neon-party,
  `7` gold-score, and `8` maintenance-white-blue. These are data palettes for
  compiled effects and future declarative bundles; changing effect behavior still
  ships through OTA.
- `SetScene` renders `off`, `solid`, `rainbow`, `fibonacci`, `aurora-breathe`,
  `comet`, `theater-chase`, `palette-cycle`, and `twinkle`. A scheduled
  `SetScene` (non-zero start) is staged and promoted at the
  synchronized start time; the node keeps rendering its active scene until then
  (keep-last-valid — a scheduled change never blanks a running show).
- **Durable `SetNodeConfig`**: accepted segment config is persisted in NVS
  (magic + version + CRC) and reloaded at boot before the renderer/attributes use
  it ([`main/led_orchestra_config_store.cpp`](main/led_orchestra_config_store.cpp)).
  The boot log states the source (`config loaded from NVS` vs `defaults`). GPIO
  changes are still rejected because the strip driver is bound at boot.
- `SyncClock` stores a controller-time offset used for scheduled scenes.
- Acts as a **Matter OTA Requestor** (`CONFIG_ENABLE_OTA_REQUESTOR=y`); the
  requestor cluster is auto-created by `esp_matter::start()` and the `ota_0`/
  `ota_1`/`otadata` partitions are in the N8 layout
  [`partitions-8mb.csv`](partitions-8mb.csv).
- **Brick-safe OTA:** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` plus a
  Thread-attach **health gate** in [`main/app_main.cpp`](main/app_main.cpp) — a
  freshly-applied image is confirmed only after OpenThread attaches, else the
  bootloader reverts to the previous slot. A bad OTA cannot brick a wall-mounted
  node. The device software version is set from `CONFIG_LED_ORCHESTRA_SW_VERSION`
  (via [`main/matter_project_config.h`](main/matter_project_config.h)) so OTA target
  images bump it without editing source.

## Flash Headroom (watch this)

On the deployment-standard **N8 (8 MB)** layout the app (~1.84 MB) sits in **3 MB
OTA slots (~39% free)** — comfortable, vs the ~2% free on the retired 4 MB layout
(see [`../../docs/led-node-flash-sizing-decision.md`](../../docs/led-node-flash-sizing-decision.md)).
OTA still needs the new image to fit a slot, so record `idf.py size` deltas when
adding code (notably the real-FastLED engine spike). The 4 MB
[`partitions.csv`](partitions.csv) remains for any N4 units.

## Config Targets

The `sdkconfig.defaults` file records the intended Thread direction and the
default LED wiring:

- GPIO2
- 60 LEDs
- node id 1
- virtual segment `[0, 60)`

Prototype identity note:

- The Matter setup PIN/discriminator are commissioning credentials. For Phase 3,
  test LED nodes use Matter's default development values unless factory data or
  a custom project config overrides them: setup PIN `20202021`, discriminator
  `3840` (`0xF00`). Commission one physical board at a time and assign distinct
  Matter node ids from the controller (`2`, `3`, ...).
- The LED Orchestra node id and segment range are assigned later with
  `lo-set-node-config`; they are separate from the Matter node id.
- GPIO2 is only the local LED strip data pin on each ESP32-C6. It is not a
  unique device identifier, and multiple boards can all use GPIO2.
- Production needs per-device factory data / unique setup payloads instead of
  shared test credentials.

Regenerate and inspect the full `sdkconfig` with `idf.py menuconfig` during the
first hardware build.
