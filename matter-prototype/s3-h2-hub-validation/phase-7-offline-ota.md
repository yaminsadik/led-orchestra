# Phase 7 — Offline Matter OTA (runbook + remaining plumbing)

Update commissioned LED nodes over the offline Matter/Thread fabric — no node-side
USB, no internet. This runbook covers what is **implemented**, the **exact
remaining plumbing** to make it functional, and the **field-security** layer that
comes after.

Role split (from [`../../docs/architecture.md`](../../docs/architecture.md)):

```text
operator (USB / controller Wi-Fi) or Kubernetes
  -> signed + encrypted image bytes
  -> hub: stores image, acts as Matter OTA Provider (cluster 0x0029)
  -> Matter OTA (BDX) over Thread
  -> LED node: Matter OTA Requestor verifies + applies; USB flash = recovery
```

Two independent security layers — do not collapse them: **fabric credentials**
decide *who* may invoke the OTA cluster; **image signing/encryption** decides
*what* firmware a node will run.

## Implemented (this branch)

- **LED node = OTA Requestor.** `CONFIG_ENABLE_OTA_REQUESTOR=y`; the requestor
  cluster is auto-created by `esp_matter::start()`. Partitions `ota_0`/`ota_1`/
  `otadata` are in [`../led-node/partitions.csv`](../led-node/partitions.csv).
  Builds today. (Flash watch: the app is ~2% free in each OTA slot — see the
  LED-node README. A larger image must still fit a slot.)
- **Controller = OTA Provider scaffold (build-gated).** Behind
  `CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER` (default **off**):
  - an OTA Provider cluster (`0x0029`) on the controller's root endpoint, backed
    by esp_matter's `EspOtaProvider` (default **DENY**; per-node opt-in);
  - operator console:
    `lo-ota-status`, `lo-ota-enable <node> [once]`, `lo-ota-disable <node>`,
    `lo-ota-set-image <uri> <sw-version> <version-string> <size>`.
  - `lo-ota-enable`/`lo-ota-disable` call the real `EspOtaProvider` per-node APIs.
    `lo-ota-set-image` records the local candidate the hub intends to serve.

Code: [`../controller-node/main/led_orchestra_ota_provider.cpp`](../controller-node/main/led_orchestra_ota_provider.cpp).

## Enabling the provider build (co-config)

`CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER=y` alone is not enough. Also set, and
re-validate the controller build/commissioner path before trusting it:

```text
CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER=y
CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER=y     # controller must host a data model
# esp_matter_ota_provider DCL fetch OFF for offline-first:
#   (the component's Kconfig DCL choice — do NOT point at MainNet/TestNet)
```

> The default branch build is verified with the provider **off** (proven
> commissioner unchanged). The provider-on build pairs the commissioner with a
> data-model server on one C6 and is **not yet hardware-validated** — treat the
> first provider-on build/flash as a gate, capture heap/flash, and journal it.

## The remaining offline plumbing (be honest about this)

The stock `EspOtaProvider` is DCL+HTTP oriented:

1. On `QueryImage` it looks up a candidate cache. The cache is filled **only from
   DCL** (`_query_software_version_array` → an HTTPS DCL REST URL). There is no
   public "add a local candidate" API.
2. On match it starts a **BDX** transfer whose source is an **HTTP(S)** GET of the
   candidate's `ota_url` (`OtaBdxSender` opens `esp_http_client`).

So to be truly offline, one of these must be added (this is the work that remains;
`lo-ota-set-image` records the intent but does not yet wire it):

- **Option A — hub-local endpoints (no firmware change).** Run, on the hub's
  Wi-Fi/loopback, (a) a tiny DCL-shaped REST responder returning the local
  candidate (vid/pid/version/url/size) and (b) a static HTTP server serving the
  `.ota` image from local flash/SPIFFS or the K8s-pushed file. Point the
  candidate `ota_url` at that local HTTP server. The provider then "fetches from
  DCL" and "downloads over HTTP", but both are hub-local LAN — no internet.
- **Option B — flash-backed provider extension (firmware change).** Extend the
  provider to inject a locally-registered candidate (bypass DCL) and to serve BDX
  blocks from a flash partition / loopback HTTP, driven by `lo-ota-set-image`.
  Cleaner offline story; more code to own and validate.

Either way: keep the ingress source (laptop/K8s) **off the Matter fabric** — it
only delivers image bytes to the provider.

## Bring-up procedure (once a serve path exists)

1. Restore the known-good split topology and commission the LED node(s)
   ([`stage-b-br-baseline.md`](stage-b-br-baseline.md) /
   [`../../docs/checkpoints/2026-06-09-stage-b-split-known-good.md`](../../docs/checkpoints/2026-06-09-stage-b-split-known-good.md)).
2. Build a **newer** LED-node image (bump the app version so the requestor sees an
   update) and place its `.ota` where the hub serves it.
3. On the hub: `lo-ota-set-image <local-uri> <new-sw-version> <version-str> <size>`,
   then `lo-ota-status` to confirm.
4. Tell the requestor where the provider is (write the requestor's
   `DefaultOTAProviders`, or `AnnounceOTAProvider`), then allow it:
   `lo-ota-enable <node>` (use `once` for a single controlled attempt).
5. Watch the **LED node** log: QueryImage → UpdateAvailable → BDX download →
   apply → reboot into the new slot. Confirm `otadata` switched slots and the new
   firmware version reports back.
6. **Mark OTA "functional" only after a real LED node downloads and applies an
   image over Matter/Thread.** No earlier.

## Field-readiness (separate, after functional)

Functional offline OTA can land first; field-ready OTA still needs, and must be
documented/tested:

- **Secure boot** + **flash encryption** on the LED nodes.
- **Signed + encrypted images** (`CONFIG_ENABLE_ENCRYPTED_OTA=y` + the image
  signing/encryption key flow); reject invalid/wrong-key images.
- **Key handling** for signing/encryption keys and per-device factory data.
- **Rollback/recovery tests:** a failed/aborted update must not brick a node; USB
  flash stays the recovery path. Verify the inactive-slot rollback.
- Keep fabric credentials and image keys as **separate** layers.

## Evidence (fill at the bench)

```text
Date / operator:
Provider build (off | on + server co-config):
Serve path used (A hub-local | B flash-backed):
New image version / size:
QueryImage -> UpdateAvailable seen on node? :
BDX download completed? bytes:
Applied + rebooted into new slot? otadata switched? :
New firmware version reported back? :
Heap (provider on) min-free / largest-free:
Flash headroom (provider on) controller / LED node:
Field-security items done (secure boot / flash enc / signed image / rollback):
Pass/Fail (FUNCTIONAL only after real download+apply):
Notes / journal ref:
```
