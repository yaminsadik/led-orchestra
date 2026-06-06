#!/usr/bin/env bash
set -euo pipefail

# Rebuild/reflash the Stage 0 BR-host + RCP pair for accessible XIAO ESP32C6
# side pins:
#   BR D6 / GPIO16 / TX  ->  RCP D7 / GPIO17 / RX
#   BR D7 / GPIO17 / RX  <-  RCP D6 / GPIO16 / TX
#   BR GND               ->  RCP GND
#
# This script does not modify the ESP-IDF checkout. It copies the ot_br example
# into this package's build directory, patches that copy to use GPIO16/17, then
# builds from the copy.

S0="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IDF_EXPORT="${HOME}/esp/esp-idf/export.sh"

if [[ ! -f "${IDF_EXPORT}" ]]; then
  echo "ESP-IDF export script not found: ${IDF_EXPORT}" >&2
  exit 1
fi

# shellcheck disable=SC1090
. "${IDF_EXPORT}" >/tmp/stage0-idf-export.log

BR_SRC="${S0}/build/ot_br_xiao_sidepins_src"
BR_BUILD="${S0}/build/br-host-sidepins"
RCP_BUILD="${S0}/build/rcp-sidepins"

prepare_br_source() {
  mkdir -p "$(dirname "${BR_SRC}")"
  rsync -a --delete "${IDF_PATH}/examples/openthread/ot_br/" "${BR_SRC}/"

  perl -0pi -e 's/\.rx_pin = 4,\s*\\\n\s*\.tx_pin = 5,/.rx_pin = 17,                                  \\\n            .tx_pin = 16,/s' \
    "${BR_SRC}/main/esp_ot_config.h"

  if ! rg -n "\.rx_pin = 17|\.tx_pin = 16" "${BR_SRC}/main/esp_ot_config.h" >/dev/null; then
    echo "Failed to patch BR-host spinel UART pins in ${BR_SRC}/main/esp_ot_config.h" >&2
    exit 1
  fi
}

build_rcp() {
  idf.py -C "${IDF_PATH}/examples/openthread/ot_rcp" \
    -B "${RCP_BUILD}" \
    -D SDKCONFIG="${RCP_BUILD}/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="${IDF_PATH}/examples/openthread/ot_rcp/sdkconfig.defaults;${S0}/sdkconfig.rcp-sidepins.defaults" \
    build
}

build_br() {
  prepare_br_source
  idf.py -C "${BR_SRC}" \
    -B "${BR_BUILD}" \
    -D SDKCONFIG="${BR_BUILD}/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="${IDF_PATH}/examples/openthread/ot_br/sdkconfig.defaults;${S0}/sdkconfig.br-host.defaults" \
    build
}

flash_rcp() {
  local port="${1:?usage: $0 flash-rcp /dev/cu.usbmodemXXXX}"
  build_rcp
  idf.py -C "${IDF_PATH}/examples/openthread/ot_rcp" \
    -B "${RCP_BUILD}" \
    -p "${port}" \
    erase-flash flash
}

flash_br() {
  local port="${1:?usage: $0 flash-br /dev/cu.usbmodemXXXX}"
  build_br
  idf.py -C "${BR_SRC}" \
    -B "${BR_BUILD}" \
    -p "${port}" \
    erase-flash flash
}

case "${1:-}" in
  build)
    build_rcp
    build_br
    ;;
  build-rcp)
    build_rcp
    ;;
  build-br)
    build_br
    ;;
  flash-rcp)
    shift
    flash_rcp "$@"
    ;;
  flash-br)
    shift
    flash_br "$@"
    ;;
  *)
    cat <<USAGE
Usage:
  $0 build
  $0 build-rcp
  $0 build-br
  $0 flash-rcp /dev/cu.usbmodemXXXX
  $0 flash-br  /dev/cu.usbmodemXXXX

Tomorrow flow:
  1. Flash the RCP board:
       $0 flash-rcp <RCP_PORT>
  2. Flash the BR-host board:
       $0 flash-br <BR_PORT>
  3. Wire:
       BR D6 / GPIO16 / TX  ->  RCP D7 / GPIO17 / RX
       BR D7 / GPIO17 / RX  <-  RCP D6 / GPIO16 / TX
       BR GND               ->  RCP GND
USAGE
    exit 2
    ;;
esac
