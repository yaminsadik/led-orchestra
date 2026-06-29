#pragma once

// LED Orchestra LED-node Matter project config overrides (CHIP_PROJECT_CONFIG_INCLUDE).
//
// Pins the Matter device software version from Kconfig so OTA target images can
// bump it without editing source: build the update image with
// CONFIG_LED_ORCHESTRA_SW_VERSION=N (and a matching version string). The version
// is what the OTA Requestor reports in QueryImage and what the provider compares
// against, so every OTA target must be a higher version than the running image.
//
// See matter-prototype/s3-h2-hub-validation/phase-7-offline-ota.md.

#include <sdkconfig.h>

#ifdef CONFIG_LED_ORCHESTRA_SW_VERSION
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION CONFIG_LED_ORCHESTRA_SW_VERSION
#endif

#ifdef CONFIG_LED_ORCHESTRA_SW_VERSION_STR
#define CHIP_DEVICE_CONFIG_DEVICE_SOFTWARE_VERSION_STRING CONFIG_LED_ORCHESTRA_SW_VERSION_STR
#endif
