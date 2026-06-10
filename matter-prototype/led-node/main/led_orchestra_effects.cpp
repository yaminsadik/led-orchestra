#include "led_orchestra_effects.h"

#include <algorithm>

#include "led_orchestra_matter.h"

namespace {

using lo::CRGB;
using lo::CRGBPalette16;

namespace effect = led_orchestra::matter;

// --- Palette registry (palette references as data) -------------------------
// Append-only: add palettes at the end and keep their refs stable, exactly like
// effect ids. Effects reference a palette by ref so the color data is not baked
// into effect math and can be reused/queried.
enum PaletteRef : uint8_t {
    kPaletteAurora = 0,
    kPaletteEmber = 1,
};

const CRGB kAuroraColors[16] = {
    CRGB(0, 12, 24),   CRGB(0, 40, 60),   CRGB(0, 80, 90),    CRGB(0, 130, 110),
    CRGB(10, 170, 120), CRGB(40, 200, 120), CRGB(80, 220, 110), CRGB(140, 230, 120),
    CRGB(90, 210, 150),  CRGB(40, 180, 170), CRGB(20, 130, 180), CRGB(20, 80, 160),
    CRGB(30, 40, 130),   CRGB(20, 20, 80),   CRGB(8, 10, 40),    CRGB(0, 4, 18),
};

const CRGB kEmberColors[16] = {
    CRGB(0, 0, 0),     CRGB(24, 0, 0),    CRGB(64, 4, 0),    CRGB(110, 12, 0),
    CRGB(160, 28, 0),  CRGB(200, 50, 0),  CRGB(230, 80, 4),  CRGB(245, 120, 12),
    CRGB(255, 160, 30), CRGB(255, 190, 60), CRGB(255, 210, 100), CRGB(255, 225, 150),
    CRGB(200, 160, 90), CRGB(120, 80, 30),  CRGB(50, 24, 6),     CRGB(12, 4, 0),
};

// --- Effect metadata table (append-only) -----------------------------------
const LoEffectMeta kEffects[] = {
    {effect::kEffectOff, "off", "All LEDs dark.", LoParamUse::kIgnored, false, kNoPalette},
    {effect::kEffectSolid, "solid", "Single color from red/green/blue at brightness.", LoParamUse::kColor, false,
     kNoPalette},
    {effect::kEffectRainbow, "rainbow", "Hue sweep across the virtual strip; scrolls with speed.", LoParamUse::kIgnored,
     true, kNoPalette},
    {effect::kEffectFibonacci, "fibonacci",
     "Per-pixel R/G/B are consecutive Fibonacci numbers (mod 256) along the strip; scrolls with speed.",
     LoParamUse::kIgnored, true, kNoPalette},
    {effect::kEffectAuroraBreathe, "aurora-breathe",
     "Soft overlapping color waves with a breathing intensity curve; scrolls with speed.", LoParamUse::kPalette, true,
     kPaletteAurora},
};

constexpr size_t kEffectCount = sizeof(kEffects) / sizeof(kEffects[0]);

// Fibonacci sequence (1,2,3,5,8,...) mod 256, tiled over the Pisano period.
// Built once; the render task is the only caller.
uint8_t fib_mod256(uint32_t n)
{
    static constexpr uint16_t kPisano256 = 384;
    static uint8_t table[kPisano256];
    static bool initialized = false;
    if (!initialized) {
        table[0] = 1; // F(0)=1
        table[1] = 2; // F(1)=2 → 1,2,3,5,8,13,...
        for (uint16_t i = 2; i < kPisano256; i++) {
            table[i] = static_cast<uint8_t>(table[i - 1] + table[i - 2]);
        }
        initialized = true;
    }
    return table[n % kPisano256];
}

CRGB render_solid(const LedOrchestraScene &scene)
{
    CRGB c(scene.red, scene.green, scene.blue);
    c.nscale8(scene.brightness);
    return c;
}

CRGB render_rainbow(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                    uint64_t time_ms)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint32_t time_hue = static_cast<uint32_t>((time_ms * speed) / 20);
    uint32_t pos_hue = (static_cast<uint32_t>(global_index) * 256) / std::max<uint16_t>(node.total_leds, 1);
    CRGB c = lo::hsv_to_rgb(lo::CHSV(static_cast<uint8_t>((time_hue + pos_hue) & 0xff), 255, 255));
    c.nscale8(scene.brightness);
    return c;
}

CRGB render_fibonacci(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                      uint64_t time_ms)
{
    // Scroll rate: speed=10 → 1 px/s; speed=0 → static gradient. Every 5 scroll
    // ticks the strip drifts 1 extra LED so the pattern circles end→start.
    uint64_t speed = scene.speed;
    uint32_t strip_len = std::max<uint16_t>(node.total_leds, 1);
    uint32_t scroll = (speed == 0) ? 0 : static_cast<uint32_t>((time_ms * speed) / 10000);
    uint32_t drift = scroll / 5;
    uint32_t pos = (static_cast<uint32_t>(global_index) + scroll + drift) % strip_len;

    // Every 11 positions step back 2 in the Fibonacci sequence and rotate the
    // RGB channel assignment by 1, creating overlapping color bands so adjacent
    // segments share Fibonacci values. Net advance per 11-pixel group: 9.
    uint32_t gobacks = pos / 11;
    uint32_t fib_base = pos - gobacks * 2;
    uint8_t rgb_shift = static_cast<uint8_t>(gobacks % 3);

    uint8_t channels[3] = {
        fib_mod256(fib_base),
        fib_mod256(fib_base + 1),
        fib_mod256(fib_base + 2),
    };
    CRGB c(channels[rgb_shift % 3], channels[(rgb_shift + 1) % 3], channels[(rgb_shift + 2) % 3]);
    c.nscale8(scene.brightness);
    return c;
}

CRGB render_aurora_breathe(const LedOrchestraScene &scene, uint16_t global_index, uint64_t time_ms)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint8_t phase = static_cast<uint8_t>(((time_ms * speed) / 35) + (global_index * 4));
    uint8_t breath_phase = static_cast<uint8_t>(((time_ms * speed) / 80) + (global_index * 2));
    // Breathing envelope 96..255 via a smooth wave.
    uint8_t breath = static_cast<uint8_t>(96 + lo::scale8(lo::triangle8(breath_phase), 159));

    // Color comes from the aurora palette indexed by the scroll phase, then
    // shaped by the breathing envelope. This exercises the palette + scale path
    // of the engine while keeping the soft, band-free look on sparse strips.
    CRGBPalette16 pal;
    CRGB c;
    if (led_orchestra_palette(kPaletteAurora, pal)) {
        c = lo::ColorFromPalette(pal, phase, breath);
    } else {
        // Fallback to the original triangle-wave RGB shaping if the palette is
        // somehow unavailable (keeps the effect rendering rather than blanking).
        c = CRGB(lo::scale8(static_cast<uint8_t>(64 + lo::scale8(lo::triangle8(phase), 191)), breath),
                 lo::scale8(static_cast<uint8_t>(64 + lo::scale8(lo::triangle8(static_cast<uint8_t>(phase + 85)), 191)),
                            breath),
                 lo::scale8(static_cast<uint8_t>(64 + lo::scale8(lo::triangle8(static_cast<uint8_t>(phase + 170)), 191)),
                            breath));
    }
    c.nscale8(scene.brightness);
    return c;
}

} // namespace

size_t led_orchestra_effect_count() { return kEffectCount; }

const LoEffectMeta *led_orchestra_effect_at(size_t index)
{
    return index < kEffectCount ? &kEffects[index] : nullptr;
}

const LoEffectMeta *led_orchestra_effect_meta(uint8_t effect_id)
{
    for (size_t i = 0; i < kEffectCount; i++) {
        if (kEffects[i].id == effect_id) {
            return &kEffects[i];
        }
    }
    return nullptr;
}

bool led_orchestra_palette(uint8_t palette_ref, lo::CRGBPalette16 &out)
{
    switch (palette_ref) {
    case kPaletteAurora:
        out = CRGBPalette16(kAuroraColors);
        return true;
    case kPaletteEmber:
        out = CRGBPalette16(kEmberColors);
        return true;
    default:
        return false;
    }
}

lo::CRGB led_orchestra_render_effect(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node,
                                     uint16_t global_index, uint64_t time_ms)
{
    switch (scene.effect) {
    case effect::kEffectOff:
        return CRGB(0, 0, 0);
    case effect::kEffectSolid:
        return render_solid(scene);
    case effect::kEffectRainbow:
        return render_rainbow(scene, node, global_index, time_ms);
    case effect::kEffectFibonacci:
        return render_fibonacci(scene, node, global_index, time_ms);
    case effect::kEffectAuroraBreathe:
        return render_aurora_breathe(scene, global_index, time_ms);
    default:
        return CRGB(0, 0, 0);
    }
}
