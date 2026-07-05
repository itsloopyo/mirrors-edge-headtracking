cmake_minimum_required(VERSION 3.20)
project(MirrorsEdgeHeadTracking VERSION 0.0.0 LANGUAGES CXX C)

# 32-bit only: Mirror's Edge (MirrorsEdge.exe) is an x86 UE3 build, so the .asi
# must be x86. Configure with `-A Win32`.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "MirrorsEdgeHeadTracking must build x86. Configure with -A Win32.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT so the deployed .asi has no MSVC runtime DLL dependency. Must match
# the cameraunlock-core static lib (it sets the same), or linking mixes runtimes.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MinHook (vendored source) as a static lib. Must exist BEFORE add_subdirectory
# below, because the core's hook module checks `if(NOT TARGET minhook)`.
add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde32.c
    third_party/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC third_party/minhook/include)

# Build the shared core static library + hook manager (no tests in the mod build).
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp)

add_library(MirrorsEdgeHeadTracking MODULE
    src/dllmain.cpp
    src/mod.cpp
    src/config.cpp
    src/build_profiles.cpp
    src/camera_probe.cpp
    src/camera_hook.cpp
    src/reticle_overlay.cpp
    src/framerate.cpp
)

set_target_properties(MirrorsEdgeHeadTracking PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "MirrorsEdgeHeadTracking"
)

target_include_directories(MirrorsEdgeHeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(MirrorsEdgeHeadTracking PRIVATE cameraunlock cameraunlock_hooks minhook d3d9 shell32)

if(MSVC)
    target_compile_options(MirrorsEdgeHeadTracking PRIVATE
        /W4 /EHsc
        $<$<CONFIG:Release>:/O2>
    )
    target_compile_definitions(MirrorsEdgeHeadTracking PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
    )
endif()
