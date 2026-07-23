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
    kPaletteSeafoamSurf = 9,
    kPaletteChampagneRose = 10,
    kPaletteIndependence = 11,
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

const CRGB kSeafoamSurfColors[16] = {
    CRGB(0, 3, 12),      CRGB(0, 10, 26),     CRGB(0, 24, 52),      CRGB(0, 48, 82),
    CRGB(0, 82, 118),    CRGB(0, 118, 145),   CRGB(0, 156, 166),    CRGB(24, 190, 180),
    CRGB(76, 215, 196),  CRGB(130, 230, 212), CRGB(188, 240, 224),  CRGB(225, 248, 236),
    CRGB(246, 252, 246), CRGB(255, 255, 255), CRGB(196, 235, 236),  CRGB(40, 120, 148),
};

const CRGB kChampagneRoseColors[16] = {
    CRGB(10, 4, 2),      CRGB(26, 12, 6),      CRGB(48, 24, 12),     CRGB(78, 40, 20),
    CRGB(112, 60, 32),   CRGB(150, 84, 48),    CRGB(188, 112, 68),   CRGB(220, 144, 92),
    CRGB(242, 178, 122), CRGB(252, 206, 154),  CRGB(255, 226, 184),  CRGB(255, 238, 206),
    CRGB(255, 246, 224), CRGB(255, 244, 236),  CRGB(244, 214, 196),  CRGB(186, 126, 100),
};

const CRGB kIndependenceColors[16] = {
    CRGB(10, 20, 90),    CRGB(18, 36, 155),   CRGB(35, 70, 220),    CRGB(210, 235, 255),
    CRGB(255, 255, 255), CRGB(255, 255, 255), CRGB(255, 220, 220),   CRGB(210, 20, 38),
    CRGB(255, 38, 55),   CRGB(210, 20, 38),   CRGB(255, 220, 220),   CRGB(255, 255, 255),
    CRGB(210, 235, 255), CRGB(35, 70, 220),   CRGB(18, 36, 155),    CRGB(10, 20, 90),
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
    {{kPaletteSeafoamSurf, "seafoam-surf", "Aqua surf tones with bright foamy whitecaps for ocean-theme runs."},
     kSeafoamSurfColors},
    {{kPaletteChampagneRose, "champagne-rose", "Warm champagne, blush, and rose-gold for birthdays and anniversaries."},
     kChampagneRoseColors},
    {{kPaletteIndependence, "independence", "Red, white, and blue bands with bright white star glints."},
     kIndependenceColors},
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
    {effect::kEffectOceanDrift, "ocean-drift",
     "Ambient layered underwater drift sampled from a palette (default ocean); slow and band-free.",
     LoParamUse::kPalette, true, kPaletteOcean},
    {effect::kEffectColorWave, "color-wave",
     "Luminous palette crest that travels the whole virtual strip; tune hole-to-hole with the per-node "
     "time offset for one continuous wave across gaps.",
     LoParamUse::kPalette, true, kPaletteAurora},
    {effect::kEffectPulseReveal, "pulse-reveal",
     "Bright palette pulse sweeping over a dim wash; an attraction reveal cue. Default coral palette.",
     LoParamUse::kPalette, true, kPaletteCoral},
    {effect::kEffectCelebration, "celebration",
     "Celebratory gold sparkle and shimmer for a scoring moment; default gold-score palette.",
     LoParamUse::kPalette, true, kPaletteGoldScore},
    {effect::kEffectIdentify, "identify",
     "Install/diagnostics: this node's segment in a color keyed to its node id, white-tipped ends, slow "
     "blink, for mapping physical strips to node ids.",
     LoParamUse::kIgnored, true, kNoPalette},
    {effect::kEffectTidalSurge, "tidal-surge",
     "Layered tide motion with bright whitecaps and deep undertow; default seafoam-surf palette.",
     LoParamUse::kPalette, true, kPaletteSeafoamSurf},
    {effect::kEffectPartyConfetti, "party-confetti",
     "High-energy palette confetti bursts over a dim wash for birthday and full-venue party scenes.",
     LoParamUse::kPalette, true, kPaletteNeonParty},
    {effect::kEffectChampagneToast, "champagne-toast",
     "Soft champagne shimmer with elegant glints for anniversaries and other occasion scenes.",
     LoParamUse::kPalette, true, kPaletteChampagneRose},
    {effect::kEffectIndependence, "independence",
     "Red, white, and blue moving bands with bright white star glints for patriotic scenes.",
     LoParamUse::kPalette, true, kPaletteIndependence},
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

CRGB render_aurora_breathe(const LedOrchestraScene &scene, uint16_t global_index, uint64_t time_ms,
                           uint8_t palette_ref)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint8_t phase = static_cast<uint8_t>(((time_ms * speed) / 35) + (global_index * 4));
    uint8_t breath_phase = static_cast<uint8_t>(((time_ms * speed) / 80) + (global_index * 2));
    // Breathing envelope 96..255 via a smooth wave.
    uint8_t breath = static_cast<uint8_t>(96 + lo::scale8(lo::triangle8(breath_phase), 159));

    // Color comes from the resolved palette indexed by the scroll phase, then
    // shaped by the breathing envelope. This exercises the palette + scale path
    // of the engine while keeping the soft, band-free look on sparse strips.
    CRGBPalette16 pal;
    CRGB c;
    if (led_orchestra_palette(palette_ref, pal)) {
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
                          uint64_t time_ms, uint8_t palette_ref)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint32_t pos = (static_cast<uint32_t>(global_index) * 256) / std::max<uint16_t>(node.total_leds, 1);
    uint8_t index = static_cast<uint8_t>((pos + ((time_ms * speed) / 45)) & 0xff);

    CRGBPalette16 pal;
    if (led_orchestra_palette(palette_ref, pal)) {
        return lo::ColorFromPalette(pal, index, scene.brightness);
    }
    return CRGB(0, 0, 0);
}

// --- Mini-golf installation effects ----------------------------------------
// All are pure functions of (global index, synchronized time, node config) so
// segments stay coherent across node boundaries with no shared state. Each
// palette-driven effect takes the resolved `palette_ref` so a per-node palette
// override (calibration) re-skins it without new firmware.

CRGB render_ocean_drift(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                        uint64_t time_ms, uint8_t palette_ref)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    // Two slow layers drifting in opposite directions so the water never bands.
    uint8_t a = lo::sin8(static_cast<uint8_t>((global_index * 6) + (time_ms * speed) / 120));
    uint8_t b = lo::sin8(static_cast<uint8_t>((global_index * 3) - (time_ms * speed) / 200 + 64));
    uint8_t index = static_cast<uint8_t>((a >> 1) + (b >> 1));

    CRGBPalette16 pal;
    if (led_orchestra_palette(palette_ref, pal)) {
        return lo::ColorFromPalette(pal, index, scene.brightness);
    }
    // Palette-less fallback: a calm blue-green wash so the effect never blanks.
    CRGB c(0, static_cast<uint8_t>(40 + (index >> 2)), static_cast<uint8_t>(80 + (index >> 1)));
    c.nscale8(scene.brightness);
    return c;
}

CRGB render_color_wave(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                       uint64_t time_ms, uint8_t palette_ref)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint16_t strip_len = std::max<uint16_t>(node.total_leds, 1);
    // Color is fixed by position along the virtual strip; a traveling sine in
    // brightness reads as a luminous crest moving along it. Per-node time offset
    // (applied upstream in the render clock) shifts the crest's arrival, which is
    // the hole-to-hole alignment knob.
    uint8_t pos = static_cast<uint8_t>((static_cast<uint32_t>(global_index) * 255) / strip_len);
    uint8_t crest = lo::sin8(static_cast<uint8_t>(pos - (time_ms * speed) / 60));
    uint8_t level = lo::scale8(lo::ease8(crest), scene.brightness);

    CRGBPalette16 pal;
    if (led_orchestra_palette(palette_ref, pal)) {
        return lo::ColorFromPalette(pal, pos, level);
    }
    CRGB c = lo::hsv_to_rgb(lo::CHSV(pos, 255, 255));
    c.nscale8(level);
    return c;
}

CRGB render_pulse_reveal(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                         uint64_t time_ms, uint8_t palette_ref)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint16_t strip_len = std::max<uint16_t>(node.total_leds, 1);
    uint16_t pos = static_cast<uint16_t>(global_index % strip_len);
    uint16_t head = static_cast<uint16_t>(((time_ms * speed) / 5000) % strip_len);
    uint16_t width = static_cast<uint16_t>(strip_len / 8 + 2);
    uint16_t dist = ring_distance(pos, head, strip_len);

    uint8_t base_index = static_cast<uint8_t>((static_cast<uint32_t>(pos) * 255) / strip_len);

    CRGBPalette16 pal;
    bool have_pal = led_orchestra_palette(palette_ref, pal);
    // Dim ambient wash so the strip is never fully dark between sweeps.
    CRGB color = have_pal ? lo::ColorFromPalette(pal, base_index, lo::scale8(40, scene.brightness))
                          : CRGB(0, 0, 0);
    if (dist <= width) {
        uint8_t t = static_cast<uint8_t>(255 - (dist * 255 / width));
        uint8_t lvl = lo::scale8(lo::ease8(t), scene.brightness);
        CRGB peak = have_pal ? lo::ColorFromPalette(pal, static_cast<uint8_t>(base_index + 128), lvl)
                             : CRGB(lvl, lvl, lvl);
        color = lo::blend(color, peak, lo::ease8(t));
    }
    return color;
}

CRGB render_celebration(const LedOrchestraScene &scene, uint16_t global_index, uint64_t time_ms, uint8_t palette_ref)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    // Deterministic per-pixel sparkle phase keyed by index + scene sequence so
    // every node lights the same glints; the gold palette gives the celebratory
    // look without depending on RGB.
    uint8_t seed = hash8((static_cast<uint32_t>(global_index) * 0x9E3779B1u) ^ scene.sequence);
    uint8_t shimmer = lo::sin8(static_cast<uint8_t>((time_ms * speed) / 20 + seed));
    uint8_t index = static_cast<uint8_t>(seed + (time_ms * speed) / 30);
    uint8_t level = lo::scale8(lo::ease8(shimmer), scene.brightness);

    CRGBPalette16 pal;
    if (led_orchestra_palette(palette_ref, pal)) {
        return lo::ColorFromPalette(pal, index, level);
    }
    CRGB c(255, 200, 60); // gold fallback
    c.nscale8(level);
    return c;
}

CRGB render_identify(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                     uint64_t time_ms)
{
    // Each node id maps to a distinct, saturated hue so two adjacent strips never
    // share a color. The two ends of this node's own segment are tipped white so
    // an installer can see exactly where one node's strip starts and stops.
    uint8_t hue = hash8(static_cast<uint32_t>(node.node_id) * 2654435761u);
    bool on = ((time_ms / 500) & 1u) != 0; // slow ~1 Hz blink, no speed dependence

    uint16_t local = static_cast<uint16_t>(global_index - node.segment_start);
    bool is_end = (node.segment_len > 0) && (local == 0 || local == static_cast<uint16_t>(node.segment_len - 1));

    uint8_t level = on ? scene.brightness : lo::scale8(scene.brightness, 40);
    if (is_end) {
        return CRGB(level, level, level);
    }
    CRGB c = lo::hsv_to_rgb(lo::CHSV(hue, 255, 255));
    c.nscale8(level);
    return c;
}

CRGB render_tidal_surge(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                        uint64_t time_ms, uint8_t palette_ref)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint16_t strip_len = std::max<uint16_t>(node.total_leds, 1);
    uint8_t pos = static_cast<uint8_t>((static_cast<uint32_t>(global_index) * 255) / strip_len);

    // Broad swells carry the water color while a faster top layer occasionally
    // throws a bright crest, so the venue gets a stronger "surf" read than the
    // calmer ambient ocean-drift effect.
    uint8_t swell = lo::sin8(static_cast<uint8_t>(pos + (time_ms * speed) / 90));
    uint8_t undertow = lo::sin8(static_cast<uint8_t>((pos * 3) - (time_ms * speed) / 170 + 96));
    uint8_t foam = lo::sin8(static_cast<uint8_t>((pos * 5) - (time_ms * speed) / 28));
    uint8_t index = static_cast<uint8_t>(lo::scale8(swell, 160) + lo::scale8(undertow, 95));
    uint8_t base_level = lo::scale8(static_cast<uint8_t>(96 + (undertow >> 2)), scene.brightness);

    CRGBPalette16 pal;
    bool have_pal = led_orchestra_palette(palette_ref, pal);
    CRGB base = have_pal ? lo::ColorFromPalette(pal, index, base_level)
                         : CRGB(0, static_cast<uint8_t>(28 + (swell >> 3)), static_cast<uint8_t>(74 + (swell >> 2)));
    if (!have_pal) {
        base.nscale8(scene.brightness);
    }

    if (foam <= 180) {
        return base;
    }

    uint8_t whitecap = static_cast<uint8_t>((static_cast<uint16_t>(foam - 180) * 255) / 75);
    CRGB crest = have_pal ? lo::ColorFromPalette(pal, 248, lo::scale8(scene.brightness, whitecap))
                          : CRGB(lo::scale8(scene.brightness, whitecap), lo::scale8(scene.brightness, whitecap),
                                 lo::scale8(scene.brightness, whitecap));
    return lo::blend(base, crest, whitecap);
}

CRGB render_party_confetti(const LedOrchestraScene &scene, uint16_t global_index, uint64_t time_ms, uint8_t palette_ref)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint8_t seed = hash8((static_cast<uint32_t>(global_index) * 0xA511E9B3u) ^ (scene.sequence * 17u));
    uint8_t phase = static_cast<uint8_t>((time_ms * speed) / 16 + seed);
    uint8_t burst = lo::ease8(lo::triangle8(phase));
    bool active = ((((time_ms * speed) / 180) + (seed >> 5)) & 0x03u) == 0;

    CRGBPalette16 pal;
    bool have_pal = led_orchestra_palette(palette_ref, pal);
    uint8_t base_level = lo::scale8(scene.brightness, 26);
    CRGB base = have_pal ? lo::ColorFromPalette(pal, static_cast<uint8_t>(seed + global_index * 7), base_level)
                         : lo::hsv_to_rgb(lo::CHSV(static_cast<uint8_t>(seed + global_index * 5), 255, base_level));
    if (!active) {
        return base;
    }

    uint8_t flash_level = lo::scale8(scene.brightness, burst);
    CRGB flash = have_pal ? lo::ColorFromPalette(pal, static_cast<uint8_t>(seed + (time_ms * speed) / 9), flash_level)
                          : lo::hsv_to_rgb(lo::CHSV(static_cast<uint8_t>(seed + phase), 255, flash_level));
    return lo::blend(base, flash, burst);
}

CRGB render_champagne_toast(const LedOrchestraScene &scene, uint16_t global_index, uint64_t time_ms, uint8_t palette_ref)
{
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint8_t glow = lo::sin8(static_cast<uint8_t>((time_ms * speed) / 55 + (global_index * 5)));
    uint8_t sparkle_seed = hash8((static_cast<uint32_t>(global_index) * 0x27d4eb2du) ^ scene.sequence);
    uint8_t sparkle_wave = lo::sin8(static_cast<uint8_t>((time_ms * speed) / 26 + sparkle_seed));
    uint8_t base_level = lo::scale8(static_cast<uint8_t>(96 + (glow >> 1)), scene.brightness);

    CRGBPalette16 pal;
    bool have_pal = led_orchestra_palette(palette_ref, pal);
    CRGB base = have_pal ? lo::ColorFromPalette(pal, static_cast<uint8_t>(64 + (glow >> 1)), base_level)
                         : CRGB(base_level, lo::scale8(base_level, 86), lo::scale8(base_level, 72));
    if (sparkle_wave <= 220) {
        return base;
    }

    uint8_t glint = static_cast<uint8_t>((static_cast<uint16_t>(sparkle_wave - 220) * 255) / 35);
    uint8_t sparkle_level = lo::scale8(scene.brightness, glint);
    CRGB sparkle = have_pal ? lo::ColorFromPalette(pal, 244, sparkle_level)
                            : CRGB(sparkle_level, lo::scale8(sparkle_level, 235), lo::scale8(sparkle_level, 210));
    return lo::blend(base, sparkle, glint);
}

CRGB render_independence(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                         uint64_t time_ms, uint8_t palette_ref)
{
    uint16_t strip_len = std::max<uint16_t>(node.total_leds, 1);
    uint64_t speed = std::max<uint8_t>(scene.speed, 1);
    uint16_t scroll = static_cast<uint16_t>(((time_ms * speed) / 1800) % strip_len);
    uint16_t pos = static_cast<uint16_t>((global_index + scroll) % strip_len);
    uint8_t stripe = static_cast<uint8_t>((pos / 6) % 3);
    uint8_t shimmer = static_cast<uint8_t>(210 + (lo::sin8(static_cast<uint8_t>((time_ms * speed) / 48 + pos * 5)) >> 3));

    CRGBPalette16 pal;
    bool have_pal = led_orchestra_palette(palette_ref, pal);
    uint8_t palette_index = stripe == 0 ? 24 : (stripe == 1 ? 72 : 136);
    uint8_t level = lo::scale8(scene.brightness, shimmer);
    CRGB base = have_pal ? lo::ColorFromPalette(pal, palette_index, level)
                         : (stripe == 0 ? CRGB(30, 70, 220)
                                        : (stripe == 1 ? CRGB(255, 255, 255) : CRGB(220, 20, 38)));
    if (!have_pal) {
        base.nscale8(level);
    }

    uint8_t seed = hash8((static_cast<uint32_t>(global_index) * 0x9e3779b1u) ^ (scene.sequence * 29u));
    bool star_pixel = (seed & 0x0f) == 0;
    if (!star_pixel) {
        return base;
    }

    uint8_t star_phase = static_cast<uint8_t>((time_ms * speed) / 22 + seed);
    uint8_t star_wave = lo::ease8(lo::triangle8(star_phase));
    if (star_wave <= 190) {
        return base;
    }

    uint8_t glint = static_cast<uint8_t>((static_cast<uint16_t>(star_wave - 190) * 255) / 65);
    uint8_t star_level = lo::scale8(scene.brightness, glint);
    return lo::blend(base, CRGB(star_level, star_level, star_level), glint);
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
                                     uint16_t global_index, uint64_t time_ms, uint8_t palette_ref)
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
        return render_aurora_breathe(scene, global_index, time_ms, palette_ref);
    case effect::kEffectComet:
        return render_comet(scene, node, global_index, time_ms);
    case effect::kEffectTheaterChase:
        return render_theater_chase(scene, global_index, time_ms);
    case effect::kEffectPaletteCycle:
        return render_palette_cycle(scene, node, global_index, time_ms, palette_ref);
    case effect::kEffectTwinkle:
        return render_twinkle(scene, global_index, time_ms);
    case effect::kEffectOceanDrift:
        return render_ocean_drift(scene, node, global_index, time_ms, palette_ref);
    case effect::kEffectColorWave:
        return render_color_wave(scene, node, global_index, time_ms, palette_ref);
    case effect::kEffectPulseReveal:
        return render_pulse_reveal(scene, node, global_index, time_ms, palette_ref);
    case effect::kEffectCelebration:
        return render_celebration(scene, global_index, time_ms, palette_ref);
    case effect::kEffectIdentify:
        return render_identify(scene, node, global_index, time_ms);
    case effect::kEffectTidalSurge:
        return render_tidal_surge(scene, node, global_index, time_ms, palette_ref);
    case effect::kEffectPartyConfetti:
        return render_party_confetti(scene, global_index, time_ms, palette_ref);
    case effect::kEffectChampagneToast:
        return render_champagne_toast(scene, global_index, time_ms, palette_ref);
    case effect::kEffectIndependence:
        return render_independence(scene, node, global_index, time_ms, palette_ref);
    default:
        return CRGB(0, 0, 0);
    }
}
