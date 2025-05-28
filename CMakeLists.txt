cmake_minimum_required(VERSION 3.10)
project(RecipeManager CXX C)

# Set C++ standard (e.g., C++2o or newer)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Project Source Files ---
set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# --- SQLite3 ---
# # This configuration assumes you have placed sqlite3.c and sqlite3.h
# # in the 'utils/sqlite/' directory relative to this CMakeLists.txt file.
# set(SQLITE3_SOURCES ${PROJECT_SOURCE_DIR}/utils/sqlite/sqlite3.c)
# set(SQLITE3_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/utils/sqlite)

# # Add a define that sqlite3.c often needs.
# # SQLITE_THREADSAFE=0: Single-thread. Only one thread can use SQLite.
# # SQLITE_THREADSAFE=1: Serialized. Multiple threads can use SQLite, but SQLite itself serializes access. (Default)
# # SQLITE_THREADSAFE=2: Multi-thread. Multiple threads can use SQLite concurrently if each thread uses its own database connection.
# # If you compile sqlite3.c, it might have its own defaults. This define can override.
# add_compile_definitions(SQLITE_THREADSAFE=1)

set(SQLITE3_SOURCES ${PROJECT_SOURCE_DIR}/utils/sqlite/sqlite3.c)
set(SQLITE3_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/utils/sqlite)

# --- Diagnostic messages ---
message(STATUS "PROJECT_SOURCE_DIR is: ${PROJECT_SOURCE_DIR}")
set(TEMP_SQLITE_PATH_CHECK "${PROJECT_SOURCE_DIR}/utils/sqlite/sqlite3.c") # Temporary variable for check
message(STATUS "Attempting to use SQLite source: ${TEMP_SQLITE_PATH_CHECK}")
if(NOT EXISTS "${TEMP_SQLITE_PATH_CHECK}")
    message(FATAL_ERROR "CRITICAL: SQLite source file NOT FOUND at ${TEMP_SQLITE_PATH_CHECK}")
else()
    message(STATUS "CONFIRMED: SQLite source file exists at ${TEMP_SQLITE_PATH_CHECK}")
endif()
# --- End diagnostic messages ---

# --- Recipe Database Library (Static) ---
# This library will include our database logic and SQLite3 source.
add_library(recipedb_lib STATIC
    ${PROJECT_SOURCE_DIR}/src/database.cpp
    "${PROJECT_SOURCE_DIR}/utils/sqlite/sqlite3.c"  # Compile sqlite3.c directly into our library
)

# Specify include directories for the recipedb_lib library.
# PUBLIC: Consumers of this library will also inherit these include directories.
target_include_directories(recipedb_lib PUBLIC
    ${PROJECT_SOURCE_DIR}/include       # For database.h
    ${SQLITE3_INCLUDE_DIRS}             # So database.cpp (and its headers) can find sqlite3.h
)

# --- Main Executable ---
# This is your main application that will use the recipedb_lib.
add_executable(recipe_app ${PROJECT_SOURCE_DIR}/src/main.cpp)

# Specify include directories for the main_app executable.
# PRIVATE: These include directories are only for compiling main_app itself.
target_include_directories(recipe_app PRIVATE
    ${PROJECT_SOURCE_DIR}/include       # For main.h and database.h
    # SQLITE3_INCLUDE_DIRS is already inherited if main.cpp includes database.h
)

# Link the main_app executable against our recipedb_lib.
target_link_libraries(recipe_app PRIVATE recipedb_lib)

# --- Optional: Installation ---
# If you want to install the application and headers.
# install(TARGETS recipe_app DESTINATION bin)
# install(FILES ${PROJECT_SOURCE_DIR}/include/database.h ${PROJECT_SOURCE_DIR}/include/main.h DESTINATION include/RecipeManager)

# --- Output ---
# Print some useful information.
message(STATUS "Project Source Directory: ${PROJECT_SOURCE_DIR}")
message(STATUS "SQLite3 Include Directory: ${SQLITE3_INCLUDE_DIRS}")
# message(STATUS "SQLite3 Sources: ${SQLITE3_SOURCES}")
message(STATUS "Main application 'recipe_app' will be built.")

