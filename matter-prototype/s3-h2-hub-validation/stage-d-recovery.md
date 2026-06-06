# Stage D — Repeatability + Recovery

**Goal:** move the S3+H2 one-board hub from "worked once" (Stage C) to a
**credible hub** — prove the commission → discover → CASE → `SetScene` path
survives repetition and unclean power-cycles of both the hub and the LED.

**Topology:** unchanged from [`stage-c-onehub.md`](stage-c-onehub.md) — S3
(controller + esp-thread-br host) + H2 RCP + ≥ 1 C6 LED node.

**Gate (all must hold):**

- **100% recovery** over the agreed initial cycle count (≥ 20 power-yank cycles
  per the validation doc), with the **first successful scene ≤ 60 s** after the
  hub boots.
- **No monotonic heap drift** in the short run (min free heap and largest free
  block do not trend down across the repeated operations).
- **No DNS-SD regression** (`dns browse` keeps returning the LED record).
- **No CASE-timeout regression** (no `Error 28` / `Error 32`).

> Prereq: **Stage C PASS** recorded in
> [`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md).
> If Stage C passed only with a local Wi-Fi backbone, re-run every step here in
> that same backbone configuration and note it (recovery must also restore the
> backbone path).

---

## D.1 Repeat the operation loop (no reboot)

With the LED already commissioned from Stage C, exercise the **operational** path
(no BLE, no re-commission) several times and watch the heap, per the
[2026-06-04 lesson](../../docs/debugging-journal.md) that `error 32` fires
*post-commit* — so CASE on an already-commissioned node is the right stressor:

```text
# repeat N times (start with N = 10), alternating scenes so the change is visible:
matter esp controller invoke-cmd <node-id> 1 0xFFF1FC00 0 {"0:U8":1,"1:U8":255,"2:U8":0,"3:U8":0,"4:U8":0,"5:U8":255,"6:U32":1,"7:U64":0}
matter esp controller invoke-cmd <node-id> 1 0xFFF1FC00 0 {"0:U8":1,"1:U8":0,"2:U8":0,"3:U8":255,"4:U8":0,"5:U8":255,"6:U32":2,"7:U64":0}

# after each batch, sample the heap on the hub (esp-idf 'heap' diag,
# or just read the firmware's periodic free-heap / min-free-heap log line):
heap
```

Record min free heap + largest free block at the start and after each batch.
Bump the `6:U32 sequence` field each call (it is monotonic by contract).

## D.2 Power-cycle the LED (hub stays up)

```text
# physically power-yank the C6 LED, then re-power it.
# on the hub, confirm it rediscovers + re-resolves WITHOUT a re-commission:
matter esp ot_cli dns browse _matter._tcp.default.service.arpa     # record still present
matter esp controller invoke-cmd <node-id> 1 0xFFF1FC00 0 {"0:U8":1,"1:U8":0,"2:U8":255,"3:U8":0,"4:U8":0,"5:U8":255,"6:U32":3,"7:U64":0}
```

Gate: the node rejoins Thread, the record reappears, and the next `SetScene`
renders — first scene **≤ 60 s** after the LED boots. **Keep-last-valid:** while
the LED is rebooting, an already-rendering node must hold its last scene (verify
on a second node if present).

## D.3 Power-cycle the hub (the real deployment event)

```text
# physically power-yank the S3+H2 hub, then re-power it.
matter esp ot_cli rcp version          # S3<->H2 link back (no repeated RCP resets)
matter esp ot_cli state                # leader/router (re-forms or re-attaches)
matter esp ot_cli srp server state     # running
matter esp ot_cli dns browse _matter._tcp.default.service.arpa   # LED record returns
matter esp controller invoke-cmd <node-id> 1 0xFFF1FC00 0 {"0:U8":1,"1:U8":255,"2:U8":128,"3:U8":0,"4:U8":0,"5:U8":255,"6:U32":4,"7:U64":0}
```

Gate: after an unclean hub power-down, the BR comes back, the RCP link is healthy,
discovery + CASE + `SetScene` work again, first scene **≤ 60 s**. Repeat for the
agreed cycle count (≥ 20) and log a pass/fail per cycle.

> Note the fabric/dataset must be **persistent** across reboot (NVS). If the hub
> loses its Thread dataset or Matter fabric on power-cycle, that is a Stage D
> FAIL to journal — the hub must keep credentials, not re-commission.

## Evidence (fill at the bench)

```text
Date / operator:
Backbone config (offline | local wifi, same as Stage C?):
D.1 repeat loop:  N batches, min-free-heap start -> end / largest-free-block start -> end:
D.1 heap drift?   (monotonic down = FAIL):
D.2 LED power-cycle:  rediscovered w/o re-commission? first-scene seconds:
D.2 keep-last-valid on a 2nd node? (y/n/NA):
D.3 hub power-cycle cycles:  <pass>/<total>  (target 100% of >=20):
D.3 first-scene seconds (min/median/max):
D.3 rcp version after reboot / RCP reset count over the run:
D.3 dataset+fabric persisted across reboot? (y/n):
Any Error 28 / Error 32?  (must be none):
Pass/Fail vs gate:
Notes:
```

## Decision

- **PASS** → the hub is repeatable + recoverable for one node; proceed to
  [`stage-e-scale-soak.md`](stage-e-scale-soak.md). Record the cycle count, the
  heap trend, and the first-scene timing in
  [`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md).
- **FAIL — recovery < 100% or heap drifts** → if the failure is co-located
  headroom/stability, fall back to **Fallback 1, the all-C6 split** (Stage 0
  config: controller on its own C6). If it is a discrete bug (lost dataset, RCP
  reset storm), file a [`debugging-journal.md`](../../docs/debugging-journal.md)
  entry, fix, and re-run before falling back.
