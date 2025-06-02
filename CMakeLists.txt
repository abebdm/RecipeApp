# Specifies the minimum version of CMake required to build this project.
cmake_minimum_required(VERSION 3.10)

# Declares the project name and enables C and C++ as project languages.
project(RecipeManager CXX C)

# Set the C++ standard to C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Recipe Database Library (recipedb_lib) ---
# Create a static library that includes your database logic and the SQLite source code.
add_library(recipedb_lib STATIC
    src/database.cpp
    utils/sqlite/sqlite3.c
)

# Set the include directories for the library.
# PUBLIC: Any target that links to this library will automatically inherit these include paths.
target_include_directories(recipedb_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include       # For your project headers (database.h)
    ${CMAKE_CURRENT_SOURCE_DIR}/utils/sqlite  # For the SQLite header (sqlite3.h)
)

# --- SQLite Compile Definitions ---
# Add definitions required for compiling SQLite with specific features.
# SQLITE_ENABLE_FTS5: This is crucial. It enables the Full-Text Search 5 module,
# which is required by your database.cpp code.
target_compile_definitions(recipedb_lib PRIVATE
    SQLITE_ENABLE_FTS5
)

# --- Main Executable (recipe_app) ---
# Create the main executable from main.cpp.
add_executable(recipe_app src/main.cpp)

# Link the executable against your database library.
# This gives recipe_app access to all the functions in recipedb_lib and also
# automatically sets up the necessary include paths because they were declared
# as PUBLIC in the library.
target_link_libraries(recipe_app PRIVATE recipedb_lib)



# --- Test Executable (tests) ---
# Create the test executable from test_database.cpp.
add_executable(tests tests/test_database.cpp)

# Link the test executable against your database library.
target_link_libraries(tests PRIVATE recipedb_lib)

# Set the C++ standard to C++20 for the test executable.
set_target_properties(tests PROPERTIES CXX_STANDARD 20)

# --- Optional: Installation ---
# These lines specify where to install the application and headers if you run
# 'make install'. They are commented out by default.
# install(TARGETS recipe_app DESTINATION bin)
# install(FILES
#     ${CMAKE_CURRENT_SOURCE_DIR}/include/database.h
#     ${CMAKE_CURRENT_SOURCE_DIR}/include/main.h
#     DESTINATION include/RecipeManager
# )

# --- Final Output ---
message(STATUS "Project 'RecipeManager' configured successfully.")
message(STATUS "Main executable 'recipe_app' will be built.")