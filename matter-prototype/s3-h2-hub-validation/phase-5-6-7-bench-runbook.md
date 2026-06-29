# Phase 5/6/7 Bench Runbook — Split Topology

Tomorrow's bench. Exercises the new firmware (real group control, durable config,
synchronized scheduled scenes, OTA readiness) on the **proven split topology**:
S3+H2 board as **BR-only** + a separate ESP32-C6 **controller** + ESP32-C6 **LED
nodes**. Start with 3 LED nodes; the procedure scales to the ~20-node target with
no code change (no fixed-size arrays).

This builds on the known-good Stage B split:
[`../../docs/checkpoints/2026-06-09-stage-b-split-known-good.md`](../../docs/checkpoints/2026-06-09-stage-b-split-known-good.md)
and [`stage-b-br-baseline.md`](stage-b-br-baseline.md). Do **not** revive the
Stage C one-board hub.

Build commands (both apps, `esp32c6`):

```bash
. "$HOME/esp/esp-idf/export.sh"; . "$HOME/esp/esp-matter/export.sh"; export IDF_CCACHE_ENABLE=1
idf.py -C matter-prototype/led-node       -B matter-prototype/led-node/build       set-target esp32c6 build
idf.py -C matter-prototype/controller-node -B matter-prototype/controller-node/build set-target esp32c6 build
```

## 1. Restore the known-good split topology

Reflash the S3 as the Stage B BR-only direct-RCP image, bring up the BR with the
checkpoint dataset, and enable SRP. Confirm:

```text
matter esp ot_cli rcp version        # H2 RCP responds
matter esp ot_cli state              # router/leader
matter esp ot_cli srp server state   # running   (rerun `srp server enable` if not)
```

(Exact commands + dataset TLV: the 2026-06-09 checkpoint.)

## 2. Commission 3 nodes (process stays scalable)

Reset the C6 controller before each BLE pairing; send the long pairing line with
**paced writes** and confirm the echo ends in `20202021 3840`:

```text
matter esp controller pairing ble-thread <node-id> <dataset_tlvs> 20202021 3840
```

Bench lesson from 2026-06-26: if the S3 BR is still advertising the default
discriminator `3840`, a pairing command can hit the BR instead of the LED node.
Before pairing, make sure only the intended LED node has an open commissioning
window, or move production devices to unique discriminators/passcodes.

Commission node ids `1`, `2`, `3` one at a time. The same loop adds nodes `4..20`
later — capture min-free-heap after each (step 10).

## 3. Verify DNS browse

```text
matter esp ot_cli dns browse _matter._tcp.default.service.arpa
# expect ...-0000000000000001, ...0002, ...0003, and the controller/BR record
```

## 4. Verify direct unicast (per node)

Power-limited Fibonacci (effect `3`, low brightness) — arg 2 is the **endpoint**
`1`:

```text
lo-set-scene 1 1 3 000000 10 60 <seq>
lo-set-scene 2 1 3 000000 10 60 <seq>
lo-set-scene 3 1 3 000000 10 60 <seq>
```

Also confirm durable config round-trips: `lo-set-node-config 1 1 1 0 60 180 2`,
power-cycle node 1, and check its boot log says `config loaded from NVS ...` with
the values you set (not `from defaults`).

## 5. One-time group setup, then add every node to group `0x0001`

`lo-show-group-help` prints this. Controller keyset once, then per node install
the group key (Group Key Management `0x003F`) and enroll the endpoint:

```text
matter esp controller group-settings add-keyset 0x0042 0 0xFFFFFFFFFFFFFFFF <32-hex-epoch-key>
matter esp controller group-settings bind-keyset 0x0001 0x0042
matter esp controller group-settings add-group   0x0001 orchestra

# per node (1,2,3): use the helper to emit KeySetWrite + GroupKeyMap + AddGroup
# + least-privilege ACL (Group/Operate):
./lo-provision-group-member 1 --include-controller-setup
./lo-provision-group-member 2
./lo-provision-group-member 3
```

> The node-side `KeySetWrite`/`GroupKeyMap` install is the **gating step**. Until
> every node holds the group key, group commands are not accepted and each node
> keeps its last valid scene. Journal the exact working payloads once proven.

Bench lesson from 2026-06-26: after a LED node reset/reboot, reset the C6
controller before group provisioning or group-control proof. Stale operational
CASE sessions can otherwise produce timeouts even though the node has rejoined
Thread and is advertising again. A controller reset cleared the stale sessions
and the same group-key/AddGroup/ACL sequence then succeeded cleanly.

## 6. Verify ONE real group command

```text
lo-set-scene-group 0x0001 3 000000 10 60 <seq>
```

**Pass = all three strips change from this single command.** Judge by the strips,
not the controller's `Done`.

## 7. Verify group clock sync

```text
lo-sync-clock-group 0x0001
# each node logs: clock sync controller_ms=... offset_ms=...
```

## 8. Verify scheduled group scene

```text
lo-scheduled-scene-group 0x0001 3000 1 ff0000 0 60 <seq>
```

All nodes keep their current scene for ~3 s, then **flip to red together**. Each
node logs `scheduled scene accepted ...` then `scheduled scene activated ...`.
They do **not** blank during the wait (keep-last-valid).

2026-06-26 two-node Phase 6 proof: group `SetScene` sequence `6001` reached both
LED nodes at the same log timestamp, then
`lo-sync-clock-group 0x0001` + `lo-scheduled-scene-group 0x0001 3000 1 ff0000 0 60 6002`
was accepted by both nodes and activated within about 10 ms
(`492635` vs `492645` log ticks). This closes the two-node durable-config +
synchronized-scheduled-group hardware gate; repeat at larger node counts during
scale/soak.

## 9. Keep brightness configurable and low

`brightness=60` (~24% of full scale) is the proven bench power workaround. This is
a bench power concern, not a code limit — ramp up only after strips are stable.

## 10. Record heap before/after

The hub and nodes log heap every 10 s (`lo_heap: free=.. min_free=.. largest=..`).
Capture min-free / largest-free **before** the group/Phase-6 tests and **after**,
per node count (1 / 3 / … / 20), so the per-node cost is explicit. No monotonic
downward drift.

## 11. OTA validation (separate, do not claim early)

Mark OTA **functional only after a real LED node downloads and applies an image
over Matter/Thread.** QueryImage and BDX dispatch are proven; the remaining
image-source reachability, apply proof, and rollback proof are in
[`phase-7-offline-ota.md`](phase-7-offline-ota.md). Do not flip OTA to "done" on
the strength of the requestor existing or the provider answering QueryImage.

## Evidence (fill at the bench)

```text
Date / operator:
Nodes commissioned (ids):
DNS browse records:
Unicast set-scene per node: ok?
Durable config round-trip (NVS reload after power-cycle): ok?
Group member provisioning helper / payload that worked:
ONE group set-scene -> all strips changed? :
Power-cycle one provisioned node; groupcast still works? :
Group sync-clock: offsets logged?
Scheduled group scene -> all flip together, no blank? :
Heap min-free / largest-free  before / after  @ 3 nodes:
Heap @ 20 nodes (when scaled):
OTA: download+apply on real node? (else: NOT functional)
Notes / journal ref:
```
