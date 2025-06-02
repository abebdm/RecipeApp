#include "database.h"
#include <iostream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <ranges>
#include <numeric>


Database* Database::inst = nullptr;


std::vector<RecipeIngredientInfo> parseAllIngredients(const std::string& all_ingredients_str);


std::vector<std::string> splitString(const std::string& str, char delimiter);


RecipeIngredientInfo getIngredientInfo(const std::string& ingredient_str);


Database::Database() : db_(nullptr), is_db_open_(false)
{
}


Database* Database::instance() {
    static Database inst;
    return &inst;
}


bool Database::open() {
    if (is_db_open_) return true;

    int rc = sqlite3_open_v2(db_path_.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);

    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    is_db_open_ = true;

    // Create necessary tables if they do not already exist
    if (!initialize()) {
        std::cerr << "Failed to create necessary tables." << std::endl;
        close();
        return false;
    }

    is_db_open_ = true;
    return true;
}


bool Database::open(const std::string& db_path) {
    if (is_db_open_) close();
    db_path_ = db_path;
    return open();
}


Database::~Database() {
    close();
}


void Database::close() {
    if (is_db_open_ && db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
        is_db_open_ = false;
    }
}


bool Database::isOpen() const {
    return is_db_open_ && db_ != nullptr;
}


bool Database::executeSQL(const char* sql) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot execute SQL." << std::endl;
        return false;
    }
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << " (Query: " << sql << ")" << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}


bool Database::tableExists(const std::string& tableName) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot check if table exists." << std::endl;
        return false;
    }

    std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name=?;";
    SqliteStatement stmt_wrapper(db_, sql.c_str());
    sqlite3_stmt* stmt = stmt_wrapper.stmt;
    
    if (stmt == nullptr) return false;

    sqlite3_bind_text(stmt, 1, tableName.c_str(), -1, SQLITE_STATIC);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);

    return exists;
}


bool Database::initialize() {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot create tables." << std::endl;
        return false;
    }

    // Prevents things like deleting a recipe without also handling its ingredients and tags or inserting an ingredient into the recipe_ingredients table with an invalid recipe
    if (!executeSQL("PRAGMA foreign_keys = ON;")) {
        std::cerr << "Failed to enable foreign key constraints." << std::endl;
        close();
        return false;
    }

    if (!executeSQL("BEGIN TRANSACTION;")) {
        std::cerr << "Failed to begin transaction." << std::endl;
        return false;
    }

    const char* schema_script = R"(
        CREATE TABLE IF NOT EXISTS recipes (
            recipe_id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            description TEXT,
            prep_time_minutes INTEGER,
            cook_time_minutes INTEGER,
            servings INTEGER,
            is_favorite BOOLEAN DEFAULT 0 CHECK (is_favorite IN (0, 1)),
            date_added TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            source TEXT,
            source_url TEXT,
            author TEXT
        );

        CREATE TABLE IF NOT EXISTS ingredients (
            ingredient_id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        );

        CREATE TABLE IF NOT EXISTS tags (
            tag_id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        );

        CREATE TABLE IF NOT EXISTS recipe_ingredients (
            recipe_id INTEGER NOT NULL,
            ingredient_id INTEGER NOT NULL,
            quantity REAL,
            unit TEXT,
            notes TEXT,
            optional BOOLEAN DEFAULT 0 CHECK (optional IN (0, 1)),
            PRIMARY KEY (recipe_id, ingredient_id),
            FOREIGN KEY (recipe_id) REFERENCES recipes(recipe_id) ON DELETE CASCADE,
            FOREIGN KEY (ingredient_id) REFERENCES ingredients(ingredient_id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS recipe_tags (
            recipe_id INTEGER NOT NULL,
            tag_id INTEGER NOT NULL,
            PRIMARY KEY (recipe_id, tag_id),
            FOREIGN KEY (recipe_id) REFERENCES recipes(recipe_id) ON DELETE CASCADE,
            FOREIGN KEY (tag_id) REFERENCES tags(tag_id) ON DELETE CASCADE
        );
        
        CREATE TABLE IF NOT EXISTS instructions (
            instruction_id INTEGER PRIMARY KEY AUTOINCREMENT,
            recipe_id INTEGER NOT NULL,
            step_number INTEGER NOT NULL,
            instruction TEXT NOT NULL,
            FOREIGN KEY (recipe_id) REFERENCES recipes(recipe_id) ON DELETE CASCADE,
            UNIQUE (recipe_id, step_number)
        );
    )";

    if (!executeSQL(schema_script)) {
        std::cerr << "Failed to create base tables." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    // Create FTS5 virtual table
    const char* create_fts_table_sql = R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS search USING fts5(
            recipe_id,
            name,
            description,
            author,
            ingredients,
            tags,
            tokenize = 'porter unicode61'
        );

        CREATE TRIGGER IF NOT EXISTS recipe_after_insert
        AFTER INSERT ON recipes
        BEGIN
            INSERT OR REPLACE INTO search(rowid, name, description, author)
            VALUES (new.recipe_id, new.name, new.description, new.author);
        END;

        CREATE TRIGGER IF NOT EXISTS recipe_after_update AFTER UPDATE ON recipes
        BEGIN
            UPDATE search
            SET
                name = new.name,
                description = new.description,
                author = new.author
            WHERE rowid = new.recipe_id;
        END;

        CREATE TRIGGER IF NOT EXISTS recipe_after_delete AFTER DELETE ON recipes
        BEGIN
            DELETE FROM search WHERE rowid = old.recipe_id;
        END;

        CREATE TRIGGER IF NOT EXISTS update_ingredients_on_insert
        AFTER INSERT ON recipe_ingredients
        BEGIN
            UPDATE search
            SET ingredients = (
                SELECT COALESCE(group_concat(name, '|'), '')
                from INGREDIENTS i
                JOIN recipe_ingredients ri ON i.ingredient_id = ri.ingredient_id
                WHERE ri.recipe_id = new.recipe_id
            )
            WHERE rowid = NEW.recipe_id;
        END;

        CREATE TRIGGER IF NOT EXISTS update_ingredients_on_delete
        AFTER DELETE ON recipe_ingredients
        BEGIN
            UPDATE search
            SET ingredients = (
                SELECT COALESCE(group_concat(name, '|'), '')
                FROM ingredients i
                JOIN recipe_ingredients ri ON i.ingredient_id = ri.ingredient_id
                WHERE ri.recipe_id = OLD.recipe_id
            )
            WHERE rowid = OLD.recipe_id;
        END;

        CREATE TRIGGER IF NOT EXISTS update_tags_on_insert
        AFTER INSERT ON recipe_tags
        BEGIN
            UPDATE search
            SET tags = (
                SELECT COALESCE(group_concat(name, '|'), '')
                FROM tags t
                JOIN recipe_tags rt ON t.tag_id = rt.tag_id
                WHERE rt.recipe_id = NEW.recipe_id
            )
            WHERE rowid = NEW.recipe_id;
        END;

        CREATE TRIGGER IF NOT EXISTS update_tags_on_delete
        AFTER DELETE ON recipe_tags
        BEGIN
            UPDATE search
            SET tags = (
                SELECT COALESCE(group_concat(name, '|'), '')
                FROM tags t
                JOIN recipe_tags rt ON t.tag_id = rt.tag_id
                WHERE rt.recipe_id = OLD.recipe_id
            )
            WHERE rowid = OLD.recipe_id;
        END;

        INSERT OR REPLACE INTO search (rowid, name, description, author, ingredients, tags)
        SELECT
            r.recipe_id,
            r.name,
            r.description,
            r.author,
            COALESCE(group_concat(i.name, '|'), ''),
            COALESCE(group_concat(t.name, '|'), '')
        FROM recipes AS r
        LEFT JOIN recipe_ingredients AS ri ON r.recipe_id = ri.recipe_id
        LEFT JOIN ingredients AS i ON ri.ingredient_id = i.ingredient_id
        LEFT JOIN recipe_tags AS rt ON r.recipe_id = rt.recipe_id
        LEFT JOIN tags AS t ON rt.tag_id = t.tag_id
        GROUP BY
            r.recipe_id;
    )";
    if (!executeSQL(create_fts_table_sql)) {
        std::cerr << "Failed to create FTS5 virtual table." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    if (!executeSQL("COMMIT;")) {
        std::cerr << "Failed to commit transaction." << std::endl;
        // Attempt to rollback, though the state might be inconsistent if commit itself fails
        executeSQL("ROLLBACK;");
        return false;
    }

    return true;
}


long long Database::addRecipe(const RecipeData& recipe) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot execute SQL." << std::endl;
        return false;
    }

    if (recipe.name.empty()) {
        std::cerr << "Recipe name cannot be empty. Recipe not added." << std::endl;
        return -1;
    }

    if (!executeSQL("BEGIN TRANSACTION;")) {
        std::cerr << "Failed to begin transaction." << std::endl;
        return -1;
    }

    long long new_recipe_id = -1;

    // Insert into recipes table
    const char* recipe_sql = R"(
        INSERT INTO recipes (name, description, prep_time_minutes, cook_time_minutes, servings, is_favorite, source, source_url, author)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    SqliteStatement stmt_wrapper(db_, recipe_sql);
    sqlite3_stmt* stmt = stmt_wrapper.stmt;

    if (stmt == nullptr) {
        executeSQL("ROLLBACK;");
        return -1;
    }

    sqlite3_bind_text(stmt, 1, recipe.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, recipe.description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, recipe.prep_time_minutes);
    sqlite3_bind_int(stmt, 4, recipe.cook_time_minutes);
    sqlite3_bind_int(stmt, 5, recipe.servings);
    sqlite3_bind_int(stmt, 6, recipe.is_favorite ? 1 : 0);
    sqlite3_bind_text(stmt, 7, recipe.source.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, recipe.source_url.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, recipe.author.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert recipe: " << sqlite3_errmsg(db_) << std::endl;
        executeSQL("ROLLBACK;");
        return -1;
    }
    new_recipe_id = sqlite3_last_insert_rowid(db_);
    stmt = nullptr;

    // Insert ingredients into recipe_ingredients table
    for (const RecipeIngredientInfo& ingredient : recipe.ingredients) {
        if (linkIngredientToRecipe(new_recipe_id, ingredient) == false) {
            std::cerr << "Failed to link ingredient: " << ingredient.name << " to recipe ID: " << new_recipe_id << std::endl;
            executeSQL("ROLLBACK;");
            return -1;
        }
    }

    // Insert tags into recipe_tags table
    for (const std::string& tag : recipe.tags) {
        if (linkTagToRecipe(new_recipe_id, tag) == false) {
            std::cerr << "Failed to link tag: " << tag << " to recipe ID: " << new_recipe_id << std::endl;
            executeSQL("ROLLBACK;");
            return -1;
        }
    }

    // Insert instructions
    for (size_t i = 0; i < recipe.instructions.size(); ++i) {
        if (!addInstruction(new_recipe_id, i + 1, recipe.instructions[i])) {
            std::cerr << "Failed to add instruction step " << (i + 1) << " for recipe ID: " << new_recipe_id << std::endl;
            executeSQL("ROLLBACK;");
            return -1;
        }
    }

    // Commit the transaction
    if (!executeSQL("COMMIT;")) {
        std::cerr << "Failed to commit transaction." << std::endl;
        // Attempt to rollback, though the state might be inconsistent if commit itself fails
        executeSQL("ROLLBACK;");
        return -1;
    }

    return new_recipe_id;
}


bool Database::deleteRecipe(long long recipe_id) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot remove recipe." << std::endl;
        return false;
    }

    if (recipe_id <= 0) {
        std::cerr << "Invalid recipe ID: " << recipe_id << std::endl;
        return false;
    }

    if (!executeSQL("BEGIN TRANSACTION;")) {
        std::cerr << "Failed to begin transaction." << std::endl;
        return false;
    }

    const char* delete_recipe_sql = "DELETE FROM recipes WHERE recipe_id = ?;";
    SqliteStatement stmt_wrapper(db_, delete_recipe_sql);
    sqlite3_stmt* stmt = stmt_wrapper.stmt;

    if (stmt == nullptr) {
        executeSQL("ROLLBACK;");
        return false;
    }

    sqlite3_bind_int64(stmt, 1, recipe_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to delete recipe: " << sqlite3_errmsg(db_) << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }
    stmt = nullptr;
    
    const char* clean_ingredients_sql = R"(
        DELETE FROM ingredients
        WHERE ingredient_id NOT IN (SELECT DISTINCT ingredient_id FROM recipe_ingredients);
    )";
    if (!executeSQL(clean_ingredients_sql)) {
        std::cerr << "Failed to clean ingredients table." << std::endl;
    }

    const char* clean_tags_sql = R"(
        DELETE FROM tags
        WHERE tag_id NOT IN (SELECT DISTINCT tag_id FROM recipe_tags);
    )";
    if (!executeSQL(clean_tags_sql)) {
        std::cerr << "Failed to clean tags table." << std::endl;
    }

    // Commit the transaction
    if (!executeSQL("COMMIT;")) {
        std::cerr << "Failed to commit transaction." << std::endl;
        // Attempt to rollback, though the state might be inconsistent if commit itself fails
        executeSQL("ROLLBACK;");
        return false;
    }

    return true;
}


bool Database::mergeDatabase(const std::string& source_db_path) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot merge databases." << std::endl;
        return false;
    }

    const char* attach_db_sql = R"(
        ATTACH DATABASE ? AS source_db;
    )";

    SqliteStatement stmt_wrapper(db_, attach_db_sql);
    sqlite3_stmt* stmt = stmt_wrapper.stmt;

    if (stmt == nullptr) return false;

    sqlite3_bind_text(stmt, 1, source_db_path.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to delete recipe: " << sqlite3_errmsg(db_) << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }
    stmt = nullptr;

    const char* merge_script = R"(
        BEGIN TRANSACTION;

        PRAGMA foreign_keys = OFF;

        -- STEP 1: Merge independent (ingredients and tags) tables
        INSERT INTO main.ingredients (name) SELECT s.name FROM source_db.ingredients AS s
        WHERE NOT EXISTS (SELECT 1 FROM main.ingredients AS t WHERE lower(t.name) = lower(s.name));

        CREATE TEMP TABLE ingredient_id_map (source_id INTEGER PRIMARY KEY, target_id INTEGER NOT NULL);
        INSERT INTO ingredient_id_map (source_id, target_id)
        SELECT s.ingredient_id, t.ingredient_id FROM source_db.ingredients AS s JOIN main.ingredients AS t ON lower(s.name) = lower(t.name);

        INSERT INTO main.tags (name) SELECT s.name FROM source_db.tags AS s
        WHERE NOT EXISTS (SELECT 1 FROM main.tags AS t WHERE lower(t.name) = lower(s.name));

        CREATE TEMP TABLE tag_id_map (source_id INTEGER PRIMARY KEY, target_id INTEGER NOT NULL);
        INSERT INTO tag_id_map (source_id, target_id)
        SELECT s.tag_id, t.tag_id FROM source_db.tags AS s JOIN main.tags AS t ON lower(s.name) = lower(t.name);

        -- STEP 2: Pre-calculate ingredient set for each recipe
        CREATE TEMP TABLE source_recipe_ingredients_set AS
        SELECT recipe_id, group_concat(name, '|') as ingredient_set FROM
            (SELECT ri.recipe_id, i.name FROM source_db.recipe_ingredients AS ri JOIN source_db.ingredients AS i ON ri.ingredient_id = i.ingredient_id ORDER BY i.name)
        GROUP BY recipe_id;

        CREATE TEMP TABLE target_recipe_ingredients_set AS
        SELECT recipe_id, group_concat(name, '|') as ingredient_set FROM
            (SELECT ri.recipe_id, i.name FROM main.recipe_ingredients AS ri JOIN main.ingredients AS i ON ri.ingredient_id = i.ingredient_id ORDER BY i.name)
        GROUP BY recipe_id;

        -- STEP 3: Build master recipe map
        CREATE TEMP TABLE recipe_id_map (source_id INTEGER PRIMARY KEY, target_id INTEGER NOT NULL, is_duplicate BOOLEAN NOT NULL);
        CREATE TEMP TABLE vars(max_recipe_id INTEGER);
        INSERT INTO vars(max_recipe_id) SELECT IFNULL(MAX(recipe_id), 0) FROM main.recipes;

        --Pass 1: Identify and map duplicates
        INSERT INTO recipe_id_map (source_id, target_id, is_duplicate)
        SELECT
            s.recipe_id,
            t.recipe_id,
            1
        FROM source_db.recipes AS s
        JOIN source_recipe_ingredients_set AS s_ings ON s.recipe_id = s_ings.recipe_id
        JOIN target_recipe_ingredients_set AS t_ings ON s_ings.ingredient_set = t_ings.ingredient_set
        JOIN main.recipes AS t ON t.recipe_id = t_ings.recipe_id
        WHERE
            lower(s.name) = lower(t.name)
            AND (
                (s.author IS NOT NULL AND s.author != '' AND lower(s.author) = lower(t.author)) OR
                (s.source IS NOT NULL AND s.source != '' AND lower(s.source) = lower(t.source)) OR
                (s.source_url IS NOT NULL AND s.source_url != '' AND lower(s.source_url) = lower(t.source_url))
            );

        --Pass 2: Identify and map unique recipes
        INSERT INTO recipe_id_map (source_id, target_id, is_duplicate)
        SELECT
            s.recipe_id,
            s.recipe_id + (SELECT max_recipe_id FROM vars),
            0
        FROM source_db.recipes AS s
        WHERE s.recipe_id NOT IN (SELECT source_id FROM recipe_id_map);

        --STEP 4: Perform merge based on map
        INSERT INTO main.recipes (
            recipe_id, name, description, prep_time_minutes, cook_time_minutes,
            servings, is_favorite, date_added, source, source_url, author
        )
        SELECT
            map.target_id,
            s.name,
            s.description,
            s.prep_time_minutes,
            s.cook_time_minutes,
            s.servings,
            s.is_favorite,
            s.date_added,
            s.source,
            s.source_url,
            s.author
        FROM source_db.recipes AS s
        JOIN recipe_id_map AS map ON s.recipe_id = map.source_id
        WHERE map.is_duplicate = 0;

        INSERT OR IGNORE INTO main.recipe_tags (recipe_id, tag_id)
        SELECT
            map.target_id,
            tag_map.target_id
        FROM source_db.recipe_tags AS s_rt
        JOIN recipe_id_map AS map ON s_rt.recipe_id = map.source_id
        JOIN tag_id_map AS tag_map ON s_rt.tag_id = tag_map.source_id
        WHERE map.is_duplicate = 1;

        INSERT INTO main.recipe_ingredients (
            recipe_id, ingredient_id, quantity, unit, notes, optional
        )
        SELECT
            map.target_id,
            ing_map.target_id,
            s_ri.quantity,
            s_ri.unit,
            s_ri.notes,
            s_ri.optional
        FROM source_db.recipe_ingredients AS s_ri
        JOIN recipe_id_map AS map ON s_ri.recipe_id = map.source_id
        JOIN ingredient_id_map AS ing_map ON s_ri.ingredient_id = ing_map.source_id
        WHERE map.is_duplicate = 0; -- Only insert for new recipes

        INSERT INTO main.recipe_tags (recipe_id, tag_id)
        SELECT
            map.target_id,
            tag_map.target_id
        FROM source_db.recipe_tags AS s_rt
        JOIN recipe_id_map AS map ON s_rt.recipe_id = map.source_id
        JOIN tag_id_map AS tag_map ON s_rt.tag_id = tag_map.source_id
        WHERE map.is_duplicate = 0;

        INSERT INTO main.instructions (recipe_id, step_number, instruction)
        SELECT
            map.target_id, -- The new, offset recipe ID
            s_inst.step_number,
            s_inst.instruction
        FROM source_db.instructions AS s_inst
        JOIN recipe_id_map AS map ON s_inst.recipe_id = map.source_id
        WHERE map.is_duplicate = 0;

        --STEP 5: Finalize
        DROP TABLE ingredient_id_map;
        DROP TABLE tag_id_map;
        DROP TABLE source_recipe_ingredients_set;
        DROP TABLE target_recipe_ingredients_set;
        DROP TABLE recipe_id_map;
        DROP TABLE vars;

        PRAGMA foreign_keys = ON;
        PRAGMA foreign_key_check;

        COMMIT;
    )";

    bool script_ran_successfully = executeSQL(merge_script);

    if (!script_ran_successfully) {
        std::cerr << "Failed to execute merge script" << std::endl;
        executeSQL("ROLLBACK;");
    }

    if (!executeSQL("DETACH DATABASE source_db;")) {
        std::cerr << "Failed to detach source database";
        return false;
    }

    return script_ran_successfully;
}


bool Database::loadDatabase(const std::string& db_path) {
    close();
    db_path_ = db_path;
    return open(db_path);
}


bool Database::emptyDatabase() {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot empty database." << std::endl;
        return false;
    }

    const char* delete_all_sql = R"(
        BEGIN TRANSACTION;

        DELETE FROM recipes;
        DELETE FROM ingredients;
        DELETE FROM tags;
        DELETE FROM search;
        DELETE FROM sqlite_sequence WHERE name IN ('recipes', 'ingredients', 'tags', 'instructions');

        COMMIT;
    )";

    if (!executeSQL(delete_all_sql)) {
        std::cerr << "Failed to begin transaction." << std::endl;
        return false;
    }

    return true;
}


long long Database::getOrCreateIngredientId(const std::string& name) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot get or create ingredient ID." << std::endl;
        return -1;
    }

    if (name.empty()) {
        std::cerr << "Ingredient name cannot be empty." << std::endl;
        return -1;
    }

    const char* select_sql = "SELECT ingredient_id FROM ingredients WHERE name = ?;";
    SqliteStatement stmt_wrapper(db_, select_sql);
    sqlite3_stmt* stmt = stmt_wrapper.stmt;


    if (stmt == nullptr) return -1;
    
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    long long ingredient_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) ingredient_id = sqlite3_column_int64(stmt, 0);
    
    stmt = nullptr;
    if (ingredient_id != -1) {
        return ingredient_id;
    }

    // Ingredient not found, insert it
    const char* insert_sql = "INSERT INTO ingredients (name) VALUES (?);";
    SqliteStatement insert_stmt_wrapper(db_, insert_sql);
    stmt = insert_stmt_wrapper.stmt;

    if (stmt == nullptr) return -1;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert ingredient: " << sqlite3_errmsg(db_) << std::endl;
        return -1;
    }

    stmt = nullptr;

    // Return the newly created ingredient ID
    ingredient_id = sqlite3_last_insert_rowid(db_);
    if (ingredient_id == -1) {
        std::cerr << "Failed to retrieve last insert row ID for ingredient." << std::endl;
        return -1;
    }
    return ingredient_id;
}


long long Database::getOrCreateTagId(const std::string& name) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot get or create tag ID." << std::endl;
        return -1;
    }

    if (name.empty()) {
        std::cerr << "Tag name cannot be empty." << std::endl;
        return -1;
    }

    const char* select_sql = "SELECT tag_id FROM tags WHERE name = ?;";
    SqliteStatement stmt_wrapper(db_, select_sql);
    sqlite3_stmt* stmt = stmt_wrapper.stmt;

    if (stmt == nullptr) return -1;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    long long tag_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        tag_id = sqlite3_column_int64(stmt, 0);
    }

    stmt = nullptr;
    if (tag_id != -1) return tag_id;

    // Tag not found, insert it
    const char* insert_sql = "INSERT INTO tags (name) VALUES (?);";
    SqliteStatement insert_stmt_wrapper(db_, insert_sql);
    stmt = insert_stmt_wrapper.stmt;

    if (stmt == nullptr) return -1;

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert tag: " << sqlite3_errmsg(db_) << std::endl;
        return -1;
    }

    stmt = nullptr;
    
    // Return the newly created tag ID
    tag_id = sqlite3_last_insert_rowid(db_);
    if (tag_id == -1) {
        std::cerr << "Failed to retrieve last insert row ID for tag." << std::endl;
        return -1;
    }
    
    return tag_id;
}


bool Database::addInstruction(long long recipe_id, int step_number, const std::string& instruction) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot add instruction." << std::endl;
        return false;
    }

    if (recipe_id <= 0 || step_number <= 0 || instruction.empty()) {
        std::cerr << "Invalid parameters for adding instruction." << std::endl;
        return false;
    }

    const char* insert_sql = R"(
        INSERT INTO instructions (recipe_id, step_number, instruction)
        VALUES (?, ?, ?);
    )";
    SqliteStatement stmt_wrapper(db_, insert_sql);
    sqlite3_stmt* stmt = stmt_wrapper.stmt;
    
    if (stmt == nullptr) return false;

    sqlite3_bind_int64(stmt, 1, recipe_id);
    sqlite3_bind_int(stmt, 2, step_number);
    sqlite3_bind_text(stmt, 3, instruction.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert instruction: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    return true;
}


bool Database::linkIngredientToRecipe(long long recipe_id, const RecipeIngredientInfo& ingredient) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot link ingredient to recipe." << std::endl;
        return false;
    }

    if (recipe_id <= 0 || ingredient.name.empty()) {
        std::cerr << "Invalid parameters for linking ingredient to recipe." << std::endl;
        return false;
    }

    long long ingredient_id = getOrCreateIngredientId(ingredient.name);
    if (ingredient_id == -1) {
        std::cerr << "Failed to get or create ingredient ID for: " << ingredient.name << std::endl;
        return false;
    }

    const char* insert_sql = R"(
        INSERT INTO recipe_ingredients (recipe_id, ingredient_id, quantity, unit, notes, optional)
        VALUES (?, ?, ?, ?, ?, ?);
    )";

    SqliteStatement stmt_wrapper(db_, insert_sql);
    sqlite3_stmt* stmt = stmt_wrapper.stmt;

    if (stmt == nullptr) return false;

    sqlite3_bind_int64(stmt, 1, recipe_id);
    sqlite3_bind_int64(stmt, 2, ingredient_id);
    sqlite3_bind_double(stmt, 3, ingredient.quantity);
    sqlite3_bind_text(stmt, 4, ingredient.unit.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, ingredient.notes.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, ingredient.optional ? 1 : 0);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert recipe_ingredient: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    return true;
}


bool Database::linkTagToRecipe(long long recipe_id, const std::string& tag) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot link tag to recipe." << std::endl;
        return false;
    }

    if (recipe_id <= 0 || tag.empty()) {
        std::cerr << "Invalid parameters for linking tag to recipe." << std::endl;
        return false;
    }

    long long tag_id = getOrCreateTagId(tag);
    if (tag_id == -1) {
        std::cerr << "Failed to get or create tag ID for: " << tag << std::endl;
        return false;
    }

    const char* insert_sql = R"(
        INSERT INTO recipe_tags (recipe_id, tag_id)
        VALUES (?, ?);
    )";
    SqliteStatement stmt_wrapper(db_, insert_sql);
    sqlite3_stmt* stmt = stmt_wrapper.stmt;

    if (stmt == nullptr) return false;

    sqlite3_bind_int64(stmt, 1, recipe_id);
    sqlite3_bind_int64(stmt, 2, tag_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert recipe_tag: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    return true;
}


std::optional<RecipeData> Database::getRecipeById(long long recipe_id) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot get recipe by ID." << std::endl;
        return std::nullopt;
    }

    if (recipe_id <= 0) {
        std::cerr << "Invalid recipe ID: " << recipe_id << std::endl;
        return std::nullopt;
    }

    // Get information from recipes table
    const char* select_sql = R"(
        SELECT
            r.name,
            r.description,
            r.prep_time_minutes,
            r.cook_time_minutes,
            r.servings,
            r.is_favorite,
            r.source,
            r.source_url,
            r.author,
            (SELECT COALESCE(GROUP_CONCAT(
                COALESCE(i.name, '') || '|' ||
                COALESCE(ri.quantity, '') || '|' ||
                COALESCE(ri.unit, '') || '|' ||
                COALESCE(ri.notes, '') || '|' ||
                COALESCE(ri.optional, '0'),
                char(10)
            ), '')
            FROM recipe_ingredients ri JOIN ingredients i ON ri.ingredient_id = i.ingredient_id
            WHERE ri.recipe_id = r.recipe_id) AS ingredient_list,

            (SELECT COALESCE(GROUP_CONCAT(t.name, '|'), '')
            FROM recipe_tags rt JOIN tags t ON rt.tag_id = t.tag_id
            WHERE rt.recipe_id = r.recipe_id) AS tag_list,

            (SELECT COALESCE(GROUP_CONCAT(ins.instruction, '|'), '')
            FROM instructions ins
            WHERE ins.recipe_id = r.recipe_id
            ORDER BY ins.step_number) AS instruction_list
        FROM
            recipes AS r
        WHERE
            r.recipe_id = ?;
    )";
    SqliteStatement stmt_wrapper(db_, select_sql);
    sqlite3_stmt* stmt = stmt_wrapper.stmt;

    if (stmt == nullptr) return std::nullopt;

    if (sqlite3_bind_int64(stmt, 1, recipe_id) != SQLITE_OK) {
        std::cerr << "Failed to bind recipe ID: " << sqlite3_errmsg(db_) << std::endl;
        return std::nullopt;
    }

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        RecipeData recipe;
        const char* temp_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        recipe.name = (temp_ptr ? temp_ptr : "");

        temp_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        recipe.description = (temp_ptr ? temp_ptr : "");

        recipe.prep_time_minutes = sqlite3_column_int(stmt, 2);
        recipe.cook_time_minutes = sqlite3_column_int(stmt, 3);
        recipe.servings = sqlite3_column_int(stmt, 4);
        recipe.is_favorite = sqlite3_column_int(stmt, 5) != 0;

        temp_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        recipe.source = (temp_ptr ? temp_ptr : "");

        temp_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        recipe.source_url = (temp_ptr ? temp_ptr : "");

        temp_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        recipe.author = (temp_ptr ? temp_ptr : "");

        temp_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        recipe.ingredients = parseAllIngredients((temp_ptr ? temp_ptr : ""));

        temp_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        recipe.tags = splitString((temp_ptr ? temp_ptr : ""), '|');

        temp_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        recipe.instructions = splitString((temp_ptr ? temp_ptr : ""), '|');
        return recipe;
    } else if (rc == SQLITE_DONE) {
        return std::nullopt;
    } else {
        std::cerr << "Failed to get recipe by ID: " << sqlite3_errmsg(db_) << std::endl;
        return std::nullopt;
    }
}


std::vector<std::string> splitString(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}


RecipeIngredientInfo getIngredientInfo(const std::string& ingredient_str) {
    RecipeIngredientInfo ingredient;
    std::stringstream ss(ingredient_str);
    std::string token;

    std::getline(ss, token, '|');
    ingredient.name = token;

    if (std::getline(ss, token, '|')) {
    if (!token.empty()) {
        try {
            ingredient.quantity = std::stod(token);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not parse quantity '" << token << "'. Defaulting to 0.\n";
            ingredient.quantity = 0.0;
        }
    } else {
        ingredient.quantity = 0.0;
    }
}

    std::getline(ss, token, '|');
    ingredient.unit = token;

    std::getline(ss, token, '|');
    ingredient.notes = token;

    std::getline(ss, token, '|');
    if (token == "1") {
        ingredient.optional = true;
    } else {
        ingredient.optional = false;
    }

    return ingredient;
}


std::vector<RecipeIngredientInfo> parseAllIngredients(const std::string& all_ingredients_str) {
    auto result_view = splitString(all_ingredients_str, '\n') | std::views::transform(getIngredientInfo);
    
    return std::vector<RecipeIngredientInfo>(result_view.begin(), result_view.end());
}


std::pair<std::string, std::vector<SqlValue>> Database::buildSearchQuery(const SearchData& criteria) {
    std::string sql = "SELECT DISTINCT r.recipe_id FROM recipes AS r";
    std::vector<std::string> conditions;
    std::vector<SqlValue> params;

    // Handle FTS criteria
    std::string fts_match_query;
    if (!criteria.keywords.empty()) {
        fts_match_query += criteria.keywords + " ";
    }
    if (!criteria.name.empty()) {
        fts_match_query += "{name} : \"" + criteria.name + "\" ";
    }
    if (!criteria.author.empty()) {
        fts_match_query += "{author} : \"" + criteria.author + "\" ";
    }

    if (!fts_match_query.empty()) {
        if (fts_match_query.back() == ' ') {
            fts_match_query.pop_back();
        }
        
        conditions.push_back("r.recipe_id IN (SELECT rowid FROM search WHERE search MATCH ?)");
        params.push_back(fts_match_query);
    }

    // Handle main table criteria
    if (!criteria.exact_name.empty()) {
        conditions.push_back("r.name = ?");
        params.push_back(criteria.exact_name);
    }
    if (!criteria.exact_author.empty()) {
        conditions.push_back("r.author = ?");
        params.push_back(criteria.exact_author);
    }
    if (criteria.prep_time_range.size() == 2) {
        conditions.push_back("r.prep_time_minutes BETWEEN ? AND ?");
        params.push_back(criteria.prep_time_range[0]);
        params.push_back(criteria.prep_time_range[1]);
    }
    if (criteria.cook_time_range.size() == 2) {
        conditions.push_back("r.cook_time_minutes BETWEEN ? AND ?");
        params.push_back(criteria.cook_time_range[0]);
        params.push_back(criteria.cook_time_range[1]);
    }
    if (criteria.servings_range.size() == 2) {
        conditions.push_back("r.servings BETWEEN ? AND ?");
        params.push_back(criteria.servings_range[0]);
        params.push_back(criteria.servings_range[1]);
    }
    if (criteria.is_favorite) {
        conditions.push_back("r.is_favorite = 1");
    }
    if (criteria.dates.size() == 2 && !criteria.dates[0].empty() && !criteria.dates[1].empty()) {
        conditions.push_back("date(r.date_added) BETWEEN ? AND ?");
        params.push_back(criteria.dates[0]);
        params.push_back(criteria.dates[1]);
    }
    if (!criteria.source.empty()) {
        conditions.push_back("r.source = ?");
        params.push_back(criteria.source);
    }
    if (!criteria.source_url.empty()) {
        conditions.push_back("r.source_url = ?");
        params.push_back(criteria.source_url);
    }

    // Many-to-Many
    if (!criteria.tags.empty()) {
        std::string placeholders;
        for (int i = 0; i < criteria.tags.size(); ++i) {
            placeholders += (i == 0 ? "?" : ", ?");
        }
        std::string subquery = R"(r.recipe_id IN (
            SELECT rt.recipe_id FROM recipe_tags rt JOIN tags t ON rt.tag_id = t.tag_id
            WHERE t.name in ()" + placeholders + R"()
            GROUP BY rt.recipe_id
            HAVING COUNT (DISTINCT t.name) = ?
        ))";
        conditions.push_back(subquery);
        for (const auto& tag : criteria.tags) {
            params.push_back(tag);
        }
        params.push_back(static_cast<int64_t>(criteria.tags.size()));
    }
    if (!criteria.exclude_tags.empty()) {
        std::string placeholders;
        for (size_t i = 0; i < criteria.exclude_tags.size(); ++i) {
            placeholders += (i == 0 ? "?" : ", ?");
        }

        std::string subquery = R"(NOT EXISTS (
            SELECT 1 FROM recipe_tags rt JOIN tags t ON rt.tag_id = t.tag_id
            WHERE rt.recipe_id = r.recipe_id AND t.name IN ()" + placeholders + R"()
        ))";

        conditions.push_back(subquery);

        for (const auto& tag : criteria.exclude_tags) {
            params.push_back(tag);
        }
    }
    if (!criteria.ingredients.empty()) {
        std::string placeholders;
        for (int i = 0; i < criteria.ingredients.size(); ++i) {
            placeholders += (i == 0 ? "?" : ", ?");
        }
        std::string subquery = R"(r.recipe_id IN (
            SELECT ri.recipe_id FROM recipe_ingredients ri JOIN ingredients i ON ri.ingredient_id = i.ingredient_id
            WHERE i.name in ()" + placeholders + R"()
            GROUP BY ri.recipe_id
            HAVING COUNT (DISTINCT i.name) = ?
        ))";
        conditions.push_back(subquery);
        for (const auto& ingredient : criteria.ingredients) {
            params.push_back(ingredient);
        }
        params.push_back(static_cast<int64_t>(criteria.ingredients.size()));
    }
    if (!criteria.exclude_ingredients.empty()) {
        std::string placeholders;
        for (size_t i = 0; i < criteria.exclude_ingredients.size(); ++i) {
            placeholders += (i == 0 ? "?" : ", ?");
        }

        std::string subquery = R"(NOT EXISTS (
            SELECT 1 FROM recipe_ingredients ri JOIN ingredients i ON ri.ingredient_id = i.ingredient_id
            WHERE ri.recipe_id = r.recipe_id AND i.name IN ()" + placeholders + R"()
        ))";

        conditions.push_back(subquery);

        for (const auto& ingredient : criteria.exclude_ingredients) {
            params.push_back(ingredient);
        }
    }
    if (!conditions.empty()) {
        sql += " WHERE " + conditions[0];
        for (size_t i = 1; i < conditions.size(); ++i) {
            sql += " AND " + conditions[i];
        }
    }
    sql += ";";

    return {sql, params};
}


std::vector<long long> Database::executeSearch(std::pair<std::string, std::vector<SqlValue>> query_parts) {
    const std::string& sql = query_parts.first;
    const std::vector<SqlValue>& params = query_parts.second;

    SqliteStatement stmt_wrapper(db_, sql.c_str());
    sqlite3_stmt* stmt = stmt_wrapper.stmt;

    for (int i = 0; i < params.size(); ++i) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                sqlite3_bind_text(stmt, i + 1, arg.c_str(), -1, SQLITE_TRANSIENT);
            } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, uint16_t>) {
                sqlite3_bind_int(stmt, i + 1, arg);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                sqlite3_bind_int64(stmt, i + 1, arg);
            } else if constexpr (std::is_same_v<T, double>) {
                sqlite3_bind_double(stmt, i + 1, arg);
            }
        }, params[i]);
    }

    std::vector<long long> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(sqlite3_column_int64(stmt, 0));
    }

    return results;
}


std::vector<long long> Database::search(const SearchData& criteria) {
    if (!isOpen()) {
        std::cerr << "Database not open. Cannot get recipe by ID." << std::endl;
        return {}; // Return empty recipe
    }

    return executeSearch(buildSearchQuery(criteria));
}