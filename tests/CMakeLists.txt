# Only the mod TUs the tests actually exercise are compiled in. The hook itself
# (viewpoint_hook.cpp), its diagnostics and the bootstrap only do anything
# against a live game process, so they are left out. The ones listed do pull
# core in (config.cpp needs core's logger and INI reader, game_state.cpp needs
# the UE runtime's guarded reads), which target_link_libraries satisfies.
add_executable(pinballfx_tests
    test_main.cpp
    view_injection_tests.cpp
    config_tests.cpp
    build_profile_tests.cpp
    game_state_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/game_state.cpp
    ${CMAKE_SOURCE_DIR}/src/builds/steam_offsets.cpp
)

target_include_directories(pinballfx_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)

target_link_libraries(pinballfx_tests PRIVATE
    cameraunlock
    cameraunlock_unreal
)

if(MSVC)
    target_compile_options(pinballfx_tests PRIVATE /W4 /WX)
endif()

add_test(NAME pinballfx_tests COMMAND pinballfx_tests)
