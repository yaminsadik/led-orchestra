# Requirements

This project uses ESP-IDF/ESP-Matter for the active C++ Matter-over-Thread
prototype.

The completed Rust WiFi/UDP Phase 1/2 implementation is archived on the
`archive/rust-phase-2` branch. Do not add new Rust work on `main`.

## macOS Prerequisites

```bash
xcode-select --install
brew install cmake ninja dfu-util ccache
```

On Apple Silicon Macs, install Rosetta too. Some ESP-Matter/ConnectedHomeIP
host tools, including ZAP tooling in some SDK releases, may be Intel binaries.

```bash
softwareupdate --install-rosetta --agree-to-license
```

## ESP-IDF And ESP-Matter Tooling

Install into `~/esp` to keep vendor SDKs outside this repository.

```bash
mkdir -p ~/esp
cd ~/esp

git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.4.1
git submodule update --init --recursive
./install.sh esp32c6
. ./export.sh
cd ..

git clone --depth 1 --branch release/v1.4.2 https://github.com/espressif/esp-matter.git
cd esp-matter
git submodule update --init --depth 1
cd connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --platform esp32 darwin --shallow
cd ../..
./install.sh
. ./export.sh
```

## Export In Each New Shell

Run these before building the Matter prototype in a fresh terminal:

```bash
. "$HOME/esp/esp-idf/export.sh"
. "$HOME/esp/esp-matter/export.sh"
export IDF_CCACHE_ENABLE=1
```

Sanity check:

```bash
which idf.py
idf.py --version
echo "$IDF_PATH"
echo "$ESP_MATTER_PATH"
```

## Build Checks

Matter prototype:

```bash
cd matter-prototype/led-node
idf.py set-target esp32c6
idf.py build

cd ../controller-node
idf.py set-target esp32c6
idf.py build
```

## Notes

- `docs/handoff*.md` is local-only and intentionally ignored.
- ESP-IDF generated files such as `build/`, `managed_components/`,
  `sdkconfig`, and `dependencies.lock` are ignored inside each Matter app.
- ESP-Matter `release/v1.4.2` is paired here with ESP-IDF `v5.4.1` for the
  first prototype build.
- FastLED is the preferred C++ rendering/effect direction, but it should be
  validated with ESP-Matter on ESP32-C6 before replacing the working
  `led_strip` renderer. Its ESP-IDF CMake path currently expects Arduino-ESP32
  integration, so treat that as an explicit Phase 3 spike.

## Troubleshooting

If ESP-Matter fails while running ZAP with an error like this:

```text
OSError: [Errno 86] Bad CPU type in executable: '.../.environment/cipd/packages/zap/zap-cli'
```

the usual cause on Apple Silicon is an Intel-only `zap-cli` host binary without
Rosetta installed. Install Rosetta, then rerun the ESP-Matter install/export
steps:

```bash
softwareupdate --install-rosetta --agree-to-license

cd "$HOME/esp/esp-matter"
. "$HOME/esp/esp-idf/export.sh"
./install.sh
. ./export.sh
```

To confirm the diagnosis:

```bash
uname -m
file "$HOME/esp/esp-matter/connectedhomeip/connectedhomeip/.environment/cipd/packages/zap/zap-cli"
"$HOME/esp/esp-matter/connectedhomeip/connectedhomeip/.environment/cipd/packages/zap/zap-cli" --version
```

Official setup references:

- ESP-IDF setup: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/linux-macos-setup.html
- ESP-Matter developing guide: https://docs.espressif.com/projects/esp-matter/en/release-v1.4.2/esp32c6/developing.html
