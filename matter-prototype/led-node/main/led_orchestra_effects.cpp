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
    kPaletteOcean = 2,
    kPaletteCoral = 3,
    kPaletteJungle = 4,
    kPaletteIce = 5,
    kPaletteNeonParty = 6,
    kPaletteGoldScore = 7,
    kPaletteMaintenance = 8,
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

const CRGB kOceanColors[16] = {
    CRGB(0, 2, 12),      CRGB(0, 8, 28),      CRGB(0, 18, 48),     CRGB(0, 36, 72),
    CRGB(0, 58, 95),     CRGB(0, 84, 118),    CRGB(0, 110, 132),   CRGB(0, 140, 145),
    CRGB(15, 165, 150),  CRGB(40, 185, 155),  CRGB(80, 205, 170),  CRGB(130, 220, 190),
    CRGB(185, 235, 215), CRGB(235, 250, 235), CRGB(90, 190, 210),  CRGB(0, 42, 86),
};

const CRGB kCoralColors[16] = {
    CRGB(8, 3, 12),      CRGB(26, 8, 28),     CRGB(58, 14, 45),    CRGB(100, 22, 58),
    CRGB(145, 36, 68),   CRGB(190, 58, 76),   CRGB(230, 88, 78),   CRGB(255, 122, 90),
    CRGB(255, 160, 115), CRGB(255, 196, 145), CRGB(250, 220, 170), CRGB(160, 215, 185),
    CRGB(78, 190, 185),  CRGB(28, 130, 160),  CRGB(18, 70, 120),   CRGB(8, 20, 54),
};

const CRGB kJungleColors[16] = {
    CRGB(0, 8, 2),       CRGB(0, 18, 5),      CRGB(0, 35, 9),      CRGB(0, 58, 16),
    CRGB(6, 84, 24),     CRGB(16, 112, 34),   CRGB(34, 140, 46),   CRGB(58, 165, 62),
    CRGB(90, 188, 72),   CRGB(130, 205, 80),  CRGB(175, 220, 88),  CRGB(220, 225, 92),
    CRGB(245, 205, 80),  CRGB(155, 120, 36),  CRGB(55, 62, 18),    CRGB(0, 18, 5),
};

const CRGB kIceColors[16] = {
    CRGB(0, 4, 18),      CRGB(0, 12, 38),     CRGB(0, 26, 64),     CRGB(0, 52, 96),
    CRGB(0, 86, 128),    CRGB(0, 122, 158),   CRGB(34, 158, 190),  CRGB(82, 190, 215),
    CRGB(135, 220, 235), CRGB(190, 242, 250), CRGB(240, 255, 255), CRGB(215, 245, 255),
    CRGB(160, 220, 248), CRGB(85, 165, 225),  CRGB(28, 86, 170),   CRGB(0, 24, 74),
};

const CRGB kNeonPartyColors[16] = {
    CRGB(8, 0, 24),      CRGB(36, 0, 80),     CRGB(85, 0, 155),    CRGB(155, 0, 210),
    CRGB(230, 0, 220),   CRGB(255, 18, 160),  CRGB(255, 75, 70),   CRGB(255, 150, 20),
    CRGB(250, 230, 0),   CRGB(150, 255, 0),   CRGB(0, 245, 90),    CRGB(0, 220, 200),
    CRGB(0, 145, 255),   CRGB(0, 70, 255),    CRGB(70, 0, 210),    CRGB(16, 0, 60),
};

const CRGB kGoldScoreColors[16] = {
    CRGB(0, 0, 0),       CRGB(18, 10, 0),     CRGB(45, 26, 0),     CRGB(86, 54, 0),
    CRGB(130, 86, 8),    CRGB(178, 122, 18),  CRGB(220, 165, 35),  CRGB(255, 205, 68),
    CRGB(255, 235, 118), CRGB(255, 250, 190), CRGB(255, 255, 235), CRGB(255, 230, 105),
    CRGB(235, 175, 38),  CRGB(160, 100, 14),  CRGB(65, 36, 0),     CRGB(8, 4, 0),
};

const CRGB kMaintenanceColors[16] = {
    CRGB(0, 0, 0),       CRGB(0, 0, 40),      CRGB(0, 0, 105),     CRGB(0, 35, 190),
    CRGB(0, 120, 255),   CRGB(80, 190, 255),  CRGB(210, 245, 255), CRGB(255, 255, 255),
    CRGB(255, 255, 255), CRGB(210, 245, 255), CRGB(80, 190, 255),  CRGB(0, 120, 255),
    CRGB(0, 35, 190),    CRGB(0, 0, 105),     CRGB(0, 0, 40),      CRGB(0, 0, 0),
};

struct PaletteEntry {
    LoPaletteMeta meta;
    const CRGB *colors;
};

const PaletteEntry kPalettes[] = {
    {{kPaletteAurora, "aurora", "Soft teal, green, and blue waves for ambient course lighting."}, kAuroraColors},
    {{kPaletteEmber, "ember", "Warm ember and lava tones for hazards and dramatic transitions."}, kEmberColors},
    {{kPaletteOcean, "ocean", "Deep blue-green water with bright whitecap accents."}, kOceanColors},
    {{kPaletteCoral, "coral", "Coral reef pinks, oranges, sand, and turquoise."}, kCoralColors},
    {{kPaletteJungle, "jungle", "Layered greens with lime and gold highlights."}, kJungleColors},
    {{kPaletteIce, "ice", "Cool blue, cyan, and white for crisp high-contrast scenes."}, kIceColors},
    {{kPaletteNeonParty, "neon-party", "High-energy magenta, cyan, lime, yellow, and blue."}, kNeonPartyColors},
    {{kPaletteGoldScore, "gold-score", "Black-to-gold celebratory scoring palette with white glints."}, kGoldScoreColors},
    {{kPaletteMaintenance, "maintenance-white-blue", "Blue and white diagnostic palette for alignment checks."},
     kMaintenanceColors},
};

constexpr size_t kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

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
    {effect::kEffectComet, "comet", "Single-color comet with a smooth fading tail; scrolls with speed.",
     LoParamUse::kColor, true, kNoPalette},
    {effect::kEffectTheaterChase, "theater-chase", "Three-phase single-color chase pattern; scrolls with speed.",
     LoParamUse::kColor, true, kNoPalette},
    {effect::kEffectPaletteCycle, "palette-cycle",
     "Aurora palette gradient across the virtual strip; scrolls with speed.", LoParamUse::kPalette, true,
     kPaletteAurora},
    {effect::kEffectTwinkle, "twinkle",
     "Deterministic sparse single-color twinkles keyed by global LED index and synchronized time.",
     LoParamUse::kColor, true, kNoPalette},
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

uint8_t hash8(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return static_cast<uint8_t>(x >> 24);
}

uint16_t ring_distance(uint16_t a, uint16_t b, uint16_t len)
{
    if (len == 0) {
        return 0;
    }
    uint16_t hi = std::max<uint16_t>(a, b);
    uint16_t lo = std::min<uint16_t>(a, b);
    uint16_t direct = hi - lo;
    return std::min<uint16_t>(direct, len - direct);
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

CRGB render_comet(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                  uint64_t time_ms)
{
    uint16_t strip_len = std::max<uint16_t>(node.total_leds, 1);
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint16_t head = static_cast<uint16_t>(((time_ms * speed) / 8000) % strip_len);
    uint16_t pos = static_cast<uint16_t>(global_index % strip_len);
    uint16_t dist = ring_distance(pos, head, strip_len);

    if (dist > 8) {
        return CRGB(0, 0, 0);
    }

    uint8_t tail = static_cast<uint8_t>(255 - (dist * 28));
    uint8_t level = lo::scale8(lo::ease8(tail), scene.brightness);
    CRGB c(scene.red, scene.green, scene.blue);
    c.nscale8(level);
    return c;
}

CRGB render_theater_chase(const LedOrchestraScene &scene, uint16_t global_index, uint64_t time_ms)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint8_t phase = static_cast<uint8_t>(((time_ms * speed) / 1200) % 3);
    if (((global_index + phase) % 3) != 0) {
        return CRGB(0, 0, 0);
    }

    CRGB c(scene.red, scene.green, scene.blue);
    c.nscale8(scene.brightness);
    return c;
}

CRGB render_palette_cycle(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                          uint64_t time_ms)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint32_t pos = (static_cast<uint32_t>(global_index) * 256) / std::max<uint16_t>(node.total_leds, 1);
    uint8_t index = static_cast<uint8_t>((pos + ((time_ms * speed) / 45)) & 0xff);

    CRGBPalette16 pal;
    if (led_orchestra_palette(kPaletteAurora, pal)) {
        return lo::ColorFromPalette(pal, index, scene.brightness);
    }
    return CRGB(0, 0, 0);
}

CRGB render_twinkle(const LedOrchestraScene &scene, uint16_t global_index, uint64_t time_ms)
{
    uint8_t seed = hash8((static_cast<uint32_t>(global_index) * 0x45d9f3bU) ^ scene.sequence);
    // Keep the field sparse and deterministic. Every node computes the same
    // stars for the same global index, so twinkles do not jump at segment edges.
    if ((seed & 0x07) != 0) {
        return CRGB(0, 0, 0);
    }

    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint8_t phase = static_cast<uint8_t>(((time_ms * speed) / 24) + seed);
    uint8_t level = lo::scale8(lo::ease8(lo::triangle8(phase)), scene.brightness);
    CRGB c(scene.red, scene.green, scene.blue);
    c.nscale8(level);
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

size_t led_orchestra_palette_count() { return kPaletteCount; }

const LoPaletteMeta *led_orchestra_palette_at(size_t index)
{
    return index < kPaletteCount ? &kPalettes[index].meta : nullptr;
}

const LoPaletteMeta *led_orchestra_palette_meta(uint8_t palette_ref)
{
    for (size_t i = 0; i < kPaletteCount; i++) {
        if (kPalettes[i].meta.ref == palette_ref) {
            return &kPalettes[i].meta;
        }
    }
    return nullptr;
}

bool led_orchestra_palette(uint8_t palette_ref, lo::CRGBPalette16 &out)
{
    for (size_t i = 0; i < kPaletteCount; i++) {
        if (kPalettes[i].meta.ref == palette_ref) {
            out = CRGBPalette16(kPalettes[i].colors);
            return true;
        }
    }
    return false;
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
    case effect::kEffectComet:
        return render_comet(scene, node, global_index, time_ms);
    case effect::kEffectTheaterChase:
        return render_theater_chase(scene, global_index, time_ms);
    case effect::kEffectPaletteCycle:
        return render_palette_cycle(scene, node, global_index, time_ms);
    case effect::kEffectTwinkle:
        return render_twinkle(scene, global_index, time_ms);
    default:
        return CRGB(0, 0, 0);
    }
}
