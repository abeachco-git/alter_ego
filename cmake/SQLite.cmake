# Shared native/Windows static SQLite target. Prefer committed official sources.
# While the initial vendor commit is being prepared, acquire the exact same
# verified archive into the build tree; never link a host/system SQLite.
set(AE_SQLITE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/sqlite")
if(NOT EXISTS "${AE_SQLITE_SOURCE_DIR}/sqlite3.c" OR
   NOT EXISTS "${AE_SQLITE_SOURCE_DIR}/sqlite3.h" OR
   NOT EXISTS "${AE_SQLITE_SOURCE_DIR}/sqlite3ext.h")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(AE_SQLITE_SOURCE_DIR "${CMAKE_BINARY_DIR}/verified-sqlite")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/vendor_sqlite.py"
                --output "${AE_SQLITE_SOURCE_DIR}"
        RESULT_VARIABLE AE_SQLITE_VENDOR_RESULT)
    if(NOT AE_SQLITE_VENDOR_RESULT EQUAL 0)
        message(FATAL_ERROR "Verified SQLite dependency is unavailable")
    endif()
endif()
add_library(ae_sqlite STATIC "${AE_SQLITE_SOURCE_DIR}/sqlite3.c")
target_include_directories(ae_sqlite PUBLIC "${AE_SQLITE_SOURCE_DIR}")
target_compile_definitions(ae_sqlite PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0
    SQLITE_DEFAULT_FOREIGN_KEYS=1)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(ae_sqlite PUBLIC Threads::Threads m)
endif()
