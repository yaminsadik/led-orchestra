// Copyright 2023 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// LED Orchestra offline fork.
//
// Upstream resolved OTA candidates from the CSA Distributed Compliance Ledger
// (DCL) over HTTPS and then downloaded the image over TLS. The LED Orchestra
// fabric is offline by invariant (no venue Wi-Fi, cloud, or internet), so this
// fork removes the DCL/HTTPS path entirely. Candidates are registered locally by
// the operator (see register_local_candidate(), driven by the `lo-ota-set-image`
// console command); the image bytes are streamed from a hub-local HTTP endpoint
// by the BDX sender. The "image source" URL is the swappable ingress boundary:
// an operator laptop today, a Kubernetes-served endpoint later — only the URL
// passed to `lo-ota-set-image` changes, never this firmware.
//
// See matter-prototype/s3-h2-hub-validation/phase-7-offline-ota.md.

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter_mem.h>
#include <esp_matter_ota_candidates.h>
#include <esp_matter_ota_local_candidate.h>
#include <esp_matter_ota_provider.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <string.h>

namespace esp_matter {
namespace ota_provider {

static constexpr char TAG[] = "ota_provider";

// A small fixed cache is plenty for a private fabric: typically one image per
// node type. (Upstream made this a Kconfig knob for the DCL cache; that knob is
// gone with the DCL path.)
static constexpr size_t max_ota_candidate_count = 8;

static model_version_t *_ota_candidates_cache[max_ota_candidate_count];
static QueueHandle_t _ota_candidate_task_queue = NULL;
static SemaphoreHandle_t _ota_cache_mutex = NULL;

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint32_t software_version;
    fetch_ota_image_done_callback_t callback;
    void *callback_args;
} ota_candidate_fetch_action_t;

static bool _is_ota_candidate_valid(model_version_t *model, uint32_t current_software_version)
{
    return model->software_version > current_software_version &&
        model->max_applicable_software_version >= current_software_version &&
        model->min_applicable_software_version <= current_software_version;
}

// Caller must hold _ota_cache_mutex. Returns the cache index of an applicable
// candidate for the requestor's current version, or -1. Unlike upstream this
// never evicts a non-applicable candidate: the operator registered it, so a node
// that is already up to date simply gets NotAvailable while the candidate stays
// cached for other (older) nodes.
static int _search_ota_candidate(uint16_t vendor_id, uint16_t product_id, uint32_t software_ver)
{
    for (size_t index = 0; index < max_ota_candidate_count; ++index) {
        model_version_t *cur_model = _ota_candidates_cache[index];
        if (cur_model && cur_model->vendor_id == vendor_id && cur_model->product_id == product_id) {
            return _is_ota_candidate_valid(cur_model, software_ver) ? (int) index : -1;
        }
    }
    return -1;
}

// Caller must hold _ota_cache_mutex. Returns an existing slot for this vid/pid
// (to overwrite), else an empty slot, else -1 when the cache is full.
static int _find_candidate_slot(uint16_t vendor_id, uint16_t product_id)
{
    int empty = -1;
    for (size_t index = 0; index < max_ota_candidate_count; ++index) {
        model_version_t *cur = _ota_candidates_cache[index];
        if (cur && cur->vendor_id == vendor_id && cur->product_id == product_id) {
            return (int) index;
        }
        if (!cur && empty < 0) {
            empty = (int) index;
        }
    }
    return empty;
}

esp_err_t register_local_candidate(uint16_t vendor_id, uint16_t product_id, uint32_t software_version,
                                   const char *software_version_str, const char *ota_url, uint32_t ota_file_size,
                                   uint32_t min_applicable_software_version, uint32_t max_applicable_software_version)
{
    if (!ota_url || !software_version_str) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ota_url) >= OTA_URL_MAX_LEN || strlen(software_version_str) >= SOFTWARE_VERSION_STR_MAX_LEN) {
        ESP_LOGE(TAG, "ota_url or version string too long");
        return ESP_ERR_INVALID_SIZE;
    }
    if (!_ota_cache_mutex) {
        ESP_LOGE(TAG, "candidate cache not initialized (call init_ota_candidates first)");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;
    xSemaphoreTake(_ota_cache_mutex, portMAX_DELAY);
    int slot = _find_candidate_slot(vendor_id, product_id);
    if (slot < 0) {
        ret = ESP_ERR_NO_MEM;
    } else {
        model_version_t *model = _ota_candidates_cache[slot];
        if (!model) {
            model = (model_version_t *) esp_matter_mem_calloc(1, sizeof(model_version_t));
            if (model) {
                _ota_candidates_cache[slot] = model;
            } else {
                ret = ESP_ERR_NO_MEM;
            }
        }
        if (model) {
            model->vendor_id = vendor_id;
            model->product_id = product_id;
            model->software_version = software_version;
            strncpy(model->software_version_str, software_version_str, sizeof(model->software_version_str) - 1);
            model->software_version_str[sizeof(model->software_version_str) - 1] = '\0';
            strncpy(model->ota_url, ota_url, sizeof(model->ota_url) - 1);
            model->ota_url[sizeof(model->ota_url) - 1] = '\0';
            model->ota_file_size = ota_file_size;
            model->min_applicable_software_version = min_applicable_software_version;
            model->max_applicable_software_version = max_applicable_software_version;
            model->cd_version_number = 0;
            model->lifetime = 0;
        }
    }
    xSemaphoreGive(_ota_cache_mutex);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "registered local OTA candidate vid=0x%04X pid=0x%04X version=%lu size=%lu url=%s", vendor_id,
                 product_id, (unsigned long) software_version, (unsigned long) ota_file_size, ota_url);
    } else {
        ESP_LOGE(TAG, "failed to register local OTA candidate (%s)", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t clear_local_candidates()
{
    if (!_ota_cache_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(_ota_cache_mutex, portMAX_DELAY);
    for (size_t index = 0; index < max_ota_candidate_count; ++index) {
        if (_ota_candidates_cache[index]) {
            esp_matter_mem_free(_ota_candidates_cache[index]);
            _ota_candidates_cache[index] = nullptr;
        }
    }
    xSemaphoreGive(_ota_cache_mutex);
    ESP_LOGI(TAG, "cleared all local OTA candidates");
    return ESP_OK;
}

static void _ota_candidate_fetch_handler(ota_candidate_fetch_action_t &action)
{
    assert(action.callback);

    char url[OTA_URL_MAX_LEN] = {0};
    char ver_str[SOFTWARE_VERSION_STR_MAX_LEN] = {0};
    uint32_t size = 0;
    uint32_t sw_version = 0;
    bool found = false;

    xSemaphoreTake(_ota_cache_mutex, portMAX_DELAY);
    int index = _search_ota_candidate(action.vendor_id, action.product_id, action.software_version);
    if (index >= 0 && _ota_candidates_cache[index]) {
        model_version_t *candidate = _ota_candidates_cache[index];
        strncpy(url, candidate->ota_url, sizeof(url) - 1);
        strncpy(ver_str, candidate->software_version_str, sizeof(ver_str) - 1);
        size = candidate->ota_file_size;
        sw_version = candidate->software_version;
        found = true;
    }
    xSemaphoreGive(_ota_cache_mutex);

    if (found) {
        action.callback(EspOtaProvider::OTAQueryStatus::kUpdateAvailable, url, size, sw_version, ver_str,
                        action.callback_args);
        return;
    }

    ESP_LOGW(TAG, "no local OTA candidate for vid=0x%04X pid=0x%04X (requestor at version %lu); reply NotAvailable",
             action.vendor_id, action.product_id, (unsigned long) action.software_version);
    action.callback(EspOtaProvider::OTAQueryStatus::kNotAvailable, nullptr, 0, 0, nullptr, action.callback_args);
}

static void ota_candidate_task(void *ctx)
{
    ota_candidate_fetch_action_t action;
    while (true) {
        if (xQueueReceive(_ota_candidate_task_queue, &action, portMAX_DELAY) == pdTRUE) {
            _ota_candidate_fetch_handler(action);
        }
    }
    vQueueDelete(_ota_candidate_task_queue);
    vTaskDelete(NULL);
}

esp_err_t fetch_ota_candidate(const uint16_t vendor_id, const uint16_t product_id, const uint32_t software_version,
                              fetch_ota_image_done_callback_t callback, void *ctx)
{
    if (!_ota_candidate_task_queue) {
        ESP_LOGE(TAG, "Failed to search ota candidate as the task queue is not initialized");
        return ESP_ERR_NOT_FOUND;
    }
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    ota_candidate_fetch_action_t action;
    action.vendor_id = vendor_id;
    action.product_id = product_id;
    action.software_version = software_version;
    action.callback = callback;
    action.callback_args = ctx;
    if (xQueueSend(_ota_candidate_task_queue, &action, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed send search ota candidate action");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t init_ota_candidates()
{
    memset(_ota_candidates_cache, 0, sizeof(_ota_candidates_cache));

    if (!_ota_cache_mutex) {
        _ota_cache_mutex = xSemaphoreCreateMutex();
        if (!_ota_cache_mutex) {
            ESP_LOGE(TAG, "Failed to create ota_candidate cache mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    if (_ota_candidate_task_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    _ota_candidate_task_queue = xQueueCreate(8, sizeof(ota_candidate_fetch_action_t));
    if (!_ota_candidate_task_queue) {
        ESP_LOGE(TAG, "Failed to create ota_candidate task queue");
        return ESP_ERR_NO_MEM;
    }

    // Local-only resolution: no JSON/TLS, so the larger stack upstream needed for
    // DCL parsing is no longer required.
    if (xTaskCreate(ota_candidate_task, "ota_candidate", 4096, NULL, 5, NULL) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create ota_candidate task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

} // namespace ota_provider
} // namespace esp_matter
