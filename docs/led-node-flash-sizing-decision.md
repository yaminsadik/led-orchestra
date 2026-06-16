# LED Node Flash Sizing — N8 (8 MB) vs N4 (4 MB) Decision

**Date:** 2026-06-15 · **Status:** **Decided + implemented + hardware-verified** —
deployment standardizes on **N8 (8 MB)**; 8 MB partition layout landed and confirmed
on-chip (see [§5](#5-implementation--done)).
**Tested on:** 1× ESP32-C6-WROOM-1-**N8** devkit (`/dev/cu.usbmodem1101`), rev v0.2,
8 MB flash confirmed by `esptool flash_id`. 3 N8 units on hand; rest pending this call.

---

## TL;DR / Recommendation

**Buy the rest as N8 (8 MB). Do not standardize the deployment on 4 MB (N4).**

The LED-node firmware already fills **98 % of a 4 MB OTA app slot** (36 KB free).
ESP-IDF itself prints `Warning: The smallest app partition is nearly full (2 %
free space left)`. On 4 MB there is effectively **zero headroom for OTA growth** —
a single new feature on the roadmap (offline OTA, scheduled scenes, multi-output
config, more effects) or a toolchain bump overflows the slot and **breaks the
ability to field-update an installed show**. The N8 premium is ~$1–2/node
(~$15–40 across the build); the downside of N8 is essentially zero (same RISC-V
core, same 512 KB SRAM, same pinout). N8 lets us re-lay the partition table for
~3 MB/slot and restore large OTA headroom.

**The N8 passes the test it was bought for** — but only realizes its value after
the one-line config change in [§5](#5-action-required-to-actually-use-n8).

---

## 1. What was tested

- Built current `led-node` source on the pinned toolchain (ESP-IDF v5.4.1 +
  esp-matter v1.4.2), target `esp32c6`.
- Flashed to the connected N8 board (`idf.py flash`, hash verified, exit 0).
- Reset and captured the boot log.

**Result — board is fully usable:**

| Check | Observed |
|---|---|
| Boot | clean; app `led_orchestra_matter_led_node` v0.1.0, compile Jun 15 2026 |
| Renderer | `lo_renderer: renderer started: gpio=2 leds=600`, effect=2 |
| Matter | custom cluster `0xFFF1FC00` on endpoint 1; CASE server listening |
| Thread | `OpenThread started: OK`, device type ROUTER |
| Commissioning | CHIPoBLE advertising, **commissioning window opened** |
| RAM health | **215 KB free heap** after full Matter+Thread+BLE init (min 210 KB) |

> The DNS-SD `Failed to advertise commissionable node: 3` lines are expected for a
> node booted standalone with no Thread network/BR attached; not a board fault.

## 2. The core finding — 4 MB is full

Current partition table ([`partitions.csv`](../matter-prototype/led-node/partitions.csv))
is the standard Matter dual-OTA layout:

| Region | Size | Notes |
|---|---|---|
| `ota_0` (app A) | 0x1E0000 = **1.875 MB** | |
| `ota_1` (app B) | 0x1E0000 = **1.875 MB** | required for A/B OTA |
| nvs / nvs_keys / otadata / phy / fctry / secure_cert | ~0.18 MB | |
| **Total** | **~4.0 MB** | layout maxes out a 4 MB part |

The app binary measured **today**:

```
binary size 0x1d7110 (1,929,488 B). Smallest app partition 0x1e0000 (1,966,080 B).
0x8ef0 (36,592 B = 2%) free.
Warning: The smallest app partition is nearly full (2% free space left)!
```

There is **no room to enlarge the OTA slots** on 4 MB: the layout already ends at
0x3E6000, leaving only ~104 KB unallocated — and because OTA is A/B, every 1 KB of
app growth costs 2 KB of layout. Practical ceiling on 4 MB ≈ 1.9 MB/slot. We are
there now.

## 3. Why the image is irreducibly large

It is a full Matter-over-Thread node. Top flash contributors (`idf.py size-components`):

| Component | Flash | What |
|---|---:|---|
| `esp_matter` (CHIP) | 653 KB | data model, clusters, commissioning |
| `openthread` | 242 KB | Thread stack |
| `ble_app` + `bt` | 287 KB | BLE for commissioning |
| `esp_app_format` | 169 KB | app/descriptor + embedded cert data |
| `lwip` | 104 KB | IPv6 |
| `mbedcrypto` + `mbedx509` | 102 KB | Matter crypto/PKI |

Matter + Thread + BLE-commissioning is ~1.6 MB before any app code. You **cannot
ship this product meaningfully under ~1.6 MB**, so the 1.875 MB slot was always
going to be tight — and the roadmap only adds code.

## 4. What N8 (8 MB) buys

- Re-lay partitions to **~3 MB per OTA slot** (≈ 1 MB headroom each) plus larger
  `nvs`/`fctry` for richer per-node config (the mini-golf multi-output extension).
- **RAM is unchanged** — N4 and N8 differ *only* in flash. The 215 KB free heap
  measured above is what you get on both. N8 is not a RAM upgrade.
- Future-proofs offline OTA across an installed, hard-to-reach 18-node show.

## 5. Implementation — done

The 8 MB layout is landed and verified on hardware (2026-06-15). A 4 MB-configured
image ignores the extra flash (`W spi_flash: Detected size(8192k) larger than the
size in the binary image header(4096k)`), so the realized change was:

1. ✅ `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y` in
   [`sdkconfig.defaults`](../matter-prototype/led-node/sdkconfig.defaults)
   (4 MB / N4 fallback documented inline).
2. ✅ Added [`partitions-8mb.csv`](../matter-prototype/led-node/partitions-8mb.csv)
   — 3 MB OTA slots, front matter byte-identical to the 4 MB table, ~1.9 MB tail
   reserved; `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` points at it. The 4 MB
   [`partitions.csv`](../matter-prototype/led-node/partitions.csv) is retained for N4.
3. ✅ Rebuilt + flashed the N8 unit.

**Verification (this board):**
- Build `check_sizes`: app `0x1d7100` into `0x300000` slot → **39 % free (~1.21 MB
  headroom/slot)**, vs ~2 % on 4 MB.
- On-chip partition table read back via `esptool read_flash 0x8000`:
  `ota_0`/`ota_1` = **3 M** each, `fctry` @ `0x620000`.
- Boot log: `Loaded app from partition at offset 0x20000`, renderer + Matter
  `0xFFF1FC00` + OpenThread + commissioning all up, **223 KB free heap**, and the
  4 MB size-mismatch warning is **gone**.

> Note: the new table moves `fctry` (0x3E0000 → 0x620000); `nvs` is unchanged at
> 0x10000. Re-commission/re-provision after switching layouts on an existing unit.

## 6. Cost

| | N4 (4 MB) | N8 (8 MB) |
|---|---|---|
| Bare WROOM-1 module | ~$2.95 | ~$1–2 more |
| Devkit | ~$8–12 | ~$8–15 |

For the ~15 remaining nodes the N8 premium is **~$15–40 total** — negligible
versus a truck-roll to USB-reflash 18 installed nodes when a 4 MB OTA won't fit.

## 7. Alternative considered — stay on 4 MB and slim the image

Possible (drop unused clusters, trim embedded cert data, free BLE post-commission
— RAM only, not flash). Realistic recovery ~200–400 KB, but it is **fragile,
recurring engineering** and leaves a permanently tight margin that the next
esp-matter/IDF bump erodes. Not worth it to save ~$1–2/node. **Rejected.**

## 8. Decision

**Standardize the deployment on ESP32-C6-WROOM-1-N8 (8 MB).** Buy the remaining
nodes as N8. Land the 8 MB partition layout (§5) before the bench scale gate so
soak/OTA testing happens on the shipping flash geometry.

---
*Sources: Adafruit ESP32-C6-WROOM-1-N4/N8 listings; DigiKey 17728866. On-hardware
measurements captured 2026-06-15 from the connected N8 unit.*
