#pragma once

#include <esp_err.h>
#include <stdint.h>

struct LedOrchestraScene {
    uint8_t effect;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t speed;
    uint8_t brightness;
    uint32_t sequence;
    uint64_t scheduled_start_ms;
};

struct LedOrchestraNodeConfig {
    uint16_t node_id;
    uint16_t segment_start;
    uint16_t segment_len;
    uint16_t total_leds;
    uint8_t led_gpio;
};

// Sentinel for "no palette override; use the effect's compiled default palette".
// Mirrors led_orchestra::matter::kPaletteOverrideNone so renderer.h has no
// dependency on the cluster header or the effect registry.
static constexpr uint8_t kCalibNoPaletteOverride = 0xFF;

// Field calibration — per-node runtime tuning delivered as DATA (SetCalibration),
// not firmware. These are the knobs an installer tweaks after the strips are on
// the wall: synchronized timing alignment, brightness ceiling, palette choice,
// and LED color correction. Defaults below are identity (no change vs. an
// un-calibrated node), so an existing install behaves exactly as before until a
// SetCalibration arrives. Persisted in NVS; reloaded at boot.
struct LedOrchestraCalibration {
    // Signed ms added to this node's render clock. Shifts where this segment sits
    // in a synchronized animation so a wave/comet can be aligned across physical
    // gaps and bends. Affects effect math only, never scheduled-scene activation.
    int32_t time_offset_ms;
    // Master-brightness ceiling 0..255 (255 = no cap), applied after per-scene
    // brightness. Field-tunable per hole/zone.
    uint8_t brightness_cap;
    // Palette ref replacing the effect's default for palette-driven effects;
    // kCalibNoPaletteOverride (0xFF) = use the effect default.
    uint8_t palette_override;
    // 0x00RRGGBB per-channel LED correction / white-point (0x00FFFFFF = identity).
    uint32_t color_correction;
    uint32_t color_temperature;
};

esp_err_t led_orchestra_renderer_start();
esp_err_t led_orchestra_set_scene(const LedOrchestraScene &scene);
esp_err_t led_orchestra_set_node_config(const LedOrchestraNodeConfig &config);
esp_err_t led_orchestra_set_calibration(const LedOrchestraCalibration &calibration);
void led_orchestra_sync_clock(uint64_t controller_time_ms);
LedOrchestraScene led_orchestra_get_scene();
LedOrchestraNodeConfig led_orchestra_get_node_config();
LedOrchestraCalibration led_orchestra_get_calibration();
