add_executable(extract_assets
    main.c
)
target_include_directories(extract_assets PRIVATE
    ${CMAKE_SOURCE_DIR}/GFX
    ${CMAKE_SOURCE_DIR}/SOURCE
    ${CMAKE_SOURCE_DIR}/port/platform
)
target_compile_definitions(extract_assets PRIVATE EXTRACT_TOOL=1)
