cmake_minimum_required(VERSION 3.20)

# Cross-compilation setup for Windows on Linux (must be before project()).
# Use the UCRT variant to match the upstream DLL runtime family. Do not switch
# to generic x86_64-w64-mingw32 (MSVCRT) for production builds.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32ucrt-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32ucrt-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32ucrt-windres)

project(AlterEgo VERSION 1.1.2)
enable_language(RC)

# File/product metadata is separate from the upstream Nexus update version.
set(AE_BUILD_NUMBER 0)
set(AE_BUILD_ATTEMPT 0)
set(AE_BUILD_RUN_ID 0)
set(AE_BUILD_COMMIT "unknown")
set(AE_BUILD_CHANNEL "Development")
if(DEFINED ENV{GITHUB_ACTIONS} AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true")
    set(AE_BUILD_NUMBER "$ENV{GITHUB_RUN_NUMBER}")
    set(AE_BUILD_ATTEMPT "$ENV{GITHUB_RUN_ATTEMPT}")
    set(AE_BUILD_RUN_ID "$ENV{GITHUB_RUN_ID}")
    set(AE_BUILD_COMMIT "$ENV{GITHUB_SHA}")
    foreach(field AE_BUILD_NUMBER AE_BUILD_ATTEMPT AE_BUILD_RUN_ID)
        if(NOT "${${field}}" MATCHES "^[1-9][0-9]*$")
            message(FATAL_ERROR "Invalid CI build identity: ${field}")
        endif()
    endforeach()
    string(LENGTH "${AE_BUILD_COMMIT}" AE_COMMIT_LENGTH)
    if(NOT AE_BUILD_COMMIT MATCHES "^[0-9a-f]+$" OR NOT AE_COMMIT_LENGTH EQUAL 40)
        message(FATAL_ERROR "Invalid CI commit SHA")
    endif()
else()
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE AE_GIT_RESULT
            OUTPUT_VARIABLE AE_LOCAL_SHA OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        string(LENGTH "${AE_LOCAL_SHA}" AE_COMMIT_LENGTH)
        if(AE_GIT_RESULT EQUAL 0 AND AE_LOCAL_SHA MATCHES "^[0-9a-f]+$" AND AE_COMMIT_LENGTH EQUAL 40)
            set(AE_BUILD_COMMIT "${AE_LOCAL_SHA}")
            execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE AE_STATUS_RESULT
                OUTPUT_VARIABLE AE_GIT_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(NOT AE_STATUS_RESULT EQUAL 0 OR NOT AE_GIT_STATUS STREQUAL "")
                set(AE_BUILD_COMMIT "${AE_LOCAL_SHA} (local changes or status unknown)")
            endif()
        endif()
    endif()
endif()
# Each fixed Windows version component is an unsigned 16-bit integer.
if(AE_BUILD_NUMBER GREATER 65535)
    message(FATAL_ERROR "CI build number exceeds the Windows version field; revise version policy")
endif()
set(AE_FILE_VERSION "${PROJECT_VERSION}.${AE_BUILD_NUMBER}")
set(AE_PRODUCT_VERSION "${PROJECT_VERSION}-dev")
configure_file(src/BuildInfo.cpp.in "${CMAKE_BINARY_DIR}/BuildInfo.cpp" @ONLY)
configure_file(src/BuildVersion.rc.in "${CMAKE_BINARY_DIR}/BuildVersion.rc" @ONLY)
message(STATUS "Alter Ego file version: ${AE_FILE_VERSION}; commit: ${AE_BUILD_COMMIT}; attempt: ${AE_BUILD_ATTEMPT}")
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
    "${CMAKE_BINARY_DIR}/BuildInfo.cpp"
    "${CMAKE_BINARY_DIR}/BuildVersion.rc"
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
