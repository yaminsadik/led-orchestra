#pragma once

#include <stddef.h>
#include <stdint.h>

#include "led_color.h"
#include "led_orchestra_renderer.h" // LedOrchestraScene, LedOrchestraNodeConfig

// Effect metadata + rendering for the LED Orchestra LED nodes.
//
// This is the "effect metadata architecture": every effect has a stable,
// append-only id, a name/description, which scene params it actually consumes,
// and an optional palette reference (palettes are *data*, kept in a registry).
// The hub/controller can later query this table to list effects and their
// parameter metadata. There is no runtime-uploaded effect code: new effect
// *behavior* ships as compiled firmware via OTA; only params are live.
//
// Effects are pure functions of (effect id + params, node config, global LED
// index, synchronized controller time). No per-pixel mutable state, so segments
// stay aligned across node boundaries without coordination.

// Which kind of scene parameter a metadata entry describes.
enum class LoParamUse : uint8_t {
    kIgnored = 0, // field present on the wire but not used by this effect
    kColor = 1,   // honors red/green/blue
    kSpeed = 2,   // animates with speed
    kPalette = 3, // color comes from a palette, not red/green/blue
};

struct LoEffectMeta {
    uint8_t id;              // append-only effect id (matches matter::EffectId)
    const char *name;        // short stable name
    const char *description; // human-readable summary
    LoParamUse color_use;    // how red/green/blue are interpreted
    bool scrolls;            // whether `speed` animates the effect
    uint8_t palette_ref;     // index into the palette registry; kNoPalette if none
};

struct LoPaletteMeta {
    uint8_t ref;             // append-only palette ref
    const char *name;        // short stable name
    const char *description; // human-readable summary
};

static constexpr uint8_t kNoPalette = 0xFF;

// Registry lookups (append-only; callers must not assume id == array index).
size_t led_orchestra_effect_count();
const LoEffectMeta *led_orchestra_effect_at(size_t index);
const LoEffectMeta *led_orchestra_effect_meta(uint8_t effect_id);

// Palette registry — palette references as data. Returns true and fills `out`
// for a known ref; false for kNoPalette / unknown.
size_t led_orchestra_palette_count();
const LoPaletteMeta *led_orchestra_palette_at(size_t index);
const LoPaletteMeta *led_orchestra_palette_meta(uint8_t palette_ref);
bool led_orchestra_palette(uint8_t palette_ref, lo::CRGBPalette16 &out);

// Render one pixel for `scene.effect`. Returns the engine color BEFORE the
// renderer's output policy (color correction / temperature / master brightness)
// is applied. Per-scene `brightness` IS applied here.
lo::CRGB led_orchestra_render_effect(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node,
                                     uint16_t global_index, uint64_t time_ms);
