# Controller Node Prototype

This is the active ESP32-C6 Matter controller/commissioner for LED Orchestra.
It owns the private Matter fabric, commissions LED nodes, sends custom-cluster
commands, manages groups, and exposes the operator shell.

Hardware bring-up established that this controller needs a real OpenThread
Border Router. The offline co-located S3+H2 one-board hub then failed its
validation gate, so the selected architecture keeps this app on a **separate
ESP32-C6 controller** alongside the **S3+H2 board used as BR-only**. The older
all-C6 split remains a historical fallback. See
[`../../docs/controller-topology-adr.md`](../../docs/controller-topology-adr.md).

It receives operator intent over USB serial and controller-local Wi-Fi, and
validated program bundles from Kubernetes over IP. Those clients are ingress
only, not the Matter controller. The hub is the local Matter OTA Provider. LED
nodes remain controlled over Thread; no venue Wi-Fi, cloud, or internet is
required to render scenes.

## Quick Start

Create local AP credentials before building a controller image:

```bash
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
```

Set the private AP SSID/password in `sdkconfig.defaults.local`, then build:

```bash
. "$HOME/esp/esp-idf/export.sh"
. "$HOME/esp/esp-matter/export.sh"

idf.py set-target esp32c6
idf.py build
```

Open the monitor after flashing:

```bash
idf.py -p <CONTROLLER_PORT> monitor
```

For the full command reference, monitor setup, and group enrollment steps, use
[`../../docs/console.md`](../../docs/console.md).

## Controller Flash Size

The controller can be run on either **4 MB** or **8 MB** ESP32-C6 hardware, but
the recommended choice depends on which controller build you want:

- **Commissioner-only controller** (the default build in `sdkconfig.defaults`):
  proven on the 4 MB partition layout and also flashable to an 8 MB board.
- **Provider-on controller** (offline OTA Provider enabled with
  `sdkconfig.ota-provider.defaults`): use an **8 MB** controller board and the
  `partitions-8mb.csv` layout.

Practical recommendation:

- If you are buying or assigning a controller board now, make the controller an
  **ESP32-C6-WROOM-1-N8 (8 MB)** class board.
- Any `esp32c6` board with **8 MB flash** and working USB serial/JTAG is a good
  controller candidate; the controller firmware does not depend on a special LED
  output pin map the way the LED nodes do.

Compatibility rule:

- A **4 MB controller image can run on an 8 MB controller board**.
- The **8 MB provider-on controller image must not be flashed to a 4 MB board**.

## Current Status

- Builds for `esp32c6` as the separate controller/commissioner.
- Boots to the LED Orchestra shell over USB serial/JTAG.
- Starts a standalone private operator AP when local credentials are configured.
- Sends unicast `SetScene`, `SetNodeConfig`, and `SyncClock`.
- Sends Matter group scene and clock commands through group node ids.
- Can address the full current LED effect registry, including ocean-theme,
  party, and occasion scenes added on the LED nodes.
- Includes a build-gated OTA Provider scaffold; functional offline OTA still
  needs local image plumbing and hardware validation.

## Acceptance Criteria

- Builds with ESP-IDF/ESP-Matter for target `esp32c6` as the selected separate
  controller/commissioner; historical S3+OTBR scaffolding is retained in
  `../s3-h2-hub-validation/`.
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
- USB shell helpers for the custom cluster (unicast):
  - `lo-set-scene <node-id> <endpoint-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]`
  - `lo-set-node-config <node-id> <endpoint-id> <orchestra-node-id> <segment-start> <segment-len> <total-leds> <led-gpio>`
  - `lo-sync-clock <node-id> <endpoint-id> [controller-time-ms]`
  - `lo-set-calibration <node-id> <endpoint-id> <time-offset-ms|-> <brightness-cap|-> <palette-override|-> [corr-rrggbb|-] [temp-rrggbb|-]`
    — per-node field tuning as **data, not firmware** (synchronized timing offset,
    brightness cap, palette override, LED color correction); persisted on the node.
    Use `-` to keep any existing field unchanged.
- **Real group control** (encodes group ids with `chip::NodeIdFromGroupId`):
  - `lo-add-group <node-id> <endpoint-id> [group-id] [group-name]` (Groups cluster AddGroup)
  - `lo-set-scene-group <group-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]`
  - `lo-sync-clock-group <group-id> [controller-time-ms]`
  - `lo-scheduled-scene-group <group-id> <delay-ms> <effect-id> <rrggbb> <speed> <brightness> [sequence]`
  - `lo-set-calibration-group <group-id> <time-offset-ms|-> <brightness-cap|-> <palette-override|-> [corr-rrggbb|-] [temp-rrggbb|-]`
  - `lo-show-group-help` — prints the one-time group key + enrollment sequence
  - `../s3-h2-hub-validation/lo-provision-group-member` — host-side helper that
    prints the four per-node provisioning commands with a least-privilege
    Group/Operate ACL
  - Group keysets use the built-in `controller group-settings add-keyset/bind-keyset/add-group`.
    Each node also needs Group Key Management state and an Access Control ACL
    entry for the group subject; that node-side sequence is the hardware-gated step (see
    [`docs/console.md`](../../docs/console.md#one-time-group-key--enrollment-setup)).
- **Offline OTA Provider (build-gated, `CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER`,
  default off):** OTA Provider endpoint `1` + `lo-ota-status`,
  `lo-ota-grant-access`, `lo-ota-enable`, `lo-ota-disable`, and
  `lo-ota-set-image <http-uri> <sw-version> <version-string> <size> <vendor-id>
  <product-id>`. Backed by `lo_ota_provider`
  ([`components/lo_ota_provider/`](components/lo_ota_provider/)) — our offline fork
  of esp-matter's `esp_matter_ota_provider` with the DCL candidate fetch and forced
  TLS download removed: `lo-ota-set-image` registers a **local** candidate and the
  provider streams the image over plain HTTP from a hub-local control-LAN endpoint
  (operator laptop now, Kubernetes-served later). The provider BDX handler is
  registered on both the Matter server ExchangeManager and the controller
  `DeviceControllerFactory` ExchangeManager; the latter is the path used by an
  already-commissioned requestor's `BDX:ReceiveInit`. Enabling it also needs the
  Matter server — see the Phase 7 runbook
  [`../s3-h2-hub-validation/phase-7-offline-ota.md`](../s3-h2-hub-validation/phase-7-offline-ota.md).
  Because this build hosts two full CHIP stacks (server + commissioner) on one C6,
  it is RAM-bound: the overlay disables OpenThread BR (`OPENTHREAD_BORDER_ROUTER=n`;
  the S3+H2 is the BR) and BLE (`BT_ENABLED=n`; this build never BLE-commissions)
  and shrinks the Wi-Fi softAP buffers to keep `controller.init()` and the AP from
  running out of heap. See the 2026-06-28 `controller.init()` OOM entry in
  [`../../docs/debugging-journal.md`](../../docs/debugging-journal.md).
- **Historical (not selected):** an S3+OTBR build path for this app — a committed
  `sdkconfig.controller-node.s3-otbr.defaults` overlay and a UART-RCP variant of
  `main/esp_ot_config.h` — lives in
  [`../s3-h2-hub-validation/`](../s3-h2-hub-validation/). It compiles toward the
  rejected one-board S3+H2 hub and is retained only for history/reference.

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
Border Router; the selected architecture is now the **split topology** with
this controller on its own C6 and the S3+H2 board as BR-only. See
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
Kconfig. In the selected architecture this app stays on its own **ESP32-C6** and
joins the S3+H2 BR's mesh over its native 802.15.4 radio; historical S3+H2
one-board overlays live in [`../s3-h2-hub-validation/`](../s3-h2-hub-validation/). See
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
