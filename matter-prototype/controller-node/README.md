# Controller Node Prototype

The controller node is an ESP32-C6 Matter controller/commissioner for the
private LED Orchestra fabric, and the local source of truth for scenes, node
inventory, and groups. It receives operator intent over USB serial first, and
over controller-local Wi-Fi for laptop/mobile convenience. Laptop/mobile
clients are ingress only, not the Matter controller. The controller node is the
local Matter OTA Provider in Phase 6. LED nodes remain operated and controlled
through Thread by this controller node; no venue Wi-Fi, cloud, or internet is
required.

## Acceptance Criteria

- Builds with ESP-IDF/ESP-Matter for target `esp32c6`.
- Provides USB serial commands for commissioning, listing nodes, setting scenes,
  setting node config, syncing clocks, and loading OTA images. Provides
  controller-local Wi-Fi ingress through a private AP by default; station mode
  is an explicit build-time option for private/local networks.
- Commissions LED nodes into the private fabric.
- Sends unicast custom-cluster commands for per-node provisioning.
- Sends Matter group/multicast commands for all-node scene changes.
- Continues controlling commissioned nodes without venue WiFi/router/internet.

## Implemented Slice

- ESP-IDF app skeleton for `esp32c6`.
- ESP-Matter controller/commissioner initialization.
- Controller-local Wi-Fi ingress config: private AP by default, station mode as
  an explicit build-time option.
- Thread-oriented `sdkconfig.defaults`.
- USB shell helpers for the custom cluster:
  - `lo-set-scene <node-id|group-id> <endpoint-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]`
  - `lo-set-node-config <node-id> <endpoint-id> <orchestra-node-id> <segment-start> <segment-len> <total-leds> <led-gpio>`
  - `lo-sync-clock <node-id|group-id> <endpoint-id> [controller-time-ms]`

Confirmed on hardware:

- Boots to `LED Orchestra controller node ready`; USB serial/JTAG shell is live.
- The standalone operator softAP (SSID/password from the gitignored
  `sdkconfig.defaults.local`, channel 6) comes up, stays up, and is joinable from
  a laptop/phone.
- OpenThread starts as `ROUTER` and the controller joins development fabric
  index 1.

Bring-up gotcha (important): CHIP's own Wi-Fi must stay disabled
(`CONFIG_ENABLE_WIFI_AP=n`, `CONFIG_ENABLE_WIFI_STATION=n`). If either is on,
`CHIP_DEVICE_CONFIG_ENABLE_WIFI` becomes non-zero and the Matter connectivity
manager takes over the radio, forces STA, and stops the operator AP
(`WIFI_EVENT_AP_STOP`). The operator AP is a standalone `esp_wifi` softAP in
`controller_wifi_ingress.cpp`, independent of Matter (which runs over Thread).

Known open issue: with CHIP Wi-Fi off and no Thread SRP server / border router
present, Matter DNS-SD advertising over IP errors at boot (`chip[DIS]: Failed to
advertise ... : 3`). This ties into the Thread-controller open risk below.

Keep USB serial as the required recovery/baseline operator ingress. A private
Wi-Fi AP, TUI bridge, phone app, or physical controls are additional operator
surfaces layered on the controller node; they do not make LED nodes Wi-Fi
devices and do not move Matter control off the controller node.

## Config Targets

The `sdkconfig.defaults` file records the intended private-fabric controller
direction. It currently uses Thread, enables controller-local private AP ingress
by default, and disables WiFi station mode unless deliberately selected through
Kconfig. **Open hardware risk:** the first hardware test must confirm whether
ESP-Matter supports a Thread-side embedded controller on ESP32-C6, or whether the
controller path needs an explicit OpenThread Border Router role. Operator Wi-Fi
does not add venue Wi-Fi, internet, or LED-node Wi-Fi control.
