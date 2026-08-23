cmake_minimum_required(VERSION 3.20)
project(PinballFXHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP)
endif()

# MinHook (for the GetPlayerViewPoint trampoline). Fetched + built locally;
# cameraunlock-core's hook manager links against this `minhook` target.
#
# Pinned to a commit, not a tag: a tag is a mutable ref upstream, so a build
# that names one is not reproducible and cannot state which bytes it linked -
# which is also what THIRD-PARTY-NOTICES has to record. The commit below is
# what the v1.3.3 tag pointed at.
include(FetchContent)
FetchContent_Declare(
    minhook_src
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        9fbd087432700d73fc571118d6a9697a36443d88  # v1.3.3, 2017-01-07
)
FetchContent_MakeAvailable(minhook_src)
add_library(minhook STATIC
    ${minhook_src_SOURCE_DIR}/src/hook.c
    ${minhook_src_SOURCE_DIR}/src/buffer.c
    ${minhook_src_SOURCE_DIR}/src/trampoline.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde64.c
    ${minhook_src_SOURCE_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC ${minhook_src_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /wd4267 /wd4244 /wd4127 /wd4131)
endif()

# Shared cameraunlock-core (C++ side) with the hook manager and UE runtime
# helpers enabled.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_UNREAL ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/viewpoint_hook.cpp
    src/hook_diagnostics.cpp
    src/caller_trace.cpp
    src/config.cpp
    src/exe_paths.cpp
    src/game_state.cpp
    src/hotkeys.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

add_library(PinballFXHeadTracking SHARED ${MOD_SOURCES})

# The shipped binary is held to the same warning bar as the tests. A hook that
# writes into the game's stack has no margin for an implicit conversion or an
# uninitialised local going unremarked.
if(MSVC)
    target_compile_options(PinballFXHeadTracking PRIVATE /W4 /WX)
endif()

# The version the log banner reports. A log a user sends is unattributable
# without it, and release.ps1 already stamps project(... VERSION ...) above.
target_compile_definitions(PinballFXHeadTracking PRIVATE
    PINBALLFX_HT_VERSION="${PROJECT_VERSION}"
)

target_include_directories(PinballFXHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(PinballFXHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_unreal
    minhook
    ws2_32
    psapi
)

# Output as .asi for Ultimate ASI Loader.
set_target_properties(PinballFXHeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)

# Characterization tests for the parts of the hook that are decidable without a
# game process: the caller gate, the pose maths and the INI contract.
# `pixi run test` builds and runs them.
option(PINBALLFX_BUILD_TESTS "Build unit tests" ON)
if(PINBALLFX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
