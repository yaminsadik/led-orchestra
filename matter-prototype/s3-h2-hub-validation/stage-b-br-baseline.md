# Stage B — S3+H2 as a BR-Only Baseline

**Goal:** replace the hand-wired C6 BR-host + C6 RCP (Stage 0) with the **official
S3+H2 board acting as a Thread Border Router only**, while keeping the *existing*
separate C6 `controller-node` as the commissioner/resolver and a C6 LED node as
the accessory. This isolates the board's BR/SRP/DNS-SD from co-location before
Stage C trusts the co-located hub.

**Topology**

```text
S3+H2 board   = Thread Border Router ONLY (S3 esp-thread-br host + H2 RCP)
C6 controller = Matter commissioner/controller, border router OFF, operator Wi-Fi OFF
C6 LED node   = Matter Thread accessory (custom cluster 0xFFF1FC00)
```

**Gate (all must hold)**

- `rcp version` on the S3 BR returns a version → S3↔H2 spinel link healthy.
- The BR reaches `leader`, and `srp server` is `running`.
- The separate C6 controller's `dns browse` returns the LED's `_matter._tcp`
  record **through the S3+H2 BR** (not `Error 28`).
- CASE succeeds and `SetScene` renders on the physical LED (no `Error 32`).

> Prereq: **Finding F-A1** (S3 Xtensa toolchain) is **resolved** and the BR host
> build is verified (`thread_border_router.bin` ~1.88 MB; see Stage A). The
> remaining prereq is Stage A's **bench inventory** (USB ports, H2 flashing path)
> before flashing.

---

## B.0 Why `thread_border_router` (not patched `ot_br`)

The board's S3↔H2 link is on-PCB at the same pins the **esp-matter
`thread_border_router`** example already uses (UART 17/18; RCP update reset 7 /
boot 8), and that example owns **host-side RCP update** (`CONFIG_AUTO_UPDATE_RCP`)
— the S3 flashes the H2 over the board pins from a bundled `/rcp_fw/ot_rcp` image.
That matches a board whose H2 is normally **not** directly USB-flashable. So Stage
B uses `thread_border_router` + the offline overlay
[`sdkconfig.s3-br-host.defaults`](sdkconfig.s3-br-host.defaults) — no SDK patching.

*Alternative (only if the board exposes a direct-to-H2 USB/UART path):* flash the
H2 with stock `ot_rcp` (`./build-s3-hub.sh flash-rcp <H2_PORT>`) and run a BR that
does not auto-update the RCP. Confirm the H2 path in Stage A first.

## B.1 Build + flash the S3 BR (auto-updates the H2 RCP)

```bash
./build-s3-hub.sh build-br                 # thread_border_router + offline overlay -> esp32s3
./build-s3-hub.sh flash-br <S3_PORT>       # flashes S3; first boot updates the H2 RCP
./build-s3-hub.sh monitor br <S3_PORT>     # watch the RCP-update + boot log
```

`build-br` builds the H2 `ot_rcp` first and bundles it into the BR's `rcp_fw`
partition (`CONFIG_AUTO_UPDATE_RCP=y`; the out-of-tree RCP-path wiring is Finding
F-A2). Host build-verified 2026-06-06 (`thread_border_router.bin` ~1.88 MB, 19%
app-partition free). On first boot, expect RCP-update log lines (the S3 programming
the H2) and then a healthy console.

## B.2 Bring up Thread + SRP on the BR (offline, by hand)

On the S3 BR console (USB-Serial/JTAG). See
[`../../docs/console.md#what-the-ot_cli-bring-up-commands-do`](../../docs/console.md#what-the-ot_cli-bring-up-commands-do).

```text
matter esp ot_cli rcp version                 # expect a version string (S3<->H2 OK)
matter esp ot_cli dataset init new
matter esp ot_cli dataset commit active
matter esp ot_cli dataset active -x           # COPY the <dataset_tlvs> hex
matter esp ot_cli ifconfig up
matter esp ot_cli thread start
matter esp ot_cli state                        # expect: leader
matter esp ot_cli srp server state             # expect: running  (enable it if not)
```

## B.3 Build + flash the C6 LED node and the C6 controller (Stage 0 client overlay)

The C6 controller reuses the **Stage 0 client overlay** (border router OFF; SRP +
DNS *clients* ON; operator Wi-Fi OFF — the `error 32` fix), so it is a pure
commissioner/resolver, not its own BR:

```bash
# C6 controller-node as the separate commissioner/resolver:
REPO="$(cd ../.. && pwd)"
S0="$REPO/matter-prototype/stage0-br-validation"
rm -f "$S0/build/client/sdkconfig"   # force the overlay to reseed sdkconfig
EXTRA_SDKCONFIG_DEFAULTS="$S0/sdkconfig.client.defaults" \
idf.py -C "$REPO/matter-prototype/controller-node" -B "$S0/build/client" \
  -D SDKCONFIG="$S0/build/client/sdkconfig" \
  set-target esp32c6 build flash -p <C6_CTRL_PORT>

# C6 LED node (stock app), put into commissioning at boot:
idf.py -C ../led-node -B ../led-node/build set-target esp32c6 build flash -p <C6_LED_PORT>
```

(The Stage 0 runbook documents this client overlay in detail:
[`../stage0-br-validation/README.md`](../stage0-br-validation/README.md). Do
not use `-D SDKCONFIG_DEFAULTS` for the controller: `controller-node` deliberately
sets its own defaults, so the Stage 0 overlay must be layered with
`EXTRA_SDKCONFIG_DEFAULTS`.)

## B.4 Join the BR mesh, commission, resolve, render

On the **C6 controller** console, using the BR's `<dataset_tlvs>` from B.2:

```text
matter esp ot_cli dataset set active <dataset_tlvs>
matter esp ot_cli dataset commit active
matter esp ot_cli ifconfig up
matter esp ot_cli thread start
matter esp ot_cli state                        # expect: child or router (attached to the BR mesh)

# commission the LED over BLE -> Thread (LED node advertising; default test creds):
matter esp controller pairing ble-thread <node-id> <dataset_tlvs> 20202021 3840

# the decisive discovery test — resolve the LED THROUGH the S3+H2 BR:
matter esp ot_cli dns browse _matter._tcp.default.service.arpa
#   expect a record like the Stage 0 evidence, NOT "Error 28":
#     <instance>-<node>
#     Port:5540
#     Host:<id>.default.service.arpa.
#     HostAddress:fd...:....

# operational CASE + custom cluster 0xFFF1FC00 SetScene (solid red):
lo-set-scene <node-id> 1 1 ff0000 0 255
```

`lo-set-scene` performs CASE + invokes the custom cluster; the LED must render
solid red with **no `error 32`** (`CHIP_ERROR_TIMEOUT`).

## Current bench status (2026-06-08)

First Stage B flash to the S3 host on `/dev/cu.usbmodem1101` succeeded, but the
BR did **not** reach the first gate (`rcp version`). Boot logs showed a spinel
response timeout followed by a host-side RCP-update failure:

```text
OPENTHREAD: spinel UART interface initialization completed
P-SpinelDrive-: Wait for response timeout
OpenThread: Internal RCP Version: openthread-esp32/4c2820d377-005c5cefc; esp32h2; 2026-06-06 23:36:41 UTC
RCP_UPDATE: Failed to connect to RCP
RCP firmware not found in storage, will reboot to try next image
```

Treat this as **Stage B blocked**, not failed against the BR/DNS-SD gate yet. The
H2 USB1 path now works: `esptool chip_id` reports ESP32-H2 rev v1.2, and direct
`ot_rcp` flash succeeded at 115200 baud.

Update 2026-06-07: a no-auto-update direct-RCP diagnostic was added and flashed
to the S3 (`flash-br-direct /dev/cu.usbmodem101`) after direct-flashing the H2 and
unplugging H2 USB1. The generated config had `CONFIG_AUTO_UPDATE_RCP` disabled,
the app used no `rcp_fw` image, and the S3 still timed out twice and aborted in
`ResetCoprocessor() at spinel_driver.cpp:160`. This proved the RCP-update SPIFFS
sequence was not the only failing subpath at that physical state.

Update 2026-06-08: the same `br-direct` image passed the spinel boundary with S3
USB2 only. macOS exposed the S3 as `/dev/cu.usbmodem1101`; `chip_id` confirmed
MAC `9c:13:9e:0a:46:88`. The monitor reached
`Software reset co-processor successfully`, `OpenThread attached to netif`, and
Matter server initialization. The live direct-flashed-H2 hardware path is usable,
but the stock app shell did not register the BR OT CLI commands:
`matter esp ot_cli rcp version`, `state`, and `srp server state` all returned
`Error: 83886338`.

Update 2026-06-08, later: Stage B now builds from
[`thread_border_router_otcli/`](thread_border_router_otcli/), a local copy of the
stock `thread_border_router` app shell with
`esp_matter::console::otcli_register_commands()` registered before console init.
`flash-br-direct /dev/cu.usbmodem1101` rebuilt, erased, flashed, and verified the
image. Boot again reached `Software reset co-processor successfully` and
`OpenThread attached to netif`. The Matter console now exposes the control path:

```text
matter esp help
    ot_cli: Openthread cli commands. Usage: matter esp ot_cli <command>.

matter esp ot_cli rcp version
openthread-esp32/4c2820d377-005c5cefc; esp32h2; 2026-06-07 01:01:49 UTC

matter esp ot_cli state
disabled

matter esp ot_cli srp server state
disabled
```

This unblocked B.2 for the direct-flashed-H2 path. B.2 then passed:
`dataset init new` / `dataset commit active` returned `Done`, `ifconfig up`
raised the OpenThread netif, `thread start` elected the BR as `leader`, and
`srp server enable` made `srp server state` return `running`.

Dataset TLVs for the next C6 controller attach/commissioning step:

```text
0e08000000000001000000030000154a0300001035060004001fffe00208a4722531cc1f59020708fd783f10c811bc78051056e95233105cc18d2419a9f1839e5364030f4f70656e5468726561642d643431610102d41a04108ab87ae955aa7c39249a334e01c5beea0c0402a0f7f8
```

Update 2026-06-08: the C6 controller was reflashed with the Stage 0 client
overlay through `EXTRA_SDKCONFIG_DEFAULTS` and
`matter-prototype/stage0-br-validation/build/client/sdkconfig`. The generated
config confirmed `CONFIG_OPENTHREAD_BORDER_ROUTER` unset,
`CONFIG_LED_ORCHESTRA_OPERATOR_WIFI_MODE_DISABLED=y`, and
`CONFIG_OPENTHREAD_NUM_MESSAGE_BUFFERS=128`.

The controller joined the S3+H2 BR dataset and reached `router`. Fast DNS-SD
triage passed: after re-enabling the S3 SRP server following a monitor-induced
S3 reset, the C6 registered a throwaway SRP `probe` service and
`dns browse _matter._tcp.default.service.arpa` returned both the probe and the
controller's own `_matter._tcp` record. The `probe` service was removed; the
controller remained registered and a later browse returned:

```text
49F59A617842C60B-000000000001B669
    Port:5580, Priority:0, Weight:0, TTL:7175
    Host:F6B47AAAFC8975AE.default.service.arpa.
    HostAddress:fd78:3f10:c811:bc78:ad94:fe8:844b:8320 TTL:7175
    TXT:[SII=32303030, SAI=32303030, SAT=34303030] TTL:7175
```

Next: continue B.4 with the C6 LED node commissioning, then run the real LED
DNS browse + CASE/SetScene gate. The host-side `CONFIG_AUTO_UPDATE_RCP` path
remains a separate unresolved subpath.

Update 2026-06-08, later — **Stage B PASS (end-to-end)**. The C6 controller
commissioned a C6 LED node over BLE→Thread (attempt 1 succeeded with a fresh
controller reboot just before `pairing ble-thread`: PASE → `AddNOC` → Thread
attach → SRP register → CASE → `CommissioningComplete`, LED logged
`lo_led_node: commissioning complete`). `dns browse` on the controller resolved
the LED **through the S3+H2 BR** (`49F59A617842C60B-0000000000000001`), CASE
established in ~1 s after a clean controller reset, and `lo-set-scene`
(`endpoint 1`, effect 1) rendered solid red/green/blue. Three gotchas are
recorded in the [debugging journal](../../docs/debugging-journal.md)
(2026-06-08 end-to-end entry): a **stale CASE session** silently swallows
`SetScene` after a node reboot (fix: reset the controller); `lo-set-scene` **arg
2 is the endpoint** (not a sequence); and the bench **WS2815 is wired RGB** while
`led_strip` emits GRB (fix: swap R/G in the renderer, reflash without erasing
NVS). The host-side `CONFIG_AUTO_UPDATE_RCP` path remains unresolved separately.

## Evidence (fill at the bench)

```text
Date / operator:                            2026-06-08 / Codex + operator
S3 BR firmware:  thread_border_router_otcli + sdkconfig.s3-br-direct-rcp.defaults (esp32s3)
RCP path used:   direct H2 flash succeeded at 115200; no-auto br-direct diagnostic passed spinel init and OT CLI on 2026-06-08
rcp version:     openthread-esp32/4c2820d377-005c5cefc; esp32h2; 2026-06-07 01:01:49 UTC
BR state / srp server:                       leader/router (varies across reboots) / running
C6 controller attach state:                   leader/router (varies across reboots; attached to the BR mesh)
dns browse result (paste the _matter._tcp record, or the error): PASS; C6 browse via S3+H2 BR returned the LED 49F59A617842C60B-0000000000000001 (plus the controller's …-000000000001B669)
CASE / SetScene:  rendered? color/observed:   PASS; solid red/green/blue rendered on the physical 12V WS2815 strip via operational CASE; correct after an R/G color-order swap (see notes); no error 32 on the render runs
OT bufferinfo (free / max-used):              C6 126 / 8; S3 63 / 14
S3 heap: free / min-free / largest-free-block:
Pass/Fail vs gate:                            PASS — full Stage B gate met: rcp version OK; BR leader + srp running; the separate C6 controller resolves the LED's _matter._tcp through the S3+H2 BR; CASE + SetScene render on the physical LED; no Error 28/32
Notes:                                        (1) commission the LED with a fresh controller reboot before each BLE pairing attempt (clean BLE stack; dodges single-radio BLE/802.15.4 contention on the C6). (2) After a node reboot, reset the controller to drop a stale operational CASE session, else SetScene is acked but silently never reaches the node. (3) lo-set-scene arg 2 is the ENDPOINT (=1), not a sequence. (4) Bench strip is a WS2815 wired RGB; led_strip only emits GRB, so the renderer swaps R/G — reflash with a plain `flash` (no NVS erase) to keep commissioning. After S3 monitor reset, re-run `srp server enable`. Auto-update BR path remains unresolved separately.
```

## Decision

**Result 2026-06-08: PASS** — proceeding to Stage C (co-locate the controller
onto the S3 BR host).

- **PASS** → proceed to [`stage-c-onehub.md`](stage-c-onehub.md) (co-locate the
  controller onto the S3 BR host).
- **FAIL — separate client can't resolve through the S3+H2 BR** → the C6/H2
  esp-thread-br path is suspect. Capture logs, file a
  [`debugging-journal.md`](../../docs/debugging-journal.md) entry, and consider
  **Fallback 2 (Pi `ot-br-posix`)** per the validation decision rules.
- **FAIL — `error 32` on CASE** → unexpected here (the C6 client already drops its
  Wi-Fi softAP and the radios are split across S3/H2); capture `bufferinfo` + heap
  and journal it.
