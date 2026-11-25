# Player Status Modifier d217

Tested target is Crimson Desert Windows x64 1.0.0.3732. Attribution remains with the original Player Status Modifier authors and contributors. The vendored dependency source carries its own notices.

Build on MSVC 19.52 x64 with Windows SDK 10.0.26100, CMake 3.28 or later and Ninja. Use the native x64 developer prompt from this directory.

    cmake -S PSM -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded -DBUILD_TESTING=OFF -DPLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR="%CD%/PSM/deps/zydis" -DPLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR="%CD%/PSM/deps/zydis/dependencies/zycore"
    cmake --build build --target player-status-modifier

Use absolute dependency paths in the command if the CMake version resolves a relative path outside this folder. The output is build/player-status-modifier.asi. The tests accept local executable images and optional provider images through the CMake fixture variables. The source manifest records the vendored inputs.

d217 keeps the live typed-stat observer from invalidating the damage layout selector. The dispatcher hook uses a verified interior instruction span separate from the Menu resolver and Tailwind. The 3732 test reproduces actual hook installation order. All 31 offline suites passed. FreedomFlyer uses a separate motion path. Live resource and combat comparison awaits gameplay.
