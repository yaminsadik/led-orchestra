# 2026-06-09 Checkpoint — Known-Good Split Topology Before Stage C

This checkpoint exists so Stage C can reflash the S3+H2 board into the
co-located hub firmware without losing the path back to today's working bench.

## Git

Branch: `feat/s3-h2-hub`

Commit this file together with the 2026-06-09 debugging/runbook updates before
flashing Stage C. Use the resulting commit hash as the repo rollback point.

## Hardware State At Checkpoint

| Role | Board / state |
| --- | --- |
| BR | S3+H2 board running `thread_border_router_otcli` with direct-flashed H2 RCP |
| Controller | Separate ESP32-C6 `controller-node`, Stage 0 client overlay, fabric intact |
| LED node 1 | ESP32-C6, commissioned as Matter node id `1` |
| LED node 2 | ESP32-C6, commissioned as Matter node id `2` |

Do not assume USB ports survive reconnects. Re-map `/dev/cu.usbmodem*` every
session before flashing or opening monitors.

## Thread Dataset TLV

```text
0e08000000000001000000030000154a0300001035060004001fffe00208a4722531cc1f59020708fd783f10c811bc78051056e95233105cc18d2419a9f1839e5364030f4f70656e5468726561642d643431610102d41a04108ab87ae955aa7c39249a334e01c5beea0c0402a0f7f8
```

## Known-Good Runtime Checks

On the S3+H2 BR:

```text
matter esp ot_cli rcp version
matter esp ot_cli state
matter esp ot_cli srp server state
matter esp ot_cli srp server enable      # rerun after any S3 reset if not running
matter esp ot_cli dataset active -x
```

On the C6 controller:

```text
matter esp ot_cli dns browse _matter._tcp.default.service.arpa
```

Expected records when both LED nodes are powered:

```text
49F59A617842C60B-0000000000000001
49F59A617842C60B-0000000000000002
49F59A617842C60B-000000000001B669
```

Power-limited Fibonacci scene for the bench:

```text
lo-set-scene 1 1 3 000000 10 60 <seq>
lo-set-scene 2 1 3 000000 10 60 <seq>
```

`effect=3` is Fibonacci. `brightness=60` is the proven runtime workaround for
the current bench power shortage. It is about 24% of the 0-255 scale.

## How To Restore This Split Topology After Stage C

1. Check out the checkpoint commit that contains this file.
2. Reflash the S3 as the Stage B BR-only direct-RCP image:

   ```bash
   ./matter-prototype/s3-h2-hub-validation/build-s3-hub.sh flash-br-direct <S3_PORT>
   ```

   The H2 should already have the direct-flashed `ot_rcp` from Stage B. If
   `matter esp ot_cli rcp version` fails, re-provision the H2 RCP before chasing
   Thread or Matter symptoms.

3. Bring the BR back up with the dataset above and enable SRP:

   ```text
   matter esp ot_cli dataset set active <dataset_tlv>
   matter esp ot_cli dataset commit active
   matter esp ot_cli ifconfig up
   matter esp ot_cli thread start
   matter esp ot_cli srp server enable
   matter esp ot_cli srp server state
   ```

4. Reflash the separate C6 controller only if its firmware/NVS was changed. If
   needed, use the Stage 0 client overlay:

   ```bash
   S0="$PWD/matter-prototype/stage0-br-validation"
   EXTRA_SDKCONFIG_DEFAULTS="$S0/sdkconfig.client.defaults" \
   idf.py -C "$PWD/matter-prototype/controller-node" -B "$S0/build/client" \
     -D SDKCONFIG="$S0/build/client/sdkconfig" \
     set-target esp32c6 build flash -p <C6_CTRL_PORT>
   ```

   Do not erase NVS unless intentionally recommissioning, because the controller
   fabric and node credentials live there.

5. Reset the controller after node power-cycles to clear stale CASE sessions.

## Gotchas To Preserve

- Send long `pairing ble-thread` commands with paced writes (`sercap.py` or
  equivalent); confirm the echo ends in `20202021 3840`.
- `CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH` is not the root fix for truncated
  pairing commands.
- `lo-set-scene` arg 2 is the endpoint and must be `1` for the LED endpoint.
- `lo-set-scene 0x0001 ...` was not proven as true groupcast in this bench; use
  direct per-node commands unless the Stage E group path has been configured and
  verified.
