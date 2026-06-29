# The OTA provider integration is compiled in but its body is gated by
# CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER (no-op stubs when disabled), so the
# default proven build is unchanged. lo_ota_provider is our offline fork of
# esp-matter's esp_matter_ota_provider (DCL/TLS removed, local candidate + hub-
# local HTTP image source); it fully replaces the stock component (never
# co-linked). We require it UNCONDITIONALLY so its include dir always propagates
# here — the component self-gates its own sources on the same config, compiling to
# an empty header-only lib in the default commissioner-only build.
idf_component_register(SRCS
                           "app_main.cpp"
                           "controller_wifi_ingress.cpp"
                           "led_orchestra_console.cpp"
                           "led_orchestra_ota_provider.cpp"
                       INCLUDE_DIRS
                           "."
                           "../../common"
                       REQUIRES
                           esp_event
                           esp_netif
                           esp_wifi
                       PRIV_REQUIRES
                           lo_ota_provider
                       LDFRAGMENTS
                           "linker.lf")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
