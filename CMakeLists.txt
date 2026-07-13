cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(BUILD_TESTING "Build focused offline regression tests" OFF)

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
  src/core/config_manager.cpp
  src/core/qr_generator.cpp
  src/core/secret_store.cpp
  src/ui/menu_manager.cpp
  src/ui/dialog_factory.cpp
)

find_package(libobs REQUIRED)
find_package(CURL REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs CURL::libcurl OBS::obs-frontend-api Qt6::Core Qt6::Widgets
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

if(BUILD_TESTING)
  enable_testing()
  add_executable(
    plugin-core-tests
    tests/core_utils_tests.cpp
    src/core/api_utils.cpp
    src/core/secret_store.cpp
    src/json11/json11.cpp
  )
  target_include_directories(plugin-core-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  if(WIN32)
    target_link_libraries(plugin-core-tests PRIVATE crypt32)
  endif()
  add_test(NAME plugin-core-tests COMMAND plugin-core-tests)
endif()
