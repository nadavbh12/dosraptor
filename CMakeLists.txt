cmake_minimum_required(VERSION 3.20)

# Apple Silicon arm64 only.
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "" FORCE)

project(raptor C OBJC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
# The 1992 game source predates ANSI guarantees. The legacy translation
# units are compiled at -std=gnu89 so K&R features (implicit int, return
# from void-returning func, etc.) don't error out. Platform shim files
# stay at the project default (c11/gnu11).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 REQUIRED)
# SDL2_mixer doesn't always ship a CMake config in Homebrew; fall back to
# a hand-rolled find via pkg-config / direct link if find_package misses.
find_package(SDL2_mixer QUIET)
if(NOT SDL2_mixer_FOUND)
    find_path(SDL2_MIXER_INCLUDE_DIR SDL_mixer.h
        HINTS /opt/homebrew/include/SDL2 /usr/local/include/SDL2)
    find_library(SDL2_MIXER_LIBRARY NAMES SDL2_mixer
        HINTS /opt/homebrew/lib /usr/local/lib)
endif()

# ---- Source globs ------------------------------------------------------
# Explicit list because file(GLOB) on macOS with mixed-case filenames
# silently drops one casing when APFS canonicalization gets involved.
# Excluded from build (kept on disk as reference):
#   GFX/DPMIAPI.C   -> port/platform/dpmi_stub.c
#   GFX/TSMAPI.C    -> port/platform/tsm_sdl.c
# File list mirrors SOURCE/OBJDEF.MIF exactly. DERBIS.C, GETGLB.C, TEMP.C
# are vestigial — not in the original Watcom makefile, reference symbols
# that aren't exported (e.g. DERBIS.C uses ESHOT.C's static `eshots[]`).
set(RAPTOR_SOURCE_C
    SOURCE/RAP.C    SOURCE/TILE.C   SOURCE/SHOTS.C  SOURCE/ESHOT.C
    SOURCE/SHADOWS.C SOURCE/ENEMY.C SOURCE/FLAME.C  SOURCE/OBJECTS.C
    SOURCE/BONUS.C  SOURCE/FX.C     SOURCE/INPUT.C  SOURCE/ANIMS.C
    SOURCE/MOVIE.C  SOURCE/STORE.C  SOURCE/INTRO.C  SOURCE/LOADSAVE.C
    SOURCE/HELP.C   SOURCE/WINDOWS.C)
set(RAPTOR_GFX_C
    GFX/exitapi.c   GFX/GFXAPI.C    GFX/GLBAPI.C    GFX/IMSAPI.C
    GFX/KBDAPI.C    GFX/PREFAPI.C   GFX/PTRAPI.C    GFX/swdapi.c
    GFX/VMEMAPI.C)

set(RAPTOR_PLATFORM_SOURCES
    port/platform/sys_main.c
    port/platform/gfx_sdl.c
    port/platform/kbd_sdl.c
    port/platform/ptr_sdl.c
    port/platform/dpmi_stub.c
    port/platform/tsm_sdl.c
    port/platform/test_hooks.c
    port/platform/frame_dump.c
    port/platform/playthrough.c
    port/platform/crash_trace.c
    port/platform/macos_activate.m)

set(RAPTOR_ASM_C_SOURCES
    port/asm_c/gfx_blit.c
    port/asm_c/ptr_blit.c
    port/asm_c/tile_blit.c
    port/asm_c/anim_render.c)

set(RAPTOR_AUDIO_SOURCES
    port/audio/audio_jfa.c
    apodmx/MUS2MID.C)        # apodmx folder is otherwise excluded; this file is portable C
# Same `.C is C++` quirk that bites the legacy sources.
set_source_files_properties(apodmx/MUS2MID.C PROPERTIES
    LANGUAGE C
    COMPILE_FLAGS "-std=gnu99")

set(RAPTOR_LEGACY_SOURCES ${RAPTOR_SOURCE_C} ${RAPTOR_GFX_C})

add_executable(raptor MACOSX_BUNDLE
    ${RAPTOR_PLATFORM_SOURCES}
    ${RAPTOR_ASM_C_SOURCES}
    ${RAPTOR_AUDIO_SOURCES}
    ${RAPTOR_LEGACY_SOURCES})

# ---- Include paths -----------------------------------------------------
# `dos_shim` MUST come first so `#include <dos.h>` etc. hit our empty
# wrappers instead of any host header that happens to share the name.
target_include_directories(raptor PRIVATE
    port/platform/dos_shim
    port/platform
    port/audio
    GFX
    SOURCE
    apodmx)

# ---- Compile options ---------------------------------------------------
# Watcom-era source tolerance, plus the global -include shim that
# neutralizes near/far/cdecl/_interrupt/int386/_disable/_enable etc.
target_compile_options(raptor PRIVATE
    -Wno-deprecated-non-prototype
    -Wno-int-conversion
    -Wno-incompatible-pointer-types
    -Wno-incompatible-function-pointer-types
    -Wno-implicit-function-declaration
    -Wno-implicit-int
    -Wno-unknown-pragmas
    -Wno-unused-value
    -Wno-parentheses
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
    -Wno-pointer-sign
    -Wno-return-type
    -Wno-return-mismatch
    -Wno-switch
    -Wno-deprecated-declarations
    -Wno-nonportable-include-path
    -Wno-non-literal-null-conversion
    -Wno-enum-conversion
    -Wno-enum-compare
    -fno-strict-aliasing
    -include ${CMAKE_SOURCE_DIR}/port/platform/dos_compat.h)

# Apply -std=gnu89 only to the legacy translation units. New port code
# lives in modern C11 idioms. Also force LANGUAGE C — CMake treats
# uppercase `.C` as C++ by default, which silently excludes the legacy
# files from a C-only project.
set_source_files_properties(${RAPTOR_LEGACY_SOURCES} PROPERTIES
    COMPILE_FLAGS "-std=gnu89"
    LANGUAGE C)

# RAP.C defines `main(int, char**)` — rename so it doesn't clash with the
# SDL entry point in port/platform/sys_main.c.
set_source_files_properties(SOURCE/RAP.C PROPERTIES
    COMPILE_DEFINITIONS "main=raptor_main")

# ---- SDL2 link ---------------------------------------------------------
if(TARGET SDL2::SDL2)
    target_link_libraries(raptor PRIVATE SDL2::SDL2 SDL2::SDL2main)
else()
    target_include_directories(raptor PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(raptor PRIVATE ${SDL2_LIBRARIES})
endif()

if(TARGET SDL2_mixer::SDL2_mixer)
    target_link_libraries(raptor PRIVATE SDL2_mixer::SDL2_mixer)
elseif(SDL2_MIXER_LIBRARY)
    target_include_directories(raptor PRIVATE ${SDL2_MIXER_INCLUDE_DIR})
    target_link_libraries(raptor PRIVATE ${SDL2_MIXER_LIBRARY})
else()
    message(FATAL_ERROR "SDL2_mixer not found — `brew install sdl2_mixer`")
endif()

# AppKit needed for macos_activate.m's NSApp foreground promotion.
# (The earlier "AppKit hangs SDL_PollEvent" was actually the macOS
# NSPersistentUIRestorer modal alert, now neutralized in the test
# harness via `defaults write ApplePersistenceIgnoreState YES`.)
target_link_libraries(raptor PRIVATE "-framework AppKit")

# ---- Bundle ------------------------------------------------------------
set_target_properties(raptor PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist
    OUTPUT_NAME "raptor")

# ---- Tools -------------------------------------------------------------
add_subdirectory(tools/extract_assets)
