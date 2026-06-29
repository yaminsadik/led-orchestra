# Phase 7 — Offline Matter OTA (runbook + remaining plumbing)

Update commissioned LED nodes over the offline Matter/Thread fabric — no node-side
USB, no internet. This runbook covers what is **implemented**, the **exact
remaining plumbing** to make it functional, and the **field-security** layer that
comes after.

Role split (from [`../../docs/architecture.md`](../../docs/architecture.md)):

```text
operator (USB / controller Wi-Fi) or Kubernetes
  -> signed + encrypted image bytes
  -> hub: stores image, acts as Matter OTA Provider (cluster 0x0029)
  -> Matter OTA (BDX) over Thread
  -> LED node: Matter OTA Requestor verifies + applies; USB flash = recovery
```

Two independent security layers — do not collapse them: **fabric credentials**
decide *who* may invoke the OTA cluster; **image signing/encryption** decides
*what* firmware a node will run.

## Implemented (this branch)

- **LED node = OTA Requestor.** `CONFIG_ENABLE_OTA_REQUESTOR=y`; the requestor
  cluster is auto-created by `esp_matter::start()`. Dual 3 MB OTA slots on the N8
  (8 MB) layout [`../led-node/partitions-8mb.csv`](../led-node/partitions-8mb.csv)
  (~63% free per slot).
- **LED node = brick-safe.** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` plus a
  Thread-attach health gate in
  [`../led-node/main/app_main.cpp`](../led-node/main/app_main.cpp): a freshly
  applied image is confirmed only after OpenThread attaches, else it rolls back to
  the previous slot on the next reset. A bad OTA cannot brick a wall-mounted node.
- **LED node = versioned for OTA.** Device software version comes from
  `CONFIG_LED_ORCHESTRA_SW_VERSION` (via
  [`../led-node/main/matter_project_config.h`](../led-node/main/matter_project_config.h));
  build the update image with [`../led-node/sdkconfig.ota-v2.defaults`](../led-node/sdkconfig.ota-v2.defaults).
- **Controller = offline OTA Provider** (build-gated by
  `CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER`, default **off**). The provider is our
  **offline fork** of esp-matter's component,
  [`../controller-node/components/lo_ota_provider/`](../controller-node/components/lo_ota_provider/):
  the DCL candidate fetch and forced-TLS download are removed. `lo-ota-set-image`
  registers a **local** candidate (`register_local_candidate`) and the BDX sender
  streams the bytes over **plain HTTP** from a hub-local control-LAN endpoint.
  Operator console: `lo-ota-status`, `lo-ota-enable <node> [once]`,
  `lo-ota-disable <node>`, `lo-ota-grant-access <node>`,
  `lo-ota-set-image <http-uri> <sw-version> <version-string> <size> <vendor-id> <product-id>`.
- **Provider endpoint = `1`.** The provider is a real OTA Provider endpoint, not
  a cluster bolted onto root endpoint `0`. `lo-ota-status` prints the endpoint
  table, active/codegen Interaction Model provider pointers, registry server, and
  accepted commands; endpoint `1`, cluster `0x0029`, accepts QueryImage
  (`0x00000000`), ApplyUpdateRequest (`0x00000002`), and NotifyUpdateApplied
  (`0x00000004`).
- **Controller-side access and BDX routing are wired.** `lo-ota-grant-access`
  verifies or installs the provider ACL entry for a requestor node. The provider's
  BDX `ReceiveInit` handler is registered on both ExchangeManagers in the
  commissioner+server build: the Matter server manager and the controller
  `DeviceControllerFactory` manager. The latter is the one that receives the
  already-commissioned LED requestor's BDX packet.
- **Host-side ingress** = [`lo-ota-image-server.py`](lo-ota-image-server.py): a
  dependency-free plain-HTTP server for the `.ota`. This is the **swappable
  ingress boundary** — operator laptop today, a Kubernetes-served endpoint later;
  only the URL passed to `lo-ota-set-image` changes, never the firmware. The
  ingress host never joins the Matter fabric.

Code: [`../controller-node/main/led_orchestra_ota_provider.cpp`](../controller-node/main/led_orchestra_ota_provider.cpp).

## Enabling the provider build (verified)

The provider-on controller is built from an overlay
([`../controller-node/sdkconfig.ota-provider.defaults`](../controller-node/sdkconfig.ota-provider.defaults))
so the proven default commissioner build is untouched:

```text
CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER=y    # build + register the provider cluster
CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER=y      # controller must host a data model
CONFIG_SUPPORT_CLOSURE_CONTROL_CLUSTER=n      # this esp-matter snapshot's Closure
CONFIG_SUPPORT_CLOSURE_DIMENSION_CLUSTER=n    #   cluster servers don't compile
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y              # N8 controller, 5 MB factory layout
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-8mb.csv"
```

Build (keeps the generated sdkconfig inside the build dir so it does not clobber
the proven default):

```bash
cd matter-prototype/controller-node
EXTRA_SDKCONFIG_DEFAULTS=sdkconfig.ota-provider.defaults \
  idf.py -B build-ota-provider -D SDKCONFIG=build-ota-provider/sdkconfig \
    set-target esp32c6 build
```

> **Build status (2026-06-28):** the provider-on controller builds clean and the
> image fits (latest diagnostic commissioner+server+provider binary:
> `0x2dbc90` bytes; about 43% free in the 5 MB factory). The provider cluster is
> **confirmed live on hardware** (`lo-ota-status`).
> The fork never reaches the internet: no DCL, no TLS-to-cloud, bytes come from the
> control LAN.

> **Debug status (2026-06-28, superseded — see below):** QueryImage and BDX
> handler dispatch were proven on hardware in an earlier run this day. The
> requestor reached endpoint `1`, received `UpdateAvailable`, and sent
> `BDX:ReceiveInit`; the controller matched that to the OTA BDX sender. The
> blocker at the time was lower-level networking: the provider-on bench image had
> operator Wi-Fi disabled (stale sdkconfig in build dir), so the controller could
> not route to the laptop image server (`Host is unreachable`). That was fixed by
> pinning Wi-Fi AP mode in `sdkconfig.ota-provider.defaults`.
>
> **Blocker root-caused + mostly fixed (2026-06-28):** the `controller.init()`
> crash-loop was **heap exhaustion** (`CHIP_ERROR_NO_MEMORY`, 0x0b), not a
> structural interop bug. The provider-on build runs two full CHIP stacks (server
> + commissioner) plus OpenThread BR + BLE on one C6 and was over the RAM budget —
> only ~10 KB free / 6.4 KB largest block before the call. Fix in
> `sdkconfig.ota-provider.defaults`: `CONFIG_OPENTHREAD_BORDER_ROUTER=n` (S3+H2 is
> the BR) and `CONFIG_BT_ENABLED=n`/`ENABLE_ESP32_BLE_CONTROLLER=n` (this build
> never BLE-commissions). That freed ~70 KB → `controller.init()` +
> `setup_commissioner()` succeed (free≈79 KB, largest≈53 KB), and the server
> fabric table now shows the commissioner fabric (`server_fabric_count=1
> present_in_server_table=yes`), confirming the provider shares the nodes' fabric.
>
> **Remaining (2026-06-28, awaiting hardware):** freeing the Matter heap surfaced a
> *downstream* OOM — `controller_wifi_ingress_start()` fails with `ESP_ERR_NO_MEM`
> (`esp_wifi_init`: "Expected to init 10 rx buffer, actual is 3") because the
> Matter stack leaves too little for the Wi-Fi driver's default pool. Staged fix:
> shrink the softAP to a single-client footprint (`STATIC_RX_BUFFER_NUM=4`,
> dynamic RX/TX `32→8`, AMPDU off, `MGMT_SBUF_NUM=8`) + a flash-only unused-cluster
> trim. Build is clean (45% flash free); **boot past Wi-Fi init is unverified — the
> bench was disconnected before reflashing.** Reflash `usbmodem11101` and confirm
> the boot reaches "controller node ready". Full investigation + follow-up: the
> 2026-06-28 "Provider-On Controller Crashes In `controller.init()`" entry in
> [`../../docs/debugging-journal.md`](../../docs/debugging-journal.md).

## Provider-on commissioning constraint (found at the gate, 2026-06-27)

The provider-on controller **cannot BLE-commission new nodes.** Enabling the Matter
server (required to host the OTA Provider cluster) makes the controller advertise
itself as a commissionable device — it claims BLE in the *peripheral* role, which
conflicts with the commissioner's *central* (scan/connect) role. esp-matter's BLE
layer is effectively single-role, so `pairing ble-thread` from the provider-on
build connects to nothing (the node advertises CHIPoBLE the whole time; the
controller never connects). Closing the controller's own commissioning window (done
in `app_main.cpp`) stops the self-advertising but does not restore a working
commissioner BLE-central path.

**This does not block OTA** — OTA rides Thread/CASE, not BLE. The flow is:
**commission each node once with the commissioner-only build (BLE central works),
then add the provider and OTA over the air.** This matches deployment: you
commission during setup, then field-update over Thread.

## Bring-up procedure (bench proof)

1. **Flash the rollback-enabled baseline (v1)** to each LED node over USB
   (`idf.py -p <port> flash`) — lands the rollback bootloader, the health gate, and
   software version 1. **Commission the node(s) with the commissioner-only build**
   (the proven default controller, OTA provider OFF) over the BR
   ([`stage-b-br-baseline.md`](stage-b-br-baseline.md)) — its BLE central is free.
2. **Add the provider:** reflash the controller to the **provider-on** build
   *without erasing NVS* (`esptool ... write_flash @flash_args`, no `erase_flash`;
   NVS/`nvs_keys`/`esp_secure_cert` offsets are identical between the layouts, so the
   fabric + commissioned nodes survive). The provider now hosts on the same fabric
   (node 1, fabric `112233`); commissioned nodes reach it over Thread/CASE.
3. **Build the v2 target image** and wrap it as `.ota` (see
   [`../led-node/sdkconfig.ota-v2.defaults`](../led-node/sdkconfig.ota-v2.defaults)
   for the exact `idf.py` + `ota_image_tool.py` commands; vid `0xFFF1`, pid
   `0x8000` — the esp-matter default the node actually advertises — version `2`).
4. **Serve it** from an address the controller can reach. The controller runs its
   own Wi-Fi AP (gateway `192.168.4.1`; DHCP assigns `192.168.4.2`+ to clients).
   **Join the controller's AP on your laptop**, then run the server:
   `./lo-ota-image-server.py led-node-v2.ota --sw-version 2 --version-string 2.0.0 --vendor-id 0xFFF1 --product-id 0x8000`
   — it prints the ready-to-paste `lo-ota-set-image` line using your AP-side IP
   (`192.168.4.x`). Do **not** use your home LAN / router IP — the controller has
   no route to it.
5. **Stage it on the hub:**
   `lo-ota-set-image http://192.168.4.<N>:8070/led-node-v2.ota 2 2.0.0 <size> 0xFFF1 0x8000`
   (use the IP printed by the server or from `ifconfig`/`ipconfig` on the controller
   AP interface), then `lo-ota-status` to confirm the candidate is registered.
6. Grant cluster access once per node if needed:
   `lo-ota-grant-access <node-id>`. The command is idempotent and reports when the
   ACL already allows the node.
7. Grant access and enable before announcing, then announce once:

   ```text
   lo-ota-grant-access <node-id>
   lo-ota-enable <node-id> once
   matter esp controller invoke-cmd <node-id> 0 0x002A 0x00 {"0:U64":112233,"1:U16":65521,"2:U8":0,"4:U16":1}
   ```

   `lo-ota-enable` now pre-creates the requestor entry, so a single
   `AnnounceOTAProvider` triggers QueryImage → UpdateAvailable directly.
   The `once` flag auto-revokes access after one successful apply.
   Use node-id `4` for the first commissioned node (default bench setup).

8. Watch the **LED node** log: QueryImage → UpdateAvailable → BDX download →
   apply → reboot. The **health gate** then waits for Thread attach and confirms
   the image (`rollback cancelled`); `otadata` shows the new slot and the node
   reports version 2.
9. **Then prove rollback:** build a deliberately-bad v3 (e.g. that never attaches
   Thread), OTA it, and confirm the health gate does **not** confirm it and the
   node reverts to v2 on the next reset. This is the install-safety guarantee.
10. **Mark OTA "functional" only after a real LED node downloads and applies an
    image over Matter/Thread**, and **"install-ready" only after the rollback case
    is proven.** No earlier.

## Field-readiness (separate, after functional)

Functional offline OTA can land first; field-ready OTA still needs, and must be
documented/tested:

- **Secure boot** + **flash encryption** on the LED nodes.
- **Signed + encrypted images** (`CONFIG_ENABLE_ENCRYPTED_OTA=y` + the image
  signing/encryption key flow); reject invalid/wrong-key images.
- **Key handling** for signing/encryption keys and per-device factory data.
- **Rollback/recovery tests:** a failed/aborted update must not brick a node; USB
  flash stays the recovery path. Verify the inactive-slot rollback.
- Keep fabric credentials and image keys as **separate** layers.

## Evidence (fill at the bench)

```text
Date / operator:
Provider build (off | on + server co-config):
Serve path: lo_ota_provider fork (local candidate) + lo-ota-image-server.py (LAN HTTP)
New image version / size:
QueryImage -> UpdateAvailable seen on node? :
BDX download completed? bytes:
Applied + rebooted into new slot? otadata switched? :
New firmware version reported back? :
Heap (provider on) min-free / largest-free:
Flash headroom (provider on) controller / LED node:
Field-security items done (secure boot / flash enc / signed image / rollback):
Pass/Fail (FUNCTIONAL only after real download+apply):
Notes / journal ref:
```

## Debug Evidence (2026-06-28)

These are the breadcrumbs that define the current boundary. Treat earlier
`UnsupportedEndpoint` / `UnsupportedAccess` / "no BDX handler" failures as solved
unless a regression reproduces them.

```text
# lo-ota-status after provider endpoint + IM provider fixes
provider endpoint: 1
ember endpoints count=16
[0] endpoint=0 enabled=yes ota-provider=no
[1] endpoint=1 enabled=yes ota-provider=yes
IM provider ptr  : active=0x40816ca4 codegen=0x40816ca4
accepted commands: err=OK count=3 0x00000000 0x00000002 0x00000004

# QueryImage reaches endpoint 1 / cluster 0x0029 / command 0
DBG CheckCommandExistence provider=0x40816ca4 ep=1 cluster=0x0000_0029 command=0x0000_0000 accepted_err=0 count=3
DBG Codegen Invoke ep=1 cluster=0x0000_0029 command=0x0000_0000 registry=0x4081b558
DBG OtaProviderServer::InvokeCommand ep=1 cluster=0x0000_0029 command=0x0000_0000
DBG OtaProviderLogic::QueryImage ep=1 delegate=0x40847500

# BDX handler registration must happen on the controller ExchangeManager too
DBG RegisterUMH new this=0x40870e38 protocol=(0, 2) type=4 handler=0x4084750c
registered BDX ReceiveInit handler 0x40847508 on exchange manager 0x40870e38
OTA BDX handler bound on controller exchange manager 0x40870e38

# Clean BDX dispatch proof
Bdx Sender will query the OTA image from http://192.168.1.74:8070/led-node-v2.ota
DBG unsolicited handler match protocol=(0, 2) type=4 handler=0x4084750c
DBG TransferFacilitator::OnMessageReceived ec=23390r type=0x4 protocol=(0, 2)

# Current blocker: controller cannot route to the laptop HTTP URL
esp-tls: [sock=54] connect() error: Host is unreachable
HTTP_CLIENT: Connection failed, sock < 0
ota_provider: Failed to open HTTP connection: ESP_ERR_HTTP_CONNECT
```
