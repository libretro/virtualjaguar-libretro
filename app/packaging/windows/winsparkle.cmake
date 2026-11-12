# Included by the Windows app target. Managed, Store and Steam builds use the default.
option(VJ_WINDOWS_DIRECT_UPDATES "Enable WinSparkle for a direct Windows installer" OFF)
if(NOT VJ_WINDOWS_DIRECT_UPDATES)
    return()
endif()

set(VJ_WINSPARKLE_PUBLIC_KEY "" CACHE STRING "Base64 WinSparkle Ed25519 public key")
set(VJ_WINSPARKLE_FEED_BASE "" CACHE STRING "HTTPS directory serving Windows appcasts")
set(VJ_WINSPARKLE_NIGHTLY_BUILD "" CACHE STRING "Monotonic nightly build sequence")

string(LENGTH "${VJ_WINSPARKLE_PUBLIC_KEY}" _vj_key_length)
if(NOT _vj_key_length EQUAL 44 OR NOT VJ_WINSPARKLE_PUBLIC_KEY MATCHES "^[A-Za-z0-9+/]+=$")
    message(FATAL_ERROR "Direct Windows updates require a base64 Ed25519 public key")
endif()
if(NOT VJ_WINSPARKLE_FEED_BASE MATCHES "^https://[A-Za-z0-9.-]+(:[0-9]+)?(/[A-Za-z0-9._/-]+)?$")
    message(FATAL_ERROR "Direct Windows updates require an HTTPS feed directory")
endif()
if(VJ_BUILD_CHANNEL STREQUAL "nightly")
    if(NOT VJ_WINSPARKLE_NIGHTLY_BUILD MATCHES "^[1-9][0-9]*$" OR
       VJ_WINSPARKLE_NIGHTLY_BUILD GREATER 2147483647)
        message(FATAL_ERROR "Direct Windows nightlies require a positive build sequence")
    endif()
    set(_vj_update_channel nightly)
    set(_vj_update_version "${VJ_WINSPARKLE_NIGHTLY_BUILD}")
elseif(VJ_BUILD_CHANNEL STREQUAL "release")
    set(_vj_update_channel stable)
    set(_vj_update_version "${PROJECT_VERSION}")
else()
    message(FATAL_ERROR "Direct Windows updates require the release or nightly build channel")
endif()

if(CMAKE_VS_PLATFORM_NAME)
    string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" _vj_processor)
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _vj_processor)
endif()
if(_vj_processor MATCHES "^(arm64|aarch64)$")
    set(_vj_sdk_arch ARM64)
    set(_vj_update_arch arm64)
elseif(_vj_processor MATCHES "^(amd64|x86_64|x64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_vj_sdk_arch x64)
    set(_vj_update_arch x64)
else()
    message(FATAL_ERROR "WinSparkle direct installers support x64 and ARM64")
endif()

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(winsparkle_sdk
    URL https://github.com/vslavik/winsparkle/releases/download/v0.9.4/WinSparkle-0.9.4.zip
    URL_HASH SHA256=6037df37fc263bd1650a1c4949681a9d40ffe991d01f35892a406cb5d103c976
    SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(winsparkle_sdk)
set(_vj_sdk_bin "${winsparkle_sdk_SOURCE_DIR}/${_vj_sdk_arch}/Release")

configure_file("${CMAKE_CURRENT_LIST_DIR}/winsparkle_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/winsparkle_config.h" @ONLY)
target_sources(virtualjaguar PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/platform/winsparkle_updates.cpp")
target_include_directories(virtualjaguar PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src/platform")
target_compile_definitions(virtualjaguar PRIVATE VJ_WINDOWS_DIRECT_UPDATES=1)
target_include_directories(virtualjaguar PRIVATE
    "${winsparkle_sdk_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(virtualjaguar PRIVATE "${_vj_sdk_bin}/WinSparkle.lib")
add_custom_command(TARGET virtualjaguar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_vj_sdk_bin}/WinSparkle.dll"
        "$<TARGET_FILE_DIR:virtualjaguar>")
install(FILES "${_vj_sdk_bin}/WinSparkle.dll" DESTINATION bin)
install(FILES "${winsparkle_sdk_SOURCE_DIR}/COPYING"
    DESTINATION bin RENAME WinSparkle-LICENSE.txt)
install(FILES "${winsparkle_sdk_SOURCE_DIR}/COPYING.expat"
    DESTINATION bin RENAME WinSparkle-COPYING.expat.txt)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/winsparkle-build.txt"
    CONTENT "${_vj_update_channel} ${_vj_update_arch} ${_vj_update_version}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/winsparkle-build.txt" DESTINATION share/doc/virtualjaguar)
