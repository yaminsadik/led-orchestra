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

#include <lib/core/NodeId.h>

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

// Send a cluster invoke to a unicast NodeId or a Matter group NodeId. When
// `destination` is a group NodeId (>= 0xFFFFFFFFFFFF0000) the SDK ignores the
// endpoint and dispatches a groupcast; otherwise it is a unicast invoke.
esp_err_t send_invoke(uint64_t destination, uint16_t endpoint, uint32_t cluster, uint32_t command, const char *json)
{
    ESP_LOGI(TAG, "invoke destination=0x%" PRIX64 " (%s) endpoint=%u cluster=0x%" PRIX32 " command=0x%" PRIX32 " %s",
             destination, chip::IsGroupId(destination) ? "group" : "unicast", endpoint, cluster, command,
             json ? json : "{}");
    return esp_matter::controller::send_invoke_cluster_command(destination, endpoint, cluster, command, json);
}

// Convenience overload for the LED Orchestra custom cluster (the common case).
esp_err_t send_invoke(uint64_t destination, uint16_t endpoint, uint32_t command, const char *json)
{
    return send_invoke(destination, endpoint, led_orchestra::matter::kClusterId, command, json);
}

// Build the SetScene custom-cluster JSON payload shared by the unicast and
// groupcast handlers. Returns false if the buffer is too small. The tag/type
// layout is the append-only wire contract in cluster/led-orchestra.md.
bool build_set_scene_json(char *json, size_t json_len, uint8_t effect, uint8_t red, uint8_t green, uint8_t blue,
                          uint8_t speed, uint8_t brightness, uint32_t sequence, uint64_t scheduled_start_ms)
{
    int len = snprintf(json, json_len,
                       "{\"0:U8\":%u,\"1:U8\":%u,\"2:U8\":%u,\"3:U8\":%u,\"4:U8\":%u,\"5:U8\":%u,\"6:U32\":%" PRIu32
                       ",\"7:U64\":%" PRIu64 "}",
                       effect, red, green, blue, speed, brightness, sequence, scheduled_start_ms);
    return len > 0 && len < static_cast<int>(json_len);
}

int set_scene_handler(int argc, char **argv)
{
    // esp_console passes the command name as argv[0]; drop it so the positional
    // parsing below is 0-based over the real arguments.
    argc--;
    argv++;

    if (argc < 6 || argc > 8) {
        ESP_LOGE(TAG, "usage: lo-set-scene <node-id> <endpoint-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]");
        ESP_LOGE(TAG, "  unicast only; for a whole group use lo-set-scene-group <group-id> ...");
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
    if (!build_set_scene_json(json, sizeof(json), effect, red, green, blue, speed, brightness, sequence,
                              scheduled_start_ms)) {
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
        ESP_LOGE(TAG, "usage: lo-sync-clock <node-id> <endpoint-id> [controller-time-ms]");
        ESP_LOGE(TAG, "  unicast only; for a whole group use lo-sync-clock-group <group-id> [controller-time-ms]");
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

// ---------------------------------------------------------------------------
// Phase 5 — real Matter group control.
//
// A group destination is NOT a small id: in CHIP a group is addressed as a
// NodeId in the range 0xFFFFFFFFFFFF0000..0xFFFFFFFFFFFFFFFF. An application
// group id `g` (0x0001..0xFEFF) is encoded with chip::NodeIdFromGroupId(g);
// send_invoke_cluster_command() then dispatches a groupcast because
// chip::IsGroupId(dest) is true. Passing a bare `0x0001` to a unicast command
// would instead target *unicast node 1*, which is the trap this avoids.
//
// Before groupcasts are accepted by nodes, the fabric needs group keys in place
// (controller-side: `controller group-settings add-keyset/bind-keyset/add-group`;
// node-side: KeySetWrite + GroupKeyMap on each node). `lo-show-group-help`
// prints the full one-time sequence; see docs/console.md.
// ---------------------------------------------------------------------------

// lo-add-group <node-id> <endpoint-id> [group-id] [group-name]
// Enroll one node endpoint into an application group via the standard Groups
// cluster (0x0004) AddGroup command. Unicast (per node), reliable. Membership is
// what makes a later groupcast reach this endpoint.
int add_group_handler(int argc, char **argv)
{
    argc--;
    argv++;

    if (argc < 2 || argc > 4) {
        ESP_LOGE(TAG, "usage: lo-add-group <node-id> <endpoint-id> [group-id] [group-name]");
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t node_id = 0;
    uint16_t endpoint = 0;
    uint16_t group_id = led_orchestra::matter::kGroupAllNodes;
    const char *group_name = "orchestra";

    if (!parse_u64(argv[0], node_id) || !parse_u16(argv[1], endpoint)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (argc >= 3 && !parse_u16(argv[2], group_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (argc >= 4) {
        group_name = argv[3];
    }
    if (strlen(group_name) > 16) {
        ESP_LOGE(TAG, "group name must be <= 16 chars (Matter Groups cluster limit)");
        return ESP_ERR_INVALID_ARG;
    }

    char json[64];
    int len = snprintf(json, sizeof(json), "{\"0:U16\":%u,\"1:STR\":\"%s\"}", group_id, group_name);
    if (len <= 0 || len >= static_cast<int>(sizeof(json))) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "enrolling node 0x%" PRIX64 " endpoint %u into group 0x%04X (%s)", node_id, endpoint, group_id,
             group_name);
    return send_invoke(node_id, endpoint, led_orchestra::matter::groups_cluster::kClusterId,
                       led_orchestra::matter::groups_cluster::kCommandAddGroup, json);
}

// lo-set-scene-group <group-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]
// Groupcast SetScene to every node enrolled in the application group.
int set_scene_group_handler(int argc, char **argv)
{
    argc--;
    argv++;

    if (argc < 5 || argc > 7) {
        ESP_LOGE(TAG, "usage: lo-set-scene-group <group-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]");
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t group_id = 0;
    uint8_t effect = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t speed = 0;
    uint8_t brightness = 0;
    uint32_t sequence = g_sequence++;
    uint64_t scheduled_start_ms = 0;

    if (!parse_u16(argv[0], group_id) || !parse_u8(argv[1], effect) || !parse_rgb(argv[2], red, green, blue) ||
        !parse_u8(argv[3], speed) || !parse_u8(argv[4], brightness)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (argc >= 6 && !parse_u32(argv[5], sequence)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (argc >= 7 && !parse_u64(argv[6], scheduled_start_ms)) {
        return ESP_ERR_INVALID_ARG;
    }

    char json[192];
    if (!build_set_scene_json(json, sizeof(json), effect, red, green, blue, speed, brightness, sequence,
                              scheduled_start_ms)) {
        return ESP_ERR_NO_MEM;
    }

    uint64_t destination = chip::NodeIdFromGroupId(group_id);
    return send_invoke(destination, led_orchestra::matter::kEndpointIdHint, led_orchestra::matter::command::kSetScene,
                       json);
}

// lo-sync-clock-group <group-id> [controller-time-ms]
// Groupcast SyncClock; defaults to controller uptime in ms.
int sync_clock_group_handler(int argc, char **argv)
{
    argc--;
    argv++;

    if (argc < 1 || argc > 2) {
        ESP_LOGE(TAG, "usage: lo-sync-clock-group <group-id> [controller-time-ms]");
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t group_id = 0;
    uint64_t controller_time_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);

    if (!parse_u16(argv[0], group_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (argc == 2 && !parse_u64(argv[1], controller_time_ms)) {
        return ESP_ERR_INVALID_ARG;
    }

    char json[64];
    int len = snprintf(json, sizeof(json), "{\"0:U64\":%" PRIu64 "}", controller_time_ms);
    if (len <= 0 || len >= static_cast<int>(sizeof(json))) {
        return ESP_ERR_NO_MEM;
    }

    uint64_t destination = chip::NodeIdFromGroupId(group_id);
    return send_invoke(destination, led_orchestra::matter::kEndpointIdHint, led_orchestra::matter::command::kSyncClock,
                       json);
}

// lo-scheduled-scene-group <group-id> <delay-ms> <effect-id> <rrggbb> <speed> <brightness> [sequence]
// Compute (controller uptime + delay) and groupcast a scheduled SetScene so all
// segments flip together at the same synchronized local time. Pair with a recent
// lo-sync-clock-group so every node's clock offset is aligned first.
int scheduled_scene_group_handler(int argc, char **argv)
{
    argc--;
    argv++;

    if (argc < 6 || argc > 7) {
        ESP_LOGE(TAG, "usage: lo-scheduled-scene-group <group-id> <delay-ms> <effect-id> <rrggbb> <speed> <brightness> [sequence]");
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t group_id = 0;
    uint64_t delay_ms = 0;
    uint8_t effect = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t speed = 0;
    uint8_t brightness = 0;
    uint32_t sequence = g_sequence++;

    if (!parse_u16(argv[0], group_id) || !parse_u64(argv[1], delay_ms) || !parse_u8(argv[2], effect) ||
        !parse_rgb(argv[3], red, green, blue) || !parse_u8(argv[4], speed) || !parse_u8(argv[5], brightness)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (argc >= 7 && !parse_u32(argv[6], sequence)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    uint64_t scheduled_start_ms = now_ms + delay_ms;

    char json[192];
    if (!build_set_scene_json(json, sizeof(json), effect, red, green, blue, speed, brightness, sequence,
                              scheduled_start_ms)) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "scheduled group scene: now=%" PRIu64 "ms +%" PRIu64 "ms -> start=%" PRIu64 "ms", now_ms, delay_ms,
             scheduled_start_ms);
    uint64_t destination = chip::NodeIdFromGroupId(group_id);
    return send_invoke(destination, led_orchestra::matter::kEndpointIdHint, led_orchestra::matter::command::kSetScene,
                       json);
}

// lo-show-group-help — print the one-time group-enablement sequence so the
// operator does not have to reconstruct the Matter group key dance from memory.
int show_group_help_handler(int, char **)
{
    const uint16_t g = led_orchestra::matter::kGroupAllNodes;
    const uint16_t ks = led_orchestra::matter::group_key::kDefaultKeysetId;
    printf("\n");
    printf("LED Orchestra group control — one-time setup (all-nodes group 0x%04X):\n", g);
    printf("\n");
    printf("  Controller-side group keyset (esp-matter built-ins, run once):\n");
    printf("    matter esp controller group-settings add-keyset 0x%04X 0 0xFFFFFFFFFFFFFFFF <32-hex-epoch-key>\n", ks);
    printf("    matter esp controller group-settings bind-keyset 0x%04X 0x%04X\n", g, ks);
    printf("    matter esp controller group-settings add-group   0x%04X orchestra\n", g);
    printf("\n");
    printf("  Per node (after commissioning) — install the same key + map, then enroll the endpoint:\n");
    printf("    matter esp controller invoke-cmd <node> 0 0x003F 0x00 \"<KeySetWrite GroupKeySetStruct>\"\n");
    printf("    matter esp controller write-attr <node> 0 0x003F 0x00 \"[{...GroupKeyMapStruct...}]\"\n");
    printf("    lo-add-group <node> %u 0x%04X orchestra\n", led_orchestra::matter::kEndpointIdHint, g);
    printf("\n");
    printf("  Then drive every node with one command:\n");
    printf("    lo-set-scene-group 0x%04X <effect> <rrggbb> <speed> <brightness>\n", g);
    printf("    lo-sync-clock-group 0x%04X\n", g);
    printf("    lo-scheduled-scene-group 0x%04X <delay-ms> <effect> <rrggbb> <speed> <brightness>\n", g);
    printf("\n");
    printf("  The node-side KeySetWrite/GroupKeyMap step is the part to confirm on hardware;\n");
    printf("  see docs/console.md (Group Control) for the exact GroupKeySetStruct payload.\n");
    printf("\n");
    return ESP_OK;
}

} // namespace

esp_err_t led_orchestra_console_register_commands()
{
    const esp_console_cmd_t set_scene = {
        .command = "lo-set-scene",
        .help = "Send LED Orchestra SetScene (unicast): <node> <endpoint> <effect> <rrggbb> <speed> <brightness> [sequence] [start-ms]",
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
        .help = "Send LED Orchestra SyncClock (unicast): <node> <endpoint> [controller-time-ms]",
        .hint = nullptr,
        .func = &sync_clock_handler,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&sync_clock), TAG, "failed to register lo-sync-clock");

    const esp_console_cmd_t add_group = {
        .command = "lo-add-group",
        .help = "Enroll a node endpoint into a group (Groups AddGroup): <node> <endpoint> [group-id] [group-name]",
        .hint = nullptr,
        .func = &add_group_handler,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&add_group), TAG, "failed to register lo-add-group");

    const esp_console_cmd_t set_scene_group = {
        .command = "lo-set-scene-group",
        .help = "Groupcast SetScene: <group-id> <effect> <rrggbb> <speed> <brightness> [sequence] [start-ms]",
        .hint = nullptr,
        .func = &set_scene_group_handler,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&set_scene_group), TAG, "failed to register lo-set-scene-group");

    const esp_console_cmd_t sync_clock_group = {
        .command = "lo-sync-clock-group",
        .help = "Groupcast SyncClock: <group-id> [controller-time-ms]",
        .hint = nullptr,
        .func = &sync_clock_group_handler,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&sync_clock_group), TAG, "failed to register lo-sync-clock-group");

    const esp_console_cmd_t scheduled_scene_group = {
        .command = "lo-scheduled-scene-group",
        .help = "Groupcast scheduled SetScene at uptime+delay: <group-id> <delay-ms> <effect> <rrggbb> <speed> <brightness> [sequence]",
        .hint = nullptr,
        .func = &scheduled_scene_group_handler,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&scheduled_scene_group), TAG,
                        "failed to register lo-scheduled-scene-group");

    const esp_console_cmd_t show_group_help = {
        .command = "lo-show-group-help",
        .help = "Print the one-time Matter group key + enrollment setup sequence",
        .hint = nullptr,
        .func = &show_group_help_handler,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&show_group_help), TAG, "failed to register lo-show-group-help");

    return ESP_OK;
}
