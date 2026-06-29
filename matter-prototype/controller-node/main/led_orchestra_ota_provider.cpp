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
#include <esp_matter_controller_utils.h>
#include <esp_matter_ota_local_candidate.h>
#include <esp_matter_ota_provider.h>

#include <access/AccessControl.h>
#include <access/AuthMode.h>
#include <access/Privilege.h>
#include <app/clusters/ota-provider/ota-provider-cluster.h>
#include <app/clusters/ota-provider/CodegenIntegration.h>
#include <app/ConcreteClusterPath.h>
#include <app/InteractionModelEngine.h>
#include <app/data-model-provider/MetadataTypes.h>
#include <app/server/Server.h>
#include <app/util/endpoint-config-api.h>
#include <app-common/zap-generated/callback.h>
#include <clusters/OtaSoftwareUpdateProvider/ClusterId.h>
#include <controller/CHIPDeviceControllerFactory.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
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
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
};

LocalOtaCandidate g_candidate;
uint16_t g_provider_endpoint_id = 0xFFFF;

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

chip::FabricIndex current_fabric_index()
{
    chip::FabricIndex configured = esp_matter::controller::get_fabric_index();
    return configured != chip::kUndefinedFabricIndex ? configured : kFabricIndex;
}

bool privilege_allows_operate(chip::Access::Privilege privilege)
{
    return privilege == chip::Access::Privilege::kOperate || privilege == chip::Access::Privilege::kManage ||
        privilege == chip::Access::Privilege::kAdminister;
}

bool target_matches_ota_provider(const chip::Access::AccessControl::Entry::Target &target)
{
    using Target = chip::Access::AccessControl::Entry::Target;
    if ((target.flags & Target::kDeviceType) != 0) {
        return false;
    }
    bool endpoint_ok = (target.flags & Target::kEndpoint) == 0 || target.endpoint == g_provider_endpoint_id;
    bool cluster_ok =
        (target.flags & Target::kCluster) == 0 || target.cluster == chip::app::Clusters::OtaSoftwareUpdateProvider::Id;
    return endpoint_ok && cluster_ok;
}

bool ota_acl_entry_allows_node(chip::Access::AccessControl::Entry &entry, chip::NodeId node_id)
{
    chip::Access::AuthMode auth_mode = chip::Access::AuthMode::kNone;
    chip::Access::Privilege privilege = chip::Access::Privilege::kView;
    if (entry.GetAuthMode(auth_mode) != CHIP_NO_ERROR || auth_mode != chip::Access::AuthMode::kCase ||
        entry.GetPrivilege(privilege) != CHIP_NO_ERROR || !privilege_allows_operate(privilege)) {
        return false;
    }

    size_t subject_count = 0;
    if (entry.GetSubjectCount(subject_count) != CHIP_NO_ERROR || subject_count == 0) {
        return false;
    }
    bool subject_found = false;
    for (size_t subject_index = 0; subject_index < subject_count; ++subject_index) {
        chip::NodeId subject = chip::kUndefinedNodeId;
        if (entry.GetSubject(subject_index, subject) == CHIP_NO_ERROR && subject == node_id) {
            subject_found = true;
            break;
        }
    }
    if (!subject_found) {
        return false;
    }

    size_t target_count = 0;
    if (entry.GetTargetCount(target_count) != CHIP_NO_ERROR) {
        return false;
    }
    if (target_count == 0) {
        return true;
    }
    for (size_t target_index = 0; target_index < target_count; ++target_index) {
        chip::Access::AccessControl::Entry::Target target;
        if (entry.GetTarget(target_index, target) == CHIP_NO_ERROR && target_matches_ota_provider(target)) {
            return true;
        }
    }
    return false;
}

esp_err_t ensure_ota_acl_for_node(chip::NodeId node_id)
{
    if (g_provider_endpoint_id == 0xFFFF) {
        ESP_LOGE(TAG, "cannot grant OTA provider access before endpoint initialization");
        return ESP_ERR_INVALID_STATE;
    }

    chip::FabricIndex fabric_index = current_fabric_index();
    if (fabric_index == chip::kUndefinedFabricIndex) {
        ESP_LOGE(TAG, "cannot grant OTA provider access without an active fabric");
        return ESP_ERR_INVALID_STATE;
    }

    auto &access_control = chip::Access::GetAccessControl();
    size_t entry_count = 0;
    CHIP_ERROR err = access_control.GetEntryCount(fabric_index, entry_count);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "failed to read ACL entry count for fabric %u (%" CHIP_ERROR_FORMAT ")",
                 static_cast<unsigned>(fabric_index), err.Format());
        return ESP_FAIL;
    }
    for (size_t index = 0; index < entry_count; ++index) {
        chip::Access::AccessControl::Entry existing;
        if (access_control.ReadEntry(fabric_index, index, existing) == CHIP_NO_ERROR &&
            ota_acl_entry_allows_node(existing, node_id)) {
            ESP_LOGI(TAG, "OTA provider ACL already allows node 0x%" PRIX64 " on fabric %u", node_id,
                     static_cast<unsigned>(fabric_index));
            return ESP_OK;
        }
    }

    chip::Access::AccessControl::Entry entry;
    err = access_control.PrepareEntry(entry);
    if (err == CHIP_NO_ERROR) {
        err = entry.SetAuthMode(chip::Access::AuthMode::kCase);
    }
    if (err == CHIP_NO_ERROR) {
        err = entry.SetFabricIndex(fabric_index);
    }
    if (err == CHIP_NO_ERROR) {
        err = entry.SetPrivilege(chip::Access::Privilege::kOperate);
    }
    if (err == CHIP_NO_ERROR) {
        err = entry.AddSubject(nullptr, node_id);
    }
    if (err == CHIP_NO_ERROR) {
        chip::Access::AccessControl::Entry::Target target = {
            .flags = chip::Access::AccessControl::Entry::Target::kEndpoint |
                chip::Access::AccessControl::Entry::Target::kCluster,
            .cluster = chip::app::Clusters::OtaSoftwareUpdateProvider::Id,
            .endpoint = g_provider_endpoint_id,
            .deviceType = 0,
        };
        err = entry.AddTarget(nullptr, target);
    }

    size_t created_index = 0;
    if (err == CHIP_NO_ERROR) {
        err = access_control.CreateEntry(nullptr, fabric_index, &created_index, entry);
    }
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "failed to create OTA provider ACL for node 0x%" PRIX64 " on fabric %u (%" CHIP_ERROR_FORMAT ")",
                 node_id, static_cast<unsigned>(fabric_index), err.Format());
        return err == CHIP_ERROR_BUFFER_TOO_SMALL ? ESP_ERR_NO_MEM : ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "granted node 0x%" PRIX64 " CASE/operate access to OTA provider endpoint %u cluster 0x%08" PRIX32
             " on fabric %u (acl index %u)",
             node_id, g_provider_endpoint_id, chip::app::Clusters::OtaSoftwareUpdateProvider::Id,
             static_cast<unsigned>(fabric_index), static_cast<unsigned>(created_index));
    return ESP_OK;
}

int ota_status_handler(int, char **)
{
    printf("\nLED Orchestra OTA Provider status:\n");
    printf("  provider cluster: built-in (CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER=y)\n");
    if (g_provider_endpoint_id != 0xFFFF) {
        printf("  provider endpoint: %u (use this in AnnounceOTAProvider field 4)\n", g_provider_endpoint_id);
    } else {
        printf("  provider endpoint: <not initialized>\n");
    }
    printf("  ember endpoints  : count=%u\n", emberAfEndpointCount());
    for (uint16_t index = 0; index < emberAfEndpointCount(); ++index) {
        chip::EndpointId endpoint_id = emberAfEndpointFromIndex(index);
        bool enabled = emberAfEndpointIndexIsEnabled(index);
        bool has_ota_provider =
            (emberAfFindServerCluster(endpoint_id, chip::app::Clusters::OtaSoftwareUpdateProvider::Id) != nullptr);
        printf("                    [%u] endpoint=%u enabled=%s ota-provider=%s\n", index, endpoint_id,
               enabled ? "yes" : "no", has_ota_provider ? "yes" : "no");
    }
    if (g_provider_endpoint_id != 0xFFFF) {
        auto *im_provider = chip::app::InteractionModelEngine::GetInstance()->GetDataModelProvider();
        auto &codegen_provider = chip::app::CodegenDataModelProvider::Instance();
        chip::app::ConcreteClusterPath path(g_provider_endpoint_id,
                                            chip::app::Clusters::OtaSoftwareUpdateProvider::Id);
        chip::ReadOnlyBufferBuilder<chip::app::DataModel::AcceptedCommandEntry> accepted;
        CHIP_ERROR accepted_err = codegen_provider.AcceptedCommands(path, accepted);
        auto *server_cluster = codegen_provider.Registry().Get(path);
        printf("  IM provider ptr  : active=%p codegen=%p\n", im_provider, &codegen_provider);
        printf("  registry server  : %p\n", server_cluster);
        printf("  accepted commands: err=%s count=%u", accepted_err == CHIP_NO_ERROR ? "OK" : "ERR",
               static_cast<unsigned>(accepted.Size()));
        auto commands = accepted.TakeBuffer();
        for (const auto &entry : commands) {
            printf(" 0x%08" PRIX32, entry.commandId);
        }
        printf("\n");
    }
    if (g_candidate.set) {
        printf("  local candidate : version=%" PRIu32 " (%s) size=%" PRIu32 " bytes vid=0x%04X pid=0x%04X\n",
               g_candidate.software_version, g_candidate.version_string, g_candidate.size, g_candidate.vendor_id,
               g_candidate.product_id);
        printf("  candidate uri   : %s\n", g_candidate.uri);
        printf("                    (registered with provider; serve this URL over hub-local HTTP)\n");
    } else {
        printf("  local candidate : <none set> (use lo-ota-set-image)\n");
    }
    printf("  per-node allow  : default DENY; enable a node with lo-ota-enable <node> [once]\n");
    printf("  offline note    : the URL must be reachable over the control LAN (operator laptop now,\n");
    printf("                    Kubernetes-served later); see the Phase 7 runbook.\n\n");
    return ESP_OK;
}

int ota_grant_access_handler(int argc, char **argv)
{
    argc--;
    argv++;
    if (argc != 1) {
        ESP_LOGE(TAG, "usage: lo-ota-grant-access <node-id>");
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t node_id = 0;
    if (!parse_u64(argv[0], node_id) || !chip::IsOperationalNodeId(node_id)) {
        ESP_LOGE(TAG, "node-id must be an operational Matter node id");
        return ESP_ERR_INVALID_ARG;
    }
    return ensure_ota_acl_for_node(static_cast<chip::NodeId>(node_id));
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
        ESP_LOGE(TAG, "EnableOtaForNode(0x%" PRIX64 ") failed (%s)", node_id, esp_err_to_name(err));
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
    if (argc != 6) {
        ESP_LOGE(TAG,
                 "usage: lo-ota-set-image <http-uri> <software-version> <version-string> <size> <vendor-id> "
                 "<product-id>");
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t sw_version = 0;
    uint32_t size = 0;
    uint32_t vid = 0;
    uint32_t pid = 0;
    if (strlen(argv[0]) >= sizeof(g_candidate.uri) || strlen(argv[2]) >= sizeof(g_candidate.version_string)) {
        ESP_LOGE(TAG, "uri or version-string too long");
        return ESP_ERR_INVALID_ARG;
    }
    if (!parse_u32(argv[1], sw_version) || !parse_u32(argv[3], size)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!parse_u32(argv[4], vid) || !parse_u32(argv[5], pid) || vid > 0xFFFF || pid > 0xFFFF) {
        ESP_LOGE(TAG, "vendor-id and product-id must be 16-bit values");
        return ESP_ERR_INVALID_ARG;
    }

    g_candidate.set = true;
    strncpy(g_candidate.uri, argv[0], sizeof(g_candidate.uri) - 1);
    g_candidate.uri[sizeof(g_candidate.uri) - 1] = '\0';
    g_candidate.software_version = sw_version;
    strncpy(g_candidate.version_string, argv[2], sizeof(g_candidate.version_string) - 1);
    g_candidate.version_string[sizeof(g_candidate.version_string) - 1] = '\0';
    g_candidate.size = size;
    g_candidate.vendor_id = static_cast<uint16_t>(vid);
    g_candidate.product_id = static_cast<uint16_t>(pid);

    // Register with the provider so QueryImage from a matching node is answered
    // offline. min=0/max=UINT32_MAX offers the image to any node currently below
    // sw_version. The URL is served from the control LAN (laptop now, K8s later).
    esp_err_t reg = esp_matter::ota_provider::register_local_candidate(
        g_candidate.vendor_id, g_candidate.product_id, sw_version, g_candidate.version_string, g_candidate.uri, size,
        /*min_applicable=*/0, /*max_applicable=*/UINT32_MAX);
    if (reg != ESP_OK) {
        ESP_LOGE(TAG, "failed to register candidate with provider (%s)", esp_err_to_name(reg));
        g_candidate.set = false;
        return reg;
    }

    ESP_LOGI(TAG, "staged local OTA candidate: version=%" PRIu32 " (%s) size=%" PRIu32 " vid=0x%04X pid=0x%04X uri=%s",
             sw_version, g_candidate.version_string, size, g_candidate.vendor_id, g_candidate.product_id,
             g_candidate.uri);
    ESP_LOGW(TAG, "ensure %s is reachable over the control LAN before enabling a node (lo-ota-enable)", g_candidate.uri);
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
    EspOtaProvider::GetInstance().Init(/*otaAllowedDefault=*/false);

    endpoint::ota_provider::config_t config;
    config.ota_provider.delegate = &EspOtaProvider::GetInstance();
    endpoint_t *provider_endpoint = endpoint::ota_provider::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
    if (!provider_endpoint) {
        ESP_LOGE(TAG, "failed to create OTA provider endpoint");
        return ESP_FAIL;
    }
    g_provider_endpoint_id = endpoint::get_id(provider_endpoint);

    ESP_LOGI(TAG, "OTA provider endpoint %u ready (default DENY; per-node enable via lo-ota-enable)",
             g_provider_endpoint_id);
    return ESP_OK;
}

esp_err_t led_orchestra_ota_provider_bind_delegate()
{
    if (g_provider_endpoint_id == 0xFFFF) {
        ESP_LOGE(TAG, "cannot bind OTA provider delegate before endpoint initialization");
        return ESP_ERR_INVALID_STATE;
    }

    auto &codegen_provider = chip::app::CodegenDataModelProvider::Instance();
    chip::app::ConcreteClusterPath path(g_provider_endpoint_id, chip::app::Clusters::OtaSoftwareUpdateProvider::Id);
    auto *server_cluster = codegen_provider.Registry().Get(path);
    if (server_cluster == nullptr) {
        ESP_LOGW(TAG, "OTA provider server missing from codegen registry; running cluster init callback");
        emberAfOtaSoftwareUpdateProviderClusterInitCallback(g_provider_endpoint_id);
        server_cluster = codegen_provider.Registry().Get(path);
    }
    if (server_cluster == nullptr) {
        ESP_LOGE(TAG, "OTA provider server still missing from codegen registry after bind attempt");
        return ESP_FAIL;
    }

    static_cast<chip::app::Clusters::OtaProviderServer *>(server_cluster)->SetDelegate(&EspOtaProvider::GetInstance());
    esp_err_t err = EspOtaProvider::GetInstance().RegisterBdxHandler();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "OTA provider delegate bound on endpoint %u", g_provider_endpoint_id);
    return ESP_OK;
}

esp_err_t led_orchestra_ota_provider_bind_controller_exchange()
{
    auto *system_state = chip::Controller::DeviceControllerFactory::GetInstance().GetSystemState();
    if (system_state == nullptr || system_state->ExchangeMgr() == nullptr) {
        ESP_LOGE(TAG, "controller exchange manager is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = EspOtaProvider::GetInstance().RegisterBdxHandler(*system_state->ExchangeMgr());
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "OTA BDX handler bound on controller exchange manager %p", system_state->ExchangeMgr());
    return ESP_OK;
}

esp_err_t led_orchestra_ota_provider_register_commands()
{
    esp_err_t err = ESP_OK;
    err |= register_cmd("lo-ota-status", "Show OTA provider status and the recorded local image candidate",
                        ota_status_handler);
    err |= register_cmd("lo-ota-grant-access", "Grant a node CASE/operate ACL access to the OTA provider: <node-id>",
                        ota_grant_access_handler);
    err |= register_cmd("lo-ota-enable", "Allow a node to OTA: <node-id> [once]", ota_enable_handler);
    err |= register_cmd("lo-ota-disable", "Deny a node's OTA: <node-id>", ota_disable_handler);
    err |= register_cmd(
        "lo-ota-set-image",
        "Stage the local OTA image: <http-uri> <software-version> <version-string> <size> <vendor-id> <product-id>",
        ota_set_image_handler);
    return err == ESP_OK ? ESP_OK : ESP_FAIL;
}

#else // CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER disabled — no-op stubs

esp_err_t led_orchestra_ota_provider_init() { return ESP_OK; }
esp_err_t led_orchestra_ota_provider_bind_delegate() { return ESP_OK; }
esp_err_t led_orchestra_ota_provider_bind_controller_exchange() { return ESP_OK; }
esp_err_t led_orchestra_ota_provider_register_commands() { return ESP_OK; }

#endif // CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER
