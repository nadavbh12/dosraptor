# Mirror the legacy compile flags applied to GFX/* in the top-level
# CMakeLists.txt — the GLB code uses Watcom typedefs and DOS shims.
set(EXTRACT_LEGACY_SOURCES
    ${CMAKE_SOURCE_DIR}/GFX/GLBAPI.C
    ${CMAKE_SOURCE_DIR}/GFX/exitapi.c
    ${CMAKE_SOURCE_DIR}/GFX/VMEMAPI.C
)

set_source_files_properties(${EXTRACT_LEGACY_SOURCES}
    PROPERTIES
        COMPILE_FLAGS "-std=gnu89 -include ${CMAKE_SOURCE_DIR}/port/platform/dos_compat.h -w"
        LANGUAGE C
)

# MUS2MID.C is portable C99; needs LANGUAGE C set so CMake does not try to
# invoke a C++ compiler (the .C extension triggers that without the override).
set_source_files_properties(${CMAKE_SOURCE_DIR}/apodmx/MUS2MID.C
    PROPERTIES
        COMPILE_FLAGS "-std=gnu99 -w"
        LANGUAGE C
)

find_package(PNG REQUIRED)

add_executable(extract_assets
    main.c
    png_writer.c
    demo_dumper.c
    level_dumper.c
    sprite_meta_dumper.c
    wav_writer.c
    ${CMAKE_SOURCE_DIR}/apodmx/MUS2MID.C
    ${EXTRACT_LEGACY_SOURCES}
)
target_include_directories(extract_assets PRIVATE
    ${CMAKE_SOURCE_DIR}/GFX
    ${CMAKE_SOURCE_DIR}/SOURCE
    ${CMAKE_SOURCE_DIR}/port/platform
    ${CMAKE_SOURCE_DIR}/port/platform/dos_shim
    ${CMAKE_SOURCE_DIR}/apodmx
)
target_compile_definitions(extract_assets PRIVATE EXTRACT_TOOL=1)
target_link_libraries(extract_assets PRIVATE PNG::PNG)
