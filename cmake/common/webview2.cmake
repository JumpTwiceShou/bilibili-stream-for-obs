# Pinned Microsoft SDK. Link its static loader; use the installed Evergreen runtime.
set(webview2_version "1.0.3537.50")
set(webview2_root "${CMAKE_CURRENT_SOURCE_DIR}/.deps/webview2-${webview2_version}")
if(NOT EXISTS "${webview2_root}/build/native/include/WebView2.h")
  set(webview2_archive "${CMAKE_CURRENT_SOURCE_DIR}/.deps/webview2-${webview2_version}.nupkg")
  file(DOWNLOAD
    "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${webview2_version}/microsoft.web.webview2.${webview2_version}.nupkg"
    "${webview2_archive}"
    EXPECTED_HASH SHA256=5ea526bbd728adda0da4d31219267e96460494a427e4894c4e09d9f320f4b9aa
    TLS_VERIFY ON)
  file(ARCHIVE_EXTRACT INPUT "${webview2_archive}" DESTINATION "${webview2_root}")
endif()
function(target_link_webview2 target)
  target_include_directories(${target} SYSTEM PRIVATE "${webview2_root}/build/native/include")
  target_link_libraries(${target} PRIVATE
    "${webview2_root}/build/native/x64/WebView2LoaderStatic.lib" ole32 oleaut32 shlwapi version)
endfunction()
target_link_webview2(${CMAKE_PROJECT_NAME})
install(FILES "${webview2_root}/LICENSE.txt" "${webview2_root}/NOTICE.txt"
  DESTINATION "${CMAKE_PROJECT_NAME}/data/webview2")
