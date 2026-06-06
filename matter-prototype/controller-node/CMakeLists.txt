cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")

# Layer a gitignored local file on top of the committed defaults so secrets
# (operator AP SSID/password) never enter version control. See
# sdkconfig.defaults.local.example. Later entries override earlier ones.
#
# This plain set() deliberately shadows any -D SDKCONFIG_DEFAULTS, so an external
# overlay must be layered through EXTRA_SDKCONFIG_DEFAULTS (env var) instead. The
# Stage 0 border-router validation uses this to build the controller as a non-BR
# client; see matter-prototype/stage0-br-validation/README.md. No-op when unset.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local")
if(DEFINED ENV{EXTRA_SDKCONFIG_DEFAULTS})
  set(SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS};$ENV{EXTRA_SDKCONFIG_DEFAULTS}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(led_orchestra_matter_controller_node)
