# LED Node Prototype

The LED node is an ESP32-C6 Matter-over-Thread device that owns one WS2812B
segment and exposes the LED Orchestra custom cluster.

## Acceptance Criteria

- Builds with ESP-IDF/ESP-Matter for target `esp32c6`.
- Runs in Thread mode, not WiFi station mode.
- Commissions into the private development fabric.
- Receives `SetScene` over Matter and changes the physical LEDs.
- Keeps rendering the last valid scene when the controller is unreachable.
- Later: enables Matter OTA Requestor and accepts signed/encrypted OTA images.

## Implemented Slice

- ESP-IDF app skeleton for `esp32c6`.
- Matter on/off light endpoint with the LED Orchestra custom cluster attached.
- Thread-oriented `sdkconfig.defaults`.
- WS2812 renderer using ESP-IDF `led_strip`.
- `SetScene` changes the render state for `off`, `solid`, and `rainbow`.
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

Regenerate and inspect the full `sdkconfig` with `idf.py menuconfig` during the
first hardware build.
