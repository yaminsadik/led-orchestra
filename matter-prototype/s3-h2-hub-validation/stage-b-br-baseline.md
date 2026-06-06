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
idf.py -C ../controller-node -B ../controller-node/build \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.local;../stage0-br-validation/sdkconfig.client.defaults" \
  set-target esp32c6 build flash -p <C6_CTRL_PORT>

# C6 LED node (stock app), put into commissioning at boot:
idf.py -C ../led-node -B ../led-node/build set-target esp32c6 build flash -p <C6_LED_PORT>
```

(The Stage 0 runbook documents this client overlay in detail:
[`../stage0-br-validation/README.md`](../stage0-br-validation/README.md).)

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

## Evidence (fill at the bench)

```text
Date / operator:
S3 BR firmware:  thread_border_router + sdkconfig.s3-br-host.defaults (esp32s3)
RCP path used:   host-side AUTO_UPDATE_RCP | direct H2 flash
rcp version:                                  (paste)
BR state / srp server:  leader / running?     (paste)
C6 controller attach state:                   (paste)
dns browse result (paste the _matter._tcp record, or the error):
CASE / SetScene:  rendered? color/observed:   no error 32? (y/n)
OT bufferinfo (free / max-used):              (paste, watch for pool starvation)
S3 heap: free / min-free / largest-free-block:
Pass/Fail vs gate:
Notes:
```

## Decision

- **PASS** → proceed to [`stage-c-onehub.md`](stage-c-onehub.md) (co-locate the
  controller onto the S3 BR host).
- **FAIL — separate client can't resolve through the S3+H2 BR** → the C6/H2
  esp-thread-br path is suspect. Capture logs, file a
  [`debugging-journal.md`](../../docs/debugging-journal.md) entry, and consider
  **Fallback 2 (Pi `ot-br-posix`)** per the validation decision rules.
- **FAIL — `error 32` on CASE** → unexpected here (the C6 client already drops its
  Wi-Fi softAP and the radios are split across S3/H2); capture `bufferinfo` + heap
  and journal it.
