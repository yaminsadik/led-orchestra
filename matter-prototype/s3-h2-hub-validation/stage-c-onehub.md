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
`4293914112`. The LED must render **solid red** with no `error 32`.

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
