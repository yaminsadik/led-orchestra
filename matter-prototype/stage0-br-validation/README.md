# Stage 0 — Border-Router Discovery Validation (Runbook)

This is a historical but still useful fallback runbook. It records the all-C6
border-router discovery validation that proved the important architectural fact:
a **separate Thread client can resolve an LED node's `_matter._tcp` record
through a real OpenThread Border Router**.

The selected product topology is now **S3+H2 board as BR-only + separate
ESP32-C6 controller + ESP32-C6 LED nodes**. Use this directory when you need the
older all-C6 fallback evidence or a known-good diagnostic path for border-router
discovery. For the active S3+H2 BR-only path, start with
[`../s3-h2-hub-validation/README.md`](../s3-h2-hub-validation/README.md).

This runbook was the executable **Phase 4, Stage 0** go/no-go for the original
controller/border-router topology ladder.

Read alongside:
[`../../docs/controller-topology-validation.md`](../../docs/controller-topology-validation.md)
(the experiment), [`../../docs/controller-topology-adr.md`](../../docs/controller-topology-adr.md)
(the decision), and [`../../docs/debugging-journal.md`](../../docs/debugging-journal.md)
(the 2026-06-02 colocated-discovery failure this retires).

```text
Stage 0 PASS  -> the C6 BR (host+RCP) owns SRP/DNS-SD and a separate client
                 resolves the LED's _matter._tcp through it. Continue to Stage 1.
Stage 0 FAIL  -> separate client cannot resolve through the BR (Error 28:
                 ResponseTimeout). Run the Wi-Fi-backbone diagnostic (§9) to
                 localize, then per the decision rules jump to Option 4 (Pi /
                 ot-br-posix) if the C6 BR path itself is the problem.
```

We run the **backbone-less / fully-offline BR first** (the pure product shape),
and only fall back to a **Wi-Fi-backbone BR** as a diagnostic if the offline run
fails. LED control stays on Matter/Thread in both runs.

---

## 1. Roles and boards

Four ESP32-C6 boards. Only **two** need a Mac serial console at runtime — the
**BR-host** and the **client** — which fits a 2-port MacBook. The **RCP** and
the **LED node** only need power once flashed.

| # | Role | Firmware | Serial at runtime? | Power at runtime |
| - | ---- | -------- | ------------------ | ---------------- |
| 1 | **BR-host** | `ot_br` side-pin build (`rebuild-sidepins.sh`) | **Yes** (USB -> Mac port A) | Mac USB |
| 2 | **RCP** | `ot_rcp` + `sdkconfig.rcp-sidepins.defaults` | No | USB hub / charger (common GND with BR-host) |
| 3 | **Client / controller stand-in** | `controller-node` + `sdkconfig.client.defaults` | **Yes** (USB -> Mac port B) | Mac USB |
| 4 | **LED node** | existing `led-node` (unchanged) | No | USB hub / charger |

The **client** is the controller stand-in from the validation doc: a Matter
commissioner that joins the BR's mesh over its **own native 802.15.4 radio** and
is **not** a border router itself (its on-board SRP server is disabled). This is
exactly the Option 3 controller role, used in Stage 0 to isolate the BR's
discovery from co-location.

---

## 2. Wiring (RCP ↔ BR-host spinel link)

The BR-host drives the RCP over a hardware UART (spinel, 460800 baud). This is
the only inter-board wiring. **Crossover** TX/RX, plus a **common ground**.

For Seeed XIAO ESP32C6 boards, use the accessible side pins. The ESP-IDF
defaults (`GPIO4`/`GPIO5`) map to tiny back pads (`MTMS`/`MTDI`) on this board,
so the 2026-06-04 hardware run used `rebuild-sidepins.sh` to move the BR/RCP
spinel link to `D6`/`D7`:

```text
   BR-host C6 (ot_br)                 RCP C6 (ot_rcp)
   ┌──────────────────┐               ┌──────────────────┐
   │ D6 / GPIO16 (TX)  ├──────────────►│ D7 / GPIO17 (RX)  │
   │ D7 / GPIO17 (RX)  │◄──────────────┤ D6 / GPIO16 (TX)  │
   │ GND               ├───────────────┤ GND               │
   │ native USB → Mac  │               │ native USB → hub  │
   └──────────────────┘               └──────────────────┘
```

- **Common ground is mandatory.** If the RCP is powered from a different
  USB supply than the BR-host, the GND jumper above is what ties them together.
- Flash and console for **both** boards use each board's **native USB
  (USB-Serial/JTAG) port**, *not* the side-pin UART used for the spinel link.
- Pins are fixed by the side-pin build:
  - BR-host radio UART: RX=`GPIO17`, TX=`GPIO16`.
  - RCP host UART: RX=`GPIO17`, TX=`GPIO16`.

---

## 3. Shell setup (run once per terminal)

```bash
. "$HOME/esp/esp-idf/export.sh"
. "$HOME/esp/esp-matter/export.sh"     # only needed for the client (controller) build
export IDF_CCACHE_ENABLE=1

export REPO="$HOME/developer/led-orchestra"
export S0="$REPO/matter-prototype/stage0-br-validation"
```

Find a board's port after plugging it in:

```bash
ls /dev/cu.usbmodem*
```

> 2-port workflow: you can build all four images up front, then **flash one
> board at a time** (each needs USB only briefly). At runtime keep the BR-host
> on port A and the client on port B; power the RCP and LED node from a hub.

---

## 4. Build and flash — one board at a time

All build state is written under `$S0/build/<role>/` (gitignored). For the
side-pin BR/RCP build, use the helper below: it copies `ot_br` into
`$S0/build/ot_br_xiao_sidepins_src`, patches only that copy to RX=`GPIO17` /
TX=`GPIO16`, and leaves the ESP-IDF checkout untouched.

### 4.1 RCP (board 2)

```bash
matter-prototype/stage0-br-validation/rebuild-sidepins.sh build-rcp
# plug in the RCP's native USB port, then:
matter-prototype/stage0-br-validation/rebuild-sidepins.sh flash-rcp <RCP_PORT>
```

Unplug the RCP from the Mac and move it to hub power once flashed.

### 4.2 BR-host (board 1)

```bash
matter-prototype/stage0-br-validation/rebuild-sidepins.sh build-br
matter-prototype/stage0-br-validation/rebuild-sidepins.sh flash-br <BR_PORT>
```

> `ot_br` pulls `espressif/esp_ot_cli_extension` and `espressif/mdns` from the
> component registry, so the **first** build needs network access. `ot_rcp` and
> the client build offline.

### 4.3 Client / controller stand-in (board 3)

The controller-node `CMakeLists.txt` does a plain `set(SDKCONFIG_DEFAULTS …)`,
which shadows `-D SDKCONFIG_DEFAULTS`. Layer the Stage 0 client overlay through
the `EXTRA_SDKCONFIG_DEFAULTS` env var hook it exposes instead:

```bash
EXTRA_SDKCONFIG_DEFAULTS="$S0/sdkconfig.client.defaults" \
idf.py -C "$REPO/matter-prototype/controller-node" -B "$S0/build/client" \
  -D SDKCONFIG="$S0/build/client/sdkconfig" \
  set-target esp32c6 build
idf.py -C "$REPO/matter-prototype/controller-node" -B "$S0/build/client" -p <CLIENT_PORT> flash
```

(`sdkconfig.defaults.local` holds your operator-AP credentials and must exist —
see the controller-node README. Confirm the override took: `grep
OPENTHREAD_BORDER_ROUTER "$S0/build/client/sdkconfig"` should read `=n`.)

### 4.4 LED node (board 4, unchanged firmware)

```bash
cd "$REPO/matter-prototype/led-node"
idf.py set-target esp32c6 build
idf.py -p <LED_PORT> flash
```

After flashing, unplug the RCP and LED node from the Mac and power them from the
hub. Keep **BR-host on port A** and **client on port B**.

---

## 5. Power-on and verify the spinel link (user item 5)

1. Power the **RCP** (hub) and the **BR-host** (Mac port A), wired per §2.
2. Open the BR-host console:

   ```bash
   idf.py -C "$S0/build/ot_br_xiao_sidepins_src" -B "$S0/build/br-host-sidepins" -p <BR_PORT> monitor
   ```

3. At the BR-host `ot_cli` prompt, confirm the host can talk to the RCP:

   ```text
   > rcp version
   OPENTHREAD/<...>; esp32c6; <build date>
   Done
   ```

**Healthy:** `rcp version` returns a version string within ~1 s.
**Unhealthy (wiring/baud):** boot log shows `Timeout waiting for spinel
response` / `Failed to communicate with RCP` / continuous RCP resets — re-check
the side-pin **crossover** (`BR D6 -> RCP D7`, `BR D7 -> RCP D6`), the
**common GND**, and that neither board is being flashed/consoled over the
side-pin UART.

---

## 6. Bring up Thread + SRP/DNS-SD on the BR-host (offline)

All commands at the **BR-host** `ot_cli` prompt (raw OpenThread CLI — no
`matter esp` prefix):

```text
> dataset init new
Done
> dataset commit active
Done
> ifconfig up
Done
> thread start
Done
```

Wait a few seconds, then confirm the BR is the leader and the SRP server is up:

```text
> state
leader
> srp server enable
Done
> srp server state
running
```

Capture the active dataset — you will paste it into the client and the pairing
command:

```text
> dataset active -x
0e08000011111111222233...<~200 hex chars on ONE line>...f8
Done
```

Note the BR's mesh-local addresses (used for the optional `dns config` fallback
in §8):

```text
> ipaddr
fd<...>:...:fc00     <- anycast locator (ALOC)
fd<...>:...:ff:fe00:<rloc16>
fd<...>:...            <- ML-EID
fe80:...
Done
```

**Healthy:** `state` reaches `leader`; `srp server state` is `running`.

---

## 7. (Optional, recommended) Fast BR DNS-SD triage from the client

Before the heavier commissioning flow, confirm in ~1 minute that the BR's
DNS-SD server answers a **separate node** at all. The client registers a
throwaway `_matter._tcp` instance via SRP to the BR, then browses for it. The
SRP store and DNS-SD responder are on the **BR** (separate from the client), so
this directly exercises the gate path without depending on Matter commissioning.

First join the client to the BR's mesh (also needed for §8). On the **client**
(`controller-node`, commands prefixed `matter esp ot_cli`):

```text
> matter esp factoryreset          # clear any stale dataset/fabric, board reboots
...
> matter esp ot_cli dataset set active <BR-dataset-hex-from-§6>
Done
> matter esp ot_cli ifconfig up
Done
> matter esp ot_cli thread start
Done
> matter esp ot_cli state
child            (then `router` within ~30 s)
```

Register + browse the throwaway record:

```text
> matter esp ot_cli srp client autostart enable
Done
> matter esp ot_cli srp client host name stage0probe
Done
> matter esp ot_cli srp client service add probe _matter._tcp 5540
Done
> matter esp ot_cli dns browse _matter._tcp.default.service.arpa
DNS browse response for _matter._tcp.default.service.arpa.
probe
    Port:5540, Priority:0, Weight:0, TTL:...
    Host:stage0probe.default.service.arpa.
    HostAddress:fd<...> TTL:...
Done
```

- **Triage PASS:** the `probe` record returns → the BR's DNS-SD answers a
  separate client. Proceed to §8 for the real LED-node proof.
- **Triage FAIL:** `Error 28: ResponseTimeout` → the offline C6 BR DNS-SD path
  is not answering. Skip to the **Wi-Fi-backbone diagnostic (§9)**; do not bother
  with commissioning yet.

Clean up the throwaway record before §8 so only the real LED record is present:

```text
> matter esp ot_cli srp client service remove probe _matter._tcp
Done
```

---

## 8. Real LED node publishes `_matter._tcp`; client resolves it through the BR

This is the literal Stage 0 proof: the **real LED node** publishes its
operational record, and the **separate client** resolves it **through the BR**.

1. **Prepare the LED node.** Power it (hub). If it was previously commissioned,
   factory-reset it first (LED node console, one-time, then move to hub power):

   ```text
   > matter esp factoryreset
   ```

   On boot it logs `lo_led_node: commissioning window opened`.

2. **Commission from the client**, handing it the **BR's** dataset so the LED
   joins the **BR's** mesh (not a controller-owned one). Use the LED node's setup
   passcode/discriminator (ESP-Matter test defaults `20202021` / `3840` unless
   your LED build overrides them — the same values you used in Phase 3
   bring-up). Node id `2` is an example:

   ```text
   > matter esp controller pairing ble-thread 2 <BR-dataset-hex-from-§6> 20202021 3840
   ```

3. **Watch the client log.** The decisive contrast with 2026-06-02 is that
   operational discovery now resolves **through the BR**:

   ```text
   chip[CTL]: Commissioning stage ... kFindOperationalForStayActive
   chip[DIS]: ... operational discovery ...   (resolves; no 60 s retry loop)
   chip[CTL]: Commissioning completed successfully
   lo_controller: commissioning complete
   ```

   FAIL signature (the original bug, if the BR still isn't answering):

   ```text
   Error on commissioning step 'kFindOperationalForStayActive': 'Error CHIP:0x00000032'
   ```

4. **Browse for the real LED record from the client** — the exact gate command:

   ```text
   > matter esp ot_cli dns browse _matter._tcp.default.service.arpa
   DNS browse response for _matter._tcp.default.service.arpa.
   <FABRICID>-0000000000000002
       Port:5540, Priority:0, Weight:0, TTL:...
       Host:<host>.default.service.arpa.
       HostAddress:fd<...> TTL:...
       TXT:[...] TTL:...
   Done
   ```

   If commissioning stalled instead of completing, run this `dns browse`
   **during** the ~60 s discovery-retry window (before the LED's fail-safe
   expires and rolls back the record) to capture whether the BR returns the
   record or `Error 28`.

   Optional, if the browse times out but you suspect server auto-discovery
   rather than the responder: point the client's DNS client straight at the BR
   and retry —

   ```text
   > matter esp ot_cli dns config <BR-ML-EID-from-§6>
   > matter esp ot_cli dns browse _matter._tcp.default.service.arpa
   ```

---

## Stage 0 PASS / FAIL

**PASS — all of:**
- BR-host reaches `state = leader` with `srp server state = running` (§6).
- `rcp version` succeeds (host↔RCP spinel link healthy, §5).
- The **separate client** `dns browse _matter._tcp.default.service.arpa` returns
  the LED node's record (§8.4) — **not** `Error 28: ResponseTimeout`.
- Corroborating: client-side commissioning reaches `commissioning complete`
  (operational resolution through the BR succeeded — the step that timed out on
  2026-06-02).

**FAIL — any of:**
- The separate client's `dns browse` returns `Error 28: ResponseTimeout`.
- Commissioning stalls at `kFindOperationalForStayActive` with `Error
  CHIP:0x00000032` and the browse does not return the record during the window.

> **`error 32` (`0x32` = `CHIP_ERROR_TIMEOUT`) is the *operational* discovery/CASE
> timeout, and on this single-C6 commissioner it was radio contention, not the BR
> DNS-SD path.** The client build runs the operator Wi-Fi softAP **and** BLE
> **and** native 802.15.4 on one 2.4 GHz radio. Disable the operator AP on the
> commissioner (`sdkconfig.client.defaults` →
> `CONFIG_LED_ORCHESTRA_OPERATOR_WIFI_MODE_DISABLED=y`) before judging `error 32`
> a Stage-0 FAIL; confirmed fix on 2026-06-04 (see the journal). Stage 1's
> co-located Hub commissioner offloads 802.15.4 to the RCP and avoids this by
> construction.

On FAIL, run §9 to localize, then apply the decision rules: if even the
Wi-Fi-backbone BR cannot make a separate client resolve, the C6 esp-thread-br
DNS-SD path is not doing the job → **Option 4 (Pi / `ot-br-posix`)**.

---

## 9. Wi-Fi-backbone diagnostic fallback (only if §7/§8 FAIL)

This tells us whether the missing piece is the **border-router init**
(advertising proxy / OMR / border routing) that the offline manual path skips —
versus the C6 BR/DNS-SD path itself being broken. The Wi-Fi is the BR-host's
backbone only and carries **no LED control**; a local AP with no internet is
fine.

1. Put real credentials in a **gitignored** local overlay (never commit them):

   ```bash
   cp "$S0/sdkconfig.br-host-wifi.defaults" "$S0/sdkconfig.br-host-wifi.local.defaults"
   # edit *.local.defaults: set CONFIG_EXAMPLE_WIFI_SSID / _PASSWORD to a local AP
   ```

2. Rebuild + reflash the BR-host with the Wi-Fi overlay appended:

   ```bash
   idf.py -C "$IDF_PATH/examples/openthread/ot_br" -B "$S0/build/br-host-wifi" \
     -D SDKCONFIG="$S0/build/br-host-wifi/sdkconfig" \
     -D SDKCONFIG_DEFAULTS="$IDF_PATH/examples/openthread/ot_br/sdkconfig.defaults;$S0/sdkconfig.br-host.defaults;$S0/sdkconfig.br-host-wifi.local.defaults" \
     set-target esp32c6 build
   idf.py -C "$IDF_PATH/examples/openthread/ot_br" -B "$S0/build/br-host-wifi" -p <BR_PORT> flash monitor
   ```

3. With auto-start, the BR joins Wi-Fi, **auto-forms** Thread, and runs
   `esp_openthread_border_router_init()`. Capture the new dataset
   (`dataset active -x`) and **repeat §7/§8** with it.

Interpretation:
- **§7/§8 now PASS with Wi-Fi backbone but FAILED offline** → the gap is the
  border-router init / advertising proxy / OMR, not the radio path. Record this;
  it shapes whether the offline product needs that init wired up (a fixable
  bring-up gap) before committing to Option 2/3.
- **Still FAIL even with Wi-Fi backbone** → the C6 esp-thread-br DNS-SD path
  itself is not viable → **Option 4 (Pi / `ot-br-posix`)**.

---

## 10. Results capture

```text
### Stage 0 result — 2026-06-04

Hardware:
- BR-host C6:  Seeed XIAO ESP32C6, port /dev/cu.usbmodem101,
                MAC 58:e6:c5:ff:fe:1b:6d:fc
- RCP C6:      Seeed XIAO ESP32C6, MAC 58:e6:c5:ff:fe:1b:8b:54
- Client C6:   Seeed XIAO ESP32C6, port /dev/cu.usbmodem1101,
                MAC 58:e6:c5:ff:fe:1b:6c:60
- LED node C6: Seeed XIAO ESP32C6, MAC 58:e6:c5:ff:fe:1b:8b:08
Toolchain: ESP-IDF v5.4.1, esp_matter 1.4.2~2
Repo: 90b6b3d-dirty

Run 1 (offline / backbone-less):
- rcp version:            openthread-esp32/4c2820d377-005c5cefc; esp32c6;
                          2026-06-04 16:55:38 UTC
- BR state / srp server:  leader / running
- BR active dataset:      0e08000000000001000000030000104a0300001935060004001fffe00208de82414c1383cf2b0708fd42957b9cb64fbc0510278feaba96b30f1356cb36a6e1b350f3030f4f70656e5468726561642d35383037010258070410e9d7bcb235adc0f8dfe51bd200963e680c0402a0f7f8
- BR ML-EID:              fd42:957b:9cb6:4fbc:795d:f5ed:38b0:999f
- Client state:           child, then router after LED reset/retry
- §7 triage dns browse:   PASS; separate client got DNS browse response for
                          _matter._tcp.default.service.arpa including probe
- §8 commissioning:       PARTIAL; no commissioning complete line captured.
                          Client logged operational discovery/session error 32:
                          "OperationalSessionSetup[1:0000000000000002]:
                          operational discovery failed: 32"
- §8 dns browse (LED):    PASS; real LED node record resolved through BR:
                          525E53F22D34B3AE-0000000000000002
                          Port:5540
                          Host:120633A3E1E984B0.default.service.arpa.
                          HostAddress:fd42:957b:9cb6:4fbc:d25c:e012:d49c:57d6
                          TXT:[SII=32303030, SAI=32303030, SAT=34303030]
- RESULT: DISCOVERY GATE PASS

Run 2 (Wi-Fi backbone, only if Run 1 FAIL): not run; offline DNS-SD resolved.

Operational follow-up (error 32 resolved):
- Reflashed the client with the operator Wi-Fi AP OFF
  (CONFIG_LED_ORCHESTRA_OPERATOR_WIFI_MODE_DISABLED=y) + OT buffers 128.
  Boot confirmed "operator Wi-Fi ingress disabled".
- Node 2 stayed commissioned on the fabric (error 32 fires after device
  commit), so validated operationally WITHOUT re-pairing:
    controller read-attr 2 0 0x28 0   -> drove into CASE Sigma2, no error 32
    controller invoke-cmd 2 1 0xFFF1FC00 0 {SetScene solid red} -> LED rendered red
- OT bufferinfo peaked max-used 14/128 (vs prior 65/65) => Wi-Fi removal, not
  the buffer bump, was the operative fix.

Stage 0 verdict: PASS (discovery + operational CASE). BR DNS-SD path PASS
(no Error 28); operational error 32 root-caused to single-C6 radio contention
and fixed by disabling the commissioner's Wi-Fi AP. Stage 1 unblocked. See the
2026-06-04 entry in ../../docs/debugging-journal.md.
Notable log lines:
- BR:
  OPENTHREAD: spinel UART interface initialization completed
  P-SpinelDrive-: Software reset co-processor successfully
  state -> leader
  srp server state -> running
- Client:
  DNS browse response for _matter._tcp.default.service.arpa.
  525E53F22D34B3AE-0000000000000002
  DNS service resolution response for
  525E53F22D34B3AE-0000000000000002 for service
  _matter._tcp.default.service.arpa.
```

---

## Troubleshooting

- **`dns browse` → `Error 28: ResponseTimeout`** — the client isn't getting an
  answer from the BR's DNS-SD server. Confirm `srp server state = running` on
  the BR, that the client is on the **same** dataset (`matter esp ot_cli state`
  is `child`/`router`, and it can `ping` the BR's ML-EID), then try the §8
  `dns config <BR-ML-EID>` fallback. If it still times out offline, go to §9.
- **Console panic when pasting the dataset** (`Guru Meditation … Store access
  fault`) — the long `dataset set active <tlvs>` line can race the next line over
  USB-Serial/JTAG (see the debugging journal). Paste it **alone**, wait for
  `Done`, then continue. The node restores the dataset from NVS across reboots,
  so you do not need to re-send it.
- **RCP keeps resetting / spinel timeouts** — wiring: verify the side-pin
  crossover (`BR D6 -> RCP D7`, `BR D7 -> RCP D6`) and common GND; verify you
  flashed the RCP over its **native USB** port, not through the side-pin UART.
- **Two SRP servers** — only the BR may run one. The client build sets
  `OPENTHREAD_BORDER_ROUTER=n` so it cannot; if you reused a different image,
  confirm the client has no `srp server` command.

---

## 11. Bench-driving notes (what bit us 2026-06-04)

Hard-won gotchas from driving this bench over scripted serial. These cost the
most time; read them before the next run.

### Reflash: the overlay is silently ignored unless you delete `sdkconfig`

ESP-IDF only regenerates `sdkconfig` from `SDKCONFIG_DEFAULTS` when **no**
`sdkconfig` exists. The client recipe points `-D SDKCONFIG` at
`build/client/sdkconfig`, so editing `sdkconfig.client.defaults` and rebuilding
does **nothing** — the stale file shadows your change. Always:

```bash
rm -f "$S0/build/client/sdkconfig"   # then rebuild
```

Verify the change took in the regenerated file, e.g.
`grep -E 'WIFI_MODE_DISABLED|NUM_MESSAGE_BUFFERS' "$S0/build/client/sdkconfig"`.

### Driving the USB-Serial/JTAG console from scripts

- **`idf.py monitor` needs a TTY.** In a non-interactive/background shell it dies
  with `Monitor requires standard input to be attached to TTY`. Use a raw serial
  reader instead — `tools/serlog.py` (one-shot read / `--reset` / `--cmd`) and
  `tools/sercap.py` (continuous capture to a logfile + command injection via a
  FIFO). Example:

  ```bash
  mkfifo /tmp/ctrl.fifo; : > /tmp/ctrl.log
  python tools/sercap.py /dev/cu.usbmodem1101 /tmp/ctrl.log /tmp/ctrl.fifo &  # owns the port
  printf 'matter esp ot_cli state\n' > /tmp/ctrl.fifo   # inject
  tail -f /tmp/ctrl.log                                 # read
  printf '__EXIT__\n' > /tmp/ctrl.fifo                  # release the port
  ```

- **Long lines truncate silently.** The console RX overflows past ~235 chars, so
  the 198-char `pairing ble-thread <dataset>` arg arrives **mangled** and the
  commissioner no-ops with no error (just `Done` or nothing). `sercap.py` sends in
  16-byte/30 ms chunks to avoid this — confirm the echo contains the *end* of the
  line (`… 20202021 3840`) before trusting a pairing run.

- **macOS:** use `/dev/cu.*` (not `/dev/tty.*`) so opening the port doesn't assert
  DTR and reset the board. Reset the C6 deliberately by pulsing RTS
  (`serlog.py --reset`: `dtr=False; rts=True; …; rts=False`).

- **Serial access is sandboxed** — these commands need the host's device-access
  path (run them where `/dev/cu.*` is reachable).

### Commissioner logs are quiet — judge by ERROR + effect, not INFO

The controller's default log level is WARN and the per-tag INFO raises in
`app_main` do not reliably surface commissioning progress; **`log level *
verbose` is not a valid command here** (returns `Error: 1108018494`). So a
*successful* read/commission prints almost nothing. Read the verdict from:

- **absence** of the ERROR-level `error 32` / CASE-timeout (failures *are* loud), and
- a side effect you can observe — `SetScene` rendering on the LED, or an SRP/`dns
  browse` record appearing.

`Error: 83886338` (`0x05000002`) on `matter esp ot_cli state` is a transient
race; `matter esp wifi` returning it is **expected** once the Wi-Fi AP is
disabled — don't chase either.

### Don't re-commission to test the operational path

`error 32` fires at `kFindOperationalForStayActive`, **after** the device commits
to the fabric, so a node that hit it is still commissioned (it does not roll
back). To test the operational session, skip BLE re-pairing entirely and just
exercise CASE on the already-commissioned node:

```text
matter esp controller read-attr 2 0 0x28 0                      # forces discovery + CASE
matter esp controller invoke-cmd 2 1 0xFFF1FC00 0 <SetScene-json>  # visible proof
```

A stuck `pairing ble-thread` (returns `Done` but the LED window was closed) holds
a `There is already a pairing process` lock that blocks new pairings; only a
controller reset clears it.
