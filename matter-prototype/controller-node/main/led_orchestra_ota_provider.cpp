#include "led_orchestra_ota_provider.h"

#include <sdkconfig.h>

#if CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_console.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_ota_provider.h>

#include <app/clusters/ota-provider/ota-provider-cluster.h>
#include <app/server/Server.h>
#include <lib/core/ScopedNodeId.h>

using esp_matter::ota_provider::EspOtaProvider;

namespace {

static const char *TAG = "lo_ota";

// Development fabric index. The commissioner is initialized on fabric index 1
// (see controller.init in app_main.cpp / docs/console.md). Per-node OTA allow
// flags are scoped to this fabric.
constexpr chip::FabricIndex kFabricIndex = 1;

// Operator-recorded local OTA candidate. This is the image the hub intends to
// serve. NOTE: the stock EspOtaProvider sources candidates from DCL and fetches
// the bytes over HTTP from the candidate URL; wiring this recorded candidate
// into the provider's candidate cache (a hub-local DCL/HTTP endpoint, or a
// provider extension that serves from flash) is the documented remaining
// offline plumbing. Recording + validating it here is the operator-facing half.
struct LocalOtaCandidate {
    bool set = false;
    char uri[256] = {0};
    uint32_t software_version = 0;
    char version_string[64] = {0};
    uint32_t size = 0;
};

LocalOtaCandidate g_candidate;

bool parse_u32(const char *value, uint32_t &out)
{
    char *end = nullptr;
    errno = 0;
    unsigned long long parsed = strtoull(value, &end, 0);
    if (errno != 0 || end == value || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    return true;
}

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

int ota_status_handler(int, char **)
{
    printf("\nLED Orchestra OTA Provider status:\n");
    printf("  provider cluster: built-in (CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER=y)\n");
    if (g_candidate.set) {
        printf("  local candidate : version=%" PRIu32 " (%s) size=%" PRIu32 " bytes\n", g_candidate.software_version,
               g_candidate.version_string, g_candidate.size);
        printf("  candidate uri   : %s\n", g_candidate.uri);
    } else {
        printf("  local candidate : <none set> (use lo-ota-set-image)\n");
    }
    printf("  per-node allow  : default DENY; enable a node with lo-ota-enable <node> [once]\n");
    printf("  offline note    : serving the bytes needs a hub-local image endpoint;\n");
    printf("                    see the Phase 7 runbook (matter-prototype/s3-h2-hub-validation/).\n\n");
    return ESP_OK;
}

int ota_enable_handler(int argc, char **argv)
{
    argc--;
    argv++;
    if (argc < 1 || argc > 2) {
        ESP_LOGE(TAG, "usage: lo-ota-enable <node-id> [once]");
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t node_id = 0;
    if (!parse_u64(argv[0], node_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    bool once = (argc == 2 && strcmp(argv[1], "once") == 0);

    chip::ScopedNodeId scoped(node_id, kFabricIndex);
    esp_err_t err = EspOtaProvider::GetInstance().EnableOtaForNode(scoped, once);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "EnableOtaForNode(0x%" PRIX64 ") failed (%s); the node must have sent a QueryImage at least once so a "
                 "requestor entry exists",
                 node_id, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "OTA enabled for node 0x%" PRIX64 "%s", node_id, once ? " (once)" : "");
    return ESP_OK;
}

int ota_disable_handler(int argc, char **argv)
{
    argc--;
    argv++;
    if (argc != 1) {
        ESP_LOGE(TAG, "usage: lo-ota-disable <node-id>");
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t node_id = 0;
    if (!parse_u64(argv[0], node_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    chip::ScopedNodeId scoped(node_id, kFabricIndex);
    esp_err_t err = EspOtaProvider::GetInstance().DisableOtaForNode(scoped);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DisableOtaForNode(0x%" PRIX64 ") failed (%s)", node_id, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "OTA disabled for node 0x%" PRIX64, node_id);
    return ESP_OK;
}

int ota_set_image_handler(int argc, char **argv)
{
    argc--;
    argv++;
    if (argc != 4) {
        ESP_LOGE(TAG, "usage: lo-ota-set-image <uri-or-path> <software-version> <version-string> <size>");
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t sw_version = 0;
    uint32_t size = 0;
    if (strlen(argv[0]) >= sizeof(g_candidate.uri) || strlen(argv[2]) >= sizeof(g_candidate.version_string)) {
        ESP_LOGE(TAG, "uri or version-string too long");
        return ESP_ERR_INVALID_ARG;
    }
    if (!parse_u32(argv[1], sw_version) || !parse_u32(argv[3], size)) {
        return ESP_ERR_INVALID_ARG;
    }

    g_candidate.set = true;
    strncpy(g_candidate.uri, argv[0], sizeof(g_candidate.uri) - 1);
    g_candidate.uri[sizeof(g_candidate.uri) - 1] = '\0';
    g_candidate.software_version = sw_version;
    strncpy(g_candidate.version_string, argv[2], sizeof(g_candidate.version_string) - 1);
    g_candidate.version_string[sizeof(g_candidate.version_string) - 1] = '\0';
    g_candidate.size = size;

    ESP_LOGI(TAG, "recorded local OTA candidate: version=%" PRIu32 " (%s) size=%" PRIu32 " uri=%s", sw_version,
             g_candidate.version_string, size, g_candidate.uri);
    ESP_LOGW(TAG, "candidate recorded; serving it requires the hub-local image endpoint plumbing (Phase 7 runbook)");
    return ESP_OK;
}

esp_err_t register_cmd(const char *name, const char *help, int (*func)(int, char **))
{
    const esp_console_cmd_t cmd = {
        .command = name,
        .help = help,
        .hint = nullptr,
        .func = func,
    };
    return esp_console_cmd_register(&cmd);
}

} // namespace

esp_err_t led_orchestra_ota_provider_init()
{
    using namespace esp_matter;

    node::config_t node_config;
    node_t *node = node::create(&node_config, nullptr, nullptr);
    if (!node) {
        ESP_LOGE(TAG, "failed to create node for OTA provider");
        return ESP_FAIL;
    }
    endpoint_t *root = endpoint::get(node, 0);
    if (!root) {
        ESP_LOGE(TAG, "failed to get root endpoint");
        return ESP_FAIL;
    }

    EspOtaProvider::GetInstance().Init(/*otaAllowedDefault=*/false);

    cluster::ota_provider::config_t config;
    config.delegate = &EspOtaProvider::GetInstance();
    cluster_t *cluster = cluster::ota_provider::create(root, &config, CLUSTER_FLAG_SERVER);
    if (!cluster) {
        ESP_LOGE(TAG, "failed to create OTA provider cluster");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA provider cluster ready (default DENY; per-node enable via lo-ota-enable)");
    return ESP_OK;
}

esp_err_t led_orchestra_ota_provider_register_commands()
{
    esp_err_t err = ESP_OK;
    err |= register_cmd("lo-ota-status", "Show OTA provider status and the recorded local image candidate",
                        ota_status_handler);
    err |= register_cmd("lo-ota-enable", "Allow a node to OTA: <node-id> [once]", ota_enable_handler);
    err |= register_cmd("lo-ota-disable", "Deny a node's OTA: <node-id>", ota_disable_handler);
    err |= register_cmd("lo-ota-set-image",
                        "Record the local OTA image: <uri-or-path> <software-version> <version-string> <size>",
                        ota_set_image_handler);
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}

#else // CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER disabled — no-op stubs

esp_err_t led_orchestra_ota_provider_init() { return ESP_OK; }
esp_err_t led_orchestra_ota_provider_register_commands() { return ESP_OK; }

#endif // CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER
