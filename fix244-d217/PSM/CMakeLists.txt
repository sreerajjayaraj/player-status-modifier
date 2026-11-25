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
    src/hooks/hud_hooks.cpp
    src/hooks/mounted_hooks.cpp
    src/hooks/oxygen_hooks.cpp
    src/oxygen_scanner.cpp
    src/runtime/oxygen_resource.cpp
    src/hud_scanner.cpp
    src/runtime/resource_hud.cpp
    src/runtime/mounted_resource.cpp
    src/runtime/ship_trace.cpp
    src/runtime/ship_proof.cpp
    src/runtime/item_timing.cpp
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
    PLAYER_STATUS_MODIFIER_BUILD_ID="fix244-d217-stat-apply-order"
)

if(MSVC)
    target_link_options(player-status-modifier PRIVATE /Brepro)
    target_compile_options(player-status-modifier PRIVATE
        /Brepro
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
    src/hooks/hud_hooks.cpp
    src/hooks/mounted_hooks.cpp
    src/hooks/oxygen_hooks.cpp
    src/oxygen_scanner.cpp
    src/runtime/oxygen_resource.cpp
    src/hud_scanner.cpp
    src/runtime/resource_hud.cpp
    src/runtime/mounted_resource.cpp
    src/runtime/ship_trace.cpp
    src/runtime/ship_proof.cpp
    src/runtime/item_timing.cpp
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

include(CTest)
if(BUILD_TESTING)
    add_executable(resource-hud-tests tests/resource_hud_tests.cpp)
    target_include_directories(resource-hud-tests PRIVATE src)
    add_test(NAME resource-hud-tests COMMAND resource-hud-tests)
    add_executable(hud-scanner-tests tests/hud_scanner_tests.cpp src/hud_scanner.cpp src/scanner.cpp)
    target_include_directories(hud-scanner-tests PRIVATE src)
    target_compile_definitions(hud-scanner-tests PRIVATE PSM_SCANNER_TESTING NOMINMAX)
    add_executable(mount-scanner-tests tests/mount_scanner_tests.cpp src/hud_scanner.cpp)
    target_include_directories(mount-scanner-tests PRIVATE src)
    target_compile_definitions(mount-scanner-tests PRIVATE NOMINMAX)
    get_target_property(psm_runtime_sources player-status-modifier SOURCES)
    list(FILTER psm_runtime_sources EXCLUDE REGEX "(dllmain\\.cpp|version\\.rc)$")
    add_executable(item-receipt-tests tests/item_receipt_tests.cpp tests/item_plan_tests.cpp tests/menu_item_gain_tests.cpp ${psm_runtime_sources})
    target_include_directories(item-receipt-tests PRIVATE src)
    target_compile_definitions(item-receipt-tests PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN PSM_ITEM_RECEIPT_TESTING)
    target_link_libraries(item-receipt-tests PRIVATE safetyhook Xinput9_1_0)
    add_test(NAME item-receipt-tests COMMAND item-receipt-tests)
    add_executable(item-timing-tests tests/item_timing_tests.cpp src/runtime/item_timing.cpp)
    target_include_directories(item-timing-tests PRIVATE src)
    target_compile_definitions(item-timing-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN PSM_ITEM_TIMING_TESTING)
    add_test(NAME item-timing-tests COMMAND item-timing-tests)

    add_executable(resource-hud-integration-tests
        tests/resource_hud_integration_tests.cpp ${psm_runtime_sources})
    target_include_directories(resource-hud-integration-tests PRIVATE src)
    target_compile_definitions(resource-hud-integration-tests PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
    target_link_libraries(resource-hud-integration-tests PRIVATE safetyhook Xinput9_1_0)
    add_test(NAME resource-hud-integration-tests COMMAND resource-hud-integration-tests)
    add_executable(mounted-resource-tests tests/mounted_resource_tests.cpp ${psm_runtime_sources})
    target_include_directories(mounted-resource-tests PRIVATE src)
    target_compile_definitions(mounted-resource-tests PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN PSM_MOUNT_TESTING)
    target_link_libraries(mounted-resource-tests PRIVATE safetyhook Xinput9_1_0)
    add_test(NAME mounted-resource-tests COMMAND mounted-resource-tests)
    add_test(NAME mounted-resource-3446-tests COMMAND mounted-resource-tests --3446)
    add_executable(damage-resource-isolation-tests
        tests/damage_resource_isolation_tests.cpp ${psm_runtime_sources})
    target_include_directories(damage-resource-isolation-tests PRIVATE src)
    target_compile_definitions(damage-resource-isolation-tests PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
    target_link_libraries(damage-resource-isolation-tests PRIVATE safetyhook Xinput9_1_0)
    add_test(NAME damage-resource-isolation-tests COMMAND damage-resource-isolation-tests)
    add_executable(scanner-image-tests tests/scanner_image_tests.cpp src/scanner.cpp)
    target_include_directories(scanner-image-tests PRIVATE src)
    target_compile_definitions(scanner-image-tests PRIVATE PSM_SCANNER_TESTING NOMINMAX)
    add_executable(native-resource-scaling-tests tests/native_resource_scaling_tests.cpp)
    target_include_directories(native-resource-scaling-tests PRIVATE src)
    add_test(NAME native-resource-scaling-tests COMMAND native-resource-scaling-tests)
    add_executable(player-status-routing-policy-tests
        tests/resource_routing_policy_tests.cpp
    )
    target_include_directories(player-status-routing-policy-tests PRIVATE src)
    add_test(NAME player-status-routing-policy-tests COMMAND player-status-routing-policy-tests)
endif()








if(BUILD_TESTING)
    add_executable(scanner-2944-tests tests/scanner_2944_tests.cpp src/scanner.cpp)
    target_include_directories(scanner-2944-tests PRIVATE src)
    target_compile_definitions(scanner-2944-tests PRIVATE PSM_SCANNER_TESTING NOMINMAX)
endif()

if(BUILD_TESTING)
    add_executable(hud-scanner-2944-tests tests/hud_scanner_2944_tests.cpp src/hud_scanner.cpp src/scanner.cpp)
    target_include_directories(hud-scanner-2944-tests PRIVATE src)
    target_compile_definitions(hud-scanner-2944-tests PRIVATE PSM_SCANNER_TESTING NOMINMAX)
endif()
if(BUILD_TESTING)
 add_executable(scanner-3446-tests tests/scanner_3446_tests.cpp src/scanner.cpp src/hud_scanner.cpp)
 target_include_directories(scanner-3446-tests PRIVATE src)
 target_compile_definitions(scanner-3446-tests PRIVATE PSM_SCANNER_TESTING NOMINMAX)
endif()
set(PSM_NATIVE_TEST_IMAGE "" CACHE FILEPATH "Optional inert 1.0.0.3663 PE for native scanner regression tests")
if(BUILD_TESTING AND PSM_NATIVE_TEST_IMAGE)
 add_test(NAME scanner-3446-tests COMMAND scanner-3446-tests "${PSM_NATIVE_TEST_IMAGE}")
endif()
if(BUILD_TESTING)
 add_executable(oxygen-resource-tests tests/oxygen_resource_tests.cpp src/runtime/oxygen_resource.cpp)
 target_include_directories(oxygen-resource-tests PRIVATE src)
 target_compile_definitions(oxygen-resource-tests PRIVATE NOMINMAX)
 add_test(NAME oxygen-resource-tests COMMAND oxygen-resource-tests)
 add_executable(oxygen-integration-tests tests/oxygen_integration_tests.cpp ${psm_runtime_sources})
 target_include_directories(oxygen-integration-tests PRIVATE src)
 target_compile_definitions(oxygen-integration-tests PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN PSM_MOUNT_TESTING PSM_STAT_WRITE_TESTING)
 target_link_libraries(oxygen-integration-tests PRIVATE safetyhook Xinput9_1_0)
 add_test(NAME oxygen-integration-tests COMMAND oxygen-integration-tests)
 add_executable(oxygen-scanner-tests tests/oxygen_scanner_tests.cpp src/oxygen_scanner.cpp)
 target_include_directories(oxygen-scanner-tests PRIVATE src)
 target_compile_definitions(oxygen-scanner-tests PRIVATE NOMINMAX)
 if(PSM_NATIVE_TEST_IMAGE)
  add_test(NAME oxygen-scanner-tests COMMAND oxygen-scanner-tests "${PSM_NATIVE_TEST_IMAGE}")
 endif()

 if(PSM_OXYGEN_3732_TEST_IMAGE)
  add_executable(oxygen-3732-scanner-tests tests/oxygen_3732_scanner_tests.cpp src/oxygen_scanner.cpp)
  target_include_directories(oxygen-3732-scanner-tests PRIVATE src)
  target_compile_definitions(oxygen-3732-scanner-tests PRIVATE NOMINMAX)
  add_test(NAME oxygen-3732-scanner-tests COMMAND oxygen-3732-scanner-tests "${PSM_OXYGEN_3732_TEST_IMAGE}")
 endif()
endif()

if(BUILD_TESTING)
 add_executable(item-metadata-3446-tests tests/item_metadata_3446_tests.cpp)
 target_include_directories(item-metadata-3446-tests PRIVATE src)
 target_compile_definitions(item-metadata-3446-tests PRIVATE NOMINMAX)
 add_test(NAME item-metadata-3446-tests COMMAND item-metadata-3446-tests)
 add_executable(item-scanner-3446-tests tests/item_scanner_3446_tests.cpp src/scanner.cpp)
 target_include_directories(item-scanner-3446-tests PRIVATE src)
 target_compile_definitions(item-scanner-3446-tests PRIVATE PSM_SCANNER_TESTING NOMINMAX)
 if(PSM_NATIVE_TEST_IMAGE)
  add_test(NAME item-scanner-3446-tests COMMAND item-scanner-3446-tests "${PSM_NATIVE_TEST_IMAGE}")
 endif()
endif()
if(BUILD_TESTING)
 add_executable(stat-write-3446-tests tests/stat_write_3446_tests.cpp ${psm_runtime_sources})
 target_include_directories(stat-write-3446-tests PRIVATE src)
 target_compile_definitions(stat-write-3446-tests PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN PSM_STAT_WRITE_TESTING)
 target_link_libraries(stat-write-3446-tests PRIVATE safetyhook Xinput9_1_0)
 add_test(NAME stat-write-3446-tests COMMAND stat-write-3446-tests)
endif()

if(BUILD_TESTING)
 add_executable(item-gain-math-tests tests/item_gain_math_tests.cpp)
 target_include_directories(item-gain-math-tests PRIVATE src)
 add_test(NAME item-gain-math-tests COMMAND item-gain-math-tests)
endif()

if(BUILD_TESTING)
    add_executable(startup-mid-hook-tests tests/startup_mid_hook_tests.cpp)
    target_include_directories(startup-mid-hook-tests PRIVATE src)
    target_compile_definitions(startup-mid-hook-tests PRIVATE NOMINMAX)
    target_link_libraries(startup-mid-hook-tests PRIVATE safetyhook)
    add_test(NAME startup-mid-hook-tests COMMAND startup-mid-hook-tests)
endif()

if(BUILD_TESTING)
 add_executable(item-capture-compat-tests tests/item_capture_compat_tests.cpp)
 target_include_directories(item-capture-compat-tests PRIVATE src)
 target_compile_definitions(item-capture-compat-tests PRIVATE NOMINMAX)
 if(PSM_NATIVE_TEST_IMAGE AND PSM_LEGACY_TEST_IMAGE)
  add_test(NAME item-capture-compat-tests COMMAND item-capture-compat-tests "${PSM_NATIVE_TEST_IMAGE}" "${PSM_LEGACY_TEST_IMAGE}")
 endif()
endif()

if(BUILD_TESTING)
 add_executable(ship-trace-tests tests/ship_trace_tests.cpp ${psm_runtime_sources})
 target_include_directories(ship-trace-tests PRIVATE src)
 target_compile_definitions(ship-trace-tests PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
 target_link_libraries(ship-trace-tests PRIVATE safetyhook Xinput9_1_0)
 add_test(NAME ship-trace-tests COMMAND ship-trace-tests)
endif()

if(BUILD_TESTING)
 add_executable(ship-trace-scanner-tests tests/ship_trace_scanner_tests.cpp)
 target_include_directories(ship-trace-scanner-tests PRIVATE src)
 target_compile_definitions(ship-trace-scanner-tests PRIVATE NOMINMAX)
 if(PSM_NATIVE_TEST_IMAGE)
  add_test(NAME ship-trace-scanner-tests COMMAND ship-trace-scanner-tests "${PSM_NATIVE_TEST_IMAGE}")
 endif()
endif()

if(BUILD_TESTING)
 add_executable(ship-trace-clock-tests tests/ship_trace_clock_tests.cpp ${psm_runtime_sources})
 target_include_directories(ship-trace-clock-tests PRIVATE src)
 target_compile_definitions(ship-trace-clock-tests PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
 target_link_libraries(ship-trace-clock-tests PRIVATE safetyhook Xinput9_1_0)
 add_test(NAME ship-trace-clock-tests COMMAND ship-trace-clock-tests)
endif()

if(BUILD_TESTING)
 add_executable(item-resolver-diagnostics-tests tests/item_resolver_diagnostics_tests.cpp src/scanner.cpp)
 target_include_directories(item-resolver-diagnostics-tests PRIVATE src)
 target_compile_definitions(item-resolver-diagnostics-tests PRIVATE PSM_SCANNER_TESTING NOMINMAX)
 if(PSM_NATIVE_TEST_IMAGE AND PSM_MENU206_TEST_IMAGE)
  add_test(NAME item-resolver-diagnostics-tests COMMAND item-resolver-diagnostics-tests "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU206_TEST_IMAGE}")
 endif()
endif()

if(BUILD_TESTING)
 add_executable(item-capture-menu206-tests tests/item_capture_menu206_tests.cpp)
 target_include_directories(item-capture-menu206-tests PRIVATE src)
 target_compile_definitions(item-capture-menu206-tests PRIVATE NOMINMAX)
 if(PSM_NATIVE_TEST_IMAGE AND PSM_MENU206_TEST_IMAGE)
  add_test(NAME item-capture-menu206-tests COMMAND item-capture-menu206-tests "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU206_TEST_IMAGE}")
 endif()
endif()
if(BUILD_TESTING)
 add_executable(review-stale-entry-tests tests/review_stale_entry_tests.cpp ${psm_runtime_sources})
 target_include_directories(review-stale-entry-tests PRIVATE src)
 target_compile_definitions(review-stale-entry-tests PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN PSM_STAT_WRITE_TESTING)
 target_link_libraries(review-stale-entry-tests PRIVATE safetyhook Xinput9_1_0)
 add_test(NAME review-stale-entry-tests COMMAND review-stale-entry-tests)
endif()

if(BUILD_TESTING)
 add_executable(ship-callback-benchmark "${CMAKE_SOURCE_DIR}/../private-tests/tests/ShipCallbackBenchmark.cpp" ${psm_runtime_sources})
 target_include_directories(ship-callback-benchmark PRIVATE src "${CMAKE_SOURCE_DIR}/../private-tests/tests")
 target_compile_definitions(ship-callback-benchmark PRIVATE NOMINMAX UNICODE _UNICODE WIN32_LEAN_AND_MEAN PSM_STAT_WRITE_TESTING)
 target_link_libraries(ship-callback-benchmark PRIVATE safetyhook Xinput9_1_0)
endif()

# Add alongside existing capture regressions. The current Menu input stays DATA only.
set(PSM_MENU509_TEST_IMAGE "" CACHE FILEPATH "Exact DesertLinkModMenu v5.0.9 ASI used only as inert test data")
if(BUILD_TESTING AND PSM_MENU509_TEST_IMAGE)
 add_executable(item-capture-menu509-tests tests/item_capture_menu509_tests.cpp)
 target_include_directories(item-capture-menu509-tests PRIVATE src)
 target_compile_definitions(item-capture-menu509-tests PRIVATE NOMINMAX)
 add_test(NAME item-capture-menu509-tests COMMAND item-capture-menu509-tests "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU509_TEST_IMAGE}")
endif()

set(PSM_MENU510_TEST_IMAGE "" CACHE FILEPATH "Exact Menu510 used only as inert test DATA")
if(BUILD_TESTING AND PSM_MENU510_TEST_IMAGE)
 add_executable(item-capture-menu510-tests tests/item_capture_menu510_tests.cpp)
 target_include_directories(item-capture-menu510-tests PRIVATE src)
 target_compile_definitions(item-capture-menu510-tests PRIVATE NOMINMAX)
 add_test(NAME item-capture-menu510-tests COMMAND item-capture-menu510-tests "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU510_TEST_IMAGE}")
endif()

if(BUILD_TESTING AND PSM_MENU512_TEST_IMAGE)
 add_executable(item-capture-menu512-tests tests/item_capture_menu512_tests.cpp)
 target_include_directories(item-capture-menu512-tests PRIVATE src)
 target_compile_definitions(item-capture-menu512-tests PRIVATE NOMINMAX)
 add_test(NAME item-capture-menu512-tests COMMAND item-capture-menu512-tests "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU512_TEST_IMAGE}")
endif()

if(BUILD_TESTING AND PSM_MENU512B_TEST_IMAGE)
 add_executable(item-capture-menu512b-tests tests/item_capture_menu512b_tests.cpp)
 target_include_directories(item-capture-menu512b-tests PRIVATE src)
 target_compile_definitions(item-capture-menu512b-tests PRIVATE NOMINMAX)
 add_test(NAME item-capture-menu512b-tests COMMAND item-capture-menu512b-tests "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU512B_TEST_IMAGE}")
endif()
if(BUILD_TESTING AND PSM_BULK_TEST_IMAGE)
 foreach(variant IN ITEMS "" "-b")
  if(variant STREQUAL "")
   set(bulk_source tests/item_capture_bulk_tests.cpp)
   set(menu_image "${PSM_MENU512_TEST_IMAGE}")
  else()
   set(bulk_source tests/item_capture_bulk_b_tests.cpp)
   set(menu_image "${PSM_MENU512B_TEST_IMAGE}")
  endif()
  add_executable(item-capture-bulk${variant}-tests ${bulk_source})
  target_include_directories(item-capture-bulk${variant}-tests PRIVATE src)
  target_compile_definitions(item-capture-bulk${variant}-tests PRIVATE NOMINMAX)
  add_test(NAME item-capture-bulk${variant}-tests COMMAND item-capture-bulk${variant}-tests "${PSM_NATIVE_TEST_IMAGE}" "${menu_image}" "${PSM_BULK_TEST_IMAGE}")
 endforeach()
endif()

if(BUILD_TESTING AND PSM_MENU512B_TEST_IMAGE AND PSM_NATIVE_TEST_IMAGE)
 add_executable(menu-item-gain-resolver-tests tests/menu_item_gain_resolver_tests.cpp)
 target_include_directories(menu-item-gain-resolver-tests PRIVATE src)
 target_compile_definitions(menu-item-gain-resolver-tests PRIVATE NOMINMAX)
 add_test(NAME menu-item-gain-resolver-tests COMMAND menu-item-gain-resolver-tests "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU512B_TEST_IMAGE}")
endif()

set(PSM_MENU60_TEST_IMAGE "" CACHE FILEPATH "Exact Menu 6.0 file used as inert test data")
if(BUILD_TESTING AND PSM_NATIVE_TEST_IMAGE AND PSM_MENU60_TEST_IMAGE)
    add_executable(damage-3663-scanner-tests tests/damage_3663_scanner_tests.cpp)
    add_executable(item-menu60-scanner-tests tests/item_menu60_scanner_tests.cpp)
    foreach(target IN ITEMS damage-3663-scanner-tests item-menu60-scanner-tests)
        target_include_directories(${target} PRIVATE src)
        target_compile_definitions(${target} PRIVATE NOMINMAX)
        add_test(NAME ${target} COMMAND ${target} "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU60_TEST_IMAGE}")
    endforeach()
endif()
set(PSM_MENU601_TEST_IMAGE "" CACHE FILEPATH "Exact Menu 6.0.1 file used as inert test data")
if(TARGET item-menu60-scanner-tests AND PSM_MENU601_TEST_IMAGE)
    add_test(NAME item-menu601-scanner-tests COMMAND item-menu60-scanner-tests
             "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU601_TEST_IMAGE}")
    add_test(NAME item-menu601-rebuilt-scanner-tests COMMAND item-menu60-scanner-tests
             "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU601_TEST_IMAGE}" rebuilt)
endif()

set(PSM_MENU604_TEST_IMAGE "" CACHE FILEPATH "DLMM 6.0.4 used as inert V1 contract test data")
if(TARGET item-menu60-scanner-tests AND PSM_MENU604_TEST_IMAGE)
    add_test(NAME item-menu604-scanner-tests COMMAND item-menu60-scanner-tests
             "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU604_TEST_IMAGE}")
    add_test(NAME item-menu604-rebuilt-scanner-tests COMMAND item-menu60-scanner-tests
             "${PSM_NATIVE_TEST_IMAGE}" "${PSM_MENU604_TEST_IMAGE}" rebuilt)
endif()

set(PSM_MENU605_TEST_IMAGE "" CACHE FILEPATH "Optional inert DesertLink Menu 6.0.5 PE fixture")
if(BUILD_TESTING AND PSM_OXYGEN_3732_TEST_IMAGE AND PSM_MENU605_TEST_IMAGE)
    add_executable(item-3732-scanner-tests tests/item_3732_scanner_tests.cpp)
    target_include_directories(item-3732-scanner-tests PRIVATE src)
    target_compile_definitions(item-3732-scanner-tests PRIVATE NOMINMAX)
    add_test(NAME item-3732-scanner-tests COMMAND item-3732-scanner-tests
        "${PSM_OXYGEN_3732_TEST_IMAGE}" "${PSM_MENU605_TEST_IMAGE}")
endif()

if(BUILD_TESTING AND PSM_OXYGEN_3732_TEST_IMAGE AND PSM_MENU605_TEST_IMAGE)
    add_executable(resource-3732-scanner-tests tests/resource_3732_scanner_tests.cpp
        src/hud_scanner.cpp src/scanner.cpp)
    target_include_directories(resource-3732-scanner-tests PRIVATE src)
    target_compile_definitions(resource-3732-scanner-tests PRIVATE PSM_SCANNER_TESTING NOMINMAX)
    add_test(NAME resource-3732-scanner-tests COMMAND resource-3732-scanner-tests
        "${PSM_OXYGEN_3732_TEST_IMAGE}" "${PSM_MENU605_TEST_IMAGE}")
endif()
