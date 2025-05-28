#include "database.h"
#include <iostream>
#include <vector>
#include <sstream>


Database* Database::inst = nullptr;

Database::Database() : db_(nullptr), is_db_open_(false)
{
}

Database* Database::instance() {
     {
        if (!inst) {
            inst = new Database();
        }
        return inst;
    }
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

    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name=?;";
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for tableExists: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    sqlite3_bind_text(stmt, 1, tableName.c_str(), -1, SQLITE_STATIC);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
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
            ingredients,
            tags,
            tokenize = 'porter unicode61'
        );

        CREATE TRIGGER IF NOT EXISTS recipe_after_insert
        AFTER INSERT ON recipes
        BEGIN
            INSERT INTO search(rowid, name, description)
            VALUES (new.recipe_id, new.name, new.description);
        END;

        CREATE TRIGGER IF NOT EXISTS recipe_after_update AFTER UPDATE ON recipes
        BEGIN
            UPDATE search
            SET
                name = new.name,
                description = new.description
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

        INSERT INTO search (rowid, name, description, ingredients, tags)
        SELECT
            r.recipe_id,
            r.name,
            r.description,
            COALESCE(group_concat(DISTINCT i.name, '|'), ''),
            COALESCE(group_concat(DISTINCT t.name, '|'), '')
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

    if (!executeSQL("BEGIN TRANSACTION;")) {
        std::cerr << "Failed to begin transaction." << std::endl;
        return -1;
    }

    sqlite3_stmt* stmt = nullptr;
    long long new_recipe_id = -1;

    // Insert into recipes table
    const char* recipe_sql = R"(
        INSERT INTO recipes (name, description, prep_time_minutes, cook_time_minutes, servings, is_favorite, source, source_url, author)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    if (sqlite3_prepare_v2(db_, recipe_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare insert recipe statement: " << sqlite3_errmsg(db_) << std::endl;
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
        sqlite3_finalize(stmt);
        executeSQL("ROLLBACK;");
        return -1;
    }
    new_recipe_id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(stmt);
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

    sqlite3_stmt* stmt = nullptr;
    const char* delete_recipe_sql = "DELETE FROM recipes WHERE recipe_id = ?;";

    if (sqlite3_prepare_v2(db_, delete_recipe_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare delete recipe statement: " << sqlite3_errmsg(db_) << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    sqlite3_bind_int64(stmt, 1, recipe_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to delete recipe: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        executeSQL("ROLLBACK;");
        return false;
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;
    
    const char* clean_ingredients_sql = R"(
        DELETE FROM ingredients
        WHERE ingredient_id NOT IN (SELECT DISTINCT ingredient_id FROM recipe_ingredients);
    )";
    if (!executeSQL(clean_ingredients_sql)) {
        std::cerr << "Failed to clean ingredients table." << std::endl;
    }

    const char* clean_tags_sql = R"(
        DELETE FROM ingredients
        WHERE ingredient_id NOT IN (SELECT DISTINCT ingredient_id FROM recipe_ingredients);
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

    // Temporarily disable foreign key constraints to allow merging
    if (!executeSQL("PRAGMA foreign_keys = OFF;")) {
        std::cerr << "Failed to temporarily disable foreign key constraints." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    if (!executeSQL("BEGIN TRANSACTION;")) {
        std::cerr << "Failed to begin transaction." << std::endl;
        return false;
    }

    // Attach the source database
    sqlite3_stmt* stmt = nullptr;
    const char* attach_sql = "ATTACH DATABASE ? AS source_db;";

    if (sqlite3_prepare_v2(db_, attach_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare insert recipe statement: " << sqlite3_errmsg(db_) << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    sqlite3_bind_text(stmt, 1, source_db_path.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to attach source database: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        executeSQL("ROLLBACK;");
        return false;
    }

    sqlite3_finalize(stmt);

    // STEP 1: Merge independent (ingredients and tags) tables
    const char* merge_ingredients_sql = R"(
        INSERT INTO main.ingredients (name) SELECT s.name FROM source_db.ingredients AS s
        WHERE NOT EXISTS (SELECT 1 FROM main.ingredients AS t WHERE lower(t.name) = lower(s.name));
    )";
    if (!executeSQL(merge_ingredients_sql)) {
        std::cerr << "Failed to merge ingredients." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    const char* create_and_populate_ingredient_map_sql = R"(
        CREATE TEMP TABLE ingredient_id_map (source_id INTEGER PRIMARY KEY, target_id INTEGER NOT NULL);
        INSERT INTO ingredient_id_map (source_id, target_id)
        SELECT s.ingredient_id, t.ingredient_id FROM source_db.ingredients AS s JOIN main.ingredients AS t ON lower(s.name) = lower(t.name);
    )";
    if (!executeSQL(create_and_populate_ingredient_map_sql)) {
        std::cerr << "Failed to create and populate ingredient_id_map." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    const char* merge_tags_sql = R"(
        INSERT INTO main.tags (name) SELECT s.name FROM source_db.tags AS s
        WHERE NOT EXISTS (SELECT 1 FROM main.tags AS t WHERE lower(t.name) = lower(s.name));
    )";
    if (!executeSQL(merge_tags_sql)) {
        std::cerr << "Failed to merge tags." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    const char* create_and_populate_tag_map_sql = R"(
        CREATE TEMP TABLE tag_id_map (source_id INTEGER PRIMARY KEY, target_id INTEGER NOT NULL);
        INSERT INTO tag_id_map (source_id, target_id)
        SELECT s.tag_id, t.tag_id FROM source_db.tags AS s JOIN main.tags AS t ON lower(s.name) = lower(t.name);
    )";
    if (!executeSQL(create_and_populate_tag_map_sql)) {
        std::cerr << "Failed to create and populate tag_id_map." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    // STEP 2: Pre-calculate ingredient set for each recipe
    const char* calculate_recipe_ingredients_sql = R"(
        CREATE TEMP TABLE source_recipe_ingredients_set AS
        SELECT ri.recipe_id, group_concat(i.name, '|') AS ingredient_set
        FROM source_db.recipe_ingredients AS ri JOIN source_db.ingredients AS i ON ri.ingredient_id = i.ingredient_id
        GROUP BY ri.recipe_id
        ORDER BY i.name;

        CREATE TEMP TABLE target_recipe_ingredients_set AS
        SELECT ri.recipe_id, group_concat(i.name, '|') AS ingredient_set
        FROM main.recipe_ingredients AS ri JOIN main.ingredients AS i ON ri.ingredient_id = i.ingredient_id
        GROUP BY ri.recipe_id
        ORDER BY i.name;
    )";
    if (!executeSQL(calculate_recipe_ingredients_sql)) {
        std::cerr << "Failed to calculate recipe ingredients sets." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    // STEP 3: Build master recipe map
    const char* initialize_map_sql = R"(
        CREATE TEMP TABLE recipe_id_map (source_id INTEGER PRIMARY KEY, target_id INTEGER NOT NULL, is_duplicate BOOLEAN NOT NULL);
        CREATE TEMP TABLE vars(max_recipe_id INTEGER);
        INSERT INTO vars(max_recipe_id) SELECT IFNULL(MAX(recipe_id), 0) FROM main.recipes;
    )";
    if (!executeSQL(initialize_map_sql)) {
        std::cerr << "Failed to initialize recipe ID map." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    // Pass 1: Identify and map duplicates
    const char* pass1_sql = R"(
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
    )";
    if (!executeSQL(pass1_sql)) {
        std::cerr << "Failed to identify duplicates in recipes." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    // Pass 2: Identify and map unique recipes
    const char* pass2_sql = R"(
        INSERT INTO recipe_id_map (source_id, target_id, is_duplicate)
        SELECT
            s.recipe_id,
            s.recipe_id + (SELECT max_recipe_id FROM vars),
            0
        FROM source_db.recipes AS s
        WHERE s.recipe_id NOT IN (SELECT source_id FROM recipe_id_map);
    )";
    if (!executeSQL(pass2_sql) || !executeSQL(pass2_sql)) {
        std::cerr << "Failed to build master recipe map." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    // STEP 4: Perform merge based on map
    const char* insert_new_recipes_sql = R"(
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
    )";
    if (!executeSQL(insert_new_recipes_sql)) {
        std::cerr << "Failed to insert new recipes." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    // Merge tags for duplicate recipes
    const char* merge_recipe_tags_sql = R"(
        INSERT OR IGNORE INTO main.recipe_tags (recipe_id, tag_id)
        SELECT
            map.target_id,
            tag_map.target_id
        FROM source_db.recipe_tags AS s_rt
        JOIN recipe_id_map AS map ON s_rt.recipe_id = map.source_id
        JOIN tag_id_map AS tag_map ON s_rt.tag_id = tag_map.source_id
        WHERE map.is_duplicate = 1;
    )";
    if (!executeSQL(merge_recipe_tags_sql)) {
        std::cerr << "Failed to merge tags for duplicate recipes." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    const char* insert_new_ingredients_sql = R"(
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
    )";
    if (!executeSQL(insert_new_ingredients_sql)) {
        std::cerr << "Failed to insert new ingredients." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    const char* insert_new_tags_sql = R"(
        INSERT INTO main.recipe_tags (recipe_id, tag_id)
        SELECT
            map.target_id,
            tag_map.target_id
        FROM source_db.recipe_tags AS s_rt
        JOIN recipe_id_map AS map ON s_rt.recipe_id = map.source_id
        JOIN tag_id_map AS tag_map ON s_rt.tag_id = tag_map.source_id
        WHERE map.is_duplicate = 0; 
    )";
    if (!executeSQL(insert_new_tags_sql)) {
        std::cerr << "Failed to insert new tags." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    const char* insert_instructions_sql = R"(
        INSERT INTO main.instructions (recipe_id, step_number, instruction)
        SELECT
            map.target_id, -- The new, offset recipe ID
            s_inst.step_number,
            s_inst.instruction
        FROM source_db.instructions AS s_inst
        JOIN recipe_id_map AS map ON s_inst.recipe_id = map.source_id
        WHERE map.is_duplicate = 0;
    )";
    if (!executeSQL(insert_instructions_sql)) {
        std::cerr << "Failed to insert instructions." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    // STEP 5: Finalize
    const char* remove_temporary_tables_sql = R"(
        DROP TABLE ingredient_id_map;
        DROP TABLE tag_id_map;
        DROP TABLE source_recipe_ingredients_set;
        DROP TABLE target_recipe_ingredients_set;
        DROP TABLE recipe_id_map;
        DROP TABLE vars;
    )";
    if (!executeSQL(remove_temporary_tables_sql)) {
        std::cerr << "Failed to remove temporary tables." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    if (!executeSQL("PRAGMA foreign_keys = ON;")) {
        std::cerr << "Failed to reenable foreign key constraints." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    if (!executeSQL("PRAGMA foreign_key_check;")) {
        std::cerr << "Foreign key check failed after merge." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    if (!executeSQL("COMMIT;")) {
        std::cerr << "Failed to commit transaction for merge." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    return true;
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

    if (!executeSQL("BEGIN TRANSACTION;")) {
        std::cerr << "Failed to begin transaction." << std::endl;
        return false;
    }

    // Delete all data from child tables
    if (!executeSQL("DELETE FROM instructions;")) {
        std::cerr << "Failed to delete from instructions table." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }
    if (!executeSQL("DELETE FROM recipe_tags;")) {
        std::cerr << "Failed to delete from recipe_tags table." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }
    if (!executeSQL("DELETE FROM recipe_ingredients;")) {
        std::cerr << "Failed to delete from recipe_ingredients table." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    // Delete all data from parent tables
    if (!executeSQL("DELETE FROM recipes;")) {
        std::cerr << "Failed to delete from recipes table." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }
    if (!executeSQL("DELETE FROM ingredients;")) {
        std::cerr << "Failed to delete from ingredients table." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }
    if (!executeSQL("DELETE FROM tags;")) {
        std::cerr << "Failed to delete from tags table." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    // Reset AUTOINCREMENT counters
    if (!executeSQL("DELETE FROM sqlite_sequence WHERE name IN ('recipes', 'ingredients', 'tags', 'instructions');")) {
        std::cerr << "Failed to reset AUTOINCREMENT counters." << std::endl;
        executeSQL("ROLLBACK;");
        return false;
    }

    if (!executeSQL("COMMIT;")) {
        std::cerr << "Failed to commit transaction." << std::endl;
        executeSQL("ROLLBACK;");
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

    sqlite3_stmt* stmt = nullptr;
    const char* select_sql = "SELECT ingredient_id FROM ingredients WHERE name = ?;";
    if (sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare select ingredient statement: " << sqlite3_errmsg(db_) << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    long long ingredient_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ingredient_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;
    if (ingredient_id != -1) {
        return ingredient_id;
    }

    // Ingredient not found, insert it
    const char* insert_sql = "INSERT INTO ingredients (name) VALUES (?);";
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare insert ingredient statement: " << sqlite3_errmsg(db_) << std::endl;
        return -1;
    }   
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert ingredient: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
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

    sqlite3_stmt* stmt = nullptr;
    const char* select_sql = "SELECT tag_id FROM tags WHERE name = ?;";
    if (sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare select tag statement: " << sqlite3_errmsg(db_) << std::endl;
        return -1;
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    long long tag_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        tag_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;
    if (tag_id != -1) {
        return tag_id;
    }

    // Tag not found, insert it
    const char* insert_sql = "INSERT INTO tags (name) VALUES (?);";
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare insert tag statement: " << sqlite3_errmsg(db_) << std::endl;
        return -1;
    }   
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert tag: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
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

    sqlite3_stmt* stmt = nullptr;
    const char* insert_sql = R"(
        INSERT INTO instructions (recipe_id, step_number, instruction)
        VALUES (?, ?, ?);
    )";
    
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare insert instruction statement: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    sqlite3_bind_int64(stmt, 1, recipe_id);
    sqlite3_bind_int(stmt, 2, step_number);
    sqlite3_bind_text(stmt, 3, instruction.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert instruction: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
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

    sqlite3_stmt* stmt = nullptr;
    const char* insert_sql = R"(
        INSERT INTO recipe_ingredients (recipe_id, ingredient_id, quantity, unit, notes, optional)
        VALUES (?, ?, ?, ?, ?, ?);
    )";

    if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare insert recipe_ingredients statement: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    sqlite3_bind_int64(stmt, 1, recipe_id);
    sqlite3_bind_int64(stmt, 2, ingredient_id);
    sqlite3_bind_double(stmt, 3, ingredient.quantity);
    sqlite3_bind_text(stmt, 4, ingredient.unit.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, ingredient.notes.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, ingredient.optional ? 1 : 0);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert recipe_ingredient: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
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

    sqlite3_stmt* stmt = nullptr;
    const char* insert_sql = R"(
        INSERT INTO recipe_tags (recipe_id, tag_id)
        VALUES (?, ?);
    )";

    if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare insert recipe_tags statement: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    sqlite3_bind_int64(stmt, 1, recipe_id);
    sqlite3_bind_int64(stmt, 2, tag_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Failed to insert recipe_tag: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    return true;
}