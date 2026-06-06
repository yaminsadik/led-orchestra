#!/usr/bin/env bash
set -euo pipefail

# Build/flash the S3+H2 one-board hub validation firmware from the stock
# Espressif examples + this package's committed overlays. Nothing in the SDK is
# patched for the hub/BR builds: the ESP Thread BR board's S3<->H2 link is on-PCB
# at the SAME pins the stock examples already use (UART rx=GPIO17 / tx=GPIO18 @
# 460800; RCP update reset=GPIO7 / boot=GPIO8; bundled image dir /rcp_fw/ot_rcp).
#
# Targets:
#   rcp   : ESP-IDF examples/openthread/ot_rcp           -> esp32h2  (the H2 radio)
#   br    : esp-matter examples/thread_border_router      -> esp32s3  (Stage B: BR only)
#   hub   : esp-matter examples/controller + OTBR overlay -> esp32s3  (Stage C: controller + BR host)
#
# This script does NOT modify the ESP-IDF or esp-matter checkout; it builds the
# examples in place with an out-of-tree build dir (-B) under this package's
# build/ (gitignored). See README.md and the per-stage runbooks.

PKG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IDF_EXPORT="${HOME}/esp/esp-idf/export.sh"
MATTER_EXPORT="${HOME}/esp/esp-matter/export.sh"

if [[ ! -f "${IDF_EXPORT}" ]]; then
  echo "ESP-IDF export script not found: ${IDF_EXPORT}" >&2
  exit 1
fi
if [[ ! -f "${MATTER_EXPORT}" ]]; then
  echo "esp-matter export script not found: ${MATTER_EXPORT}" >&2
  exit 1
fi

# shellcheck disable=SC1090
. "${IDF_EXPORT}" >/tmp/s3hub-idf-export.log 2>&1
# shellcheck disable=SC1090
. "${MATTER_EXPORT}" >/tmp/s3hub-matter-export.log 2>&1
export IDF_CCACHE_ENABLE=1

RCP_SRC="${IDF_PATH}/examples/openthread/ot_rcp"
BR_SRC="${ESP_MATTER_PATH}/examples/thread_border_router"
HUB_SRC="${ESP_MATTER_PATH}/examples/controller"

RCP_BUILD="${PKG}/build/rcp-h2"
BR_BUILD="${PKG}/build/br-s3"
HUB_BUILD="${PKG}/build/hub-s3"

# If a build dir exists but was never configured (e.g. a prior interrupted run),
# idf.py's set-target refuses to clean it. Remove only such half-built dirs.
ensure_clean_builddir() {
  local d="$1"
  if [[ -d "${d}" && ! -f "${d}/CMakeCache.txt" ]]; then
    echo "Removing stale (unconfigured) build dir: ${d}"
    rm -rf "${d}"
  fi
}

build_rcp() {
  ensure_clean_builddir "${RCP_BUILD}"
  idf.py -C "${RCP_SRC}" -B "${RCP_BUILD}" \
    -D SDKCONFIG="${RCP_BUILD}/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="${RCP_SRC}/sdkconfig.defaults" \
    set-target esp32h2 build
}

# The thread_border_router builds with CONFIG_AUTO_UPDATE_RCP=y, which bundles an
# RCP OTA image (the rcp_fw SPIFFS partition) generated from CONFIG_RCP_SRC_DIR.
# That Kconfig defaults to the stock in-tree path
# ($IDF_PATH/examples/openthread/ot_rcp/build), but we build the RCP out-of-tree
# under build/rcp-h2. So point CONFIG_RCP_SRC_DIR at our RCP build via a
# machine-local defaults fragment (kept under the gitignored build/, since the
# value is an absolute, host-specific path that must not be committed).
rcp_src_dir_fragment() {
  local frag="${PKG}/build/rcp-src-dir.defaults"
  mkdir -p "${PKG}/build"
  printf 'CONFIG_RCP_SRC_DIR="%s"\n' "${RCP_BUILD}" > "${frag}"
  echo "${frag}"
}

build_br() {
  ensure_clean_builddir "${BR_BUILD}"
  build_rcp                                  # the BR bundles the RCP image; build it first
  local frag; frag="$(rcp_src_dir_fragment)"
  idf.py -C "${BR_SRC}" -B "${BR_BUILD}" \
    -D SDKCONFIG="${BR_BUILD}/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="${BR_SRC}/sdkconfig.defaults;${PKG}/sdkconfig.s3-br-host.defaults;${frag}" \
    set-target esp32s3 build
}

build_hub() {
  ensure_clean_builddir "${HUB_BUILD}"
  idf.py -C "${HUB_SRC}" -B "${HUB_BUILD}" \
    -D SDKCONFIG="${HUB_BUILD}/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="${HUB_SRC}/sdkconfig.defaults.otbr;${PKG}/sdkconfig.s3-otbr-controller.defaults" \
    set-target esp32s3 build
}

flash_rcp() {
  local port="${1:?usage: $0 flash-rcp /dev/cu.usbmodemXXXX  (the H2 USB, if your board exposes it)}"
  build_rcp
  idf.py -C "${RCP_SRC}" -B "${RCP_BUILD}" -p "${port}" erase-flash flash
}

flash_br() {
  local port="${1:?usage: $0 flash-br /dev/cu.usbmodemXXXX  (the S3 USB)}"
  build_br
  idf.py -C "${BR_SRC}" -B "${BR_BUILD}" -p "${port}" erase-flash flash
}

flash_hub() {
  local port="${1:?usage: $0 flash-hub /dev/cu.usbmodemXXXX  (the S3 USB)}"
  build_hub
  idf.py -C "${HUB_SRC}" -B "${HUB_BUILD}" -p "${port}" erase-flash flash
}

monitor() {
  local src="${1:?usage: $0 monitor <rcp|br|hub> /dev/cu.usbmodemXXXX}"
  local port="${2:?usage: $0 monitor <rcp|br|hub> /dev/cu.usbmodemXXXX}"
  case "${src}" in
    rcp) idf.py -C "${RCP_SRC}" -B "${RCP_BUILD}" -p "${port}" monitor ;;
    br)  idf.py -C "${BR_SRC}"  -B "${BR_BUILD}"  -p "${port}" monitor ;;
    hub) idf.py -C "${HUB_SRC}" -B "${HUB_BUILD}" -p "${port}" monitor ;;
    *)   echo "unknown source '${src}' (want rcp|br|hub)" >&2; exit 2 ;;
  esac
}

case "${1:-}" in
  build)      build_rcp; build_br; build_hub ;;
  build-rcp)  build_rcp ;;
  build-br)   build_br ;;
  build-hub)  build_hub ;;
  flash-rcp)  shift; flash_rcp "$@" ;;
  flash-br)   shift; flash_br "$@" ;;
  flash-hub)  shift; flash_hub "$@" ;;
  monitor)    shift; monitor "$@" ;;
  clean)      rm -rf "${PKG}/build" && echo "removed ${PKG}/build" ;;
  *)
    cat <<USAGE
S3+H2 one-board hub validation builder.

Usage:
  $0 build              # host build-verify all three (rcp + br + hub)
  $0 build-rcp          # ot_rcp           -> esp32h2  (the H2 radio)
  $0 build-br           # thread_border_router + offline overlay -> esp32s3 (Stage B)
  $0 build-hub          # controller + OTBR overlay -> esp32s3 (Stage C)
  $0 flash-rcp  <port>  # flash the H2 directly (only if the board exposes H2 USB)
  $0 flash-br   <port>  # flash the S3 with the Stage B BR (auto-updates the H2 RCP)
  $0 flash-hub  <port>  # flash the S3 with the Stage C co-located hub
  $0 monitor <rcp|br|hub> <port>
  $0 clean              # remove build/ (all out-of-tree build dirs)

Ports: pass the S3 USB for br/hub. The H2 RCP is normally updated host-side by
the S3 over the on-board reset/boot pins (CONFIG_AUTO_UPDATE_RCP); flash-rcp is
only for boards that expose a direct-to-H2 USB/UART path. See Stage A.
USAGE
    exit 2
    ;;
esac
