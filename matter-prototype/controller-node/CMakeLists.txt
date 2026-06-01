cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")

# Layer a gitignored local file on top of the committed defaults so secrets
# (operator AP SSID/password) never enter version control. See
# sdkconfig.defaults.local.example. Later entries override earlier ones.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(led_orchestra_matter_controller_node)
