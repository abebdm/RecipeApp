#include <iostream>
#include <vector>
#include <cassert>
#include "database.h"
#include "main.h"

// Function to create a sample recipe for demonstration
RecipeData createSamplePancakesRecipe() {
    RecipeData pancakes;
    pancakes.name = "Classic Pancakes";
    pancakes.description = "Fluffy and delicious pancakes, a breakfast favorite.";
    pancakes.prep_time_minutes = 10;
    pancakes.cook_time_minutes = 15;
    pancakes.servings = 4;
    pancakes.is_favorite = true;
    pancakes.source = "Family Recipe";
    pancakes.source_url = "http://example.com/pancakes";
    pancakes.author = "Mom";

    pancakes.ingredients = {
        {"All-purpose flour", 1.5, "cups", "", false},
        {"Granulated sugar", 2, "tablespoons", "or to taste", false},
        {"Baking powder", 2, "teaspoons", "", false},
        {"Salt", 0.5, "teaspoon", "", false},
        {"Milk", 1.25, "cups", "whole milk recommended", false},
        {"Egg", 1, "large", "", false},
        {"Melted butter", 3, "tablespoons", "plus more for griddle", false},
        {"Vanilla extract", 1, "teaspoon", "optional", true}
    };

    pancakes.tags = {"breakfast", "easy", "classic", "sweet"};

    pancakes.instructions = {
        "In a large bowl, whisk together flour, sugar, baking powder, and salt.",
        "In a separate bowl, whisk together milk, egg, and melted butter (and vanilla if using).",
        "Pour the wet ingredients into the dry ingredients and stir until just combined (do not overmix; a few lumps are okay).",
        "Heat a lightly oiled griddle or frying pan over medium-high heat.",
        "Pour or scoop the batter onto the griddle, using approximately 1/4 cup for each pancake.",
        "Cook for about 2-3 minutes per side, or until golden brown and cooked through. Flip when bubbles appear on the surface.",
        "Serve warm with your favorite toppings like maple syrup, fruit, or whipped cream."
    };
    return pancakes;
}

RecipeData createSampleSpaghettiRecipe() {
    RecipeData spaghetti;
    spaghetti.name = "Spaghetti Aglio e Olio";
    spaghetti.description = "A simple yet delicious Italian pasta dish with garlic and oil.";
    spaghetti.prep_time_minutes = 5;
    spaghetti.cook_time_minutes = 10;
    spaghetti.servings = 2;
    spaghetti.is_favorite = false;
    spaghetti.source = "Italian tradition";
    spaghetti.source_url = "";
    spaghetti.author = "Nonna";

    spaghetti.ingredients = {
        {"Spaghetti", 200, "grams", "", false},
        {"Garlic", 4, "cloves", "thinly sliced", false},
        {"Olive oil", 0.25, "cup", "extra virgin", false},
        {"Red pepper flakes", 0.5, "teaspoon", "or to taste", true},
        {"Fresh parsley", 0.25, "cup", "chopped", false},
        {"Salt", 1, "pinch", "to taste", false}
    };

    spaghetti.tags = {"pasta", "italian", "quick", "garlic"};

    spaghetti.instructions = {
        "Cook spaghetti according to package directions until al dente.",
        "Reserve about 1/2 cup of pasta water before draining.",
        "While pasta cooks, heat olive oil in a large skillet over medium-low heat.",
        "Add garlic and red pepper flakes (if using). Cook until garlic is golden, about 1-2 minutes. Do not burn.",
        "Drain pasta and add it directly to the skillet with the garlic and oil.",
        "Toss to combine. Add a splash of reserved pasta water if needed to create a light sauce.",
        "Stir in fresh parsley and season with salt to taste.",
        "Serve immediately."
    };
    return spaghetti;
}

void printRecipe(const RecipeData& recipe) {
    std::cout << "Name: " << recipe.name << std::endl;
    std::cout << "Description: " << recipe.description << std::endl;
    std::cout << "Author: " << recipe.author << std::endl;
    std::cout << "Ingredients: " << std::endl;
    for (const auto& ingredient : recipe.ingredients) {
        std::cout << "  - " << ingredient.name << " " << ingredient.quantity << " " << ingredient.unit << std::endl;
    }
    std::cout << "Tags: " << std::endl;
    for (const auto& tag : recipe.tags) {
        std::cout << "  - " << tag << std::endl;
    }
    std::cout << "Instructions: " << std::endl;
    for (const auto& instruction : recipe.instructions) {
        std::cout << "  - " << instruction << std::endl;
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "Recipe Manager C++ Demo" << std::endl;

    Database* recipe_db_ptr = Database::instance();
    Database& recipe_db = *recipe_db_ptr;

    // Test open
    std::cout << "--- Testing Database Open ---" << std::endl;
    if (!recipe_db.open("./test.db")) {
        std::cerr << "Failed to open database!" << std::endl;
        return 1;
    }
    std::cout << "Database opened successfully." << std::endl;

    // Test emptyDatabase
    std::cout << "\n--- Testing Empty Database ---" << std::endl;
    recipe_db.emptyDatabase();
    std::cout << "Database emptied." << std::endl;

    // Test addRecipe
    std::cout << "\n--- Testing Add Recipe ---" << std::endl;
    RecipeData pancakes = createSamplePancakesRecipe();
    long long pancake_id = recipe_db.addRecipe(pancakes);
    assert(pancake_id != -1);
    std::cout << "Pancakes recipe added with ID: " << pancake_id << std::endl;

    RecipeData spaghetti = createSampleSpaghettiRecipe();
    long long spaghetti_id = recipe_db.addRecipe(spaghetti);
    assert(spaghetti_id != -1);
    std::cout << "Spaghetti recipe added with ID: " << spaghetti_id << std::endl;

    // Test getRecipeById
    std::cout << "\n--- Testing Get Recipe By ID ---" << std::endl;
    auto fetched_pancakes_optional = recipe_db.getRecipeById(pancake_id);
    assert(fetched_pancakes_optional.has_value());
    RecipeData fetched_pancakes = fetched_pancakes_optional.value();
    printRecipe(fetched_pancakes);

    // Test search
    std::cout << "\n--- Testing Search ---" << std::endl;
    SearchData search_criteria;
    search_criteria.keywords = "pancakes";
    std::vector<long long> search_results = recipe_db.search(search_criteria);
    assert(search_results.size() == 1);
    assert(search_results[0] == pancake_id);
    std::cout << "Found " << search_results.size() << " recipe(s) with keyword 'pancakes'" << std::endl;

    search_criteria = {};
    search_criteria.ingredients = {"Garlic", "Spaghetti"};
    search_results = recipe_db.search(search_criteria);
    assert(search_results.size() == 1);
    assert(search_results[0] == spaghetti_id);
    std::cout << "Found " << search_results.size() << " recipe(s) with ingredients 'Garlic' and 'Spaghetti'" << std::endl;

    // Test deleteRecipe
    std::cout << "\n--- Testing Delete Recipe ---" << std::endl;
    assert(recipe_db.deleteRecipe(pancake_id));
    std::cout << "Deleted recipe with ID: " << pancake_id << std::endl;
    auto deleted_pancakes_optional = recipe_db.getRecipeById(pancake_id);
    assert(!deleted_pancakes_optional.has_value() || deleted_pancakes_optional.value().name.empty());


    // Test mergeDatabase
    std::cout << "\n--- Testing Merge Database ---" << std::endl;
    // Create and populate the second database
    recipe_db.loadDatabase("./other.db");
    recipe_db.emptyDatabase();
    RecipeData pizza = createSamplePancakesRecipe(); // Using pancakes recipe as a stand-in for a new recipe
    pizza.name = "Pizza";
    pizza.tags = {"dinner", "italian"};
    long long pizza_id = recipe_db.addRecipe(pizza);
    assert(pizza_id != -1);

    // Re-open the main database and merge
    recipe_db.loadDatabase("./test.db");
    assert(recipe_db.mergeDatabase("./other.db"));
    std::cout << "Merged other.db into test.db" << std::endl;

    search_criteria = {};
    search_criteria.keywords = "Pizza";
    search_results = recipe_db.search(search_criteria);
    assert(search_results.size() == 1);
    std::cout << "Found " << search_results.size() << " recipe(s) with keyword 'Pizza' after merge" << std::endl;


    // Test loadDatabase
    std::cout << "\n--- Testing Load Database ---" << std::endl;
    assert(recipe_db.loadDatabase("./other.db"));
    std::cout << "Loaded other.db" << std::endl;

    search_criteria = {};
    search_criteria.keywords = "Pizza";
    search_results = recipe_db.search(search_criteria);
    assert(search_results.size() == 1);
    std::cout << "Found " << search_results.size() << " recipe(s) with keyword 'Pizza' in other.db" << std::endl;

    // Test close
    std::cout << "\n--- Testing Close Database ---" << std::endl;
    recipe_db.close();
    std::cout << "Database closed." << std::endl;

    std::cout << "\nAll tests passed!" << std::endl;

    return 0;
}