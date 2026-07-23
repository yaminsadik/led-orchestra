#pragma once

#include <stdint.h>

namespace led_orchestra {
namespace matter {

// Development vendor id 0xFFF1 plus manufacturer-specific cluster range 0xFC00.
// Replace the vendor id before any production or certified build.
static constexpr uint32_t kClusterId = 0xFFF1FC00;
static constexpr uint16_t kEndpointIdHint = 1;

// Default "all LED nodes" application group. An application group id is a plain
// 16-bit id (0x0001..0xFEFF); it is encoded into a Matter group NodeId with
// chip::NodeIdFromGroupId(group) (= 0xFFFFFFFFFFFF0000 | group) before a
// groupcast invoke. Group `0x0001` is the conventional all-nodes group for the
// orchestra. Group ids are an append-only namespace: never reuse a retired one.
static constexpr uint16_t kGroupAllNodes = 0x0001;

// Standard Matter Groups cluster (0x0004), used unicast on each LED endpoint to
// enroll that endpoint into an application group. Once an endpoint is a group
// member, a groupcast invoke of the LED Orchestra custom cluster reaches it.
// These are SDK/spec ids, restated here so the controller console does not pull
// the full zap-generated cluster objects.
namespace groups_cluster {
static constexpr uint32_t kClusterId = 0x00000004;
static constexpr uint32_t kCommandAddGroup = 0x00000000;     // AddGroup(group_id, group_name)
static constexpr uint32_t kCommandRemoveGroup = 0x00000003;  // RemoveGroup(group_id)
namespace add_group_tag {
static constexpr uint8_t kGroupId = 0;
static constexpr uint8_t kGroupName = 1;
} // namespace add_group_tag
} // namespace groups_cluster

// Group key material defaults for the development fabric. The controller must
// install a group keyset and bind it to the application group before groupcast
// custom-cluster commands are accepted by member nodes (see
// `lo-show-group-help` and docs/console.md). These are dev/test values only;
// production rotates real epoch keys through the Kubernetes control plane.
namespace group_key {
static constexpr uint16_t kDefaultKeysetId = 0x0042;
// Key policy 0 = TrustFirst (CacheAndSync requires synced time). 16-byte epoch
// key as a 32-char hex octet string for `controller group-settings add-keyset`.
static constexpr uint8_t kDefaultKeyPolicy = 0;
} // namespace group_key

namespace attribute {
static constexpr uint32_t kCurrentScene = 0x00000000;
static constexpr uint32_t kSegmentStart = 0x00000001;
static constexpr uint32_t kSegmentLength = 0x00000002;
static constexpr uint32_t kTotalLeds = 0x00000003;
static constexpr uint32_t kLedGpio = 0x00000004;
static constexpr uint32_t kFirmwareVersion = 0x00000005;
static constexpr uint32_t kLastSequence = 0x00000006;
// Field-calibration read-back (append-only). These mirror the last accepted
// SetCalibration so an operator can confirm per-node tuning survived a
// power-cycle with `lo-read-config` (no monitor required). kCalibTimeOffsetMs is
// a signed int32 carried in a U32 attribute as two's-complement bits.
static constexpr uint32_t kCalibBrightnessCap = 0x00000007;
static constexpr uint32_t kCalibPaletteOverride = 0x00000008;
static constexpr uint32_t kCalibTimeOffsetMs = 0x00000009;
} // namespace attribute

namespace command {
static constexpr uint32_t kSetScene = 0x00000000;
static constexpr uint32_t kSetNodeConfig = 0x00000001;
static constexpr uint32_t kSyncClock = 0x00000002;
// Field calibration: per-node runtime tuning (timing/sync offset, brightness
// cap, palette override, LED color correction). Tuning is DATA, not firmware —
// these knobs change after install with no reflash. Append-only: never reorder
// or reuse command ids.
static constexpr uint32_t kSetCalibration = 0x00000003;
} // namespace command

namespace set_scene_tag {
static constexpr uint8_t kEffect = 0;
static constexpr uint8_t kRed = 1;
static constexpr uint8_t kGreen = 2;
static constexpr uint8_t kBlue = 3;
static constexpr uint8_t kSpeed = 4;
static constexpr uint8_t kBrightness = 5;
static constexpr uint8_t kSequence = 6;
static constexpr uint8_t kScheduledStartMs = 7;
} // namespace set_scene_tag

namespace set_node_config_tag {
static constexpr uint8_t kNodeId = 0;
static constexpr uint8_t kSegmentStart = 1;
static constexpr uint8_t kSegmentLength = 2;
static constexpr uint8_t kTotalLeds = 3;
static constexpr uint8_t kLedGpio = 4;
} // namespace set_node_config_tag

namespace sync_clock_tag {
static constexpr uint8_t kControllerTimeMs = 0;
} // namespace sync_clock_tag

// SetCalibration field tags (append-only). All fields are optional on the wire:
// an omitted tag keeps the node's current value, so the controller can tweak one
// knob (e.g. just the timing offset) without resending the rest.
//   kTimeOffsetMs    : signed int32 ms added to THIS node's render clock so a
//                      synchronized wave/comet can be nudged hole-to-hole after
//                      install (phase / travel-delay / start-delay compensation).
//                      Carried as a U32 of two's-complement bits to use only the
//                      proven U32 wire token; the node reinterprets it as int32.
//                      It shifts effect *math* only; scheduled-scene activation
//                      still uses the unmodified synchronized clock.
//   kBrightnessCap   : U8 master-brightness ceiling 0..255 (255 = no cap),
//                      applied after the per-scene brightness.
//   kPaletteOverride : U8 palette ref that replaces the effect's default palette
//                      for palette-driven effects; 0xFF = use the effect default.
//   kColorCorrection : U32 0x00RRGGBB per-channel LED correction (0x00FFFFFF =
//                      identity) for matching different strip batches/zones.
//   kColorTemperature: U32 0x00RRGGBB white-point trim (0x00FFFFFF = identity).
namespace set_calibration_tag {
static constexpr uint8_t kTimeOffsetMs = 0;
static constexpr uint8_t kBrightnessCap = 1;
static constexpr uint8_t kPaletteOverride = 2;
static constexpr uint8_t kColorCorrection = 3;
static constexpr uint8_t kColorTemperature = 4;
} // namespace set_calibration_tag

// Palette override sentinel: "use the effect's compiled default palette".
static constexpr uint8_t kPaletteOverrideNone = 0xFF;

enum EffectId : uint8_t {
    kEffectOff = 0,
    kEffectSolid = 1,
    kEffectRainbow = 2,
    // Per-pixel color whose R/G/B channels are consecutive Fibonacci numbers
    // (mod 256) along the virtual strip; the sequence scrolls down the strip
    // with speed. Append-only: never reorder or reuse effect ids.
    kEffectFibonacci = 3,
    // Soft overlapping RGB waves with a global breathe curve. Designed to avoid
    // hard primary-only bands on sparse/short bench strips.
    kEffectAuroraBreathe = 4,
    // Single-color FastLED-style comet with a fading tail. Uses RGB + speed.
    kEffectComet = 5,
    // Single-color three-phase chase pattern. Uses RGB + speed.
    kEffectTheaterChase = 6,
    // Palette gradient that scrolls through the virtual strip. Ignores RGB.
    kEffectPaletteCycle = 7,
    // Deterministic sparse twinkles, segment-safe because the sparkle field is
    // derived from global index + synchronized time rather than local RNG state.
    kEffectTwinkle = 8,
    // --- Mini-golf installation effects (append-only) ----------------------
    // Ambient layered underwater drift sampled from a palette (default ocean);
    // slow, band-free, for scenic/perimeter runs under the water mural.
    kEffectOceanDrift = 9,
    // A luminous palette crest that travels the whole virtual strip; designed to
    // be tuned hole-to-hole with the per-node time offset so it reads as one
    // continuous wave across physical gaps and bends.
    kEffectColorWave = 10,
    // A bright palette pulse that sweeps the strip over a dim wash — an
    // attraction "look here now" reveal cue.
    kEffectPulseReveal = 11,
    // Celebratory gold sparkle + shimmer (default gold-score palette) for a
    // hole-in-one / scoring moment.
    kEffectCelebration = 12,
    // Install/diagnostics: render this node's segment in a color keyed to its
    // orchestra node id with white-tipped segment ends, blinking slowly, so an
    // installer can map which physical strip is which node id.
    kEffectIdentify = 13,
    // Layered tide motion with bright whitecaps and deep undertow for the
    // ocean-theme perimeter; palette-driven with a surf palette by default.
    kEffectTidalSurge = 14,
    // Dense neon confetti bursts over a dim wash for birthday parties and
    // full-venue party scenes. Palette-driven.
    kEffectPartyConfetti = 15,
    // Soft champagne shimmer with elegant glints for anniversaries and other
    // upscale occasion scenes. Palette-driven.
    kEffectChampagneToast = 16,
    // Red, white, and blue moving bands with white star glints for Independence
    // Day / patriotic scenes. Palette-driven.
    kEffectIndependence = 17,
};

} // namespace matter
} // namespace led_orchestra
