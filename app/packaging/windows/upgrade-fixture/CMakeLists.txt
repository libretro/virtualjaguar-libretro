cmake_minimum_required(VERSION 3.20)
project(VirtualJaguarUpgradeFixture VERSION 3.12.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(VJ_BUILD_CHANNEL nightly)
set(VJ_WINDOWS_DIRECT_UPDATES ON)

add_executable(virtualjaguar fixture.cpp)
target_include_directories(virtualjaguar PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(virtualjaguar PRIVATE shell32)
if(MSVC)
    set_property(TARGET virtualjaguar PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
include(../winsparkle.cmake)

install(TARGETS virtualjaguar RUNTIME DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../../../LICENSE"
    DESTINATION share/doc/virtualjaguar RENAME LICENSE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../THIRD-PARTY-NOTICES.txt"
    DESTINATION share/doc/virtualjaguar)
