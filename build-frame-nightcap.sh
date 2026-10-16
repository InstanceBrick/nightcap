#!/bin/bash
set -euo pipefail
cmake -S /work -B /work/build-frame-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DMOCKTAIL_ENABLE_VR=ON -DMOCKTAIL_ENABLE_FSR3=OFF -DMOCKTAIL_VR_DIAGNOSTICS=OFF -DBUILD_TESTING=OFF -DMOCKTAIL_BUILD_FREEBSD_SOCKET_HELPER=OFF -DCMAKE_INSTALL_PREFIX=/opt/nightcap-vr-arm64
cmake --build /work/build-frame-arm64 -j3
cmake --install /work/build-frame-arm64
c++ -std=c++17 -O2 /work/tools/frame_video_settings/nightcap-vr-settings.cc -ldl -o /opt/nightcap-vr-arm64/bin/nightcap-vr-settings
printf 'NIGHTCAP_ARM64_BUILD_COMPLETE\n'
