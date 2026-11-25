# Shared native/Windows static SQLite target. All sources are vendored.
set(AE_SQLITE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/sqlite")
foreach(file sqlite3.c sqlite3.h sqlite3ext.h)
    if(NOT EXISTS "${AE_SQLITE_SOURCE_DIR}/${file}")
        message(FATAL_ERROR "Missing vendored SQLite file: ${file}. See lib/sqlite/README.md.")
    endif()
endforeach()
add_library(ae_sqlite STATIC "${AE_SQLITE_SOURCE_DIR}/sqlite3.c")
target_include_directories(ae_sqlite PUBLIC "${AE_SQLITE_SOURCE_DIR}")
target_compile_definitions(ae_sqlite PRIVATE
    SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0
    SQLITE_DEFAULT_FOREIGN_KEYS=1)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(ae_sqlite PUBLIC Threads::Threads m)
endif()
