#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <variant>
#include <optional>
#include <iostream>
#include "sqlite3.h"

using SqlValue = std::variant<std::string, int, double, int64_t>;

// Structure to hold information for adding an ingredient to a recipe
struct RecipeIngredientInfo {
    std::string name;        // Name of the ingredient
    double quantity;         // Quantity of the ingredient --- if this isn't specified, it will equal -1
    std::string unit;        // Unit for the quantity (e.g., "grams", "ml", "pcs")
    std::string notes;       // Optional notes for this ingredient in the recipe
    bool optional;           // Is this ingredient optional?
};

// Structure to hold all information for a new recipe
struct RecipeData {
    std::string name;                       // Name of the recipe
    std::string description;                // Description of the recipe
    uint16_t prep_time_minutes;              // Preparation time in minutes
    uint16_t cook_time_minutes;              // Cooking time in minutes
    uint16_t servings;                       // Number of servings
    bool is_favorite;                   // Is this a favorite recipe?
    std::string source;                     // Source of the recipe (e.g., "Grandma's cookbook")
    std::string source_url;                 // URL for the recipe source
    std::string author;                     // Author of the recipe

    std::vector<RecipeIngredientInfo> ingredients; // List of ingredients for the recipe
    std::vector<std::string> tags;                 // List of tags associated with the recipe
    std::vector<std::string> instructions;         // List of cooking instructions (steps)
};

// Structure for holding search information
struct SearchData {
    // Main tables
    std::string exact_name; // Exact name of the recipe (for exact match searches)
    std::vector<uint16_t> prep_time_range;  // Preparation time range in minutes
    std::vector<uint16_t> cook_time_range;  // Cooking time range in minutes
    std::vector<uint16_t> servings_range;   // Servings range
    bool is_favorite = false;   // Is this a favorite recipe? False will find both recipes that are favorited and ones that aren't
    std::string source; // Source of the recipe (e.g., "Grandma's cookbook")
    std::string source_url; // URL for the recipe source
    std::string exact_author; // Exact name of the author of the recipe
    std::vector<std::string> dates; // Range of dates to search for recipes added within this range

    // FTS5
    std::string name;       // Search within all names for this string
    std::string keywords;   // Additional keywords to search (in standard FTS5 search format so one string has all keywords)
    std::string author;     // Author to search for

    // Many-to-Many
    std::vector<std::string> ingredients;   // List of ingredients to search for
    std::vector<std::string> tags;  // List of tags to search for
    std::vector<std::string> exclude_tags;  // List of tags to exclude
    std::vector<std::string> exclude_ingredients;   // List of ingredients to exclude
};

class Database {
public:

    static Database* instance();


    /**
     * Destructor
     * Rolls back any uncommitted transactions.
     * Closes the database connection if it is open.
     */
    ~Database();

    /**
     * Opens connection to the SQLite database.
     * If the database file does not exist, it will be created.
     * Creates necessary tables if they do not already exist.
     * @return true if the database is opened successfully, false otherwise.
     */
    bool open();

    /**
     * Opens connection to the SQLite database.
     * If the database file does not exist, it will be created.
     * Creates necessary tables if they do not already exist.
     * @param db_path The path to the SQLite database file to open
     * @return true if the database is opened successfully, false otherwise.
     */
    bool open(const std::string& db_path);

    /**
     * Rolls back any uncommitted transactions.
     * Closes the database connection.
     */
    void close();

    /**
     * Adds a new recipe to the database
     * @param recipe The RecipeData struct containing all recipe information
     * @return The recipe_id of the newly added recipe on success, -1 on failure.
     */
    long long addRecipe(const RecipeData& recipe);

    /**
     * Removes a recipe from the database by its ID.
     * This will also remove all connections to ingredients, tags, and delete associated instructions.
     * It will remove all ingredients and tags from the database if they are not linked to any other recipe.
     * @param recipe_id The ID of the recipe to remove
     * @return true if the recipe was removed successfully, false otherwise.
     */
    bool deleteRecipe(long long recipe_id);

    /**
     * Merges the current database with another database file.
     * Removes any duplicate recipes based on their name, source and author, and ingredients.
     * @param source_db_path The path to the source database file to merge
     * @return true if the merge was successful, false otherwise.
     */
    bool mergeDatabase(const std::string& source_db_path);

    /**
     * Closes connection to current database and opens a new one.
     * @param db_path The path to the new database file to open
     * @return true if the new database is opened successfully, false otherwise.
     */
    bool loadDatabase(const std::string& db_path);

    /**
     * Deletes all data from the current database.
     * This will not delete the database file itself, only its contents.
     * @return true if the database was emptied successfully, false otherwise.
     */
    bool emptyDatabase();

    /**
     * @return true if the database connection is open, false otherwise.
     */
    bool isOpen() const;

    /**
     * Searches for recipes based on the provided search criteria.
     * This includes searching by name, description, preparation time, cooking time, servings, favorite status, source, source URL, author, ingredients, tags, and date ranges.
     * @param search_data The SearchData struct containing all search criteria
     * @return A vector of recipe IDs that match the search criteria.
     * If no recipes match the criteria, an empty vector is returned.
     */
    std::vector<long long> search(const SearchData& criteria);

    /**
     * Retrieves a recipe by its ID.
     * @param recipe_id The ID of the recipe to retrieve
     * @return A RecipeData struct containing all information about the recipe.
     * If the recipe does not exist, an empty RecipeData struct is returned.
     * If an error occurs, std::nullopt is returned
     */
    std::optional<RecipeData> getRecipeById(long long recipe_id);

private:
    sqlite3* db_;                // Pointer to the SQLite database connection object
    std::string db_path_;        // Path to the SQLite database file
    bool is_db_open_;            // Flag to track if the DB is open
    static Database* inst; // Singleton instance of the Database class

    /**
     * Constructor
     * Creates a new Database object without opening a connection.
     */
    explicit Database();

    /**
     * Executes a simple SQL statement
     * @param sql The SQL statement to execute
     * @return true if the SQL statement executed successfully, false otherwise.
     */
    bool executeSQL(const char* sql);

    /**
     * Creates all necessary tables if they do not already exist.
     * @return true if all tables were created successfully or already existed, false otherwise.
     */
    bool initialize();


    /**
     * Gets the ID of an ingredient by name.
     * If the ingredient doesn't exist, it is created.
     * @param name The name of the ingredient
     * @return The ingredient_id of the ingredient on success, -1 on failure.
     */
    long long getOrCreateIngredientId(const std::string& name);


    /**
     * Gets the ID of a tag by name.
     * If the tag doesn't exist, it is created.
     * @param name The name of the tag
     * @return The tag_id of the tag on success, -1 on failure.
     */
    long long getOrCreateTagId(const std::string& name);

    /**
     * Adds a single instruction to a recipe.
     * @param recipe_id The ID of the recipe to which the instruction belongs
     * @param step_number The step number of the instruction
     * @param instruction_text The text of the instruction
     * @return true if the instruction was added successfully, false otherwise.
     */
    bool addInstruction(long long recipe_id, int step_number, const std::string& instruction_text);

    /**
     * Links an ingredient to a recipe.
     * If the ingredient does not exist, it is created.
     * @param recipe_id The ID of the recipe
     * @param ing_info The RecipeIngredientInfo struct containing ingredient details
     * @return true if the ingredient was linked successfully, false otherwise.
     */
    bool linkIngredientToRecipe(long long recipe_id, const RecipeIngredientInfo& ing_info);

    /**
     * Links a tag to a recipe.
     * If the tag does not exist, it is created.
     * @param recipe_id The ID of the recipe
     * @param tag The name of the tag to link
     * @return true if the tag was linked successfully, false otherwise.
     */
    bool linkTagToRecipe(long long recipe_id, const std::string& tag);

    /**
     * Checks if a table exists in the database.
     * @param tableName The name of the table to check
     * @return true if the table exists, false otherwise.
     */
    bool tableExists(const std::string& tableName);

    /**
     * Builds a search query
     * @param criteria The SearchData struct to build the search based on
     * @return A pair containing the SQL query and the values to bind to it
     */
    std::pair<std::string, std::vector<SqlValue>> buildSearchQuery(const SearchData& criteria);

    /**
     * Executes a search
     * @param query_parts std::pair of the sql and parameters created by buildSearchQuery
     * @return std::vector of recipe ids that fall within the search criteria
     */
    std::vector<long long> executeSearch(std::pair<std::string, std::vector<SqlValue>> query_parts);
};

// RAII wrapper for sqlite statements
class SqliteStatement {
public:
    sqlite3_stmt* stmt = nullptr;
    SqliteStatement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
            stmt = nullptr;
        }
    }
    ~SqliteStatement() {
        if (stmt) sqlite3_finalize(stmt);
    }
    operator sqlite3_stmt*() const { return stmt; }
    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;
};

#endif // DATABASE_H
