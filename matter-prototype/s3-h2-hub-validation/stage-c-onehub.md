# Stage C — Co-Located One-Board Hub Proof (decisive)

**Goal:** prove the **ESP32-S3 can host BOTH the Matter commissioner/controller
AND the esp-thread-br host** while the on-board **ESP32-H2 is the RCP**. This is
the gate that decides whether one S3+H2 board can be the LED Orchestra hub.

**Topology**

```text
ESP32-S3 = Matter commissioner/controller + esp-thread-br host
ESP32-H2 = 802.15.4 Thread RCP (on-PCB UART 17/18; RCP update reset 7 / boot 8)
C6 LED   = Matter Thread accessory (custom cluster 0xFFF1FC00)
```

**Gate (all must hold):** the S3 commissions one C6 LED over BLE→Thread; the LED
registers `_matter._tcp` through the board's BR/SRP path; the S3 resolves the
operational node through **its own** BR path; CASE succeeds; the S3 sends
`SetScene`; the **physical LED renders**. Capture heap, largest free block, flash
usage, RCP health, Thread/SRP/DNS-SD state, and serial logs.

> Prereq: **Finding F-A1** (S3 Xtensa toolchain) resolved; Stage A bench inventory
> done. Stage B PASS is recommended (it isolates the BR before co-location), but
> not strictly required to attempt C.

---

## C.0 Hybrid approach (per the chosen plan)

- **Gate path (proves the hypothesis fast):** the **stock esp-matter `controller`
  example** built with its `sdkconfig.defaults.otbr` + our
  [`sdkconfig.s3-otbr-controller.defaults`](sdkconfig.s3-otbr-controller.defaults)
  overlay (8 MB flash, UART RCP, PSRAM quad). Drive the custom cluster with the
  built-in `controller invoke-cmd` — the same lower-level encode/CASE/invoke path
  Stage 0 proved, just issued from the co-located S3.
- **Scaffold path (staged, NOT yet gated):** fold the hub onto **our own**
  `controller-node` app later, via
  [`sdkconfig.controller-node.s3-otbr.defaults`](sdkconfig.controller-node.s3-otbr.defaults)
  and the UART-RCP branch already added to
  [`../controller-node/main/esp_ot_config.h`](../controller-node/main/esp_ot_config.h).
  Do this only **after** the stock-example gate passes (see that overlay's "Known
  gaps"). The `lo-*` helpers move onto the hub then — and `docs/console.md` gets
  updated per the console-commands rule at that point.

## C.1 Build + flash the co-located hub

```bash
./build-s3-hub.sh build-hub                 # controller + OTBR overlay -> esp32s3
./build-s3-hub.sh flash-hub <S3_PORT>       # flashes the S3 app
./build-s3-hub.sh monitor hub <S3_PORT>
```

Host build-verified 2026-06-06: `controller.bin` ~2.29 MB, **24% free** in the app
partition (re-measure the **flash headroom** on the 8 MB board against the ≥ 25%
gate). Record the boot-time free heap / largest free block.

**Provision the H2 RCP first.** The stock `controller`+OTBR build leaves
`CONFIG_AUTO_UPDATE_RCP` **off** (unlike the Stage B BR), so flashing the hub does
**not** program the H2. Put RCP firmware on the H2 by one of:

- `./build-s3-hub.sh flash-rcp <H2_PORT>` — if the board exposes a direct-to-H2 USB
  path (confirm in Stage A); or
- enable `CONFIG_AUTO_UPDATE_RCP=y` + an `rcp_fw` partition in the hub overlay so
  the S3 host-updates the H2 over the on-board reset/boot pins (as the Stage B BR
  does), then rebuild.

Confirm the link with `matter esp ot_cli rcp version` (C.2) before commissioning.

## C.2 Bring up the BR + form Thread (offline-first)

On the S3 hub console:

```text
matter esp ot_cli rcp version                 # S3<->H2 link OK
matter esp ot_cli dataset init new
matter esp ot_cli dataset commit active
matter esp ot_cli dataset active -x           # COPY <dataset_tlvs>
matter esp ot_cli ifconfig up
matter esp ot_cli thread start
matter esp ot_cli state                        # expect: leader
matter esp ot_cli srp server state             # expect: running
```

## C.3 Commission a C6 LED and render (the gate)

LED node in commissioning mode (default test creds). On the S3 hub console:

```text
# commission over BLE -> Thread using the hub's own dataset:
matter esp controller pairing ble-thread <node-id> <dataset_tlvs> 20202021 3840

# operational discovery THROUGH the hub's own BR (expect a record, not Error 28):
matter esp ot_cli dns browse _matter._tcp.default.service.arpa

# SetScene (cluster 0xFFF1FC00, command 0) — solid red, full brightness:
matter esp controller invoke-cmd <node-id> 1 0xFFF1FC00 0 {"0:U8":1,"1:U8":255,"2:U8":0,"3:U8":0,"4:U8":0,"5:U8":255,"6:U32":1,"7:U64":0}
```

SetScene JSON field map (from
[`../cluster/led-orchestra.md`](../cluster/led-orchestra.md)): `0:U8 effect_id`
(1=solid), `1/2/3:U8 r/g/b`, `4:U8 speed`, `5:U8 brightness`, `6:U32 sequence`,
`7:U64 scheduled_start_time_ms` (0 = apply now). The device console splits on
whitespace and the JSON has none, so paste it as a single token (no quotes). Use
`0xFFF1FC00` for the cluster id; if the parser rejects hex, use the decimal
`4294048768`. The LED must render **solid red** with no `error 32`.

*If `invoke-cmd` cannot encode the custom cluster*, add
`CONFIG_ESP_MATTER_CONTROLLER_CUSTOM_CLUSTER_ENABLE=y` to the overlay and rebuild
(the stock OTBR default disables it; the generic invoke path normally does not
need it).

## C.4 Offline vs. Wi-Fi-backbone (the key open risk)

The stock controller+OTBR build resolves operational nodes via **platform mDNS**
over a Wi-Fi/Ethernet **backbone** (it disables the OT SRP/DNS *clients*; the BR's
SRP server + mDNS answer discovery). So **co-located, backbone-less discovery is
the genuine uncertainty** of this stage.

1. **Try offline first** (no `wifi connect`). If `dns browse` + CASE succeed with
   no backbone, that is the strongest possible result — record it.
2. **If offline discovery fails** (`Error 28`/`Error 32` even though `srp server`
   shows the registration), bring up a **local** backbone as a *diagnostic*:

   ```text
   matter esp wifi connect <local_ssid> <local_password>
   ```

   then retry C.3. Document exactly what changed.

This does **not** violate the invariants: commissioning is BLE; LED control is
Matter-over-Thread; the backbone is only the hub's mDNS resolution path, and the
hub can host its **own** AP (no venue Wi-Fi). Already-rendering nodes keep their
last valid scene through any backbone outage. But if a backbone is *required* for
the hub to (re)resolve nodes, capture that clearly — it shapes the deployment
(the hub must provide its own local backbone netif) and is exactly the kind of
finding the gate exists to surface.

## Evidence (fill at the bench)

```text
Date / operator:
Hub firmware: controller + sdkconfig.defaults.otbr + sdkconfig.s3-otbr-controller.defaults (esp32s3)
Flash: app size / partition / % free (>=25%?):
Boot free heap / min-free heap / largest free block:
rcp version:                                  (paste)
Thread state / srp server:  leader / running? (paste)
Commission result (pairing ble-thread):       success? node-id:
dns browse (paste the _matter._tcp record, or the error):
Backbone needed?  offline PASS | required wifi connect (why):
invoke-cmd SetScene: LED rendered? color:     no error 32? (y/n)
Post-render free heap / min-free / largest-free-block:
Pass/Fail vs gate:
Notes:
```

## Bench Result — 2026-06-09

Stage C was run with only the S3+H2 board and one freshly flashed C6 LED board
connected to the laptop.

```text
S3+H2 hub: /dev/cu.usbmodem1301 (ESP32-S3 MAC 9c:13:9e:0a:46:88)
C6 LED:   /dev/cu.usbmodem1101 (ESP32-C6 base MAC 58:e6:c5:1b:8b:54)
```

Firmware and build evidence:

```text
Hub firmware: stock esp-matter examples/controller + sdkconfig.defaults.otbr + sdkconfig.s3-otbr-controller.defaults
Hub flash: controller.bin 0x23bb10, app partition 0x2ee000, 0xb24f0 free (~24%)
LED firmware: led-node current build; flash erased before pairing
LED flash: led_orchestra_matter_led_node.bin 0x1d4c20, app partition 0x1e0000, 0xb3e0 free (~2%)
```

BR bring-up succeeded:

```text
rcp version: openthread-esp32/4c2820d377-005c5cefc; esp32h2; 2026-06-07 01:01:49 UTC
Thread state: leader
SRP server state: running
Thread dataset TLV:
0e08000000000001000000030000154a0300001935060004001fffe00208a7b69b894bf579320708fd41fcb09eefa2e20510112ded646337d18b3f230265f82c1461030f4f70656e5468726561642d313438630102148c041055b56c120755d0d89559fe44208d205d0c0402a0f7f8
```

The long `pairing ble-thread` command was sent through `sercap.py` paced writes.
The controller echo included the full tail `20202021 3840`, so this run did
**not** hit the known USB-Serial/JTAG truncation failure.

Commissioning progressed through BLE/PASE, NOC provisioning, Thread network
setup, and Thread enable. The LED joined the S3 Thread network and registered
SRP:

```text
LED: Role detached -> child
LED: SRP Client was started, detected server: fd41:fcb0:9eef:a2e2:a29d:a49d:3fba:dfc9
LED: advertising srp service: 0D6E25A2A3B685E7-0000000000000001._matter._tcp
LED: Role child -> router
```

However, the S3 controller could not resolve its own commissioned node through
the co-located discovery path:

```text
OperationalSessionSetup[1:0000000000000001]: operational discovery failed: 32
OperationalSessionSetup[1:0000000000000001]: operational discovery failed: 32
OperationalSessionSetup[1:0000000000000001]: operational discovery failed: 32
Commissioning complete for node ID 0x0000000000000001: Error CHIP:0x00000032
pairing_command: Commissioning failure with node D6E25A2A3B685E7-1
```

After the failure, the S3 SRP server still had the LED's operational record:

```text
0D6E25A2A3B685E7-0000000000000001._matter._tcp.default.service.arpa.
    port: 5540
    host: DE9EB5A552883659.default.service.arpa.
    addresses: [fd41:fcb0:9eef:a2e2:fb0b:2638:a217:dab2]
```

The CLI probe `matter esp ot_cli dns browse _matter._tcp.default.service.arpa`
returned `Error 35: InvalidCommand` in this hub image, so direct OT CLI DNS
browse is not available without a config change. The internal Matter operational
discovery failure is still decisive: SRP registration existed, but the S3
controller did not resolve/CASE the node locally before the commissioning gate
timed out.

Pass/fail vs gate: **FAIL offline**. No `invoke-cmd SetScene` was attempted
because commissioning never reached operational CASE or `CommissioningComplete`.

## Decision

- **PASS (ideally offline)** → the one-board hub hypothesis holds for one node.
  Record the evidence in
  [`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md)
  and proceed to [`stage-d-recovery.md`](stage-d-recovery.md).
- **PASS only with a local backbone** → still a pass, but document the backbone
  requirement (hub must provide its own AP); journal it and carry it into Stage F.
- **FAIL — co-located discovery or heap/stability** → fall back to **Fallback 1,
  the all-C6 split** (= the proven Stage 0 config: move the controller to its own
  C6 joining the BR's Thread mesh over its native radio). Capture logs + heap and
  file a [`debugging-journal.md`](../../docs/debugging-journal.md) entry.
