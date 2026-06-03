# Controller Node Prototype

The controller node is an ESP32-C6 Matter controller/commissioner for the
private LED Orchestra fabric, and the local source of truth for scenes, node
inventory, and groups. Hardware bring-up established it needs a real OpenThread
Border Router, so under the locked decision it evolves into the **Hub** (Matter
controller + esp-thread-br host, with an **RCP C6** radio) plus a thin Kubernetes
bundle gateway — the validation-gated Option 2. See
[`../../docs/controller-topology-adr.md`](../../docs/controller-topology-adr.md).

It receives operator intent over USB serial and controller-local Wi-Fi, and
validated program bundles from Kubernetes over IP. Those clients are ingress
only, not the Matter controller. The hub is the local Matter OTA Provider. LED
nodes remain controlled over Thread; no venue Wi-Fi, cloud, or internet is
required to render scenes.

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

For opening the monitor, log verbosity, the built-in command groups, and the
full terminal command reference, see [`docs/console.md`](../../docs/console.md).
Per the `.cursor/rules/console-commands.mdc` rule, update `docs/console.md`
whenever a console command changes.

Thread bring-up note: `matter esp ot_cli dataset init new`, `dataset commit active`,
`ifconfig up`, and `thread start` are documented with a startup diagram and
"first boot vs reboot" guidance in
[`docs/console.md#what-the-ot_cli-bring-up-commands-do`](../../docs/console.md#what-the-ot_cli-bring-up-commands-do).

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

Resolved finding: with CHIP Wi-Fi off and no border router present, Matter
operational discovery times out — an infra-less single C6 cannot self-resolve
its DNS-SD records. The controller path therefore requires a real OpenThread
Border Router; the next work is selecting Option 2/3/4 via the topology
validation. See
[`../../docs/controller-topology-adr.md`](../../docs/controller-topology-adr.md)
and [`../../docs/debugging-journal.md`](../../docs/debugging-journal.md).

Keep USB serial as the required recovery/baseline operator ingress. A private
Wi-Fi AP, TUI bridge, phone app, or physical controls are additional operator
surfaces layered on the controller node; they do not make LED nodes Wi-Fi
devices and do not move Matter control off the controller node.

## Config Targets

The `sdkconfig.defaults` file records the intended private-fabric controller
direction. It currently uses Thread, enables controller-local private AP ingress
by default, and disables WiFi station mode unless deliberately selected through
Kconfig. Under the locked decision this app grows the esp-thread-br host role (or
joins a separate BR's mesh in Option 3); see
[`../../docs/controller-topology-adr.md`](../../docs/controller-topology-adr.md).
Operator Wi-Fi does not add venue Wi-Fi, internet, or LED-node Wi-Fi control.

## Local Operator AP Credentials

Do not put the controller AP SSID or password in committed docs or defaults.
They belong in the gitignored local defaults file:

```text
matter-prototype/controller-node/sdkconfig.defaults.local
```

Create it from the committed template if it does not exist:

```bash
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
```

Then set:

```text
CONFIG_LED_ORCHESTRA_WIFI_AP_SSID="..."
CONFIG_LED_ORCHESTRA_WIFI_AP_PASSWORD="..."
```

After changing `sdkconfig.defaults.local`, remove the generated `sdkconfig` and
rebuild/flash so ESP-IDF regenerates the effective config:

```bash
rm -f sdkconfig sdkconfig.old
idf.py -p /dev/cu.usbmodem1101 build flash
```
