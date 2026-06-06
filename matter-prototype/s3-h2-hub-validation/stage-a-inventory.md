# Stage A — Hardware Inventory + Toolchain Compatibility

**Goal:** prove we know the board and the build environment *before* flashing or
refactoring. Host-only except the bench-inventory section.

**Gate:** a written hardware/toolchain matrix exists (in
[`README.md`](README.md)); the board flashing path is understood; the relevant
examples build on the pinned toolchain (ESP-IDF v5.4.1 + esp-matter v1.4.2). **Do
not upgrade ESP-IDF or esp-matter**; a target-toolchain *install* for the same
pinned IDF is allowed only with operator approval (see Finding F-A1).

---

## A.1 Toolchain confirmation (host)

```bash
. "$HOME/esp/esp-idf/export.sh"
. "$HOME/esp/esp-matter/export.sh"
idf.py --version                       # expect: ESP-IDF v5.4.1
ls -1 "$HOME/.espressif/tools"         # which target toolchains are installed
```

Result (2026-06-06):

- `idf.py --version` → **ESP-IDF v5.4.1** ✅
- esp-matter present at `~/esp/esp-matter` (release/v1.4.2) ✅
- Installed toolchains (before F-A1 fix): `riscv32-esp-elf`,
  `riscv32-esp-elf-gdb`, `cmake`, `ninja`, `openocd-esp32`, `esp-rom-elfs` —
  **RISC-V only**, no `xtensa-*`. After the F-A1 fix (below): `xtensa-esp-elf`
  14.2.0 is also installed.

### Finding F-A1 — S3 (Xtensa) toolchain not installed (RESOLVED 2026-06-06)

The repo had been all-RISC-V (C6/H2), so only `riscv32-esp-elf` was installed, and
any `esp32s3` build failed at CMake configure:

```text
The CMAKE_ASM_COMPILER:  xtensa-esp32s3-elf-gcc
is not a full path and was not found in the PATH.
HINT: Try to reinstall the toolchain for the chip that you trying to use.
```

**Resolution (NOT an ESP-IDF version upgrade — stayed v5.4.1):** with operator
approval, installed the S3 target compiler:

```bash
"$IDF_PATH/install.sh" esp32s3        # downloads Xtensa GCC into ~/.espressif
. "$HOME/esp/esp-idf/export.sh"        # re-export to add xtensa-esp-elf to PATH
```

Verified 2026-06-06: `which xtensa-esp32s3-elf-gcc` resolves under
`~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/...` (gcc 14.2.0) and
`idf.py --version` is **still ESP-IDF v5.4.1**. Both S3 example builds then
succeeded (A.2).

### Finding F-A2 — the BR bundles the RCP image from `CONFIG_RCP_SRC_DIR`

The `thread_border_router` builds with `CONFIG_AUTO_UPDATE_RCP=y`; its
`esp_rcp_update` component generates the `rcp_fw` SPIFFS image from
`CONFIG_RCP_SRC_DIR`, whose Kconfig default is the **in-tree**
`$IDF_PATH/examples/openthread/ot_rcp/build`. Our RCP is built **out-of-tree** at
`build/rcp-h2`, so the first `build-br` failed with `FileNotFoundError:
.../ot_rcp/build/rcp_version`. Fixed in `build-s3-hub.sh`: `build_br` now builds
the RCP first and overrides `CONFIG_RCP_SRC_DIR` to `build/rcp-h2` via a generated
(gitignored) `build/rcp-src-dir.defaults` fragment. The Stage C `controller`+OTBR
build leaves `AUTO_UPDATE_RCP` off (no RCP bundled) — provision the H2 separately on
hardware (see [`stage-c-onehub.md`](stage-c-onehub.md)).

## A.2 Host build verification

Builds are out-of-tree under `build/` and layer this package's overlays onto the
**stock** examples (the board's pins already match the stock examples, so the SDK
is not patched).

```bash
./build-s3-hub.sh build-rcp     # ot_rcp                -> esp32h2  (H2 radio)
./build-s3-hub.sh build-br      # thread_border_router  -> esp32s3  (Stage B BR)
./build-s3-hub.sh build-hub     # controller + OTBR     -> esp32s3  (Stage C hub)
# or all three:
./build-s3-hub.sh build
```

Result (2026-06-06, after the F-A1 + F-A2 fixes):

| Build | Target | Result |
| --- | --- | --- |
| `ot_rcp` | esp32h2 | **OK** — `esp_ot_rcp.bin` 0x31a00 (~203 KB); 81% free in the 1 MB app partition |
| `thread_border_router` + `s3-br-host` | esp32s3 | **OK** — `thread_border_router.bin` 0x1e23d0 (~1.88 MB); **19% free** in the 2368K app partition; `rcp_fw` bundled |
| `controller` + OTBR + `s3-otbr-controller` | esp32s3 | **OK** — `controller.bin` 0x23bb10 (~2.29 MB); **24% free** in the 3072K app partition |

All three build on the pinned toolchain (ESP-IDF v5.4.1 / esp-matter v1.4.2). The
S3 app-partition headroom (BR 19%, hub 24%) is at/under the 25% flash-headroom gate
target — a watch item to re-measure on the board (the 8 MB part has space outside
the app partition; the OTA-slotted app partition is the tight dimension). This is a
host-build pass only — runtime heap/flash/discovery evidence comes from Stages B–F.

## A.3 Bench hardware inventory (operator)

Run these at the bench and paste results into the **Evidence** block below. Replace
`<S3_PORT>` / `<H2_PORT>` with the actual `/dev/cu.*`.

1. **Identify the USB port(s).** With the board unplugged then plugged:

```bash
ls /dev/cu.*                 # note which node appears for the board
```

   - Which port is the **S3** (Matter/BR host, USB-Serial/JTAG console)?
   - Does the board expose a **direct-to-H2** USB/UART path, or is the H2 updated
     **only** host-side by the S3 (the `AUTO_UPDATE_RCP` default)?

2. **S3 chip / flash identity:**

```bash
esptool.py -p <S3_PORT> chip_id
esptool.py -p <S3_PORT> flash_id    # confirm 8 MB (vs. an early 4 MB sample)
```

3. **Boot log / module identity** (Ctrl-] to exit the monitor):

```bash
./build-s3-hub.sh monitor hub <S3_PORT>   # or `br`; or: idf.py -p <S3_PORT> monitor
```

   Capture: chip revision, flash size, PSRAM detected (`Found 2MB PSRAM` / quad),
   and any RCP-update log lines on first boot.

4. **Partition table** (after a Stage B/C flash):

```bash
esptool.py -p <S3_PORT> read_flash 0x8000 0xc00 /tmp/ptable.bin
python "$IDF_PATH/components/partition_table/parttool.py" \
  --partition-table-file /tmp/ptable.bin get_partition_info --partition-name factory
```

   Confirm the `partitions_br.csv` layout (factory app + `rcp_fw` SPIFFS) and the
   resulting **flash headroom** on the 8 MB part.

5. **H2 RCP path:** if a direct-to-H2 port exists, `esptool.py -p <H2_PORT>
   chip_id` should report ESP32-H2. Otherwise rely on host-side RCP update and
   verify it from the BR/hub boot log (and `rcp version` in Stage B/C).

## Evidence (fill at the bench)

```text
Date / operator:
S3 port: /dev/cu.________   H2 direct port (if any): /dev/cu.________ / none
S3 chip_id / rev:
flash_id (size):            PSRAM detected:
RCP flashing path:          host-side AUTO_UPDATE_RCP  |  direct H2 USB
Partition table (factory size, rcp_fw size, free):
First-boot RCP-update log (paste a few lines):
F-A1 install.sh esp32s3 run? (y/n, by whom):
S3 build sizes after F-A1 (br / hub, flash % free):
Notes / button-jumper actions:
```

## Gate result

- [x] Hardware/toolchain matrix written ([`README.md`](README.md)).
- [x] H2 example builds on the pinned toolchain.
- [x] S3 examples build on the pinned toolchain (F-A1 resolved; BR + hub OK).
- [ ] Flashing path + board identity confirmed at the bench (operator).

Stage A is **met on the host side**: the pinned toolchain builds all three
artifacts (H2 RCP, S3 BR, S3 hub) after the F-A1 toolchain install and the F-A2
RCP-path fix. The remaining item is the **bench inventory** (USB-port→chip mapping,
flash/PSRAM identity, partition table, RCP flashing path), which needs the
hardware.
