cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(BUILD_TESTING "Build focused offline regression tests" OFF)
option(BILIBILI_PERSONAL_BUILD "Bundle personal VPN preset resources" OFF)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "This plugin requires ENABLE_FRONTEND_API=ON and ENABLE_QT=ON")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(
  ${CMAKE_PROJECT_NAME} MODULE
  src/bilibili_api.hpp
  src/http_client.hpp
  src/plugin_utils.hpp
  src/md5.hpp
  src/qrcodegen/qrcodegen.hpp
  src/json11/json11.hpp
  src/core/api_utils.hpp
  src/core/config_manager.hpp
  src/core/qr_generator.hpp
  src/core/secret_store.hpp
  src/ui/menu_manager.hpp
  src/ui/dialog_factory.hpp
  src/ui/locale.hpp
  src/plugin-main.cpp
  src/bilibili_api.cpp
  src/http_client.cpp
  src/md5.cpp
  src/qrcodegen/qrcodegen.cpp
  src/json11/json11.cpp
  src/core/api_utils.cpp
  src/core/account_state.cpp
  src/core/login_utils.cpp
  src/core/config_manager.cpp
  src/core/obs_service_guard.cpp
  src/core/stream_routes.cpp
  src/core/qr_generator.cpp
  src/core/secret_store.cpp
  src/ui/menu_manager.cpp
  src/ui/dialog_factory.cpp
  src/ui/proxy_dialog.cpp
  src/ui/stream_routes_dialog.cpp
  src/proxy/api_request_gate.cpp
  src/proxy/proxy_settings.cpp
  src/proxy/openvpn_profile.cpp
  src/proxy/proxy_runtime.cpp
  src/proxy/proxy_runtime.hpp
  src/proxy/rtmp_rewriter.cpp
  src/proxy/stream_relay.cpp
  src/proxy/stream_relay.hpp
  src/proxy/proxy_service.cpp
)

find_package(libobs REQUIRED)
find_package(CURL REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# Optional personal resources stay outside tracked source. Never supply credentials here.
set(PERSONAL_OVPN_TCP "" CACHE FILEPATH "Local personal OpenVPN TCP profile")
set(PERSONAL_OVPN_UDP "" CACHE FILEPATH "Local personal OpenVPN UDP profile")
function(add_personal_vpn_resources target)
  if(NOT BILIBILI_PERSONAL_BUILD AND (PERSONAL_OVPN_TCP OR PERSONAL_OVPN_UDP))
    message(FATAL_ERROR "Public builds must not include personal OpenVPN profiles")
  endif()
  if(PERSONAL_OVPN_TCP OR PERSONAL_OVPN_UDP)
    set(bundled_profiles)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/personal-vpn")
    foreach(transport TCP UDP)
      set(profile "${PERSONAL_OVPN_${transport}}")
      if(NOT EXISTS "${profile}")
        message(FATAL_ERROR "Personal build requires both TCP and UDP profiles")
      endif()
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${profile}")
      file(READ "${profile}" profile_text)
      if(profile_text MATCHES "<key>|<cert>|<auth-user-pass>|PRIVATE KEY|(^|[\r\n])[ \t]*(username|password|auth-user-pass[ \t]+[^#; \r\n])")
        message(FATAL_ERROR "Personal profiles must not contain account credentials or client private keys")
      endif()
      string(TOLOWER "${transport}" alias)
      set(snapshot "${CMAKE_CURRENT_BINARY_DIR}/personal-vpn/${alias}.ovpn")
      set(snapshot_text "")
      if(EXISTS "${snapshot}")
        file(READ "${snapshot}" snapshot_text)
      endif()
      if(NOT EXISTS "${snapshot}" OR NOT snapshot_text STREQUAL profile_text)
        file(WRITE "${snapshot}" "${profile_text}")
      endif()
      list(APPEND bundled_profiles "${snapshot}")
      set_source_files_properties("${snapshot}" PROPERTIES QT_RESOURCE_ALIAS "${alias}.ovpn")
    endforeach()
    qt_add_resources(${target} "personal_vpn_${target}" PREFIX "/personal-vpn"
      FILES ${bundled_profiles})
    target_compile_definitions(${target} PRIVATE PERSONAL_OVPN_BUNDLED=1)
  endif()
endfunction()
add_personal_vpn_resources(${CMAKE_PROJECT_NAME})

set(build_has_private_presets OFF)
if(BILIBILI_PERSONAL_BUILD AND PERSONAL_OVPN_TCP AND PERSONAL_OVPN_UDP)
  set(build_has_private_presets ON)
endif()
add_custom_target(bilibili-build-identity
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DOUTPUT_HEADER=${CMAKE_CURRENT_BINARY_DIR}/build_identity.hpp"
    "-DPRIVATE_PRESETS=${build_has_private_presets}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/build_identity.cmake"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/build_identity.hpp"
  VERBATIM)
add_dependencies(${CMAKE_PROJECT_NAME} bilibili-build-identity)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs CURL::libcurl OBS::obs-frontend-api Qt6::Core Qt6::Widgets Qt6::Network
)
if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE crypt32)
endif()

target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(WIN32)
  set(PERSONAL_PROXY_CORE "${CMAKE_CURRENT_SOURCE_DIR}/.deps/personal-proxy/sing-box-1.14.3-windows-amd64" CACHE PATH "Verified personal proxy core directory")
  install(FILES "${PERSONAL_PROXY_CORE}/sing-box.exe" "${PERSONAL_PROXY_CORE}/LICENSE" DESTINATION "${CMAKE_PROJECT_NAME}/data/proxy")
endif()

if(BUILD_TESTING)
  enable_testing()
  add_executable(
    plugin-core-tests
    tests/core_utils_tests.cpp
    src/core/api_utils.cpp
    src/core/login_utils.cpp
    src/core/secret_store.cpp
    src/json11/json11.cpp
  )
  target_include_directories(plugin-core-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(plugin-core-tests PRIVATE Qt6::Core)
  if(WIN32)
    target_link_libraries(plugin-core-tests PRIVATE crypt32)
  endif()
  add_test(NAME plugin-core-tests COMMAND plugin-core-tests)
  add_executable(plugin-stream-ui-tests tests/stream_ui_tests.cpp src/ui/dialog_factory.cpp
    src/core/qr_generator.cpp src/qrcodegen/qrcodegen.cpp src/core/api_utils.cpp src/json11/json11.cpp
    src/ui/stream_routes_dialog.cpp)
  target_include_directories(plugin-stream-ui-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(plugin-stream-ui-tests PRIVATE Qt6::Core Qt6::Widgets OBS::libobs)
  add_test(NAME plugin-stream-ui-tests COMMAND plugin-stream-ui-tests -platform minimal)
  set_tests_properties(plugin-stream-ui-tests PROPERTIES TIMEOUT 10)
  add_executable(plugin-api-tests tests/bilibili_api_tests.cpp src/bilibili_api.cpp src/md5.cpp
    src/core/api_utils.cpp src/core/account_state.cpp src/core/login_utils.cpp src/json11/json11.cpp)
  target_include_directories(plugin-api-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(plugin-api-tests PRIVATE Qt6::Core Qt6::Network OBS::libobs plugin-support)
  add_test(NAME plugin-api-tests COMMAND plugin-api-tests)
  add_executable(plugin-http-lifecycle-tests tests/http_lifecycle_tests.cpp src/http_client.cpp
    src/core/login_utils.cpp src/core/api_utils.cpp src/json11/json11.cpp)
  target_include_directories(plugin-http-lifecycle-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(plugin-http-lifecycle-tests PRIVATE Qt6::Core Qt6::Network OBS::libobs CURL::libcurl plugin-support)
  add_test(NAME plugin-http-lifecycle-tests COMMAND plugin-http-lifecycle-tests)
  set_tests_properties(plugin-http-lifecycle-tests PROPERTIES TIMEOUT 20)
  if(WIN32)
    add_executable(plugin-proxy-tests tests/proxy_tests.cpp src/proxy/proxy_settings.cpp src/proxy/openvpn_profile.cpp src/core/secret_store.cpp)
    target_include_directories(plugin-proxy-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(plugin-proxy-tests PRIVATE Qt6::Core crypt32)
    add_test(NAME plugin-proxy-tests COMMAND plugin-proxy-tests)
    add_executable(plugin-rtmp-tests tests/rtmp_rewriter_tests.cpp src/proxy/rtmp_rewriter.cpp)
    target_include_directories(plugin-rtmp-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(plugin-rtmp-tests PRIVATE Qt6::Core)
    add_test(NAME plugin-rtmp-tests COMMAND plugin-rtmp-tests)
    add_executable(plugin-proxy-network-tests tests/proxy_network_tests.cpp src/proxy/proxy_settings.cpp src/proxy/openvpn_profile.cpp
      src/proxy/proxy_runtime.cpp src/proxy/proxy_runtime.hpp src/proxy/stream_relay.cpp src/proxy/stream_relay.hpp
      src/proxy/rtmp_rewriter.cpp src/core/secret_store.cpp src/http_client.cpp src/proxy/api_request_gate.cpp)
    target_sources(plugin-proxy-network-tests PRIVATE src/core/login_utils.cpp src/core/api_utils.cpp src/json11/json11.cpp)
    set_target_properties(plugin-proxy-network-tests PROPERTIES AUTOMOC ON)
    target_include_directories(plugin-proxy-network-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(plugin-proxy-network-tests PRIVATE Qt6::Core Qt6::Network crypt32 OBS::libobs CURL::libcurl plugin-support)
    add_test(NAME plugin-proxy-network-tests COMMAND plugin-proxy-network-tests "${PERSONAL_PROXY_CORE}/sing-box.exe")
    add_executable(plugin-proxy-ui-tests tests/proxy_ui_tests.cpp src/ui/proxy_dialog.cpp src/proxy/proxy_settings.cpp src/proxy/openvpn_profile.cpp
      src/proxy/proxy_runtime.cpp src/proxy/proxy_runtime.hpp src/core/secret_store.cpp src/http_client.cpp)
    target_sources(plugin-proxy-ui-tests PRIVATE src/core/login_utils.cpp src/core/api_utils.cpp src/json11/json11.cpp)
    set_target_properties(plugin-proxy-ui-tests PROPERTIES AUTOMOC ON)
    target_include_directories(plugin-proxy-ui-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(plugin-proxy-ui-tests PRIVATE Qt6::Core Qt6::Widgets Qt6::Network crypt32 OBS::libobs CURL::libcurl plugin-support)
    add_personal_vpn_resources(plugin-proxy-ui-tests)
    add_test(NAME plugin-proxy-ui-tests COMMAND plugin-proxy-ui-tests -platform minimal)
    set_tests_properties(plugin-proxy-ui-tests PROPERTIES TIMEOUT 20)
    add_executable(plugin-proxy-service-tests tests/proxy_service_tests.cpp src/proxy/proxy_service.cpp src/core/obs_service_guard.cpp
      src/core/stream_routes.cpp src/core/config_manager.cpp src/core/api_utils.cpp src/core/secret_store.cpp src/json11/json11.cpp)
    target_include_directories(plugin-proxy-service-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "$<TARGET_PROPERTY:OBS::obs-frontend-api,INTERFACE_INCLUDE_DIRECTORIES>")
    target_link_libraries(plugin-proxy-service-tests PRIVATE Qt6::Core Qt6::Network OBS::libobs crypt32 plugin-support)
    add_test(NAME plugin-proxy-service-tests COMMAND plugin-proxy-service-tests)
  endif()
endif()
