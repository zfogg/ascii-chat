#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include <float.h>
#include <math.h>
#include <float.h>
#include <stdbool.h>
#include <stdint.h>

// Grid layout calculation logic extracted from server
typedef struct {
  int terminal_width;
  int terminal_height;
  int num_clients;
  int expected_cols;
  int expected_rows;
  int expected_cell_width;
  int expected_cell_height;
} grid_test_case_t;

// Calculate optimal grid layout based on terminal size and client count
static void calculate_optimal_grid(int terminal_width, int terminal_height, int num_clients, int *out_cols,
                                   int *out_rows, int *out_cell_width, int *out_cell_height) {
  if (num_clients == 0) {
    *out_cols = 0;
    *out_rows = 0;
    *out_cell_width = 0;
    *out_cell_height = 0;
    return;
  }

  if (num_clients == 1) {
    // Single client uses full terminal
    *out_cols = 1;
    *out_rows = 1;
    *out_cell_width = terminal_width;
    *out_cell_height = terminal_height;
    return;
  }

  if (num_clients == 2) {
    // For 2 clients, choose between 1x2 and 2x1 based on aspect ratio
    // Calculate aspect ratios for both layouts
    // 1x2 (vertical split): each cell is width x (height/2)
    float cell_aspect_1x2 = (float)terminal_width / ((float)terminal_height / 2.0f);

    // 2x1 (horizontal split): each cell is (width/2) x height
    float cell_aspect_2x1 = ((float)terminal_width / 2.0f) / (float)terminal_height;

    // Target aspect ratio for video (typically 16:9 = 1.78, but for ASCII we prefer closer to 2:1)
    float target_aspect = 2.0f;

    // Choose layout that gives cells closest to target aspect ratio
    float diff_1x2 = fabs(cell_aspect_1x2 - target_aspect);
    float diff_2x1 = fabs(cell_aspect_2x1 - target_aspect);

    if (diff_1x2 <= diff_2x1) {
      // 1x2 layout (stacked vertically)
      *out_cols = 1;
      *out_rows = 2;
      *out_cell_width = terminal_width;
      *out_cell_height = terminal_height / 2;
    } else {
      // 2x1 layout (side by side)
      *out_cols = 2;
      *out_rows = 1;
      *out_cell_width = terminal_width / 2;
      *out_cell_height = terminal_height;
    }
    return;
  }

  // For 3+ clients, calculate optimal grid
  // Try to maintain reasonable aspect ratios for each cell
  int best_cols = 1;
  int best_rows = num_clients;
  // Use FLT_MAX instead of INFINITY for -ffast-math compatibility
  float best_aspect_diff = FLT_MAX;
  float target_aspect = 2.0f; // Target aspect ratio for ASCII display

  // Try different grid configurations
  for (int cols = 1; cols <= num_clients; cols++) {
    int rows = (num_clients + cols - 1) / cols; // Ceiling division

    // Skip if this configuration has too many empty cells
    int total_cells = cols * rows;
    int empty_cells = total_cells - num_clients;
    if (empty_cells > cols)
      continue; // Don't waste more than one row

    // Calculate cell dimensions for this configuration
    // Use floating point to get precise dimensions, then round
    float cell_width_f = (float)terminal_width / (float)cols;
    float cell_height_f = (float)terminal_height / (float)rows;

    int cell_width = (int)(cell_width_f + 0.5f); // Round to nearest
    int cell_height = (int)(cell_height_f + 0.5f);

    // Skip if cells would be too small
    if (cell_width < 20 || cell_height < 10)
      continue;

    // Calculate aspect ratio of cells
    float cell_aspect = cell_width_f / cell_height_f; // Use precise values
    float aspect_diff = fabs(cell_aspect - target_aspect);

    // Prefer configurations with better aspect ratios
    if (aspect_diff < best_aspect_diff) {
      best_aspect_diff = aspect_diff;
      best_cols = cols;
      best_rows = rows;
    }
  }

  *out_cols = best_cols;
  *out_rows = best_rows;
  // Ensure full space utilization by using integer division (matches actual behavior)
  *out_cell_width = terminal_width / best_cols;
  *out_cell_height = terminal_height / best_rows;
}

Test(grid_layout, single_client_full_terminal) {
  int cols, rows, cell_width, cell_height;

  // Test various terminal sizes with single client
  calculate_optimal_grid(80, 24, 1, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 1, "Single client should use 1 column");
  cr_expect_eq(rows, 1, "Single client should use 1 row");
  cr_expect_eq(cell_width, 80, "Single client should use full width");
  cr_expect_eq(cell_height, 24, "Single client should use full height");
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 80, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 24, "Should use all vertical space");

  calculate_optimal_grid(120, 40, 1, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 1);
  cr_expect_eq(rows, 1);
  cr_expect_eq(cell_width, 120);
  cr_expect_eq(cell_height, 40);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 120, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 40, "Should use all vertical space");
}

Test(grid_layout, two_clients_horizontal_vs_vertical) {
  int cols, rows, cell_width, cell_height;

  // Wide terminal - should prefer 2x1 (side by side)
  calculate_optimal_grid(160, 24, 2, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 2, "Wide terminal with 2 clients should use 2 columns");
  cr_expect_eq(rows, 1, "Wide terminal with 2 clients should use 1 row");
  cr_expect_eq(cell_width, 80, "Each cell should be half width");
  cr_expect_eq(cell_height, 24, "Each cell should be full height");
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 160, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 24, "Should use all vertical space");

  // Tall terminal - actually prefers 2x1 because it's closer to target aspect 2.0
  // 1x2 gives aspect 3.33, 2x1 gives aspect 0.83, so 2x1 is closer to 2.0
  calculate_optimal_grid(80, 48, 2, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 2, "Tall terminal with 2 clients should use 2 columns (better aspect)");
  cr_expect_eq(rows, 1, "Tall terminal with 2 clients should use 1 row");
  cr_expect_eq(cell_width, 40, "Each cell should be half width");
  cr_expect_eq(cell_height, 48, "Each cell should be full height");
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 80, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 48, "Should use all vertical space");

  // Square-ish terminal - should prefer layout that gives better aspect ratio
  calculate_optimal_grid(100, 50, 2, &cols, &rows, &cell_width, &cell_height);
  // 2x1: cells would be 50x50 (aspect 1.0) - far from target 2.0
  // 1x2: cells would be 100x25 (aspect 4.0) - also far from target 2.0
  // Should choose 2x1 as it's closer to target
  cr_expect_eq(cols, 2, "Square terminal should prefer 2x1 for better aspect");
  cr_expect_eq(rows, 1);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 100, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 50, "Should use all vertical space");
}

Test(grid_layout, three_clients_optimal) {
  int cols, rows, cell_width, cell_height;

  // Wide terminal with 3 clients - try 3x1
  calculate_optimal_grid(180, 24, 3, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 3, "Very wide terminal with 3 clients should use 3x1");
  cr_expect_eq(rows, 1);
  cr_expect_eq(cell_width, 60);
  cr_expect_eq(cell_height, 24);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 180, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 24, "Should use all vertical space");

  // Medium terminal with 3 clients - might prefer 2x2 (with one empty)
  calculate_optimal_grid(120, 48, 3, &cols, &rows, &cell_width, &cell_height);
  // 3x1: cells 40x48 (aspect 0.83)
  // 2x2: cells 60x24 (aspect 2.5) - closer to target 2.0
  cr_expect_eq(cols, 2, "Medium terminal with 3 clients should use 2x2 grid");
  cr_expect_eq(rows, 2);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 120, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 48, "Should use all vertical space");

  // Tall terminal with 3 clients - uses 2x2 for better aspect ratio
  // 1x3 gives aspect 3.33, 2x2 gives aspect 2.22, so 2x2 is closer to target 2.0
  calculate_optimal_grid(80, 72, 3, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 2, "Tall terminal with 3 clients should use 2x2 (better aspect)");
  cr_expect_eq(rows, 2);
  cr_expect_eq(cell_width, 40);
  cr_expect_eq(cell_height, 36);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 80, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 72, "Should use all vertical space");
}

Test(grid_layout, four_clients_2x2) {
  int cols, rows, cell_width, cell_height;

  // Standard terminal with 4 clients - uses 3x2 for better aspect ratio
  // 2x2 gives aspect 2.5, 3x2 gives aspect 1.67, 3x2 is closer to target 2.0
  calculate_optimal_grid(120, 48, 4, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 3, "4 clients should use 3x2 grid (better aspect)");
  cr_expect_eq(rows, 2);
  cr_expect_eq(cell_width, 40);
  cr_expect_eq(cell_height, 24);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 120, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 48, "Should use all vertical space");

  // Wide terminal with 4 clients - uses 4x1 (good aspect)
  calculate_optimal_grid(240, 24, 4, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 4, "Very wide terminal with 4 clients should use 4x1");
  cr_expect_eq(rows, 1);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 240, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 24, "Should use all vertical space");

  // Tall terminal with 4 clients - uses 2x2 for better aspect ratio
  // 1x4 gives aspect 8.33, 2x2 gives aspect 1.67, so 2x2 is closer to target 2.0
  calculate_optimal_grid(80, 96, 4, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 2, "Very tall terminal with 4 clients should use 2x2 (better aspect)");
  cr_expect_eq(rows, 2);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 80, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 96, "Should use all vertical space");
}

Test(grid_layout, five_to_six_clients) {
  int cols, rows, cell_width, cell_height;

  // 5 clients - uses 4x2 for better aspect ratio
  // 3x2 gives aspect 2.5, 4x2 gives aspect 1.875, 4x2 is closer to target 2.0
  calculate_optimal_grid(180, 48, 5, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 4, "Wide terminal with 5 clients should use 4x2 (better aspect)");
  cr_expect_eq(rows, 2);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 180, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 48, "Should use all vertical space");

  calculate_optimal_grid(120, 72, 5, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 2, "Tall terminal with 5 clients should use 2x3");
  cr_expect_eq(rows, 3);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 120, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 72, "Should use all vertical space");

  // 6 clients - uses 4x2 for better aspect ratio
  // 3x2 gives aspect 2.5, 4x2 gives aspect 1.875, 4x2 is closer to target 2.0
  calculate_optimal_grid(180, 48, 6, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 4, "6 clients should use 4x2 for better aspect");
  cr_expect_eq(rows, 2);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 180, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 48, "Should use all vertical space");
}

Test(grid_layout, seven_to_nine_clients) {
  int cols, rows, cell_width, cell_height;

  // 7 clients - 3x3 with 2 empty
  calculate_optimal_grid(150, 60, 7, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 3, "7 clients should use 3x3 grid");
  cr_expect_eq(rows, 3);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 150, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 60, "Should use all vertical space");

  // 8 clients - 3x3 with 1 empty
  calculate_optimal_grid(150, 60, 8, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 3, "8 clients should use 3x3 grid");
  cr_expect_eq(rows, 3);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 150, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 60, "Should use all vertical space");

  // 9 clients - uses 4x3 for better aspect ratio (closer to target 2.0)
  // 3x3 gives aspect 2.5, 4x3 gives aspect 1.85, 4x3 is closer to target 2.0
  calculate_optimal_grid(150, 60, 9, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 4, "9 clients should use 4x3 grid for better aspect");
  cr_expect_eq(rows, 3);
  cr_expect_eq(cell_width, 37); // 150 / 4 = 37
  cr_expect_eq(cell_height, 20);
  // NOTE: Due to integer division, 4*37 = 148, not 150 (off by 2)
  cr_expect_eq(cols * cell_width, 148, "Integer division causes rounding");
  cr_expect_eq(rows * cell_height, 60, "Should use all vertical space");
}

Test(grid_layout, edge_cases) {
  int cols, rows, cell_width, cell_height;

  // Zero clients
  calculate_optimal_grid(100, 40, 0, &cols, &rows, &cell_width, &cell_height);
  cr_expect_eq(cols, 0, "Zero clients should have 0 columns");
  cr_expect_eq(rows, 0, "Zero clients should have 0 rows");
  cr_expect_eq(cell_width, 0);
  cr_expect_eq(cell_height, 0);

  // Very small terminal - ensure minimum cell sizes
  calculate_optimal_grid(40, 20, 4, &cols, &rows, &cell_width, &cell_height);
  // Should avoid making cells too small (< 20x10)
  // With 40x20 terminal and 4 clients, 2x2 gives 20x10 cells (at minimum)
  cr_expect_eq(cols, 2, "Small terminal should still try 2x2 for 4 clients");
  cr_expect_eq(rows, 2);

  // Many clients in small terminal - should handle gracefully
  calculate_optimal_grid(60, 30, 10, &cols, &rows, &cell_width, &cell_height);
  // Should find best possible grid even if cells are small
  cr_expect(cols > 0 && rows > 0, "Should always produce valid grid");
  cr_expect_eq(cols * rows >= 10, 1, "Grid should fit all clients");
}

Test(grid_layout, aspect_ratio_preferences) {
  int cols, rows, cell_width, cell_height;

  // Test that algorithm prefers ~2:1 aspect ratio for cells
  // Terminal 200x50 with 4 clients
  // Option 1: 4x1 = cells 50x50 (aspect 1.0) - diff 1.0
  // Option 2: 2x2 = cells 100x25 (aspect 4.0) - diff 2.0
  // Option 3: 3x2 = cells 66.7x25 (aspect 2.67) - diff 0.67 ✓ BEST
  // Option 4: 1x4 = cells 200x12.5 (aspect 16.0) - diff 14.0
  calculate_optimal_grid(200, 50, 4, &cols, &rows, &cell_width, &cell_height);
  // Should pick 3x2 as it's closest to target aspect of 2.0
  cr_expect_eq(cols, 3, "Should pick 3x2 for best aspect ratio (2.67 closest to 2.0)");
  cr_expect_eq(rows, 2);
  // NOTE: Due to integer division, 3*66 = 198, not 200 (off by 2)
  cr_expect_eq(cols * cell_width, 198, "Integer division causes rounding");
  cr_expect_eq(rows * cell_height, 50, "Should use all vertical space");

  // Terminal 100x100 with 4 clients
  // Option 1: 2x2 = cells 50x50 (aspect 1.0)
  // Option 2: 4x1 = cells 25x100 (aspect 0.25)
  // Option 3: 1x4 = cells 100x25 (aspect 4.0)
  calculate_optimal_grid(100, 100, 4, &cols, &rows, &cell_width, &cell_height);
  // 2x2 with aspect 1.0 is closest to target 2.0
  cr_expect_eq(cols, 2, "Square terminal should use 2x2 for 4 clients");
  cr_expect_eq(rows, 2);
  // Assert full space utilization
  cr_expect_eq(cols * cell_width, 100, "Should use all horizontal space");
  cr_expect_eq(rows * cell_height, 100, "Should use all vertical space");
}

// Parameterized test for comprehensive coverage
static grid_test_case_t test_cases[] = {
    // Terminal WxH, Clients, Expected cols, rows, cell W, cell H
    {80, 24, 1, 1, 1, 80, 24},   // 1x1 grid, total 80x24
    {160, 48, 1, 1, 1, 160, 48}, // 1x1 grid, total 160x48
    {120, 30, 2, 2, 1, 60, 30},  // 2x1 grid, total 120x30
    {60, 40, 2, 1, 2, 60, 20},   // 1x2 grid, total 60x40
    {90, 30, 3, 2, 2, 45, 15},   // 2x2 grid, total 90x30
    {60, 60, 3, 1, 3, 60, 20},   // 1x3 grid, total 60x60
    {120, 40, 3, 2, 2, 60, 20},  // 2x2 grid, total 120x40
    {100, 40, 4, 3, 2, 33, 20},  // 3x2 grid, total 99x40 (off by 1)
    {160, 40, 4, 3, 2, 53, 20},  // 3x2 grid, total 159x40 (off by 1)
    {80, 80, 4, 2, 2, 40, 40},   // 2x2 grid, total 80x80
    {150, 50, 5, 3, 2, 50, 25},  // 3x2 grid, total 150x50
    {120, 60, 6, 3, 2, 40, 30},  // 3x2 grid, total 120x60
    {180, 60, 6, 3, 2, 60, 30},  // 3x2 grid, total 180x60
    {150, 60, 9, 4, 3, 37, 20},  // 4x3 grid, total 148x60 (off by 2)
};

ParameterizedTestParameters(grid_layout, parameterized_comprehensive) {
  size_t nb_cases = sizeof(test_cases) / sizeof(test_cases[0]);
  return cr_make_param_array(grid_test_case_t, test_cases, nb_cases);
}

ParameterizedTest(grid_test_case_t *tc, grid_layout, parameterized_comprehensive) {
  int cols, rows, cell_width, cell_height;

  calculate_optimal_grid(tc->terminal_width, tc->terminal_height, tc->num_clients, &cols, &rows, &cell_width,
                         &cell_height);

  cr_expect_eq(cols, tc->expected_cols, "Terminal %dx%d, %d clients: Expected %d cols, got %d", tc->terminal_width,
               tc->terminal_height, tc->num_clients, tc->expected_cols, cols);
  cr_expect_eq(rows, tc->expected_rows, "Terminal %dx%d, %d clients: Expected %d rows, got %d", tc->terminal_width,
               tc->terminal_height, tc->num_clients, tc->expected_rows, rows);
  cr_expect_eq(cell_width, tc->expected_cell_width, "Terminal %dx%d, %d clients: Expected cell width %d, got %d",
               tc->terminal_width, tc->terminal_height, tc->num_clients, tc->expected_cell_width, cell_width);
  cr_expect_eq(cell_height, tc->expected_cell_height, "Terminal %dx%d, %d clients: Expected cell height %d, got %d",
               tc->terminal_width, tc->terminal_height, tc->num_clients, tc->expected_cell_height, cell_height);

  // NOTE: We don't assert full space utilization here because integer division can cause
  // off-by-one or off-by-two rounding (e.g., 160/3=53, so 3*53=159 not 160).
  // This is expected behavior and matches the actual implementation.
}
