# 2026-06-30 LED Bench Commissioning Checkpoint

This is the current known-good checkpoint for the split Matter-over-Thread LED
bench:

- S3+H2 board is the Thread Border Router on `/dev/cu.usbmodem101`
- ESP32-C6 controller is on `/dev/cu.usbmodem11201`
- New LED boards typically enumerate on `/dev/cu.usbmodem11101`
- Existing LED strip data pin is `GPIO2`
- Default development setup PIN is `20202021`
- Default development discriminator is `3840`

Thread dataset TLVs:

```text
0e080000000000010000000300001535060004001ff8000208a4722531cc1f59020708fd783f10c811bc78051056e95233105cc18d2419a9f1839e5364030f4f70656e5468726561642d643431610102d41a04108ab87ae955aa7c39249a334e01c5beea0c0402a0f778
```

## Bench Rules

- Commission one LED node at a time while using shared development credentials.
- Keep only the target new LED node in commissioning mode.
- Arg 2 to `lo-set-scene` is always endpoint `1`.
- If a node reboots or rejoins and `SetScene` is accepted but does not visibly
  land, reset the controller to clear stale CASE sessions.
- Keep border-router BLE advertising disabled before pairing so the controller
  does not hit the wrong device.
- Do not erase or factory-reset existing node `2` unless explicitly requested.
- For animated rendering stability, each ESP32-C6 needs a **direct local ground
  connection to its own LED strip ground**. Do not rely only on shared/common
  grounding through the PDU, buck converter path, or another strip/node.

## Health Checks

Border router:

```text
matter esp ot_cli state
matter esp ot_cli srp server state
matter esp ot_cli srp server service
matter ble adv state
matter ble adv stop
```

Expected:

- OpenThread state is `router` or `leader`
- SRP server state is `running`
- `_matter._tcp` services exist for commissioned nodes
- BLE advertising is disabled on the BR before LED pairing

## Flash + Commission Loop

Flash the newly attached LED board:

```bash
idf.py -C matter-prototype/led-node -B build -p /dev/cu.usbmodem11101 flash
```

Confirm the node boots the LED-node firmware and opens a commissioning window:

```bash
python3 matter-prototype/stage0-br-validation/tools/serlog.py /dev/cu.usbmodem11101 --dur 8 --reset
```

Reset the controller before each pairing attempt:

```bash
python3 matter-prototype/stage0-br-validation/tools/serlog.py /dev/cu.usbmodem11201 --dur 6 --reset
```

Pair the target node:

```text
matter esp controller pairing ble-thread <node-id> <dataset-tlvs> 20202021 3840
```

Verify the new node appears in BR SRP service listings:

```text
matter esp ot_cli srp server service
```

Send a low-power render test:

```text
lo-set-scene <node-id> 1 1 00ff00 0 80 <unique-seq>
```

If needed, prove device-side receipt by tailing the node serial and looking for:

- `Received command`
- `scene seq=<seq>`
- `scene persisted`

Useful helpers:

```bash
python3 matter-prototype/stage0-br-validation/tools/serlog.py <port> --dur <seconds> [--reset] [--cmd "<command>"]
python3 matter-prototype/stage0-br-validation/tools/sercap.py <port> <logfile> <fifo>
```

## Known-Good Render Commands

Low-power green:

```text
lo-set-scene <node-id> 1 1 00ff00 0 80 <seq>
```

Low-power solid red:

```text
lo-set-scene <node-id> 1 1 ff0000 0 80 <seq>
```

Known-good jungle-style animation after lowering brightness:

```text
lo-set-calibration 2 1 - - 4
lo-set-scene 2 1 7 000000 8 80 102
```

Animated-rendering hardware rule confirmed on the bench:

- `GPIO2 -> DIN`
- `C6 GND -> same strip GND`
- Keep the data wire and that local ground physically close

If solids work but animation flickers, check the local node-side ground
reference before assuming a Matter or firmware problem.

Optional node config provisioning:

```text
lo-set-node-config <node-id> 1 <orchestra-node-id> <segment-start> <segment-len> <total-leds> 2
```

## Commissioned Nodes

| Matter node id | Physical board / strip label | Result | Notes |
| --- | --- | --- | --- |
| 1 | not recorded here | User reports commissioned | Not reworked in this session |
| 2 | existing known-good LED node | Commissioned and render-verified | Solid green and animation previously verified |
| 3 | not recorded here | User reports commissioned | User confirmed flashed and commissioned |
| 4 | pending label | Flashed, commissioned, render-verified | Node-side `SetScene` confirmed |
| 5 | pending label | Flashed, commissioned, render-verified | Node-side `CommissioningComplete` and `SetScene` confirmed |
| 6 | pending label | Flashed, commissioned, render-verified | Also tested with solid red |
| 7 | pending label | Flashed, commissioned, render-verified | Node-side `SetScene` confirmed |
| 8 | pending label | Flashed, commissioned, render-verified | Node-side `SetScene` confirmed |
| 9 | pending label | Flashed, commissioned, render-verified | SRP present and node-side `SetScene` seq `6902` confirmed |

## SRP Evidence Captured During This Pass

- Node `4`: host `AAB9D1D36748891D.default.service.arpa.`
- Node `5`: host `6EF469359DC91187.default.service.arpa.`
- Node `6`: host `CE70132E23A7BD44.default.service.arpa.`
- Node `7`: host `7AA21D36E3670628.default.service.arpa.`
- Node `8`: host `16019B320D9827B2.default.service.arpa.`
- Node `9`: service `549493DDF6A93FE3-0000000000000009._matter._tcp.default.service.arpa.`

## Non-Blocking Behavior Seen

The controller sometimes logs:

```text
operational discovery failed: 32
Session establishment failed ... error: 32
```

During this pass those messages were non-blocking when the node still completed
commissioning, appeared in SRP, and later accepted operational `SetScene`.

## 2026-07-02 Electrical Finding

Animated effects flickered badly across multiple nodes even when solid colors
looked good. The decisive fix was adding a **direct local ground** from the
ESP32-C6 node to the same LED strip ground used by that node's data input.

Implications:

- Shared/common ground only at the PDU was not enough by itself.
- The data line needed a solid **local reference ground** at the node/strip.
- After tying node `GND` directly to strip `GND`, animated rendering became
  stable on the isolated node test.
- For future bench bring-up, treat `C6 GND -> local strip GND` as mandatory
  wiring, not an optional cleanup step.

## Next Batch

The next free node ids are `10` and onward, assuming fabric state has not
changed. Before each new board:

1. Confirm BR health and BLE advertising state.
2. Flash `/dev/cu.usbmodem11101`.
3. Reset the controller.
4. Pair the new node.
5. Verify SRP.
6. Send a low-power render test and capture node-side proof if needed.
