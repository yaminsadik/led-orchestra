#pragma once

// FastLED-shaped color/effect engine for the LED Orchestra C6 LED nodes.
//
// Production intent: FastLED is the effect/color math engine on the LED nodes —
// CRGB/CHSV, palettes/gradients, blend/fade, and wave/easing helpers. FastLED's
// *physical-driver* path on ESP32-C6 + ESP-IDF v5.4.1 with ESP-Matter and
// OpenThread active is NOT yet hardware-proven, so the working physical output
// stays on the ESP-IDF `led_strip` RMT path. This header is the engine boundary:
//
//     Matter/Thread command -> renderer state -> THIS engine (color math)
//         -> existing led_strip RMT output
//
// Backends, selected at compile time so effects are written ONCE:
//
//   * CONFIG_LED_ORCHESTRA_USE_FASTLED=y — alias the `lo::` names to real
//     FastLED (<FastLED.h>). This is the hardware-gated spike; it requires
//     vendoring FastLED as an ESP-IDF component and a clean build on the pinned
//     toolchain with Matter/Thread/BLE/OTA all active (see docs/matter-thread.md
//     "FastLED Engine"). Do NOT flip this on until that spike passes.
//   * default (off) — a compact, dependency-free, FastLED-API-compatible
//     implementation backs the same names, so group control / durable config /
//     scheduling / OTA never block on the FastLED integration.
//
// Effects (led_orchestra_effects.cpp) use only the `lo::` names and the helpers
// below, so swapping the backend is a one-flag change, not an effect rewrite.

#include <stdint.h>

#include <sdkconfig.h>

#if defined(CONFIG_LED_ORCHESTRA_USE_FASTLED)

#include <FastLED.h>

namespace lo {
// Alias real FastLED so effects compile unchanged against the proven library.
using ::CHSV;
using ::CRGB;
using ::CRGBPalette16;
using ::blend;
using ::ColorFromPalette;
using ::cos8;
using ::nblend;
using ::scale8;
using ::scale8_video;
using ::sin8;
using ::triwave8;
inline uint8_t triangle8(uint8_t x) { return ::triwave8(x); }
inline uint8_t ease8(uint8_t x) { return ::ease8InOutQuad(x); }
} // namespace lo

#else // dependency-free FastLED-compatible backend

namespace lo {

// 8x8->8 scaling: i * scale / 256. Matches FastLED scale8 semantics.
static inline uint8_t scale8(uint8_t i, uint8_t scale)
{
    return static_cast<uint8_t>((static_cast<uint16_t>(i) * (static_cast<uint16_t>(scale) + 1)) >> 8);
}

// "video" variant: never scales a non-zero value to zero (keeps dim colors lit).
static inline uint8_t scale8_video(uint8_t i, uint8_t scale)
{
    uint8_t j = static_cast<uint8_t>((static_cast<uint16_t>(i) * scale) >> 8);
    return (i && scale) ? static_cast<uint8_t>(j + 1) : j;
}

static inline uint8_t qadd8(uint8_t a, uint8_t b)
{
    uint16_t sum = static_cast<uint16_t>(a) + b;
    return sum > 255 ? 255 : static_cast<uint8_t>(sum);
}

static inline uint8_t qsub8(uint8_t a, uint8_t b)
{
    return a > b ? static_cast<uint8_t>(a - b) : 0;
}

// Symmetric triangle wave: 0->255->0 across the input range.
static inline uint8_t triangle8(uint8_t x)
{
    return x < 128 ? static_cast<uint8_t>(x * 2) : static_cast<uint8_t>((255 - x) * 2);
}

// Smooth quadratic ease-in/out, FastLED ease8InOutQuad-compatible enough for
// breathing/pulse curves.
static inline uint8_t ease8(uint8_t i)
{
    uint8_t j = i;
    if (j & 0x80) {
        j = static_cast<uint8_t>(255 - j);
    }
    uint8_t jj = scale8(j, j);
    uint8_t jj2 = static_cast<uint8_t>(jj << 1);
    return (i & 0x80) ? static_cast<uint8_t>(255 - jj2) : jj2;
}

// 8-bit sine, period 256, range 0..255 centered at 128. Implemented from the
// quadratic ease over a triangle so it is smooth without a 256-byte table.
static inline uint8_t sin8(uint8_t theta)
{
    uint8_t offset = theta;
    if (theta & 0x40) {
        offset = static_cast<uint8_t>(255 - (theta - 1));
    }
    offset &= 0x7F; // 0..127
    uint8_t t = static_cast<uint8_t>(offset * 2);
    uint8_t eased = ease8(t);
    uint8_t y = scale8(eased, 255);
    return (theta & 0x80) ? static_cast<uint8_t>(128 - (y / 2) - (y / 2 > 128 ? 128 : 0))
                          : static_cast<uint8_t>(128 + (y / 2));
}

static inline uint8_t cos8(uint8_t theta) { return sin8(static_cast<uint8_t>(theta + 64)); }

struct CRGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    CRGB() : r(0), g(0), b(0) {}
    CRGB(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}

    // Scale every channel by `s` (FastLED nscale8 semantics).
    CRGB &nscale8(uint8_t s)
    {
        r = scale8(r, s);
        g = scale8(g, s);
        b = scale8(b, s);
        return *this;
    }

    CRGB &operator%=(uint8_t s) { return nscale8(s); }
};

struct CHSV {
    uint8_t h;
    uint8_t s;
    uint8_t v;
    CHSV() : h(0), s(0), v(0) {}
    CHSV(uint8_t hue, uint8_t sat, uint8_t val) : h(hue), s(sat), v(val) {}
};

// Rainbow HSV->RGB (saturated, perceptual-ish), FastLED hsv2rgb_rainbow-style.
static inline CRGB hsv_to_rgb(const CHSV &hsv)
{
    uint8_t hue = hsv.h;
    uint8_t sat = hsv.s;
    uint8_t val = hsv.v;

    uint8_t region = hue / 43;
    uint8_t remainder = static_cast<uint8_t>((hue - (region * 43)) * 6);

    uint8_t p = scale8(val, static_cast<uint8_t>(255 - sat));
    uint8_t q = scale8(val, static_cast<uint8_t>(255 - scale8(sat, remainder)));
    uint8_t t = scale8(val, static_cast<uint8_t>(255 - scale8(sat, static_cast<uint8_t>(255 - remainder))));

    switch (region) {
    case 0:
        return CRGB(val, t, p);
    case 1:
        return CRGB(q, val, p);
    case 2:
        return CRGB(p, val, t);
    case 3:
        return CRGB(p, q, val);
    case 4:
        return CRGB(t, p, val);
    default:
        return CRGB(val, p, q);
    }
}

// Per-channel linear blend from a to b by 0..255 fraction.
static inline CRGB blend(const CRGB &a, const CRGB &b, uint8_t frac)
{
    auto mix = [](uint8_t x, uint8_t y, uint8_t f) -> uint8_t {
        return static_cast<uint8_t>(scale8(x, static_cast<uint8_t>(255 - f)) + scale8(y, f));
    };
    return CRGB(mix(a.r, b.r, frac), mix(a.g, b.g, frac), mix(a.b, b.b, frac));
}

// Blend `overlay` into `base` in place by 0..255 fraction.
static inline CRGB &nblend(CRGB &base, const CRGB &overlay, uint8_t frac)
{
    base = blend(base, overlay, frac);
    return base;
}

// Minimal 16-entry gradient palette with linear interpolation, matching the
// shape of FastLED CRGBPalette16 + ColorFromPalette enough for our effects.
struct CRGBPalette16 {
    CRGB entries[16];
    CRGBPalette16() {}
    CRGBPalette16(const CRGB list[16])
    {
        for (int i = 0; i < 16; i++) {
            entries[i] = list[i];
        }
    }
};

static inline CRGB ColorFromPalette(const CRGBPalette16 &pal, uint8_t index, uint8_t brightness = 255)
{
    uint8_t hi = static_cast<uint8_t>(index >> 4);   // 0..15
    uint8_t lo = static_cast<uint8_t>(index & 0x0F); // 0..15
    uint8_t frac = static_cast<uint8_t>(lo << 4);    // scale to 0..240
    const CRGB &a = pal.entries[hi];
    const CRGB &b = pal.entries[(hi + 1) & 0x0F];
    CRGB c = blend(a, b, frac);
    if (brightness != 255) {
        c.nscale8(brightness);
    }
    return c;
}

} // namespace lo

#endif // CONFIG_LED_ORCHESTRA_USE_FASTLED

// ----------------------------------------------------------------------------
// Output policy — color correction / temperature / master brightness / power.
//
// These hooks live above the backend so they apply identically whether the math
// came from FastLED or the fallback. The renderer runs every rendered pixel
// through apply_output_policy() before handing bytes to led_strip.
// ----------------------------------------------------------------------------
namespace lo {

struct OutputPolicy {
    // Per-channel correction for the specific LED model/strip (FastLED-style
    // "color correction"); 0xFFFFFF = identity. Defaults approximate
    // TypicalLEDStrip and can later be made per-node config.
    CRGB color_correction = CRGB(0xFF, 0xFF, 0xFF);
    // White-point / temperature trim; 0xFFFFFF = identity.
    CRGB color_temperature = CRGB(0xFF, 0xFF, 0xFF);
    // Master brightness ceiling applied after the per-scene brightness, 0..255.
    uint8_t master_brightness = 255;

    // Power-budget hook (future field-policy). max_milliamps == 0 means "no
    // limit / not enforced yet". Enforcement needs whole-frame current
    // estimation; today master_brightness is the working knob and this carries
    // the budget so the renderer can adopt frame-level scaling without a wire
    // change. See compute_power_scale().
    uint16_t max_milliamps = 0;
    uint16_t milliamps_per_led_full_white = 60; // datasheet ~60 mA at full white
};

static inline CRGB apply_output_policy(CRGB c, const OutputPolicy &p)
{
    c.r = scale8(c.r, p.color_correction.r);
    c.g = scale8(c.g, p.color_correction.g);
    c.b = scale8(c.b, p.color_correction.b);
    c.r = scale8(c.r, p.color_temperature.r);
    c.g = scale8(c.g, p.color_temperature.g);
    c.b = scale8(c.b, p.color_temperature.b);
    if (p.master_brightness != 255) {
        c.nscale8(p.master_brightness);
    }
    return c;
}

// Future power-budget hook: given an accumulated frame current estimate and a
// budget, return a 0..255 scale to bring the frame under budget (255 = no
// scaling needed). Not yet wired into the per-pixel render loop; documented so
// power policy can be enforced without changing the effect math or wire format.
static inline uint8_t compute_power_scale(uint32_t estimated_milliamps, uint16_t budget_milliamps)
{
    if (budget_milliamps == 0 || estimated_milliamps <= budget_milliamps) {
        return 255;
    }
    return static_cast<uint8_t>((static_cast<uint32_t>(budget_milliamps) * 255) / estimated_milliamps);
}

} // namespace lo
