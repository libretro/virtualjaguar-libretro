# Netplay tests (link-cable play over the network).  Headless, no GL, no internet, and none of them
# needs UDP broadcast except netplay_discovery, which skips (exit 77) where it is not delivered.
#
#   netplay_unit             the stats / host-list parsers, then the REAL core in one process: what the
#                            Netplay window sets reaches the core (loopback, host on a port outside the
#                            option's list, moving the host while connected, join via vj_netlink.txt,
#                            disconnect), and the log tap sees the link go UP and DOWN.
#   netplay_pair             two app processes on localhost, one Host and one Join through the command-line
#                            twin of the window (--netplay host / join): both must see the link UP.
#   netplay_pair_voicemodem  the same with the Voice Modem selected: the core must report that device.
#   netplay_pair_shared      two COPIES on one data folder (instance.h): the test holds instance 1, so the host
#                            and the client are instances 2 and 3.  The link must still come up, the shared
#                            config.ini must be untouched, each copy gets its own save folder, and the
#                            client hands the core its address through the environment (no vj_netlink.txt).
#   netplay_no_partner       control: with nobody to talk to both must FAIL, so the check can fail.
#   netplay_discovery        a client must FIND the host by LAN discovery and join it.  Skipped (77) when
#                            no host is discovered: CI runners and many VMs do not deliver broadcast.
#
# Ports are picked at run time (20000-29999, probed free), never hard-coded.

add_executable(vj_netplay_test
    tests/netplay_test.cpp src/netplay.cpp src/netplay_addr.cpp src/core_host.cpp src/paths.cpp src/config.cpp src/instance.cpp)
target_include_directories(vj_netplay_test PRIVATE src)
target_link_libraries(vj_netplay_test PRIVATE vj_core ${VJ_SDL3_TARGET})
if(WIN32)
    target_link_libraries(vj_netplay_test PRIVATE ws2_32 iphlpapi)
endif()
if(MSVC)
    target_compile_options(vj_netplay_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_netplay_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_netplay_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

set(_yarc "${VJ_ROMS}/yarc.j64")
add_test(NAME netplay_unit COMMAND vj_netplay_test unit "${_yarc}")
add_test(NAME netplay_pair COMMAND vj_netplay_test pair $<TARGET_FILE:virtualjaguar> "${_yarc}" jaglink)
add_test(NAME netplay_pair_voicemodem COMMAND vj_netplay_test pair $<TARGET_FILE:virtualjaguar> "${_yarc}" voicemodem)
add_test(NAME netplay_pair_shared COMMAND vj_netplay_test pair-shared $<TARGET_FILE:virtualjaguar> "${_yarc}")
add_test(NAME netplay_no_partner COMMAND vj_netplay_test control $<TARGET_FILE:virtualjaguar> "${_yarc}")
add_test(NAME netplay_discovery COMMAND vj_netplay_test discovery $<TARGET_FILE:virtualjaguar> "${_yarc}")

set_tests_properties(netplay_unit netplay_pair netplay_pair_voicemodem netplay_pair_shared netplay_no_partner netplay_discovery
    PROPERTIES TIMEOUT 180)
# (netplay_no_partner's children print "netplay: FAIL no link" on purpose, so it has no FAIL pattern.)
set_tests_properties(netplay_unit netplay_pair netplay_pair_voicemodem netplay_pair_shared netplay_discovery
    PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL ")
# The ports and the discovery socket are per-test, but two of these at once would share the machine's
# loopback for no benefit: keep them in order.
set_tests_properties(netplay_unit netplay_pair netplay_pair_voicemodem netplay_no_partner netplay_discovery
    PROPERTIES RUN_SERIAL TRUE)
set_tests_properties(netplay_discovery PROPERTIES SKIP_RETURN_CODE 77)

# #967 phase 2: the same sessions with the emulation thread on -- both sides, and a mixed pair (link play between a
# threaded and a single-thread copy).  netplay_no_partner_threaded: a copy quitting with no peer must exit 1, not 3.
add_test(NAME netplay_pair_threaded COMMAND vj_netplay_test pair $<TARGET_FILE:virtualjaguar> "${_yarc}" jaglink on on)
add_test(NAME netplay_pair_voicemodem_threaded
    COMMAND vj_netplay_test pair $<TARGET_FILE:virtualjaguar> "${_yarc}" voicemodem on on)
add_test(NAME netplay_pair_mixed COMMAND vj_netplay_test pair $<TARGET_FILE:virtualjaguar> "${_yarc}" jaglink on off)
add_test(NAME netplay_no_partner_threaded COMMAND vj_netplay_test control $<TARGET_FILE:virtualjaguar> "${_yarc}" on)
set_tests_properties(netplay_pair_threaded netplay_pair_voicemodem_threaded netplay_pair_mixed netplay_no_partner_threaded
    PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE LABELS "emu_thread")
set_tests_properties(netplay_pair_threaded netplay_pair_voicemodem_threaded netplay_pair_mixed
    PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL |owner violation|ThreadSanitizer")
set_tests_properties(netplay_no_partner_threaded PROPERTIES FAIL_REGULAR_EXPRESSION "owner violation|ThreadSanitizer")

# Settings > Core options > Network: every option in the core's "network" category must show the value
# picked from its combo, across the frames (and option re-pushes) that follow.
add_executable(vj_network_options_test
    tests/network_options_test.cpp src/netplay.cpp src/netplay_addr.cpp src/core_host.cpp src/paths.cpp src/config.cpp)
target_include_directories(vj_network_options_test PRIVATE src)
target_link_libraries(vj_network_options_test PRIVATE vj_core ${VJ_SDL3_TARGET})
if(WIN32)
    target_link_libraries(vj_network_options_test PRIVATE ws2_32 iphlpapi)
endif()
if(MSVC)
    target_compile_options(vj_network_options_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_network_options_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_network_options_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME network_options COMMAND vj_network_options_test "${_yarc}")
set_tests_properties(network_options PROPERTIES TIMEOUT 120 FAIL_REGULAR_EXPRESSION "FAIL ")

# Typed (free-form) option values: validators, CoreHost acceptance, and a custom link port / host name /
# clock-scale percentage read back through the REAL core's own log lines.
add_executable(vj_typed_options_test
    tests/typed_options_test.cpp src/core_host.cpp src/paths.cpp src/config.cpp)
target_include_directories(vj_typed_options_test PRIVATE src)
target_link_libraries(vj_typed_options_test PRIVATE vj_core ${VJ_SDL3_TARGET})
if(WIN32)
    target_link_libraries(vj_typed_options_test PRIVATE ws2_32 iphlpapi)
endif()
if(MSVC)
    target_compile_options(vj_typed_options_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_typed_options_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_typed_options_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME typed_options COMMAND vj_typed_options_test "${_yarc}")
set_tests_properties(typed_options PROPERTIES TIMEOUT 120 FAIL_REGULAR_EXPRESSION "FAIL ")
