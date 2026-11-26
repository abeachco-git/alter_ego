cmake_minimum_required(VERSION 3.20)

# Cross-compilation setup for Windows on Linux (must be before project()).
# Use the UCRT variant to match the upstream DLL runtime family. Do not switch
# to generic x86_64-w64-mingw32 (MSVCRT) for production builds.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32ucrt-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32ucrt-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32ucrt-windres)

project(AlterEgo)
enable_language(RC)
include(cmake/SQLite.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release build for optimized, stripped output
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

# Static linking to avoid Windows DLL dependencies
set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -DWIN32_LEAN_AND_MEAN")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/backends)
include_directories(${CMAKE_SOURCE_DIR}/lib/nlohmann)

# ImGui source files
set(IMGUI_SOURCES
    lib/imgui/imgui.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
)

# Fixed UI artwork is version-controlled and embedded without network access.
# Runtime GW2 item/skin/skill artwork continues using the disk cache.
find_program(PYTHON3_EXECUTABLE python3 REQUIRED)
set(AE_WIKI_STATE "${CMAKE_SOURCE_DIR}/assets/reference/wiki/rotations.json" CACHE FILEPATH "Validated Wiki snapshot")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/wiki-generated")
execute_process(COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/wiki_resources.py"
    --state "${AE_WIKI_STATE}" --generate "${CMAKE_BINARY_DIR}/wiki-generated" COMMAND_ERROR_IS_FATAL ANY)
include_directories("${CMAKE_BINARY_DIR}/wiki-generated")
set(EMBEDDED_ICONS_RC "${CMAKE_BINARY_DIR}/embedded_icons.rc")
file(GLOB FIXED_ICON_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/icons/*.png" "${CMAKE_SOURCE_DIR}/assets/icons/*.jpg")
add_custom_command(
    OUTPUT "${EMBEDDED_ICONS_RC}"
    COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/generate_embedded_icons.py" "${EMBEDDED_ICONS_RC}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/generate_embedded_icons.py"
            "${CMAKE_SOURCE_DIR}/assets/icons/manifest.json" ${FIXED_ICON_FILES}
    COMMENT "Embed fixed profession, elite specialization, crafting and status icons"
    VERBATIM
)

# Source files
set(SOURCES
    src/dllmain.cpp
    "${EMBEDDED_ICONS_RC}"
    src/Clears.cpp
    src/GW2API.cpp
    src/NativeLibrary.cpp
    src/NativeLibraryStore.cpp
    src/FontManager.cpp
    src/IconManager.cpp
    src/ChatLink.cpp
    src/HttpClient.cpp
    src/ArenaNetTransport.cpp
    src/NativeAccounts.cpp
    src/SkinCache.cpp
    src/OwnedSkins.cpp
    src/Commerce.cpp
    src/WikiImage.cpp
    ${IMGUI_SOURCES}
)

# Create shared library (DLL)
add_library(AlterEgo SHARED ${SOURCES})

# Force Windows DLL extension
set_target_properties(AlterEgo PROPERTIES
    SUFFIX ".dll"
    PREFIX ""
)

# Link Windows libraries for Win32 and networking
target_link_libraries(AlterEgo
    ae_sqlite
    gdi32
    wininet
    advapi32
    shell32
    comdlg32
)

# Module definition file for clean DLL export
target_link_options(AlterEgo PRIVATE -Wl,--out-implib,AlterEgo.lib -Wl,--strip-all)
# Linker map for resolving stripped release-DLL crash RVAs (no runtime behavior change).
target_link_options(AlterEgo PRIVATE -Wl,-Map,${CMAKE_BINARY_DIR}/AlterEgo.map)
target_sources(AlterEgo PRIVATE ${CMAKE_SOURCE_DIR}/AlterEgo.def)

# Output directory
set_target_properties(AlterEgo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
