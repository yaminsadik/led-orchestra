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

#include "led_color.h"
#include "led_orchestra_config_store.h"
#include "led_orchestra_effects.h"
#include "led_orchestra_matter.h"

namespace {

static const char *TAG = "lo_renderer";
static constexpr TickType_t kFrameDelay = pdMS_TO_TICKS(16);

portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
led_strip_handle_t g_strip = nullptr;
TaskHandle_t g_task = nullptr;
int64_t g_clock_offset_ms = 0;

// Engine output policy (color correction / temperature / master brightness /
// power-budget hook). Defaults are identity-ish so the proven bench visuals are
// unchanged; this is the seam where per-node correction and power policy attach.
lo::OutputPolicy g_output_policy;

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

// A scheduled scene waits here until its synchronized start time, then the
// render task promotes it to g_scene. Until promotion the node keeps rendering
// the active g_scene (keep-last-valid: a scheduled change never blanks a running
// show). This is the per-node side of distribute-then-activate.
LedOrchestraScene g_pending = {};
bool g_has_pending = false;

LedOrchestraNodeConfig g_node = {
    .node_id = CONFIG_LED_ORCHESTRA_NODE_ID,
    .segment_start = CONFIG_LED_ORCHESTRA_SEGMENT_START,
    .segment_len = CONFIG_LED_ORCHESTRA_SEGMENT_LENGTH,
    .total_leds = CONFIG_LED_ORCHESTRA_TOTAL_LEDS,
    .led_gpio = CONFIG_LED_ORCHESTRA_LED_GPIO,
};

uint64_t monotonic_ms()
{
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

void render_task(void *)
{
    while (true) {
        LedOrchestraScene scene;
        LedOrchestraNodeConfig node;
        lo::OutputPolicy policy;
        int64_t offset;
        bool has_pending;
        LedOrchestraScene pending;

        portENTER_CRITICAL(&g_lock);
        scene = g_scene;
        node = g_node;
        policy = g_output_policy;
        offset = g_clock_offset_ms;
        has_pending = g_has_pending;
        pending = g_pending;
        portEXIT_CRITICAL(&g_lock);

        uint64_t now_ms = static_cast<uint64_t>(static_cast<int64_t>(monotonic_ms()) + offset);

        // Promote a scheduled scene once its synchronized start time arrives.
        uint32_t promoted_seq = 0;
        if (has_pending && now_ms >= pending.scheduled_start_ms) {
            portENTER_CRITICAL(&g_lock);
            // Re-check under lock; only promote the same pending scene (a newer
            // SetScene may have replaced it between our snapshot and here).
            if (g_has_pending && g_pending.sequence == pending.sequence) {
                g_scene = g_pending;
                g_has_pending = false;
                promoted_seq = g_pending.sequence;
            }
            scene = g_scene;
            portEXIT_CRITICAL(&g_lock);
        }
        if (promoted_seq != 0) {
            ESP_LOGI(TAG, "scheduled scene activated seq=%" PRIu32 " effect=%u", promoted_seq, scene.effect);
        }

        uint16_t segment_len = std::min<uint16_t>(node.segment_len, CONFIG_LED_ORCHESTRA_LED_COUNT);

        for (uint16_t local = 0; local < segment_len; local++) {
            uint16_t global = node.segment_start + local;
            lo::CRGB color = led_orchestra_render_effect(scene, node, global, now_ms);
            color = lo::apply_output_policy(color, policy);
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

    // Load durable segment config before the render task starts and before the
    // cluster seeds its attributes from get_node_config(), so a commissioned
    // node comes up already knowing its place in the virtual strip.
    {
        LedOrchestraNodeConfig loaded;
        bool from_nvs = false;
        led_orchestra_config_load(loaded, from_nvs);
        portENTER_CRITICAL(&g_lock);
        g_node = loaded;
        portEXIT_CRITICAL(&g_lock);
        ESP_LOGI(TAG, "config loaded from %s: node=%u segment=[%u,%u) total=%u gpio=%u",
                 from_nvs ? "NVS" : "defaults", loaded.node_id, loaded.segment_start,
                 loaded.segment_start + loaded.segment_len, loaded.total_leds, loaded.led_gpio);
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
    // Reject unknown effect ids via the append-only registry rather than a
    // hard-coded ceiling, so adding an effect id only touches the effects table.
    // A rejected command keeps the last valid scene running (keep-last-valid).
    if (led_orchestra_effect_meta(scene.effect) == nullptr) {
        ESP_LOGW(TAG, "rejecting unknown effect id %u; keeping last valid scene", scene.effect);
        return ESP_ERR_INVALID_ARG;
    }

    if (scene.scheduled_start_ms == 0) {
        // Apply immediately and clear any not-yet-due scheduled scene.
        portENTER_CRITICAL(&g_lock);
        g_scene = scene;
        g_has_pending = false;
        portEXIT_CRITICAL(&g_lock);
        ESP_LOGI(TAG, "scene seq=%" PRIu32 " effect=%u rgb=%u,%u,%u speed=%u brightness=%u start=now", scene.sequence,
                 scene.effect, scene.red, scene.green, scene.blue, scene.speed, scene.brightness);
    } else {
        // Stage as pending; the render task promotes it at the synchronized
        // start time. The active scene keeps rendering until then.
        portENTER_CRITICAL(&g_lock);
        g_pending = scene;
        g_has_pending = true;
        portEXIT_CRITICAL(&g_lock);
        ESP_LOGI(TAG, "scheduled scene accepted seq=%" PRIu32 " effect=%u start=%" PRIu64 " (offset-adjusted)",
                 scene.sequence, scene.effect, scene.scheduled_start_ms);
    }
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

    // Persist so the layout survives a power-cycle without re-provisioning. A
    // persistence failure is logged but does not fail the command: the live
    // render state is already updated (keep-last-valid), and the hub can
    // re-provision on the next boot if NVS is unwritable.
    esp_err_t persist = led_orchestra_config_save(config);
    if (persist != ESP_OK) {
        ESP_LOGW(TAG, "node config applied but not persisted (%s)", esp_err_to_name(persist));
    }
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
