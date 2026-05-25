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

esp_err_t led_orchestra_renderer_start();
esp_err_t led_orchestra_set_scene(const LedOrchestraScene &scene);
esp_err_t led_orchestra_set_node_config(const LedOrchestraNodeConfig &config);
void led_orchestra_sync_clock(uint64_t controller_time_ms);
LedOrchestraScene led_orchestra_get_scene();
LedOrchestraNodeConfig led_orchestra_get_node_config();
