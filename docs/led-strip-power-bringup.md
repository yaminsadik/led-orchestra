# LED Strip Power And Signal Bring-Up

This is the working hardware note for the 2026-06-15 bench session with one
ESP32-C6 LED node driving two 12V addressable LED strips. Keep architecture and
Matter command references in the main docs; use this file for power, wiring,
signal-integrity, and brightness-threshold findings.

## Current Bench Setup

- LED node: ESP32-C6 on GPIO2.
- Strip type: 12V addressable strip. The renderer code path treats the bench
  strip as WS2815-style output and swaps R/G for the strip's RGB wire order.
- Physical LEDs: two strips, 150 LEDs each, for 300 LEDs total.
- Matter live node config:

```text
lo-set-node-config 3 1 1 0 300 300 2
```

- LED node boot evidence:

```text
lo_renderer: config loaded from NVS: node=1 segment=[0,300) total=300 gpio=2
```

- Firmware capacity note: the LED-node build was temporarily raised to a
  600-LED compiled capacity while debugging the earlier assumption that the
  physical chain was 600 LEDs. The live segment config is now 300, so only the
  first 300 logical pixels are intentionally rendered.

## Commands Used

All tests below used solid blue over the all-nodes group:

```text
lo-set-scene-group 0x0001 1 0000ff 0 <brightness>
```

Useful exact commands:

```text
# Off
lo-set-scene-group 0x0001 0 000000 0 0

# Known stable low/medium blue
lo-set-scene-group 0x0001 1 0000ff 0 80

# Current best high threshold
lo-set-scene-group 0x0001 1 0000ff 0 112

# Problem range
lo-set-scene-group 0x0001 1 0000ff 0 128
lo-set-scene-group 0x0001 1 0000ff 0 255
```

## Observed Brightness Thresholds

- `80/255`: stable after common ground was added. This is the safe baseline.
- `112/255`: looks good and is the current best close-range threshold.
- `128/255`: the end of the second strip becomes very dim or some LEDs appear
  off/different. This suggests the power path is near or past its usable limit.
- `255/255`: not much brighter than the lower settings and flickers. That
  strongly suggests voltage sag, current limiting, or poor power injection rather
  than a Matter command issue.

The useful threshold for the current close-range wiring is therefore around
`112/255` for solid blue. Use `112` as the comparison point when testing against
the old 65 ft run.

Later in the same session, solid green showed similar behavior: `128` started
dimming, then `112` was used as the comparison threshold from the end of strip 1
through strip 2:

```text
lo-set-scene-group 0x0001 1 00ff00 0 112
```

### Single-Strip Isolation Test

After disconnecting strip 2 and testing only one 150-LED strip:

```text
# 20% green
lo-set-scene-group 0x0001 1 00ff00 0 51

# 50% green
lo-set-scene-group 0x0001 1 00ff00 0 128

# 75% green
lo-set-scene-group 0x0001 1 00ff00 0 192

# 88% green
lo-set-scene-group 0x0001 1 00ff00 0 224

# 100% green
lo-set-scene-group 0x0001 1 00ff00 0 255
```

Observed behavior:

- `51/255` (20%) rendered all green on the single strip.
- `128/255` (50%) looked good on the single strip.
- `192/255` (75%) stayed all green, with slight progressive dimming toward the
  end.
- `224/255` (88%) stayed all green, but the end dimmed more than at `192`.
- `255/255` (100%) broke in the middle and became blue downstream.

This narrows one failure mode to power/data integrity within a single 150-LED
strip at full brightness. Since the same strip stays the requested color through
`224` but progressively dims toward the end, the data path is probably still
valid up to that point and voltage drop is the main visible limit. At `255`, the
voltage/timing margin gets bad enough that downstream pixels decode or display
the wrong color. Injecting power at both ends of the single strip and measuring
voltage at the start, midpoint, and end should be the next single-strip test.

### Two-Strip Green Retest With New Adapter

After reconnecting the second 150-LED strip and using the newer power setup,
solid green was stepped upward over the all-nodes group:

```text
lo-set-scene-group 0x0001 1 00ff00 0 51
lo-set-scene-group 0x0001 1 00ff00 0 80
lo-set-scene-group 0x0001 1 00ff00 0 112
lo-set-scene-group 0x0001 1 00ff00 0 128
lo-set-scene-group 0x0001 1 00ff00 0 120
```

Observed behavior:

- `51/255`: all green across both strips.
- `80/255`: all green across both strips.
- `112/255`: all green across both strips.
- `128/255`: mostly green, but the end of strip 2 changed color; visually about
  60% of the two-strip chain looked good before the far-end failure.
- `120/255`: all green across both strips.

This brackets the current two-strip green threshold between `120` and `128`.
Because the failure starts at the far end of strip 2 and improves immediately
when brightness is reduced, this does not look like a Matter endpoint/config
issue. It fits voltage drop, weak ground reference, or data margin degraded by
strip-2 load. Next planned hardware change: inject a separate `12V 3A` supply at
the beginning of strip 2, with grounds tied together and the data path left as
`strip 1 DOUT -> strip 2 DIN`.

### Wiring Retest With Beginning-Only Injection

After changing the wiring so power could only be added at the beginning of strip
1, the same solid green group commands were sent as a brightness ladder:

```text
lo-set-scene-group 0x0001 1 00ff00 0 128
lo-set-scene-group 0x0001 1 00ff00 0 192
lo-set-scene-group 0x0001 1 00ff00 0 255
```

Observed behavior:

- `128/255`: initially looked good after reconnecting, but later repeated
  PDU-only tests showed it is marginal/intermittent; the last few percent of the
  downstream chain can still break color.
- `192/255`: same failure as before; mixed colors start on strip 2.
- `255/255`: remains above the usable power margin for this wiring.
- A third strip was physically present downstream during this retest and behaved
  the same as before. Treat strip-3 observations cautiously until the live Matter
  node config is explicitly updated/read back for `450` LEDs; the last documented
  config still targeted `300` LEDs.

Follow-up PDU-only threshold search:

```text
lo-set-scene-group 0x0001 1 00ff00 0 192
lo-set-scene-group 0x0001 1 00ff00 0 153
lo-set-scene-group 0x0001 1 00ff00 0 140
lo-set-scene-group 0x0001 1 00ff00 0 128
lo-set-scene-group 0x0001 1 00ff00 0 120
```

Observed behavior:

- `192/255`: mixed colors on strip 2.
- `153/255` (about 60%): still mixed.
- `140/255` (about 55%): still mixed.
- `128/255` (about 50%): almost good, but intermittent; the last ~5% can still
  break.
- `120/255`: good.

This makes `120/255` the current practical green threshold for the PDU-only /
beginning-only injection setup. Since every value above `120` tested so far can
reintroduce mixed colors downstream, the limiting factor is still power/ground
or data margin along the strip chain rather than endpoint addressing.

## What Was Fixed

### Static Scene Over-Refresh

Initially, the renderer refreshed all LEDs every 16 ms even for a solid color.
When the data source was unplugged, the strip held the last blue frame and became
stable, which pointed to repeated data refresh exposing marginal data/timing.

The LED-node renderer was changed so static effects (`off` and `solid`) render
one frame and then hold the strip's latched state until the scene/config changes.
Animated effects still refresh normally.

Expected log:

```text
lo_renderer: static scene rendered once; holding latched frame effect=1 seq=1
```

### Common Ground

Flicker improved after adding a common ground. This confirmed that the data line
needed a shared reference between the ESP32, power supply, and strip. Without
common ground, even a correct data command can be decoded as random colors.

### Local Node Ground Reference

A later multi-node bench session narrowed the remaining animation flicker down
further: common ground somewhere in the power system was **not** sufficient by
itself. The decisive fix was a **direct local ground connection** from each
ESP32-C6 node to the same LED strip ground used by that node's data input.

Confirmed lesson:

- `GPIO2 -> strip DIN`
- `C6 GND -> same local strip GND`
- Keep data and that local ground physically close over the run to the strip

Why this matters:

- Solid colors can look fine even when the local data-reference ground is weak.
- Animated effects create frequent data transitions and expose ground bounce or
  poor signal reference much more aggressively.
- A shared ground only through the PDU, buck path, or another strip can still
  be electrically "common" while being a poor local reference for the strip
  input.

Practical bench rule:

- Every node must have its own explicit `C6 GND -> local strip GND` bond.
- Keep the heavier power/common returns, but do not rely on them as the only
  strip data reference path.

## Power Findings

The power adapter currently being tested is 12V, 100W max. That is about:

```text
100W / 12V = 8.3A theoretical max
```

For continuous use, assume less than the printed maximum unless the supply is
known to be robust and well cooled. A practical 80% continuous budget is about:

```text
8.3A * 0.8 = 6.6A
```

The current symptoms fit a supply/wiring limit:

- At lower brightness, the strip is stable.
- At higher brightness, the far end of strip 2 dims or changes color.
- At full brightness, there is little added brightness and flicker appears.

That pattern means the LEDs are asking for more current than the supply/wiring
can deliver at the strip voltage, or the injection path is dropping too much
voltage before the second strip.

### USB-C PD Charger Tests

One bench test used an Anker USB-C charger advertised as 100W with a USB cable.
That rating does not mean the strip automatically received 100W.

USB-C Power Delivery only supplies high power after a PD sink/trigger negotiates
a voltage/current profile. Typical advertised limits look like:

```text
5V  at 3A = 15W
9V  at 3A = 27W
12V at 3A = 36W
15V at 3A = 45W
20V at 5A = 100W maximum
```

Important implications:

- A bare USB-C cable does not make a 12V LED strip receive 12V or 100W.
- 100W usually requires a compatible PD sink, a 5A e-marked cable, and a 20V/5A
  profile.
- If the PD path is only negotiating 12V/3A, the maximum available power is
  roughly 36W before conversion losses.
- A 12V LED load that actually wants 100W needs about 8.3A at 12V. That requires
  either a real 12V supply sized for that current, or a USB-C PD trigger plus a
  DC/DC converter that can deliver about 12V/8A on its output.
- If converting from 20V USB-C PD down to 12V, include converter losses. A 100W
  input path may provide only about 85-95W usable output depending on converter
  efficiency and thermal limits.

So the 100W charger was probably not being used at its full 100W unless the bench
had a PD trigger/converter negotiating 20V/5A and converting it to a high-current
12V rail. The observed `80` stable, `112` good, `128` weak, `255` flicker pattern
is consistent with a much lower effective 12V power budget than the printed
charger maximum.

A later close-range comparison used a different USB-C adapter rated 65W. That
adapter is even less likely to have enough 12V output current for 300 12V
addressable LEDs at high brightness unless a PD trigger/converter is explicitly
negotiating and converting the right profile.

For a 65W USB-C source, rough best-case output after conversion is below the
charger label:

```text
65W input * 0.85..0.95 converter efficiency ~= 55W..62W usable
```

At 12V that is roughly:

```text
55W / 12V ~= 4.6A
62W / 12V ~= 5.2A
```

So the observed behavior is directionally consistent: `112` can look OK, while
`128` and above starts dimming at the far end. The exact threshold depends on
which adapter, cable, PD profile, converter efficiency, wire length, and power
injection point were used for that specific test.

## 65 Ft 16 AWG Run Finding

The PDU bench path is about 65 ft away from the LED load. Treat this as the
worst-case venue run: if a wiring/power approach is stable here, shorter PDU
runs should be easier.

Do not run the 12V LED load over the old 65 ft 16 AWG cable.

16 AWG is roughly 4 ohms per 1000 ft. A 65 ft one-way power run is roughly 130 ft
round trip:

```text
130 ft * 4 ohms / 1000 ft ~= 0.52 ohms
```

Approximate voltage drop:

```text
3A   -> 1.6V drop
6A   -> 3.1V drop
8.3A -> 4.3V drop
```

On a 12V strip, that is enough to make the far end dim, miscolor, flicker, or
reset its pixel decoding. The long run should carry AC to a supply near the
strips, not high-current low-voltage DC.

The current product direction is to host LED-node electronics in/near the PDUs,
but the WS281x-style LED data path should still be treated as a short, fragile
single-ended signal unless proven otherwise. A raw 65 ft GPIO-to-DIN data wire is
not the preferred architecture; if it is tested, treat it as a controlled
experiment with a fallback to a differential line driver or moving the C6 closer
to the strip input.

### 65 Ft Data-Wire Experiment

The long-run data experiment used roughly the same worst-case PDU distance
(about 65 ft, possibly a little more). The C6 stayed at the PDU end, the strip
ground was referenced back through the PDU power ground, and the LED data line
was carried over a 16 AWG wire.

Test commands:

```text
# Solid green at the current safe PDU-only threshold
lo-set-scene-group 0x0001 1 00ff00 0 120

# Animated effects over the same long data run
lo-set-scene-group 0x0001 4 000000 24 120
lo-set-scene-group 0x0001 4 000000 8 120
lo-set-scene-group 0x0001 4 000000 4 80
lo-set-scene-group 0x0001 2 000000 1 80
lo-set-scene-group 0x0001 2 000000 0 80
```

Observed behavior:

- Solid green at `120/255` worked over the 65 ft 16 AWG data run.
- Aurora/rainbow effects flickered or looked visually broken even after reducing
  speed and brightness.
- The firmware currently treats rainbow/aurora as animated even when speed is
  sent as `0`, so those effects continuously refresh the WS281x data stream.
  Solid scenes render once and then hold the strip's latched state.

Conclusion for now: the 65 ft 16 AWG raw data run is acceptable for solid scenes
in this worst-case bench setup, but animated effects expose data-signal margin.
Do not commit to RS-485/differential drivers yet: shorter venue runs may not need
them, and adding receivers/transmitters everywhere would be unnecessary cost and
complexity if only the longest PDU path fails animation. Keep this path on solid
scenes for now and revisit Cat6 twisted-pair or RS-485 only when the hardware
layout decision is being made.

### 9 m Junction-Box Data-Run Experiment

For the junction-box C6 placement candidate, the longest expected
junction-box-to-strip data run is about 9 m (~30 ft), much shorter than the
PDU-to-strip worst case. The test moved the data source to the junction-box
position and used the longest 16 AWG data run from there to the strip.

Test commands:

```text
# Animated rainbow stress test
lo-set-scene-group 0x0001 2 000000 1 80

# Same animation at lower brightness
lo-set-scene-group 0x0001 2 000000 1 50

# After ground/power fixes
lo-set-scene-group 0x0001 2 000000 1 128
lo-set-scene-group 0x0001 2 000000 1 255
lo-set-scene-group 0x0001 2 000000 1 224
```

Observed behavior:

- At `80/255`, strip 1 rendered the rainbow effect well. Strip 2 rendered the
  effect but flickered starting around 40% into the strip.
- At `50/255`, the flicker mostly stopped, leaving only some flicker near the
  far end of strip 2 (around the last ~3%).
- Disconnecting the extra `12V` feed did not reintroduce the original failure by
  itself. Tying strip 2 ground solidly to strip 1 ground stopped most of the
  flicker. This confirmed that the strip-to-strip ground reference at the data
  handoff was a primary issue.
- At `128/255`, after the strip-to-strip ground fix, only some flicker remained
  near the end of strip 2.
- Adding the extra PDU-side power conductors for strip 2 to the strip 1 feed
  (same source/common ground) cleared the remaining strip-2 flicker.
- After both fixes, `255/255` rainbow was sent as a max stress test. It still
  showed slight, barely noticeable flicker near the far end.
- `224/255` rainbow was better than `255`: one rare/random flicker was observed,
  then it ran without obvious flicker. Treat `224` as the current practical high
  test point, not a final production brightness cap; the cap should be decided
  later with more hardware evidence.

Conclusion: the 9 m junction-box-to-strip data run is electrically viable. The
flicker was not caused by the 9 m data path; it was caused by a weak strip-to-strip
ground reference first, then by remaining power/ground margin on strip 2. The
required wiring invariant is `strip 1 DOUT -> strip 2 DIN` plus a solid
`strip 1 GND -> strip 2 GND` reference at the handoff, with the extra PDU-side
power conductors tied into the same source/common-ground system when needed.

## Recommended Wiring

Use one sufficiently sized 12V supply near the strips for the next clean test.

Recommended baseline:

- 12V supply near the LED strips.
- Short, thick power wires from supply to strip injection points.
- Inject `+12V` and `GND` at the start of strip 1.
- Inject `+12V` and `GND` again at the start of strip 2 or the far end.
- Tie ESP32 ground to strip ground at or near the strip 1 input.
- Data path must be:

```text
ESP32 GPIO2 -> strip 1 DIN -> strip 1 DOUT -> strip 2 DIN
```

Check strip arrows carefully. If strip 2 is connected to the wrong end, it may
hold stale colors or decode random data.

If using two separate 12V adapters:

- Connect grounds together.
- Do not directly connect both positive outputs together unless the supplies are
  designed for parallel operation.
- Isolate/cut `+12V` between the two powered sections if each section has its own
  adapter.
- Keep ground and data continuous across the strip boundary.

## Signal Integrity Checklist

If the first strip shows wrong colors, the problem is near the input:

- Verify ESP32 `GND` is tied directly to strip 1 `GND`.
- Verify ESP32 GPIO2 goes to strip 1 `DIN`, not `DOUT`.
- Keep the data wire short and away from high-current power loops.
- Add a 330-470 ohm series resistor near the data source.
- Prefer a 5V level shifter such as `74AHCT125` between ESP32 GPIO2 and strip
  data input, especially for long data leads or noisy power wiring.

If strip 1 is correct but strip 2 is wrong:

- Verify strip 1 `DOUT` goes to strip 2 `DIN`.
- Re-solder or bypass the data joint between strips.
- Verify strip 2 has a strong nearby ground reference.
- Temporarily drive strip 2 directly from ESP32 GPIO2 to prove whether strip 2
  itself is good.

If a single strip changes color halfway only at high brightness:

- Measure voltage at the first wrong-color pixel while the high-brightness scene
  is on.
- Inject power at both ends of that single strip.
- Inspect the first wrong-color pixel and the previous pixel for a cracked trace,
  weak solder joint, or damaged LED driver.
- If voltage is healthy but the color transition always starts at the same
  pixel, suspect a damaged pixel or data trace at that location.

## Next Test Plan

1. Keep the close-range power setup; do not use the 65 ft 12V run.
2. Measure voltage at the power supply, strip 1 input, strip 2 input, and far end
   while running `80`, `112`, `120`, `128`, `192`, and `255`.
3. Treat `120` as the current known-good PDU-only / beginning-only injection
   threshold; treat `128` as marginal/intermittent, not safe.
4. If voltage at strip 2 or the far end drops significantly at `128` or above, improve
   injection wiring or use a larger supply.
5. If voltage remains near 12V but colors are still wrong, focus on the data
   chain and level shifting.

## Next Session: Power Board Test

Tomorrow's test should replace the USB-C adapter/cable experiment with the actual
12V power board or a proper high-current 12V supply near the LEDs.

Start with one strip only:

```text
lo-set-scene-group 0x0001 1 00ff00 0 128
lo-set-scene-group 0x0001 1 00ff00 0 192
lo-set-scene-group 0x0001 1 00ff00 0 224
lo-set-scene-group 0x0001 1 00ff00 0 255
```

Record for each brightness:

- Voltage at supply output.
- Voltage at strip input.
- Voltage at strip midpoint.
- Voltage at strip far end.
- Whether the whole strip stays green.
- Whether dimming is visible from start to end.

Then repeat with power injected at both ends of the single strip. If `255` stays
green and the far-end voltage is close to the input voltage, the power board and
injection path are good for one strip.

Only after one strip passes, reconnect strip 2:

```text
lo-set-scene-group 0x0001 1 00ff00 0 51
lo-set-scene-group 0x0001 1 00ff00 0 112
lo-set-scene-group 0x0001 1 00ff00 0 128
```

For the two-strip test, measure voltage at strip 1 input, strip 1 output/strip 2
input, and strip 2 far end. If strip 1 passes alone but strip 2 changes color,
focus on strip 1 `DOUT` -> strip 2 `DIN`, strip 2 input grounding, and strip 2
power injection.

## Open Questions

- What is the exact strip model and watts-per-meter rating?
- Is the 100W supply actually maintaining 12V at the strip under load?
- Are `+12V` rails isolated correctly if multiple adapters are used?
- What voltage is present at the far end of strip 2 at brightness `112`, `128`,
  and `255`?
- Does strip 2 render correctly when driven directly from the ESP32 data line?
