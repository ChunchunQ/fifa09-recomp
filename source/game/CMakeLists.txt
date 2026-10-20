# fifa09 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fifa09 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(FIFA09_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(fifa09 WIN32 ${FIFA09_SOURCES})
else()
    add_executable(fifa09 ${FIFA09_SOURCES})
endif()

rexglue_setup_target(fifa09 GPU_PLUGINS xenos)

# FSR (AMD FidelityFX, SDK built with REXGLUE_ENABLE_FIDELITYFX=ON): rexruntime needs
# amd_fidelityfx_dx12*.dll at run time, but the SDK only links it privately, so the
# TARGET_RUNTIME_DLLS copy misses it. Put it next to fifa09.exe (package.ps1 ships *.dll).
if(WIN32 AND TARGET rex::amd_fidelityfx_dx12)
    add_custom_command(TARGET fifa09 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:rex::amd_fidelityfx_dx12> $<TARGET_FILE_DIR:fifa09>
        VERBATIM)
endif()
