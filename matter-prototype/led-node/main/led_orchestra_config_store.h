#pragma once

#include <esp_err.h>
#include <stdbool.h>

#include "led_orchestra_renderer.h"

// Durable per-node segment configuration (Phase 6).
//
// `SetNodeConfig` provisions a node's place in the virtual strip (orchestra node
// id, segment range, total strip length, LED GPIO). The prototype kept that in
// RAM only, so a power-cycle reverted every node to its compiled Kconfig
// defaults. This module persists the accepted config in NVS behind a
// magic/version/CRC wrapper so a commissioned node comes back up already knowing
// its segment without the hub re-provisioning it.
//
// The on-flash record is versioned and append-only: bump kConfigVersion and add
// a migration branch in the loader rather than reordering fields. A record that
// fails any integrity check is ignored and the compiled defaults are used, so a
// corrupt or stale blob can never blank a node's identity silently.

// Load persisted node config from NVS.
//
// On a clean first boot (no record) or any integrity failure (bad magic,
// unsupported version, CRC mismatch, out-of-range fields), `out` is filled with
// the compiled Kconfig defaults and `from_nvs` is set false. On a valid record
// `out` holds the stored config and `from_nvs` is true. Returns ESP_OK in both
// of those cases; returns an error only on an unexpected NVS-layer failure.
esp_err_t led_orchestra_config_load(LedOrchestraNodeConfig &out, bool &from_nvs);

// Persist node config durably to NVS (magic + version + CRC wrapped). Called
// after a SetNodeConfig command is accepted so the layout survives reboot.
esp_err_t led_orchestra_config_save(const LedOrchestraNodeConfig &config);

// Load the last persisted active scene from NVS. Same semantics as
// led_orchestra_config_load: on miss or integrity failure fills `out` with a
// safe default and sets `from_nvs` false. Scheduled scenes are NOT persisted
// (only immediate/active scenes are); this always reflects the last scene that
// was concretely activated on the node.
esp_err_t led_orchestra_scene_load(LedOrchestraScene &out, bool &from_nvs);

// Persist the active scene durably to NVS. Called after an immediate SetScene
// is accepted (scheduled_start_ms == 0) so the node resumes the correct scene
// after a power cycle without re-provisioning from the controller.
esp_err_t led_orchestra_scene_save(const LedOrchestraScene &scene);
