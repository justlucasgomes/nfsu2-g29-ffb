cmake_minimum_required(VERSION 3.20)
project(g29_ffb VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# NFSU2 is a 32-bit process — must build x86
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "Must target 32-bit (x86).\n"
        "Run:  cmake -A Win32\n"
        "  or: cmake -G \"Visual Studio 17 2022\" -A Win32")
endif()

# ── Sources ────────────────────────────────────────────────────────────────────
set(SOURCES
    src/dllmain.cpp
    src/dinput_proxy.cpp
    src/wheel_input.cpp
    src/force_feedback.cpp
    src/telemetry.cpp
    src/pattern_scan.cpp
    src/config.cpp
    src/logger.cpp
    src/engine_curve.cpp
    src/car_physics.cpp
    src/logitech_led.cpp
)

add_library(g29_ffb SHARED ${SOURCES})

target_include_directories(g29_ffb PRIVATE src)

target_link_libraries(g29_ffb PRIVATE
    dinput8
    dxguid
    winmm
)

target_compile_definitions(g29_ffb PRIVATE
    WIN32
    NOMINMAX
    _WIN32_WINNT=0x0601
    DIRECTINPUT_VERSION=0x0800
    _CRT_SECURE_NO_WARNINGS
    UNICODE _UNICODE
)

if(MSVC)
    target_compile_options(g29_ffb PRIVATE
        /W3 /WX- /O2 /GS- /arch:SSE2
    )
    set_target_properties(g29_ffb PROPERTIES
        LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/src/exports.def\" /MANIFEST:NO"
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(g29_ffb PRIVATE -O2 -Wall -Wno-unknown-pragmas -msse2)
    set_target_properties(g29_ffb PROPERTIES
        LINK_FLAGS "-Wl,--kill-at"
    )
endif()

# Output as dinput8.dll
set_target_properties(g29_ffb PROPERTIES
    OUTPUT_NAME "dinput8"
    PREFIX "" SUFFIX ".dll"
)

# ── WinMM proxy ────────────────────────────────────────────────────────────────
add_library(winmm_proxy SHARED src/winmm_proxy.cpp)

target_compile_definitions(winmm_proxy PRIVATE
    WIN32
    NOMINMAX
    _WIN32_WINNT=0x0601
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(winmm_proxy PRIVATE /W3 /WX- /O2 /GS- /arch:SSE2)
    set_target_properties(winmm_proxy PROPERTIES
        LINK_FLAGS "/DEF:\"${CMAKE_CURRENT_SOURCE_DIR}/src/winmm.def\" /MANIFEST:NO"
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(winmm_proxy PRIVATE -O2 -Wall -msse2)
    set_target_properties(winmm_proxy PROPERTIES LINK_FLAGS "-Wl,--kill-at")
endif()

set_target_properties(winmm_proxy PROPERTIES
    OUTPUT_NAME "winmm"
    PREFIX "" SUFFIX ".dll"
)

# ── Post-build deploy ──────────────────────────────────────────────────────────
set(NFSU2_DIR "$ENV{USERPROFILE}/OneDrive/Documents/NFSU2"
    CACHE PATH "NFSU2 game folder")

add_custom_command(TARGET g29_ffb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Deploying dinput8.dll → ${NFSU2_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:g29_ffb>" "${NFSU2_DIR}/dinput8.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
            "${NFSU2_DIR}/config.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/cars.ini"
            "${NFSU2_DIR}/cars.ini"
    COMMENT "Deploy to NFSU2"
)

add_custom_command(TARGET winmm_proxy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Deploying winmm.dll → ${NFSU2_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:winmm_proxy>" "${NFSU2_DIR}/winmm.dll"
    COMMENT "Deploy winmm to NFSU2"
)
