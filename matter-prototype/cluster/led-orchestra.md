# LED Orchestra Matter Cluster

This is the first custom-cluster contract for the ESP-Matter/Thread prototype.
It mirrors the current shared effect model while leaving transport/security to
Matter.

## Identity

- Cluster name: `LedOrchestra`
- Cluster type: vendor custom cluster
- Prototype cluster id: `0xFFF1FC00`
- Endpoint: one LED segment endpoint per physical node for the first prototype
- Effect ids:
  - `0`: off
  - `1`: solid
  - `2`: rainbow

Use the private development VID/PID during prototypes. Do not allocate final
cluster ids or certification metadata until the Matter product direction is
locked.

## Commands

### SetScene

Fields:

| Tag | Type | Field |
| --- | --- | --- |
| `0` | `U8` | `effect_id` |
| `1` | `U8` | `red` |
| `2` | `U8` | `green` |
| `3` | `U8` | `blue` |
| `4` | `U8` | `speed` |
| `5` | `U8` | `brightness` |
| `6` | `U32` | `sequence` |
| `7` | `U64` | `scheduled_start_time_ms` |

Behavior:

- Reject unknown `effect_id` values.
- Store the accepted `sequence` in `last_sequence`. Stale-sequence rejection is
  a Phase 5 refinement.
- Apply immediately when `scheduled_start_time_ms` is `0`.
- Otherwise apply at the synchronized local time matching the scheduled start.

### SetNodeConfig

Fields:

| Tag | Type | Field |
| --- | --- | --- |
| `0` | `U16` | `node_id` |
| `1` | `U16` | `segment_start` |
| `2` | `U16` | `segment_len` |
| `3` | `U16` | `total_leds` |
| `4` | `U8` | `led_gpio` |

Behavior:

- Phase 3 stores config in RAM and reports it through attributes.
- Phase 5 persists config in the node's nonvolatile storage.
- Log the loaded config at boot.
- Use unicast, not group command, for provisioning.

### SyncClock

Fields:

| Tag | Type | Field |
| --- | --- | --- |
| `0` | `U64` | `controller_time_ms` |

Behavior:

- Update the node's controller-time offset.
- Use the offset for synchronized effects and scheduled scenes.

## Prototype Attributes

| Id | Type | Attribute |
| --- | --- | --- |
| `0` | `U8` | `current_scene` |
| `1` | `U16` | `segment_start` |
| `2` | `U16` | `segment_len` |
| `3` | `U16` | `total_leds` |
| `4` | `U8` | `led_gpio` |
| `5` | `STR` | `firmware_version` |
| `6` | `U32` | `last_sequence` |

## Controller Console Helpers

The controller-node prototype registers these USB shell commands:

```text
lo-set-scene <node-id|group-id> <endpoint-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]
lo-set-node-config <node-id> <endpoint-id> <orchestra-node-id> <segment-start> <segment-len> <total-leds> <led-gpio>
lo-sync-clock <node-id|group-id> <endpoint-id> [controller-time-ms]
```

## Group Commands

Use Matter group/multicast for `SetScene` once at least two LED nodes are
commissioned. Keep `SetNodeConfig` unicast-only.
