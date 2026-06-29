# LED Orchestra offline fork of esp-matter's esp_matter_ota_provider.
#
# Renamed (lo_ota_provider) so it fully REPLACES the stock component rather than
# co-linking with it. The DCL candidate fetch and forced-TLS image download were
# removed: candidates are registered locally and image bytes stream from a
# hub-local HTTP endpoint. See
# matter-prototype/s3-h2-hub-validation/phase-7-offline-ota.md.
#
# The component self-gates its sources on CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER
# (a global sdkconfig symbol from the main component). When the provider is off it
# compiles to an empty, header-only library: the controller's main component can
# then PRIV_REQUIRES it UNCONDITIONALLY (so its include dir always propagates,
# avoiding IDF's flaky conditional-requirement early expansion) while the proven
# commissioner-only default build pulls in none of this server-side code.
set(srcs "")
set(priv_inc "")
if(CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER)
    set(srcs "src/esp_matter_ota_bdx_sender.cpp"
             "src/esp_matter_ota_candidates.cpp"
             "src/esp_matter_ota_http_downloader.cpp"
             "src/esp_matter_ota_provider.cpp")
    set(priv_inc "private_include")
endif()

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "${priv_inc}"
                       REQUIRES esp_matter esp_http_client)
