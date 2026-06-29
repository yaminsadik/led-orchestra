// LED Orchestra offline OTA: locally-registered candidate API.
//
// The upstream esp_matter_ota_provider learned about candidate images from the
// CSA DCL over HTTPS. This fork is offline-only: the hub registers the single
// image it intends to serve with register_local_candidate() (driven by the
// `lo-ota-set-image` console command), and the provider answers QueryImage from
// that local registration — no internet, no DCL. The ota_url is a hub-local HTTP
// endpoint (operator laptop now, Kubernetes-served later); the BDX sender streams
// the bytes from there.
//
// See matter-prototype/s3-h2-hub-validation/phase-7-offline-ota.md.

#pragma once

#include <esp_err.h>
#include <stdint.h>

namespace esp_matter {
namespace ota_provider {

// Register (or replace) the local OTA image candidate for a given vendor/product.
// A requestor's QueryImage matches when its vendor_id/product_id match and its
// current software_version is in [min_applicable, max_applicable] and below
// software_version. Use min=0, max=UINT32_MAX to offer the image to any older node.
//   ota_url    a hub-local HTTP URL the BDX sender will stream the image from.
//   ota_file_size  the .ota image size in bytes (0 = let the requestor learn it
//                  from the image header during transfer).
esp_err_t register_local_candidate(uint16_t vendor_id, uint16_t product_id, uint32_t software_version,
                                   const char *software_version_str, const char *ota_url, uint32_t ota_file_size,
                                   uint32_t min_applicable_software_version, uint32_t max_applicable_software_version);

// Drop all registered candidates (e.g. before staging a different image).
esp_err_t clear_local_candidates();

} // namespace ota_provider
} // namespace esp_matter
