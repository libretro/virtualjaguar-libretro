# Discord Rich Presence tests (#833).  No window, no GL, no internet, no Discord.
#
#   discord_unit      frame encode / decode, JSON escaping, socket path candidates per OS, the rate limiter,
#                     the activity JSON built from app state (title, states, timestamps, assets, buttons,
#                     escaping), and which box-art URL is allowed.
#   discord_loopback  a fake Discord IPC server on a temp Unix socket: handshake, SET_ACTIVITY, rate-limit
#                     coalescing, ping/pong, the fallback after an ERROR reply, reconnect, "Invalid Client ID",
#                     and the clear on stop.  Skipped (77) on Windows (named pipes).
add_executable(vj_discord_test tests/discord_test.cpp src/discord_ipc.cpp src/discord_presence.cpp)
target_include_directories(vj_discord_test PRIVATE src)
target_link_libraries(vj_discord_test PRIVATE vj_library ${VJ_SDL3_TARGET} Threads::Threads)
if(MSVC)
    target_compile_options(vj_discord_test PRIVATE /W3 /utf-8)
    target_compile_definitions(vj_discord_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vj_discord_test PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
add_test(NAME discord_unit COMMAND vj_discord_test unit)
add_test(NAME discord_loopback COMMAND vj_discord_test loopback)
set_tests_properties(discord_unit discord_loopback PROPERTIES TIMEOUT 60 FAIL_REGULAR_EXPRESSION "FAIL ")
set_tests_properties(discord_loopback PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
