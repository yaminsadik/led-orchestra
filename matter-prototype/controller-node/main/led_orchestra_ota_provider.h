#pragma once

#include <esp_err.h>

// LED Orchestra offline OTA Provider scaffold (Phase 7).
//
// The hub is the local Matter OTA Provider: it stores an operator/Kubernetes-
// loaded firmware image and serves it to commissioned LED nodes (OTA Requestors)
// over the offline Matter/Thread fabric — never from the internet/DCL.
//
// This module integrates esp_matter's EspOtaProvider + the OTA Provider cluster
// (0x0029) onto the controller and exposes the operator console surface. It is
// gated behind CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER so the proven commissioner
// build is unchanged by default; when the option is OFF every entry point below
// is a no-op and no `lo-ota-*` commands are registered (no placeholder commands).
//
// Enabling the provider also requires (documented in the Phase 7 runbook):
//   * CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER=y  — the controller hosts a data
//     model so requestors can invoke the provider cluster.
//   * the esp_matter_ota_provider component + DCL fetch DISABLED for offline.
//   * a hub-local image endpoint for the BDX/HTTP image source (remaining
//     plumbing — the stock provider fetches the image bytes over HTTP from a
//     candidate URL; offline-first points that URL at a hub-local server).

// Create the node/root-endpoint/OTA Provider cluster and initialize EspOtaProvider.
// MUST be called before esp_matter::start() (data-model setup happens at start).
// No-op (returns ESP_OK) when the build option is disabled.
esp_err_t led_orchestra_ota_provider_init();

// Register the `lo-ota-*` operator console commands. No-op when disabled.
esp_err_t led_orchestra_ota_provider_register_commands();
