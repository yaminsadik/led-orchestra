#include "led_orchestra_renderer.h"

#include <algorithm>
#include <inttypes.h>

#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <led_strip.h>
#include <sdkconfig.h>

#include "led_orchestra_matter.h"

namespace {

static const char *TAG = "lo_renderer";
static constexpr TickType_t kFrameDelay = pdMS_TO_TICKS(16);

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
led_strip_handle_t g_strip = nullptr;
TaskHandle_t g_task = nullptr;
int64_t g_clock_offset_ms = 0;

LedOrchestraScene g_scene = {
    .effect = led_orchestra::matter::kEffectRainbow,
    .red = 0,
    .green = 0,
    .blue = 0,
    .speed = 128,
    .brightness = 40,
    .sequence = 0,
    .scheduled_start_ms = 0,
};

LedOrchestraNodeConfig g_node = {
    .node_id = CONFIG_LED_ORCHESTRA_NODE_ID,
    .segment_start = CONFIG_LED_ORCHESTRA_SEGMENT_START,
    .segment_len = CONFIG_LED_ORCHESTRA_SEGMENT_LENGTH,
    .total_leds = CONFIG_LED_ORCHESTRA_TOTAL_LEDS,
    .led_gpio = CONFIG_LED_ORCHESTRA_LED_GPIO,
};

Rgb scale(Rgb color, uint8_t brightness)
{
    return {
        .r = static_cast<uint8_t>((static_cast<uint16_t>(color.r) * brightness) / 255),
        .g = static_cast<uint8_t>((static_cast<uint16_t>(color.g) * brightness) / 255),
        .b = static_cast<uint8_t>((static_cast<uint16_t>(color.b) * brightness) / 255),
    };
}

Rgb hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v)
{
    if (s == 0) {
        return {.r = v, .g = v, .b = v};
    }

    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;
    uint8_t p = static_cast<uint8_t>((static_cast<uint16_t>(v) * (255 - s)) / 255);
    uint8_t q = static_cast<uint8_t>((static_cast<uint16_t>(v) * (255 - ((static_cast<uint16_t>(s) * remainder) / 255))) / 255);
    uint8_t t = static_cast<uint8_t>((static_cast<uint16_t>(v) * (255 - ((static_cast<uint16_t>(s) * (255 - remainder)) / 255))) / 255);

    switch (region) {
    case 0:
        return {.r = v, .g = t, .b = p};
    case 1:
        return {.r = q, .g = v, .b = p};
    case 2:
        return {.r = p, .g = v, .b = t};
    case 3:
        return {.r = p, .g = q, .b = v};
    case 4:
        return {.r = t, .g = p, .b = v};
    default:
        return {.r = v, .g = p, .b = q};
    }
}

// Fibonacci sequence starting (1, 2, 3, 5, 8, ...) mod 256, tiled using the
// Pisano period. Seeded from (1,2) so the first values are the visible Fibonacci
// numbers 1,2,3,5,8,13,21... rather than starting with 0.
// Built once on first use; the render task is the only caller.
uint8_t fib_mod256(uint32_t n)
{
    static constexpr uint16_t kPisano256 = 384;
    static uint8_t table[kPisano256];
    static bool initialized = false;
    if (!initialized) {
        table[0] = 1;   // F(0)=1
        table[1] = 2;   // F(1)=2  → 1,2,3,5,8,13,...
        for (uint16_t i = 2; i < kPisano256; i++) {
            table[i] = static_cast<uint8_t>(table[i - 1] + table[i - 2]);
        }
        initialized = true;
    }
    return table[n % kPisano256];
}

Rgb render_pixel(const LedOrchestraScene &scene, const LedOrchestraNodeConfig &node, uint16_t global_index,
                 uint64_t time_ms)
{
    switch (scene.effect) {
    case led_orchestra::matter::kEffectOff:
        return {.r = 0, .g = 0, .b = 0};
    case led_orchestra::matter::kEffectSolid:
        return scale({.r = scene.red, .g = scene.green, .b = scene.blue}, scene.brightness);
    case led_orchestra::matter::kEffectRainbow: {
        uint64_t speed = std::max<uint8_t>(scene.speed, 1);
        uint32_t time_hue = static_cast<uint32_t>((time_ms * speed) / 20);
        uint32_t pos_hue = (static_cast<uint32_t>(global_index) * 256) / std::max<uint16_t>(node.total_leds, 1);
        return scale(hsv_to_rgb(static_cast<uint8_t>((time_hue + pos_hue) & 0xff), 255, 255), scene.brightness);
    }
    case led_orchestra::matter::kEffectFibonacci: {
        // Scroll rate: speed=10 → 1 px/s; speed=0 → static gradient.
        // Every 5 scroll ticks the strip drifts 1 extra LED so the
        // pattern gently shifts phase and circles back end→start.
        uint64_t speed = scene.speed;
        uint32_t strip_len = std::max<uint16_t>(node.total_leds, 1);
        uint32_t scroll = (speed == 0) ? 0 : static_cast<uint32_t>((time_ms * speed) / 10000);
        uint32_t drift  = scroll / 5;
        // Wrap so the pattern circles continuously across the full strip.
        uint32_t pos = (static_cast<uint32_t>(global_index) + scroll + drift) % strip_len;

        // Every 11 positions step back 2 in the Fibonacci sequence and rotate
        // the RGB channel assignment by 1. This creates overlapping colour bands
        // so adjacent segments share Fibonacci values (the "overlap" between the
        // R/G/B of neighbouring pixels). Net advance per 11-pixel group: 11-2=9.
        uint32_t gobacks   = pos / 11;
        uint32_t fib_base  = pos - gobacks * 2;           // net Fibonacci index
        uint8_t  rgb_shift = static_cast<uint8_t>(gobacks % 3);

        uint8_t channels[3] = {
            fib_mod256(fib_base),
            fib_mod256(fib_base + 1),
            fib_mod256(fib_base + 2),
        };
        Rgb color = {
            .r = channels[rgb_shift % 3],
            .g = channels[(rgb_shift + 1) % 3],
            .b = channels[(rgb_shift + 2) % 3],
        };
        return scale(color, scene.brightness);
    }
    default:
        return {.r = 0, .g = 0, .b = 0};
    }
}

uint64_t monotonic_ms()
{
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

void render_task(void *)
{
    while (true) {
        LedOrchestraScene scene;
        LedOrchestraNodeConfig node;
        int64_t offset;

        portENTER_CRITICAL(&g_lock);
        scene = g_scene;
        node = g_node;
        offset = g_clock_offset_ms;
        portEXIT_CRITICAL(&g_lock);

        uint64_t now_ms = static_cast<uint64_t>(static_cast<int64_t>(monotonic_ms()) + offset);
        uint16_t segment_len = std::min<uint16_t>(node.segment_len, CONFIG_LED_ORCHESTRA_LED_COUNT);

        for (uint16_t local = 0; local < segment_len; local++) {
            uint16_t global = node.segment_start + local;
            Rgb color = (scene.scheduled_start_ms == 0 || now_ms >= scene.scheduled_start_ms)
                            ? render_pixel(scene, node, global, now_ms)
                            : Rgb{.r = 0, .g = 0, .b = 0};
            // Bench strip is a 12V WS2815 wired in RGB wire order, but the
            // led_strip driver only emits GRB. Swap R/G so colors are correct
            // on the wire (blue is identical in both orders).
            led_strip_set_pixel(g_strip, local, color.g, color.r, color.b);
        }

        for (uint16_t local = segment_len; local < CONFIG_LED_ORCHESTRA_LED_COUNT; local++) {
            led_strip_set_pixel(g_strip, local, 0, 0, 0);
        }

        led_strip_refresh(g_strip);
        vTaskDelay(kFrameDelay);
    }
}

} // namespace

esp_err_t led_orchestra_renderer_start()
{
    if (g_strip != nullptr) {
        return ESP_OK;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_LED_ORCHESTRA_LED_GPIO,
        .max_leds = CONFIG_LED_ORCHESTRA_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        },
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &g_strip), TAG,
                        "failed to initialize WS2812 strip");
    ESP_RETURN_ON_ERROR(led_strip_clear(g_strip), TAG, "failed to clear WS2812 strip");

    BaseType_t ok = xTaskCreate(render_task, "lo_render", 4096, nullptr, 5, &g_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "failed to start renderer task");

    ESP_LOGI(TAG, "renderer started: gpio=%d leds=%d", CONFIG_LED_ORCHESTRA_LED_GPIO, CONFIG_LED_ORCHESTRA_LED_COUNT);
    return ESP_OK;
}

esp_err_t led_orchestra_set_scene(const LedOrchestraScene &scene)
{
    if (scene.effect > led_orchestra::matter::kEffectFibonacci) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_lock);
    g_scene = scene;
    portEXIT_CRITICAL(&g_lock);

    ESP_LOGI(TAG, "scene seq=%" PRIu32 " effect=%u rgb=%u,%u,%u speed=%u brightness=%u start=%" PRIu64,
             scene.sequence, scene.effect, scene.red, scene.green, scene.blue, scene.speed, scene.brightness,
             scene.scheduled_start_ms);
    return ESP_OK;
}

esp_err_t led_orchestra_set_node_config(const LedOrchestraNodeConfig &config)
{
    if (config.segment_len == 0 || config.total_leds == 0 || config.led_gpio != CONFIG_LED_ORCHESTRA_LED_GPIO) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&g_lock);
    g_node = config;
    portEXIT_CRITICAL(&g_lock);

    ESP_LOGI(TAG, "node config node=%u segment=[%u,%u) total=%u gpio=%u", config.node_id, config.segment_start,
             config.segment_start + config.segment_len, config.total_leds, config.led_gpio);
    return ESP_OK;
}

void led_orchestra_sync_clock(uint64_t controller_time_ms)
{
    portENTER_CRITICAL(&g_lock);
    g_clock_offset_ms = static_cast<int64_t>(controller_time_ms) - static_cast<int64_t>(monotonic_ms());
    portEXIT_CRITICAL(&g_lock);
    ESP_LOGI(TAG, "clock sync controller_ms=%" PRIu64 " offset_ms=%" PRId64, controller_time_ms, g_clock_offset_ms);
}

LedOrchestraScene led_orchestra_get_scene()
{
    portENTER_CRITICAL(&g_lock);
    LedOrchestraScene scene = g_scene;
    portEXIT_CRITICAL(&g_lock);
    return scene;
}

LedOrchestraNodeConfig led_orchestra_get_node_config()
{
    portENTER_CRITICAL(&g_lock);
    LedOrchestraNodeConfig config = g_node;
    portEXIT_CRITICAL(&g_lock);
    return config;
}
