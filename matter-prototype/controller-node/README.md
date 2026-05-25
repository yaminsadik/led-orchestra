# Controller Node Prototype

The controller node is an ESP32-C6 Matter controller/commissioner for the
private LED Orchestra fabric. It is the local operator entry point and the
offline OTA image holder/provider.

## Acceptance Criteria

- Builds with ESP-IDF/ESP-Matter for target `esp32c6`.
- Provides USB serial commands for commissioning, listing nodes, setting
  scenes, setting node config, syncing clocks, and loading OTA images.
- Commissions LED nodes into the private fabric.
- Sends unicast custom-cluster commands for per-node provisioning.
- Sends Matter group/multicast commands for all-node scene changes.
- Continues controlling commissioned nodes without venue WiFi/router/internet.

## Implemented Slice

- ESP-IDF app skeleton for `esp32c6`.
- ESP-Matter controller/commissioner initialization.
- Thread-oriented `sdkconfig.defaults`.
- USB shell helpers for the custom cluster:
  - `lo-set-scene <node-id|group-id> <endpoint-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]`
  - `lo-set-node-config <node-id> <endpoint-id> <orchestra-node-id> <segment-start> <segment-len> <total-leds> <led-gpio>`
  - `lo-sync-clock <node-id|group-id> <endpoint-id> [controller-time-ms]`

Keep USB serial as the first operator ingress before adding any private WiFi
AP, TUI bridge, phone app, or physical controls.

## Config Targets

The `sdkconfig.defaults` file records the intended private-fabric controller
direction. It uses Thread and disables WiFi station mode. The first hardware
test needs to confirm whether Espressif's controller stack is happy as a
Thread-only controller node, or whether it needs an explicit OpenThread border
router configuration even without venue WiFi.
