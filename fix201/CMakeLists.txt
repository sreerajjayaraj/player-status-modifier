cmake_minimum_required(VERSION 3.20)

project(player-status-modifier LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR "" CACHE PATH "Optional local Zydis v4.1.0 source directory for offline builds")
if(PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR)
    file(TO_CMAKE_PATH "${PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR}" PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR_CMAKE)
    if(EXISTS "${PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR_CMAKE}/CMakeLists.txt")
        set(FETCHCONTENT_SOURCE_DIR_ZYDIS "${PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR_CMAKE}" CACHE PATH "Local Zydis source used by SafetyHook" FORCE)
        message(STATUS "Using local Zydis source: ${PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR_CMAKE}")
    else()
        message(WARNING "PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR does not contain CMakeLists.txt: ${PLAYER_STATUS_MODIFIER_LOCAL_ZYDIS_DIR_CMAKE}")
    endif()
endif()

set(PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR "" CACHE PATH "Optional local Zycore source directory for offline Zydis builds")
if(PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR)
    file(TO_CMAKE_PATH "${PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR}" PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR_CMAKE)
    if(EXISTS "${PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR_CMAKE}/CMakeLists.txt")
        set(ZYAN_ZYCORE_PATH "${PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR_CMAKE}" CACHE PATH "Local Zycore source used by Zydis" FORCE)
        set(ZYDIS_ZYCORE_PATH "${PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR_CMAKE}" CACHE PATH "Local Zycore source used by Zydis" FORCE)
        message(STATUS "Using local Zycore source: ${PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR_CMAKE}")
    else()
        message(WARNING "PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR does not contain CMakeLists.txt: ${PLAYER_STATUS_MODIFIER_LOCAL_ZYCORE_DIR_CMAKE}")
    endif()
endif()

set(SAFETYHOOK_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SAFETYHOOK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/safetyhook)

add_library(player-status-modifier SHARED
    src/config.cpp
    src/config_watcher.cpp
        src/dllmain.cpp
        src/hooks.cpp
        src/hooks/affinity_hooks.cpp
        src/hooks/dragon_limit_hooks.cpp
        src/hooks/durability_hooks.cpp
        src/hooks/economy_hooks.cpp
    src/hooks/install_hooks.cpp
    src/hooks/player_hooks.cpp
    src/key_listener.cpp
    src/logger.cpp
    src/mod_logic.cpp
    src/ptrchain.cpp
    src/ptrchain_resources.cpp
    src/position_control.cpp
            src/runtime/actor_resolve.cpp
            src/runtime/affinity_logic.cpp
            src/runtime/damage_logic.cpp
            src/runtime/durability_logic.cpp
        src/runtime/mount_resolver.cpp
        src/runtime/runtime_state.cpp
        src/runtime/spirit_watcher.cpp
        src/runtime/stat_logic.cpp
        src/scanner.cpp
        src/version.rc
)

target_include_directories(player-status-modifier PRIVATE src)
target_link_libraries(player-status-modifier PRIVATE safetyhook Xinput9_1_0)

target_compile_definitions(player-status-modifier PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
        PLAYER_STATUS_MODIFIER_BUILD_ID="safe-rva-fixed-109-fix201-itemgain-shadow-guard"
    PLAYER_STATUS_MODIFIER_ANALYSIS_LOG_BACKUP=1
)

if(MSVC)
    target_compile_options(player-status-modifier PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
    )
endif()

set_target_properties(player-status-modifier PROPERTIES
    OUTPUT_NAME "player-status-modifier"
    SUFFIX ".asi"
)

add_library(player-status-economy SHARED
    src/config.cpp
    src/config_watcher.cpp
        src/dllmain.cpp
        src/hooks.cpp
        src/hooks/affinity_hooks.cpp
        src/hooks/dragon_limit_hooks.cpp
        src/hooks/durability_hooks.cpp
        src/hooks/economy_hooks.cpp
    src/hooks/install_hooks.cpp
    src/hooks/player_hooks.cpp
    src/key_listener.cpp
    src/logger.cpp
    src/mod_logic.cpp
    src/ptrchain.cpp
    src/ptrchain_resources.cpp
    src/position_control.cpp
            src/runtime/actor_resolve.cpp
            src/runtime/affinity_logic.cpp
            src/runtime/damage_logic.cpp
            src/runtime/durability_logic.cpp
        src/runtime/mount_resolver.cpp
        src/runtime/runtime_state.cpp
        src/runtime/spirit_watcher.cpp
        src/runtime/stat_logic.cpp
        src/scanner.cpp
        src/version.rc
)

target_include_directories(player-status-economy PRIVATE src)
target_link_libraries(player-status-economy PRIVATE safetyhook Xinput9_1_0)

target_compile_definitions(player-status-economy PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    PLAYER_STATUS_MODIFIER_ECONOMY_ONLY
    PLAYER_STATUS_MODIFIER_BUILD_ID="safe-rva-fixed-fix63-economy-stats-tracker-detach-log"
    PLAYER_STATUS_MODIFIER_CONFIG_FILE_NAME=L"player-status-economy.ini"
    PLAYER_STATUS_MODIFIER_LOG_FILE_NAME=L"player-status-economy.log"
    PLAYER_STATUS_MODIFIER_PRIMARY_CONFIG_NAME=L"player-status-economy.ini"
    PLAYER_STATUS_MODIFIER_DEFAULT_CONFIG_NAME=L"player-status-economy.default.ini"
    PLAYER_STATUS_MODIFIER_LAYER_PREFIX=L"player-status-economy."
)

if(MSVC)
    target_compile_options(player-status-economy PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:CXX>:/W4>
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
    )
endif()

set_target_properties(player-status-economy PROPERTIES
    OUTPUT_NAME "player-status-economy"
    SUFFIX ".asi"
)






