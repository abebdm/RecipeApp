#include <iostream>
#include "database.h" // Our database class
#include "main.h"     // Corresponding header for main (currently minimal)

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


int main() {
    std::cout << "Recipe Manager C++ Demo" << std::endl;

    // Define the path to the database file.
    // For this demo, it's created in the current working directory.
    // You might want to use a more specific path in a real application.
    std::string db_file_path = "recipes_cpp.db";
    Database* recipe_db_ptr = Database::instance();
    Database& recipe_db = *recipe_db_ptr;

    std::cout << "Attempting to open database: " << db_file_path << std::endl;
    if (!recipe_db.open("./recipes.db")) {
        std::cerr << "Failed to open or initialize the database. Exiting." << std::endl;
        return 1;
    }
    std::cout << "Database opened successfully." << std::endl;

    // Create a sample recipe
    RecipeData pancakes = createSamplePancakesRecipe();
    std::cout << "\nAttempting to add recipe: " << pancakes.name << std::endl;
    long long pancake_recipe_id = recipe_db.addRecipe(pancakes);

    if (pancake_recipe_id != -1) {
        std::cout << "Recipe '" << pancakes.name << "' added successfully with ID: " << pancake_recipe_id << std::endl;
    } else {
        std::cerr << "Failed to add recipe: " << pancakes.name << std::endl;
    }

    // Create another sample recipe
    RecipeData spaghetti = createSampleSpaghettiRecipe();
    std::cout << "\nAttempting to add recipe: " << spaghetti.name << std::endl;
    long long spaghetti_recipe_id = recipe_db.addRecipe(spaghetti);

    if (spaghetti_recipe_id != -1) {
        std::cout << "Recipe '" << spaghetti.name << "' added successfully with ID: " << spaghetti_recipe_id << std::endl;
    } else {
        std::cerr << "Failed to add recipe: " << spaghetti.name << std::endl;
    }
    
    // You could add more database operations here (e.g., querying recipes)

    std::cout << "\nClosing database." << std::endl;
    recipe_db.close();
    std::cout << "Program finished." << std::endl;

    return 0;
}

