# Stage D — Repeatability + Recovery (split topology)

**Goal:** move the proven split hub from "worked once" (Stage B re-confirmed on
2026-06-09) to a **credible hub** — prove the commission → discover → CASE →
`SetScene` path survives repetition and unclean power-cycles of **every** hub
component and of the LED.

> **Topology note.** Stage C (the co-located S3+H2 one-board hub) failed its
> offline operational-discovery gate on 2026-06-09 and is **parked** on the
> heap/flash-headroom rationale. Stage D therefore runs on the **split**: the
> S3+H2 board as **BR-only** + a **separate C6 controller** + ≥ 1 C6 LED. The
> one-board variant of this runbook is preserved in git history if it is ever
> revisited.

**Topology under test**

```text
ESP32-S3 (+H2 RCP) = BR-only: Thread border router + SRP server (thread_border_router_otcli)
ESP32-C6 #1        = Matter commissioner/controller (lo-* console; Thread leader; Wi-Fi off)
ESP32-C6 LED(s)    = Thread accessory(ies), custom cluster 0xFFF1FC00
```

So **"the hub" is now two devices** (the S3 BR + the C6 controller). Recovery
must restore **both** independently and together — that is the substance of D.3.

**Gate (all must hold):**

- **100% recovery** over the agreed cycle count (≥ 20 power-yank cycles per the
  validation doc), with the **first successful scene ≤ 60 s** after the cycled
  device is back.
- **No monotonic heap drift** on the controller **or** the LED across the run
  (min free heap and largest free block do not trend down).
- **No DNS-SD regression** (`dns browse` on the controller keeps returning the
  LED record).
- **No CASE-timeout regression** (no `Error 28` / `Error 32`).
- **Credentials persist across reboot (NVS):** the S3 keeps its Thread dataset,
  the C6 controller keeps its Thread dataset **and** Matter fabric. Losing either
  (forcing a re-commission) is a **FAIL** to journal.

> Prereq: the split is the **re-confirmed Stage B baseline** (2026-06-09 rollback
> validation — fresh LED commissioned as node `3`, `lo-set-scene 3 …` rendered).
> See [`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md)
> and [`../../docs/checkpoints/2026-06-09-stage-b-split-known-good.md`](../../docs/checkpoints/2026-06-09-stage-b-split-known-good.md).

## Heap instrumentation (built in)

Both `controller-node` and `led-node` now emit, every ~10 s, the two
quantitative gate metrics (added in `app_main.cpp`):

```text
lo_heap: free=<bytes> min_free=<bytes> largest=<bytes>
```

`min_free` is `esp_get_minimum_free_heap_size()`; `largest` is
`heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)` — exactly the gate's
"Min free heap" and "Largest free block". Capture by grepping the monitor logs:

```bash
idf.py -p <PORT> monitor | grep lo_heap | tee stage-d-heap-<device>.log
```

Sample `min_free`/`largest` at the start of each step and after each batch; the
**trend** is what the no-drift gate checks. (The S3 BR is the stock
`thread_border_router_otcli` image — sample its heap with the built-in `heap`
diagnostic on its console; it is not the load-bearing controller, so the C6
controller and the LED are the primary subjects.)

## D.1 Repeat the operation loop (no reboot)

With a LED already commissioned, exercise the **operational** path (no BLE, no
re-commission) several times and watch the heap, per the
[2026-06-04 lesson](../../docs/debugging-journal.md) that `error 32` fires
*post-commit* — so CASE on an already-commissioned node is the right stressor.
Drive it from the **C6 controller** console with `lo-set-scene` (arg order:
`<node-id> <endpoint> <effect> <rgbhex> <speed> <brightness> <seq>`; arg 2 is the
**endpoint**; bump `<seq>` each call — it is monotonic by contract):

```text
# repeat N times (start with N = 10), alternating scenes so the change is visible:
lo-set-scene <node-id> 1 1 ff0000 0 255 <seq>     # solid red
lo-set-scene <node-id> 1 1 0000ff 0 255 <seq+1>   # solid blue
```

Record `lo_heap min_free`/`largest` on the controller and the LED at the start
and after each batch of N.

## D.2 Power-cycle the LED (hub stays up)

```text
# physically power-yank the C6 LED, then re-power it.
# on the C6 controller, confirm it rediscovers + re-resolves WITHOUT re-commission:
matter esp ot_cli dns browse _matter._tcp.default.service.arpa     # LED record returns
lo-set-scene <node-id> 1 1 00ff00 0 255 <seq>                      # solid green
```

Gate: the node rejoins Thread, the record reappears, and the next `lo-set-scene`
renders — first scene **≤ 60 s** after the LED boots. **Keep-last-valid:** while
that LED reboots, any *other* already-rendering node must hold its last scene
(verify on a second node — nodes 1/2/3 are available per the checkpoint).

## D.3 Power-cycle the hub (the real deployment event)

In the split, "the hub" is two devices, so test each path. Run each ≥ 20 cycles
and log a pass/fail per cycle.

### D.3a — Power-cycle the S3 BR alone

```text
# power-yank the S3+H2 board, then re-power. On the S3 BR console:
matter esp ot_cli rcp version          # S3<->H2 link back (no repeated RCP resets)
matter esp ot_cli state                # re-attaches (router/leader)
matter esp ot_cli srp server state     # running
# on the C6 controller, after LEDs re-register with the returned SRP server:
matter esp ot_cli dns browse _matter._tcp.default.service.arpa   # LED record returns
lo-set-scene <node-id> 1 1 ffff00 0 255 <seq>
```

Gate: the BR returns, RCP link healthy, the SRP server repopulates (LEDs
re-register), discovery + CASE + `SetScene` work again, first scene **≤ 60 s**.
The S3 must **keep its Thread dataset** across the yank (NVS) — losing it is a
FAIL.

### D.3b — Power-cycle the C6 controller alone

```text
# power-yank the C6 controller, then re-power. On the controller console:
matter esp ot_cli state                # rejoins the mesh from NVS dataset
lo-set-scene <node-id> 1 1 ff8000 0 255 <seq>    # re-resolve + control resumes
```

Gate: the controller reloads its **Matter fabric + Thread dataset from NVS**
(never re-commissions), re-resolves the node, and the first scene renders
**≤ 60 s**. Note the [checkpoint gotcha](../../docs/checkpoints/2026-06-09-stage-b-split-known-good.md):
a controller reset clears stale operational CASE sessions — confirm whether a
clean reboot already does this or whether an explicit reset step is needed, and
record which.

### D.3c — Power-cycle both (full hub yank)

Yank S3 BR and C6 controller together, re-power, and run the full
bring-up + control check. This is the true deployment power event.

Gate: both come back from NVS, Thread re-forms, discovery + CASE + `SetScene`
work, first scene **≤ 60 s**, no Error 28/32.

## Evidence (fill at the bench)

```text
Date / operator:
Controller firmware (heap-instrumented build? y/n):  LED firmware (heap-instrumented? y/n):
D.1 repeat loop:  N batches, controller min-free start -> end / largest start -> end:
D.1 LED min-free start -> end / largest start -> end:
D.1 heap drift?   (monotonic down on either = FAIL):
D.2 LED power-cycle:  rediscovered w/o re-commission? first-scene seconds:
D.2 keep-last-valid on another node? (y/n):
D.3a S3-BR cycles:    <pass>/<total>; SRP repopulated? dataset persisted? first-scene s (min/med/max):
D.3b controller cycles: <pass>/<total>; fabric+dataset persisted (no re-commission)? reset needed for stale CASE? first-scene s:
D.3c both-yank cycles:  <pass>/<total>; first-scene s (min/med/max):
rcp version after reboot / RCP reset count over the run:
Any Error 28 / Error 32?  (must be none):
Pass/Fail vs gate:
Notes:
```

## Decision

- **PASS** → the split hub is repeatable + recoverable for one node; proceed to
  [`stage-e-scale-soak.md`](stage-e-scale-soak.md). Record the cycle count, the
  heap trend, and the first-scene timing in
  [`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md).
- **FAIL — recovery < 100%, heap drift, or lost credentials** → if it is a
  discrete bug (lost dataset, RCP reset storm, stale-CASE hang), file a
  [`debugging-journal.md`](../../docs/debugging-journal.md) entry, fix, and re-run.
  The split is already the fallback rung, so a *fundamental* split failure
  escalates to **Fallback 2 (Pi / `ot-br-posix`)** rather than back to one-board.
