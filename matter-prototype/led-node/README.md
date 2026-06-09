# LED Node Prototype

The LED node is an ESP32-C6 Matter-over-Thread device that owns one WS2812B
segment and exposes the LED Orchestra custom cluster.

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
- Matter on/off light endpoint with the LED Orchestra custom cluster attached.
- Thread-oriented `sdkconfig.defaults`.
- WS2812 renderer using ESP-IDF `led_strip`.
- `SetScene` changes the render state for `off`, `solid`, `rainbow`, and
  `fibonacci` (per-pixel R/G/B are consecutive Fibonacci numbers mod 256 down the
  strip; scrolls with `speed`).
- `SetNodeConfig` updates RAM config and cluster attributes. GPIO changes are
  rejected for now because the strip driver is initialized at boot.
- `SyncClock` stores a controller-time offset for scheduled scenes.

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
