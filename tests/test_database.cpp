#include <iostream>
#include <vector>
#include <cassert>
#include <filesystem>
#include <algorithm>
#include <set>
#include "database.h"

// A test fixture for setting up and tearing down the database for each test.
struct TestDB {
    Database* db;
    std::string db_path;

    TestDB(const std::string& path) : db_path(path) {
        db = Database::instance();
        // Ensure a clean slate before each test
        std::filesystem::remove(db_path);
        assert(db->open(db_path));
        db->emptyDatabase();
    }

    ~TestDB() {
        db->close();
        std::filesystem::remove(db_path);
    }
};

// Helper function to create a sample recipe
RecipeData createRecipe(
    const std::string& name,
    const std::string& author,
    const std::vector<std::string>& ingredients,
    const std::vector<std::string>& tags,
    int cook_time = 20,
    bool is_favorite = false
) {
    RecipeData recipe;
    recipe.name = name;
    recipe.author = author;
    recipe.description = "A delicious recipe for " + name;
    recipe.prep_time_minutes = 10;
    recipe.cook_time_minutes = cook_time;
    recipe.servings = 4;
    recipe.is_favorite = is_favorite;
    recipe.source = "Test Kitchen";

    for (const auto& ingredient_name : ingredients) {
        recipe.ingredients.push_back({ingredient_name, 1, "unit", "", false});
    }

    recipe.tags = tags;
    recipe.instructions = {"Step 1: Prep", "Step 2: Cook", "Step 3: Serve"};
    return recipe;
}

void testCoreFunctionality() {
    std::cout << "--- Testing Core Functionality ---" << std::endl;
    TestDB test_db("test_core.db");

    // Test that the database is open
    assert(test_db.db->isOpen());

    // Test that emptyDatabase clears all recipes
    test_db.db->addRecipe(createRecipe("Test Recipe", "Tester", {"Flour"}, {"test"}));
    test_db.db->emptyDatabase();
    SearchData criteria;
    assert(test_db.db->search(criteria).empty());

    std::cout << "Core Functionality Tests Passed!" << std::endl;
}

void testRecipeManagement() {
    std::cout << "\n--- Testing Recipe Management ---" << std::endl;
    TestDB test_db("test_recipes.db");

    // Add a recipe and verify its contents
    RecipeData recipe = createRecipe("Pancakes", "Mom", {"Flour", "Egg", "Milk"}, {"breakfast", "easy"});
    long long id = test_db.db->addRecipe(recipe);
    assert(id != -1);

    auto fetched_optional = test_db.db->getRecipeById(id);
    assert(fetched_optional.has_value());
    RecipeData fetched = fetched_optional.value();
    assert(fetched.name == "Pancakes");
    assert(fetched.author == "Mom");
    assert(fetched.ingredients.size() == 3);
    assert(fetched.tags.size() == 2);
    assert(fetched.instructions.size() == 3);

    // Delete the recipe and verify it's gone
    assert(test_db.db->deleteRecipe(id));
    assert(!test_db.db->getRecipeById(id).has_value());
    
    // Add a recipe with some optional fields missing
    RecipeData simple_recipe;
    simple_recipe.name = "Toast";
    simple_recipe.ingredients = {{"Bread", 1, "slice", "", false}};
    long long simple_id = test_db.db->addRecipe(simple_recipe);
    assert(simple_id != -1);
    auto fetched_simple = test_db.db->getRecipeById(simple_id);
    assert(fetched_simple.has_value() && fetched_simple.value().name == "Toast");


    std::cout << "Recipe Management Tests Passed!" << std::endl;
}

void testSearchFunctionality() {
    std::cout << "\n--- Testing Search Functionality ---" << std::endl;
    TestDB test_db("test_search.db");

    long long id1 = test_db.db->addRecipe(createRecipe("Spaghetti Bolognese", "Nonna", {"Spaghetti", "Beef", "Tomato"}, {"italian", "dinner", "pasta"}, 30, true));
    long long id2 = test_db.db->addRecipe(createRecipe("Chicken Curry", "Dad", {"Chicken", "Curry Powder", "Coconut Milk"}, {"indian", "dinner"}, 40));
    long long id3 = test_db.db->addRecipe(createRecipe("Caesar Salad", "Chef", {"Lettuce", "Chicken", "Croutons"}, {"salad", "lunch"}));
    long long id4 = test_db.db->addRecipe(createRecipe("Spaghetti Carbonara", "Nonna", {"Spaghetti", "Egg", "Bacon"}, {"italian", "dinner", "pasta"}));

    // Keyword search (should match both spaghetti recipes)
    SearchData criteria;
    criteria.keywords = "Spaghetti";
    auto results = test_db.db->search(criteria);
    assert(results.size() == 2);

    // Search by ingredient (should only find Chicken Curry and Caesar Salad)
    criteria = {};
    criteria.ingredients = {"Chicken"};
    results = test_db.db->search(criteria);
    assert(results.size() == 2);

    // Combined search: italian dinner by Nonna
    criteria = {};
    criteria.tags = {"italian", "dinner"};
    criteria.author = "Nonna";
    results = test_db.db->search(criteria);
    assert(results.size() == 2);
    
    // Search with exclusion: dinner but not italian
    criteria = {};
    criteria.tags = {"dinner"};
    criteria.exclude_tags = {"italian"};
    results = test_db.db->search(criteria);
    assert(results.size() == 1 && results[0] == id2);
    
    // Range search: cook time between 25 and 35 minutes
    criteria = {};
    criteria.cook_time_range = {25, 35};
    results = test_db.db->search(criteria);
    assert(results.size() == 1 && results[0] == id1);

    // Search for something that doesn't exist
    criteria = {};
    criteria.keywords = "NoSuchRecipe";
    assert(test_db.db->search(criteria).empty());

    std::cout << "Search Functionality Tests Passed!" << std::endl;
}

void testMergeFunctionality() {
    std::cout << "\n--- Testing Merge Functionality ---" << std::endl;

    // This fixture creates main.db and ensures it is closed and deleted when the test is over.
    TestDB main_db_fixture("main.db");
    Database* db = main_db_fixture.db;

    // Add the initial recipe to main.db
    db->addRecipe(createRecipe("Pizza", "Papa John", {"Dough", "Cheese", "Tomato"}, {"italian"}));

    // Now, we'll manually create the second database using the same singleton instance.
    // First, close the connection to main.db so we can switch to other.db
    db->close();

    // Open and populate other.db
    assert(db->open("other.db"));
    db->emptyDatabase();
    db->addRecipe(createRecipe("Burger", "Ronald", {"Bun", "Beef", "Lettuce"}, {"american"}));
    db->addRecipe(createRecipe("Pizza", "Papa John", {"Dough", "Cheese", "Tomato"}, {"italian"})); // Exact duplicate
    db->addRecipe(createRecipe("Pizza", "Pizza Hut", {"Dough", "Cheese", "Pepperoni"}, {"fast-food"}));
    db->close(); // Close the connection to other.db

    // --- The Actual Test ---
    // Re-open main.db to perform the merge. The TestDB fixture will handle cleanup.
    assert(db->open("main.db"));
    assert(db->mergeDatabase("other.db"));

    // Check the results of the merge
    SearchData criteria;
    assert(db->search(criteria).size() == 3); // Should have 3 unique recipes now

    criteria = {};
    criteria.exact_author = "Papa John";
    assert(db->search(criteria).size() == 1); // The duplicate was correctly ignored

    // Manually remove the temporary database file we created.
    // The fixture will handle removing main.db automatically.
    std::filesystem::remove("other.db");

    std::cout << "Merge Functionality Tests Passed!" << std::endl;
}

void testEdgeCasesAndErrors() {
    std::cout << "\n--- Testing Edge Cases and Errors ---" << std::endl;
    TestDB test_db("test_errors.db");
    
    // Try to get recipe with invalid ID
    assert(!test_db.db->getRecipeById(-1).has_value());
    assert(!test_db.db->getRecipeById(0).has_value());
    assert(!test_db.db->getRecipeById(999).has_value());

    // Try to delete recipe with invalid ID
    assert(!test_db.db->deleteRecipe(-1));

    // Add a recipe with an empty name (should be handled gracefully by your constraints, but good to test)
    RecipeData r;
    // Assuming your DB schema has NOT NULL on recipe name, this would fail.
    // The current implementation does not check for empty name before insertion,
    // so this will likely fail at the database level. A robust implementation
    // might check this in `addRecipe`. For now, we expect it to fail.
    assert(test_db.db->addRecipe(r) == -1);

    // Search with empty criteria (should return all recipes)
    test_db.db->addRecipe(createRecipe("R1", "A1", {}, {}));
    test_db.db->addRecipe(createRecipe("R2", "A2", {}, {}));
    assert(test_db.db->search({}).size() == 2);


    std::cout << "Edge Cases and Errors Tests Passed!" << std::endl;
}

int main() {
    std::cout << "Starting RecipeApp Robust Test Suite" << std::endl;

    testCoreFunctionality();
    testRecipeManagement();
    testSearchFunctionality();
    testMergeFunctionality();
    testEdgeCasesAndErrors();

    std::cout << "\nAll robust tests passed successfully!" << std::endl;

    return 0;
}