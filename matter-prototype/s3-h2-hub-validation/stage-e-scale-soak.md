# Stage E — Scale + Soak

**Goal:** prove the S3+H2 one-board hub candidate at **product-like load** —
scale toward ~20 C6 LED nodes, validate **group** `SetScene`, and run a long soak
within the quantitative metric targets.

**Topology:** the [`stage-c-onehub.md`](stage-c-onehub.md) hub (S3 controller +
esp-thread-br host; H2 RCP) scaled to N C6 LED nodes (grow N incrementally toward
the ~20-node target).

**Gate:** the quantitative thresholds in
[`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md#passfail-metrics-quantitative-gate)
hold at 20-node scale through the soak — specifically:

- **Min free heap** ≥ 48 KB sustained; **largest free block** ≥ 24 KB sustained
  (baseline on the first clean multi-node run, then ratify).
- **No monotonic heap drift** over the soak (eventually **72 h**).
- **Operational discovery** 100% over ≥ 50 attempts (incl. re-resolution after
  drop/rejoin); **reboot recovery** 100% over ≥ 20 cycles.
- **Group `SetScene`** reaches **all** commissioned nodes within the latency
  budget.
- **Flash headroom** ≥ 25% on the 8 MB part; **RCP** version returns with no
  repeated resets over the run.

> Prereq: **Stage D PASS**. Grow node count gradually (1 → 2 → a few → ~20);
> capture the heap/flash/discovery metrics at each step so the scaling curve is
> visible, not just the endpoint.

---

## E.1 Commission additional C6 LED nodes

Commission each new node from the hub, then confirm it resolves through the hub's
own BR before adding the next (catches per-node SRP/DNS-SD or heap regressions
early):

```text
matter esp controller pairing ble-thread <node-id-k> <dataset_tlvs> 20202021 3840
matter esp ot_cli dns browse _matter._tcp.default.service.arpa     # k records present
# sample heap after each node:
heap
```

Record min free heap + largest free block + flash headroom after each node so the
**per-node cost** is explicit.

### E.1.1 Known-good bench workflow for the next node

Do **not** restart Stage B/C debugging from scratch when adding node `k`. The
current working path is a runtime procedure, not a rebuild:

1. Keep the S3+H2 BR alive and confirm it still owns SRP/DNS-SD:

   ```text
   matter esp ot_cli state                  # router/leader is OK
   matter esp ot_cli srp server state       # must be running
   matter esp ot_cli srp server enable      # rerun if disabled/stopped after S3 reset
   matter esp ot_cli dataset active -x      # use this TLV for pairing
   ```

2. Reset the C6 controller immediately before each BLE pairing attempt. This
   clears stale pairing locks, stale CASE sessions, and the flaky C6 single-radio
   BLE/802.15.4 state.

3. Reset the new LED node and confirm it logs `commissioning window opened`.

4. Send the long pairing command with paced serial writes. Do **not** paste or
   dump the whole line at once; the USB-Serial/JTAG console path silently
   truncates long lines. Use `sercap.py` (16-byte chunks / 30 ms delay) or an
   equivalent chunked writer, and confirm the controller echo contains the final
   `20202021 3840` before trusting the attempt:

   ```text
   matter esp controller pairing ble-thread <node-id-k> <dataset_tlvs> 20202021 3840
   ```

   `CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH` is the wrong layer for this symptom;
   do not rebuild just to change it.

5. Judge success by device evidence, not quiet controller logs:

   ```text
   # LED node log
   Received CommissioningComplete
   Commissioning completed successfully
   lo_led_node: commissioning complete

   # Controller
   matter esp ot_cli dns browse _matter._tcp.default.service.arpa
   # expect ...-0000000000000001, ...-0000000000000002, ...-<node-id-k>, etc.
   ```

   A transient `operational discovery failed: 32` during commissioning can be a
   race if the node later appears in DNS-SD and direct CASE/`SetScene` works.

6. Keep bench power under the adapter limit while scaling. For the Fibonacci
   effect (`effect=3`), start low:

   ```text
   lo-set-scene <node-id-k> 1 3 000000 10 60 <seq>
   ```

   `brightness=60` is about 24% of full scale and was the proven runtime
   workaround for the blink/recover power-limit cycle. `brightness=179` is 70%
   and is **not** the same workaround. Ramp up only after the strips are stable.

Note on unicast vs group: a bare `lo-set-scene 0x0001 ...` logs as
`destination=0x1` — that is **unicast node 1**, not a group. Real groupcast goes
through the `lo-*-group` commands below (which encode the group id with
`chip::NodeIdFromGroupId`). Use direct per-node `lo-set-scene` for the power-limit
workaround until the group path in E.2 is configured and verified.

## E.2 Group `SetScene` (the scale gate)

Once ≥ 2 nodes are commissioned, do the one-time group setup, then drive **one**
group command — this is the product behavior (one virtual strip, all segments in
sync). Run `lo-show-group-help` for the exact sequence; in summary:

```text
# 1. Controller-side group keyset (built-in group-settings; once):
matter esp controller group-settings add-keyset 0x0042 0 0xFFFFFFFFFFFFFFFF <32-hex-epoch-key>
matter esp controller group-settings bind-keyset 0x0001 0x0042
matter esp controller group-settings add-group   0x0001 orchestra

# 2. Per node: install the same group key + map (Group Key Management 0x003F),
#    then enroll the LED endpoint into the group:
matter esp controller invoke-cmd <node> 0 0x003F 0x00 "<KeySetWrite GroupKeySetStruct>"
matter esp controller write-attr <node> 0 0x003F 0x00 "[{<GroupKeyMapStruct>}]"
lo-add-group <node> 1 0x0001 orchestra        # repeat for every node

# 3. One groupcast SetScene reaches all enrolled nodes:
lo-set-scene-group 0x0001 1 ff0000 10 60
lo-sync-clock-group 0x0001                     # align clocks first
lo-scheduled-scene-group 0x0001 3000 1 ff0000 0 60   # all flip together in 3 s
```

Gate: **every** commissioned node renders the group scene within the latency
budget. Because effects are pure functions of `(global_index, time_ms, params)`,
a single group `SetScene` (+ a shared clock via `SyncClock`) must keep all
segments coherent without per-node fixups.

**The node-side group-key install (step 2) is the part to confirm on hardware.**
Until every node holds the group key, a groupcast is silently not accepted and
each node keeps its last valid scene — so verify by watching **all** strips change
from one `lo-set-scene-group`, not by the controller's `Done`. Journal the exact
working `KeySetWrite`/`GroupKeyMap` payloads once proven.

## E.3 Soak

Run the hub + nodes continuously, eventually **72 h**, holding a representative
scene (or a slow rotation) and periodically re-resolving/commanding. Capture the
metrics on an interval (the hub's periodic heap log + scripted `dns browse` /
`invoke-cmd` probes). Use
[`serlog.py`](../stage0-br-validation/tools/serlog.py) /
[`sercap.py`](../stage0-br-validation/tools/sercap.py) to timestamp + persist the
hub serial log across the soak.

Track over the soak: **min free heap**, **largest free block**, **flash
headroom**, **discovery success rate**, **RCP resets**, **reboot recovery**, and
**command latency**.

## Evidence (fill at the bench)

```text
Date / operator:
Backbone config (offline | local wifi):
Node count reached:                 (target ~20)
Per-node heap cost (min-free-heap @ 1 / few / 20 nodes):
Largest free block @ 1 / few / 20 nodes:
Flash headroom @ 20 nodes (>=25%?):
Group SetScene: all nodes rendered? latency (worst case):
Soak duration (target 72 h):
Min free heap over soak (floor) / drift? (monotonic down = FAIL):
Discovery success rate (>=50 attempts):
Reboot recovery (>=20 cycles): pass/total
RCP resets over the soak:
Command latency (median / worst):
Pass/Fail vs gate:
Notes:
```

## Decision

- **PASS** → the S3+H2 one-board hub holds at product-like scale + soak; proceed
  to [`stage-f-ingress.md`](stage-f-ingress.md). Ratify the baselined thresholds
  in [`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md).
- **FAIL — heap/flash/latency at scale** → if it is a co-located-headroom wall,
  fall back to **Fallback 1, the all-C6 split** (move the controller to its own
  C6). If it is a discrete scaling bug (SRP table size, group handling), journal
  it in [`debugging-journal.md`](../../docs/debugging-journal.md), fix, and re-run.
