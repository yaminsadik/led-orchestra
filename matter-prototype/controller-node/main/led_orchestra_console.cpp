#include "led_orchestra_console.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_check.h>
#include <esp_console.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_matter_controller_cluster_command.h>

#include "led_orchestra_matter.h"

namespace {

static const char *TAG = "lo_console";
static uint32_t g_sequence = 1;

bool parse_u64(const char *value, uint64_t &out)
{
    char *end = nullptr;
    errno = 0;
    unsigned long long parsed = strtoull(value, &end, 0);
    if (errno != 0 || end == value || *end != '\0') {
        return false;
    }
    out = static_cast<uint64_t>(parsed);
    return true;
}

bool parse_u32(const char *value, uint32_t &out)
{
    uint64_t parsed = 0;
    if (!parse_u64(value, parsed) || parsed > UINT32_MAX) {
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    return true;
}

bool parse_u16(const char *value, uint16_t &out)
{
    uint64_t parsed = 0;
    if (!parse_u64(value, parsed) || parsed > UINT16_MAX) {
        return false;
    }
    out = static_cast<uint16_t>(parsed);
    return true;
}

bool parse_u8(const char *value, uint8_t &out)
{
    uint64_t parsed = 0;
    if (!parse_u64(value, parsed) || parsed > UINT8_MAX) {
        return false;
    }
    out = static_cast<uint8_t>(parsed);
    return true;
}

bool parse_rgb(const char *hex, uint8_t &red, uint8_t &green, uint8_t &blue)
{
    if (strlen(hex) != 6) {
        return false;
    }

    char byte[3] = {0};
    char *end = nullptr;

    memcpy(byte, hex, 2);
    red = static_cast<uint8_t>(strtoul(byte, &end, 16));
    if (*end != '\0') {
        return false;
    }

    memcpy(byte, hex + 2, 2);
    green = static_cast<uint8_t>(strtoul(byte, &end, 16));
    if (*end != '\0') {
        return false;
    }

    memcpy(byte, hex + 4, 2);
    blue = static_cast<uint8_t>(strtoul(byte, &end, 16));
    return *end == '\0';
}

esp_err_t send_invoke(uint64_t destination, uint16_t endpoint, uint32_t command, const char *json)
{
    ESP_LOGI(TAG, "invoke destination=0x%" PRIX64 " endpoint=%u cluster=0x%" PRIX32 " command=0x%" PRIX32 " %s",
             destination, endpoint, led_orchestra::matter::kClusterId, command, json);
    return esp_matter::controller::send_invoke_cluster_command(destination, endpoint, led_orchestra::matter::kClusterId,
                                                               command, json);
}

int set_scene_handler(int argc, char **argv)
{
    // esp_console passes the command name as argv[0]; drop it so the positional
    // parsing below is 0-based over the real arguments.
    argc--;
    argv++;

    if (argc < 6 || argc > 8) {
        ESP_LOGE(TAG, "usage: lo-set-scene <node-id|group-id> <endpoint-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]");
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t destination = 0;
    uint16_t endpoint = 0;
    uint8_t effect = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t speed = 0;
    uint8_t brightness = 0;
    uint32_t sequence = g_sequence++;
    uint64_t scheduled_start_ms = 0;

    if (!parse_u64(argv[0], destination) || !parse_u16(argv[1], endpoint) || !parse_u8(argv[2], effect) ||
        !parse_rgb(argv[3], red, green, blue) || !parse_u8(argv[4], speed) || !parse_u8(argv[5], brightness)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (argc >= 7 && !parse_u32(argv[6], sequence)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (argc >= 8 && !parse_u64(argv[7], scheduled_start_ms)) {
        return ESP_ERR_INVALID_ARG;
    }

    char json[192];
    int len = snprintf(json, sizeof(json),
                       "{\"0:U8\":%u,\"1:U8\":%u,\"2:U8\":%u,\"3:U8\":%u,\"4:U8\":%u,\"5:U8\":%u,\"6:U32\":%" PRIu32 ",\"7:U64\":%" PRIu64 "}",
                       effect, red, green, blue, speed, brightness, sequence, scheduled_start_ms);
    if (len <= 0 || len >= static_cast<int>(sizeof(json))) {
        return ESP_ERR_NO_MEM;
    }

    return send_invoke(destination, endpoint, led_orchestra::matter::command::kSetScene, json);
}

int set_node_config_handler(int argc, char **argv)
{
    // esp_console passes the command name as argv[0]; drop it so the positional
    // parsing below is 0-based over the real arguments.
    argc--;
    argv++;

    if (argc != 7) {
        ESP_LOGE(TAG, "usage: lo-set-node-config <node-id> <endpoint-id> <orchestra-node-id> <segment-start> <segment-len> <total-leds> <led-gpio>");
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t destination = 0;
    uint16_t endpoint = 0;
    uint16_t orchestra_node_id = 0;
    uint16_t segment_start = 0;
    uint16_t segment_len = 0;
    uint16_t total_leds = 0;
    uint8_t led_gpio = 0;

    if (!parse_u64(argv[0], destination) || !parse_u16(argv[1], endpoint) || !parse_u16(argv[2], orchestra_node_id) ||
        !parse_u16(argv[3], segment_start) || !parse_u16(argv[4], segment_len) || !parse_u16(argv[5], total_leds) ||
        !parse_u8(argv[6], led_gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    char json[160];
    int len = snprintf(json, sizeof(json), "{\"0:U16\":%u,\"1:U16\":%u,\"2:U16\":%u,\"3:U16\":%u,\"4:U8\":%u}",
                       orchestra_node_id, segment_start, segment_len, total_leds, led_gpio);
    if (len <= 0 || len >= static_cast<int>(sizeof(json))) {
        return ESP_ERR_NO_MEM;
    }

    return send_invoke(destination, endpoint, led_orchestra::matter::command::kSetNodeConfig, json);
}

int sync_clock_handler(int argc, char **argv)
{
    // esp_console passes the command name as argv[0]; drop it so the positional
    // parsing below is 0-based over the real arguments.
    argc--;
    argv++;

    if (argc < 2 || argc > 3) {
        ESP_LOGE(TAG, "usage: lo-sync-clock <node-id|group-id> <endpoint-id> [controller-time-ms]");
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t destination = 0;
    uint16_t endpoint = 0;
    uint64_t controller_time_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);

    if (!parse_u64(argv[0], destination) || !parse_u16(argv[1], endpoint)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (argc == 3 && !parse_u64(argv[2], controller_time_ms)) {
        return ESP_ERR_INVALID_ARG;
    }

    char json[64];
    int len = snprintf(json, sizeof(json), "{\"0:U64\":%" PRIu64 "}", controller_time_ms);
    if (len <= 0 || len >= static_cast<int>(sizeof(json))) {
        return ESP_ERR_NO_MEM;
    }

    return send_invoke(destination, endpoint, led_orchestra::matter::command::kSyncClock, json);
}

} // namespace

esp_err_t led_orchestra_console_register_commands()
{
    const esp_console_cmd_t set_scene = {
        .command = "lo-set-scene",
        .help = "Send LED Orchestra SetScene: <node|group> <endpoint> <effect> <rrggbb> <speed> <brightness> [sequence] [start-ms]",
        .hint = nullptr,
        .func = &set_scene_handler,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&set_scene), TAG, "failed to register lo-set-scene");

    const esp_console_cmd_t set_node_config = {
        .command = "lo-set-node-config",
        .help = "Send LED Orchestra SetNodeConfig: <node> <endpoint> <node-id> <start> <len> <total> <gpio>",
        .hint = nullptr,
        .func = &set_node_config_handler,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&set_node_config), TAG, "failed to register lo-set-node-config");

    const esp_console_cmd_t sync_clock = {
        .command = "lo-sync-clock",
        .help = "Send LED Orchestra SyncClock: <node|group> <endpoint> [controller-time-ms]",
        .hint = nullptr,
        .func = &sync_clock_handler,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&sync_clock), TAG, "failed to register lo-sync-clock");

    return ESP_OK;
}
