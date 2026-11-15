/* Made by tools/port/anim_recipe.py cinc from port/assets/luigi/pm64_watt_recipe.json: don't edit. Our numbers only (docs/LUIGI_ANIMS.md,
 * "New animations from a recipe"): the animations are made from the player's disc at startup. */

static const PortRecipePoint kPm64WattRecipePoints[] = {
    {0.0, 0.96, 0},
    {7.5, 0.9954, 0},
    {15.0, 1.01, 0},
    {22.5, 0.9954, 0},
    {30.0, 0.96, 0},
    {37.5, 0.9246, 0},
    {45.0, 0.91, 0},
    {52.5, 0.9246, 0},
    {60.0, 0.96, 0},
    {0.0, -90.0, 0},
    {30.0, -90.0, 0},
    {0.0, 6.3, 0},
    {30.0, 6.3, 0},
    {0.0, 0.92, 0},
    {30.0, 0.92, 0},
};
static const PortRecipeTrack kPm64WattRecipeTracks[] = {
    {"locator3", 4, 0, 9},
    {"|locator1", 8, 9, 2},
    {"|locator1", 1, 11, 2},
    {"|locator1", 4, 13, 2},
};
static const PortRecipeAnim kPm64WattRecipeAnims[] = {
    {"W_1", "S_1", 0, 0.0, 0.0, 1, 20.0, 1, 1, 0, 0, 0},
    {"R_1", "S_1", 0, 0.0, 0.0, 1, 20.0, 1, 1, 0, 0, 0},
    {"J_1", "S_1", 1, 0.0, 0.0, 1, 12.0, 1, 0, 0, 0, 0},
    {"F_1", "S_1", 1, 0.0, 0.0, 1, 10.0, 1, 1, 0, 0, 0},
    {"D_1", "T_1", 1, 7.0, 7.0, 1, 16.0, 1, 1, 0, 0, 0},
    {"D_2", "S_1", 1, 0.0, 0.0, 1, 60.0, 1, 1, 0, 0, 1},
    {"D_3", "S_1", 1, 0.0, 0.0, 1, 30.0, 1, 1, 0, 1, 3},
    {"Z_2", "S_1", 1, 0.0, 0.0, 1, 30.0, 1, 1, 0, 4, 0},
    {"X_1", "S_1", 1, 0.0, 0.0, 1, 120.0, 1, 1, 0, 4, 0},
    {"N_1", "S_1", 1, 0.0, 0.0, 1, 120.0, 1, 1, 0, 4, 0},
    {"G_1", "S_1", 1, 0.0, 0.0, 1, 20.0, 1, 1, 0, 4, 0},
    {"Y_1", "T_1", 0, 0.0, 0.0, 1, 72.0, 1, 1, 0, 4, 0},
    {"A_1", "S_1", 0, 0.0, 0.0, 1, 20.0, 1, 1, 0, 4, 0},
    {"A_2", "B_1", 0, 0.0, 0.0, 1, 20.0, 1, 1, 0, 4, 0},
};
static const PortRecipe kPm64WattRecipe = {"c_akarin", kPm64WattRecipeAnims, 14, kPm64WattRecipeTracks, kPm64WattRecipePoints};
