# ASCII-Chat Adaptive Grid Layout Architecture

## Overview
A pragmatic grid layout system that dynamically adapts to terminal dimensions and client count, maximizing space usage while maintaining readability. The system uses direct calculation with multi-factor scoring to handle N clients gracefully, similar to Zoom/Hangouts gallery view.

## Core Problems Solved

### 1. Hardcoded Client Limit
**Current Issue**: stream.c:746 hardcodes maximum of 9 clients (3x3 grid)
**Problem**: Cannot scale beyond 9 clients regardless of terminal size
**Solution**: Calculate terminal capacity dynamically based on minimum readable cell size

### 2. No Overflow Handling
**Current Issue**: System fails ungracefully when too many clients connect
**Problem**: No mechanism to handle clients beyond capacity
**Solution**: Pagination system to cycle through client pages

### 3. Suboptimal Space Usage
**Current Issue**: Simple aspect ratio scoring doesn't maximize terminal space
**Problem**: Leaves wasted space, especially for non-standard terminal sizes
**Solution**: Multi-factor scoring algorithm balancing aspect ratio, utilization, cell size, and balance

## Architecture Components

### Layout Modes

The system supports two layout modes:

1. **Gallery Mode**: All visible clients displayed in equal-sized cells arranged in an optimal grid. Supports pagination when client count exceeds terminal capacity.

2. **Focus Mode**: One client displayed large with remaining clients as thumbnails. Useful for presentations, active speaker highlighting, or odd client counts.

### Grid Layout Result
```c
typedef enum {
    LAYOUT_MODE_GALLERY,    // All cells equal size (with pagination if needed)
    LAYOUT_MODE_FOCUS       // One large cell + thumbnail strip
} layout_mode_t;

typedef struct {
    layout_mode_t mode;

    // Gallery mode fields
    struct {
        int cols;                   // Grid columns
        int rows;                   // Grid rows
        int cell_width;             // Cell width in characters
        int cell_height;            // Cell height in characters
    } gallery;

    // Focus mode fields
    struct {
        int focus_width;            // Large cell dimensions
        int focus_height;
        int thumb_width;            // Thumbnail dimensions
        int thumb_height;
        int thumb_count;            // How many thumbnails fit
        bool thumbnails_on_right;   // true=right strip, false=bottom strip
        uint32_t focus_client_id;   // Which client is focused (0=auto)
    } focus;

    // Common fields
    int visible_clients;        // How many clients fit on screen
    int total_clients;          // Total connected clients
    int page_number;            // Current page (0-indexed, for gallery mode)
    int total_pages;            // Total pages needed (for gallery mode)
    int terminal_capacity;      // Max clients this terminal can show
} grid_layout_t;
```

### Core Algorithm Components

#### 1. Terminal Capacity Calculation
```c
int calculate_terminal_capacity(int width, int height) {
    // Minimum readable cell sizes
    const int MIN_CELL_WIDTH = 15;   // 15 chars minimum for recognizable video
    const int MIN_CELL_HEIGHT = 6;   // 6 lines minimum for readable ASCII

    int max_cols = width / MIN_CELL_WIDTH;
    int max_rows = height / MIN_CELL_HEIGHT;
    int capacity = max_cols * max_rows;

    // Sanity limit to prevent excessive memory usage
    return (capacity > 100) ? 100 : capacity;
}
```

#### 2. Gallery Layout Selection
```c
grid_layout_t calculate_gallery_layout(int client_count, int width, int height) {
    int capacity = calculate_terminal_capacity(width, height);
    int visible = (client_count < capacity) ? client_count : capacity;

    // Try all possible grid configurations
    float best_score = -INFINITY;
    int best_cols = 1;
    int best_rows = visible;

    for (int cols = 1; cols <= visible; cols++) {
        int rows = (visible + cols - 1) / cols;  // Ceiling division

        // Skip configurations with excessive empty cells
        int empty_cells = (cols * rows) - visible;
        if (empty_cells > (cols < rows ? cols : rows)) {
            continue;  // Don't waste more than one row/column
        }

        int cell_w = width / cols;
        int cell_h = height / rows;

        // Skip if cells too small
        if (cell_w < 15 || cell_h < 6) {
            continue;
        }

        // Multi-factor scoring
        float score = calculate_grid_score(cols, rows, cell_w, cell_h, visible, width, height);

        if (score > best_score) {
            best_score = score;
            best_cols = cols;
            best_rows = rows;
        }
    }

    grid_layout_t layout = {0};
    layout.mode = LAYOUT_MODE_GALLERY;
    layout.gallery.cols = best_cols;
    layout.gallery.rows = best_rows;
    layout.gallery.cell_width = width / best_cols;
    layout.gallery.cell_height = height / best_rows;
    layout.visible_clients = visible;
    layout.total_clients = client_count;
    layout.page_number = 0;
    layout.total_pages = (client_count + visible - 1) / visible;
    layout.terminal_capacity = capacity;

    return layout;
}
```

#### 2b. Focus Layout Calculation
```c
grid_layout_t calculate_focus_layout(int client_count, int width, int height,
                                    uint32_t focus_client_id) {
    grid_layout_t layout = {0};
    layout.mode = LAYOUT_MODE_FOCUS;
    layout.focus.focus_client_id = focus_client_id;

    // Determine thumbnail placement based on terminal aspect ratio
    float terminal_aspect = (float)width / height;
    layout.focus.thumbnails_on_right = (terminal_aspect > 1.5);  // Wide terminals

    if (layout.focus.thumbnails_on_right) {
        // Right thumbnail strip: 75% width for focus, 25% for thumbnails
        layout.focus.focus_width = (int)(width * 0.75);
        layout.focus.focus_height = height;

        layout.focus.thumb_width = width - layout.focus.focus_width;
        layout.focus.thumb_height = 8;  // Each thumbnail 8 lines tall
        layout.focus.thumb_count = height / layout.focus.thumb_height;
    } else {
        // Bottom thumbnail strip: 70% height for focus, 30% for thumbnails
        layout.focus.focus_width = width;
        layout.focus.focus_height = (int)(height * 0.70);

        int thumb_area_width = width;
        int remaining_clients = client_count - 1;  // Minus focused client
        layout.focus.thumb_width = thumb_area_width / remaining_clients;
        layout.focus.thumb_height = height - layout.focus.focus_height;
        layout.focus.thumb_count = remaining_clients;
    }

    // Limit thumbnails to what actually fits
    if (layout.focus.thumb_count > client_count - 1) {
        layout.focus.thumb_count = client_count - 1;
    }

    layout.visible_clients = 1 + layout.focus.thumb_count;
    layout.total_clients = client_count;
    layout.terminal_capacity = 1 + layout.focus.thumb_count;  // Focus can show this many

    return layout;
}
```

#### 2c. Automatic Mode Selection
```c
grid_layout_t calculate_optimal_layout(int client_count, int width, int height,
                                      uint32_t focus_client_id, bool user_wants_focus) {
    // User explicitly requested focus mode
    if (user_wants_focus) {
        return calculate_focus_layout(client_count, width, height, focus_client_id);
    }

    // Default to gallery mode with pagination if needed
    return calculate_gallery_layout(client_count, width, height);
}
```

#### 3. Multi-Factor Scoring
```c
float calculate_grid_score(int cols, int rows, int cell_w, int cell_h,
                          int visible_clients, int term_w, int term_h) {
    // Factor 1: Aspect Ratio Score (prefer ~2:1 for ASCII characters)
    float cell_aspect = (float)cell_w / cell_h;
    float target_aspect = 2.0f;
    float aspect_score = 1.0f / (1.0f + fabsf(cell_aspect - target_aspect));

    // Factor 2: Space Utilization (minimize empty cells)
    int total_cells = cols * rows;
    float util_score = (float)visible_clients / total_cells;

    // Factor 3: Cell Size Score (prefer larger cells when possible)
    // Normalize against minimum (15x6) up to 3x minimum
    float size_w = fminf(cell_w / 15.0f, 3.0f);
    float size_h = fminf(cell_h / 6.0f, 3.0f);
    float size_score = (size_w + size_h) / 2.0f;

    // Factor 4: Terminal Shape Match (prefer grid that matches terminal shape)
    // Wide terminals get wide grids, tall terminals get tall grids
    float terminal_aspect = (float)term_w / term_h;
    float grid_aspect = (float)cols / rows;
    float shape_match = 1.0f / (1.0f + fabsf(terminal_aspect - grid_aspect));

    // Weighted combination
    // Aspect ratio most important (35%), then utilization (25%), size (25%), shape match (15%)
    return aspect_score * 0.35f +
           util_score * 0.25f +
           size_score * 0.25f +
           shape_match * 0.15f;
}
```

## Focus Mode

Focus mode displays one client large with others as thumbnails, ideal for presentations, active speaker highlighting, or when you want to emphasize one participant.

### User Controls

```c
// New packet type for focus control
typedef enum {
    FOCUS_CMD_TOGGLE,       // Toggle focus mode on/off
    FOCUS_CMD_NEXT,         // Focus next client
    FOCUS_CMD_PREV,         // Focus previous client
    FOCUS_CMD_SPECIFIC,     // Focus specific client by ID
    FOCUS_CMD_SELF          // Focus your own video
} focus_command_t;

typedef struct {
    focus_command_t command;
    uint32_t client_id;     // For FOCUS_CMD_SPECIFIC
} focus_control_packet_t;

// Server tracks focus state per client
typedef struct {
    bool focus_mode_enabled;
    uint32_t focused_client_id;  // 0 = auto (first client)
} client_focus_state_t;
```

### Keyboard Shortcuts

- `F` - Toggle focus mode on/off
- `Tab` - Focus next client (cycles through all)
- `Shift+Tab` - Focus previous client
- `1-9` - Focus client by number
- `0` - Focus self

### Visual Examples

**3 clients on 80×24 with focus (wide terminal, right thumbnails):**
```
┌──────────────────────────────────────────────┬─────────┐
│                                              │ Client2 │
│                                              │(8 lines)│
│            Client 1 (FOCUSED)                ├─────────┤
│                                              │ Client3 │
│                                              │(8 lines)│
└──────────────────────────────────────────────┴─────────┘
              60 chars wide                     20 wide
```

**5 clients on 120×40 with focus (wide terminal, right thumbnails):**
```
┌────────────────────────────────────────────────┬────────┐
│                                                │Client 2│
│                                                ├────────┤
│                                                │Client 3│
│          Client 1 (FOCUSED)                    ├────────┤
│                                                │Client 4│
│                                                ├────────┤
│                                                │Client 5│
└────────────────────────────────────────────────┴────────┘
```

**3 clients on 80×40 with focus (squarish terminal, bottom thumbnails):**
```
┌────────────────────────────────────────────────────────┐
│                                                        │
│                                                        │
│              Client 1 (FOCUSED)                        │
│                                                        │
│                                                        │
├─────────────────────────┬──────────────────────────────┤
│       Client 2          │         Client 3             │
│     (thumbnail)         │       (thumbnail)            │
└─────────────────────────┴──────────────────────────────┘
```

## Gallery Mode with Pagination

Gallery mode displays all visible clients in equal-sized cells. When client count exceeds terminal capacity, pagination allows cycling through pages.

### Page State Management
```c
typedef struct {
    int current_page;         // Current page being viewed (0-indexed)
    int clients_per_page;     // How many clients fit per page
    uint32_t *visible_clients; // Array of client IDs on current page
} page_state_t;

// Calculate which clients to show on a given page
void get_page_clients(int page, int clients_per_page,
                     uint32_t *all_clients, int total_clients,
                     uint32_t *out_visible, int *out_count) {
    int start = page * clients_per_page;
    int end = start + clients_per_page;
    if (end > total_clients) end = total_clients;

    *out_count = end - start;
    for (int i = 0; i < *out_count; i++) {
        out_visible[i] = all_clients[start + i];
    }
}
```

### Page Control Protocol
```c
// Client sends page control commands
typedef enum {
    PAGE_CMD_NEXT,      // Go to next page
    PAGE_CMD_PREV,      // Go to previous page
    PAGE_CMD_FIRST,     // Go to first page
    PAGE_CMD_LAST,      // Go to last page
    PAGE_CMD_GOTO       // Go to specific page number
} page_command_t;

typedef struct {
    page_command_t command;
    int page_number;    // For PAGE_CMD_GOTO
} page_control_packet_t;
```

### Display Indicator
```c
// Add page indicator to bottom of frame
void add_page_indicator(char *frame, int width, int height,
                       int current_page, int total_pages) {
    if (total_pages <= 1) return;  // Don't show for single page

    char indicator[64];
    snprintf(indicator, sizeof(indicator),
             " Page %d/%d | Press [ ] to navigate ",
             current_page + 1, total_pages);

    // Center at bottom of frame
    int indicator_len = strlen(indicator);
    int x_pos = (width - indicator_len) / 2;
    if (x_pos < 0) x_pos = 0;

    // Insert into last line of frame
    // Implementation depends on frame format
}
```

## Implementation Phases

### Phase 1: Core Grid System (2 weeks = 36 hours)

**Week 1: Algorithm Implementation**
- Create `lib/grid_layout.c/h` with core functions (8h)
  - `calculate_terminal_capacity()`
  - `calculate_optimal_grid()`
  - `calculate_grid_score()` with multi-factor scoring
- Remove hardcoded 9-client limit in stream.c (4h)
- Integrate new grid calculator with stream.c (6h)
- Handle edge cases (very small terminals, 1 client, etc.) (4h)

**Week 2: Testing & Polish**
- Comprehensive unit tests (10h)
  - Terminal sizes: 40x20, 80x24, 120x40, 200x60
  - Client counts: 1, 2, 3, 4, 6, 9, 12, 16, 25, 50
  - Verify optimal grid selection
  - Verify space maximization
  - Test scoring algorithm
- Bug fixing and refinement (4h)

**Deliverables:**
- ✅ Handles N clients (no hardcoded limit)
- ✅ Always calculates optimal grid
- ✅ Multi-factor scoring for quality
- ✅ Comprehensive test coverage

### Phase 2: Overflow Handling (1 week = 16 hours)

**Pagination Implementation**
- Add page state to client_info_t (2h)
- Implement page calculation logic (4h)
- Client keyboard controls for page switching (4h)
  - '[' = previous page
  - ']' = next page
- Display page indicator on screen (2h)
- Testing with 20+ clients (4h)

**Deliverables:**
- ✅ Pagination when clients > capacity
- ✅ Keyboard shortcuts for navigation
- ✅ Visual page indicator
- ✅ Smooth page transitions

### Phase 3: Optional Enhancements (1 week if needed)

**L-Shape Layout for 3 Clients**
Only implement if specifically needed:
- Detection logic for square-ish terminals (4h)
- L-shape cell positioning (6h)
- Testing with various terminal sizes (6h)

**Active Speaker Mode** (Future enhancement)
- Audio level tracking
- Auto-focus on active speaker
- Thumbnail strip for others

## Testing Strategy

### Unit Tests
```c
// Test terminal capacity calculation
Test(grid_layout, capacity_calculation) {
    cr_assert_eq(calculate_terminal_capacity(80, 24), 10);  // 5x2 grid
    cr_assert_eq(calculate_terminal_capacity(120, 40), 30); // 8x6 grid (truncated to 30)
    cr_assert_eq(calculate_terminal_capacity(200, 60), 80); // 13x10 grid
    cr_assert_eq(calculate_terminal_capacity(40, 20), 2);   // 2x1 grid
}

// Test optimal grid selection
Test(grid_layout, optimal_grid_selection) {
    grid_layout_t layout;

    // 4 clients in 80x24 terminal
    layout = calculate_optimal_grid(4, 80, 24);
    cr_assert_eq(layout.cols, 2);
    cr_assert_eq(layout.rows, 2);
    cr_assert_eq(layout.cell_width, 40);
    cr_assert_eq(layout.cell_height, 12);

    // 6 clients in 120x40 terminal
    layout = calculate_optimal_grid(6, 120, 40);
    cr_assert_eq(layout.cols, 3);
    cr_assert_eq(layout.rows, 2);

    // 50 clients exceeding capacity
    layout = calculate_optimal_grid(50, 80, 24);
    cr_assert_eq(layout.visible_clients, 10);  // Capacity
    cr_assert_eq(layout.total_clients, 50);
    cr_assert_eq(layout.total_pages, 5);
}

// Test scoring algorithm
Test(grid_layout, scoring_algorithm) {
    // 2:1 aspect ratio should score better than 4:1 for square terminals
    float score_2_1 = calculate_grid_score(2, 2, 40, 20, 4, 80, 40);
    float score_4_1 = calculate_grid_score(4, 1, 20, 24, 4, 80, 24);
    cr_assert_gt(score_2_1, score_4_1);

    // Higher utilization should score better
    float score_util_high = calculate_grid_score(2, 2, 40, 12, 4, 80, 24);  // 4/4 = 100%
    float score_util_low = calculate_grid_score(3, 2, 26, 12, 4, 78, 24);   // 4/6 = 67%
    cr_assert_gt(score_util_high, score_util_low);

    // Wide terminal should prefer wide grids (6x1 over 2x3)
    float score_wide_grid = calculate_grid_score(6, 1, 26, 24, 6, 160, 24);   // 6x1 on 160x24
    float score_tall_grid = calculate_grid_score(2, 3, 80, 8, 6, 160, 24);    // 2x3 on 160x24
    cr_assert_gt(score_wide_grid, score_tall_grid);

    // Tall terminal should prefer tall grids (1x6 over 3x2)
    float score_tall_match = calculate_grid_score(1, 6, 40, 10, 6, 40, 60);   // 1x6 on 40x60
    float score_wide_mismatch = calculate_grid_score(3, 2, 13, 30, 6, 40, 60); // 3x2 on 40x60
    cr_assert_gt(score_tall_match, score_wide_mismatch);
}
```

### Integration Tests

**Gallery Mode:**
- 1 client: full-screen usage
- 2 clients: optimal split (horizontal vs vertical based on terminal)
- 3-9 clients: optimal grid selection
- 10-25 clients: pagination behavior
- 50+ clients: multi-page handling
- Page navigation: next/prev page cycling

**Focus Mode:**
- 3 clients: focus layout calculation with thumbnail strip
- Odd client counts: focus + thumbnails arrangement
- Thumbnail positioning: right strip (wide term) vs bottom strip (tall term)
- Focus cycling: Tab/Shift+Tab navigation
- Direct focus: number key selection (1-9, 0 for self)

**Mode Switching:**
- Toggle between gallery and focus (F key)
- Terminal resize: recalculation in both modes
- Dynamic join/leave: layout updates in both modes
- Mode persistence across client changes

### Visual Validation
- No missing pixels or artifacts
- Proper aspect ratio preservation in both modes
- Maximum space utilization in both modes
- Smooth layout transitions when switching modes
- Page indicator visibility (gallery mode)
- Focus indicator visibility (focus mode)
- Clear thumbnail borders and labels

## Success Metrics

- **Space Utilization**: >95% of terminal space used (both modes)
- **Aspect Ratio**: Cell aspect ratios within 1.5-2.5 range (optimal for ASCII)
- **Performance**: <5ms layout calculation (both modes)
- **Scalability**: Handles 1-100 clients gracefully
- **Usability**: Intuitive controls (pagination in gallery, focus switching in focus mode)
- **Mode Switching**: <100ms transition time between modes

## Example Scenarios

### Standard Terminal (80x24) - Gallery Mode
- **Capacity**: 10 clients (5 cols × 2 rows at 16×12 cells)
- **1 client**: 80×24 full screen
- **2 clients**: 2×1 horizontal split (40×24 each)
- **4 clients**: 2×2 grid (40×12 each)
- **9 clients**: 3×3 grid (26×8 each)
- **15 clients**: Page 1 shows 10, page 2 shows 5

### Standard Terminal (80x24) - Focus Mode
- **3 clients**: Focus: 54×24, Thumbnails: 26×8 (right strip, 3 thumbs)
- **5 clients**: Focus: 64×24, Thumbnails: 16×6 (right strip, 4 thumbs)
- **7 clients**: Focus: 80×16, Thumbnails: 26×8 (bottom strip, 3 thumbs × 2 rows)

### Large Terminal (200×60) - Gallery Mode
- **Capacity**: 80 clients (13 cols × 6 rows at 15×10 cells)
- **1 client**: 200×60 full screen
- **4 clients**: 2×2 grid (100×30 each)
- **16 clients**: 4×4 grid (50×15 each)
- **50 clients**: Single page, 8×7 grid (25×8 each)
- **100 clients**: Page 1 shows 80, page 2 shows 20

### Large Terminal (200×60) - Focus Mode
- **10 clients**: Focus: 160×60, Thumbnails: 40×20 (right strip, 3 thumbs)
- **20 clients**: Focus: 180×60, Thumbnails: 20×15 (right strip, 4 thumbs)

### Small Terminal (40×20) - Gallery Mode
- **Capacity**: 2 clients (2 cols × 1 row at 20×20 cells)
- **1 client**: 40×20 full screen
- **2 clients**: 2×1 horizontal split (20×20 each)
- **5 clients**: Page 1 shows 2, page 2 shows 2, page 3 shows 1

### Small Terminal (40×20) - Focus Mode
- **3 clients**: Focus: 40×14, Thumbnails: 20×6 (bottom strip, 2 thumbs)

## Key Benefits

1. **Scalable**: Handles any N clients up to terminal capacity
2. **Adaptive**: Always maximizes terminal space usage in both modes
3. **Quality-aware**: Enforces minimum readable cell sizes
4. **Flexible**: Two complementary modes for different use cases
5. **User-controlled**: Full keyboard control for mode and focus selection
6. **Maintainable**: Direct calculation, no complex abstractions
7. **Testable**: Clear inputs/outputs, predictable behavior
8. **Graceful**: Pagination handles overflow, focus mode handles odd counts

## Migration from Current Implementation

### Current Code Location
- Grid calculation: `src/server/stream.c:619-695`
- ASCII grid generation: `lib/image2ascii/ascii.c:384-622`

### Migration Steps
1. Create `lib/grid_layout.c/h` with new functions
2. Implement gallery mode with capacity calculation and scoring
3. Implement focus mode with thumbnail rendering
4. Add packet protocol extensions for focus control
5. Update `stream.c` to call `calculate_optimal_layout()`
6. Remove hardcoded limits and old scoring logic
7. Update `ascii.c` to support both layout modes
8. Implement keyboard input handling for mode/focus switching
9. Add comprehensive test suite for both modes
10. Deploy and monitor

### Backward Compatibility
- New system produces same or better layouts for 1-9 clients
- Existing terminal size detection unchanged
- Aspect ratio handling improved but compatible

## Future Enhancements

### Priority Queue (Optional)
When pagination is active, could implement:
- Active speaker detection (via audio levels)
- Priority-based client ordering
- Recently active clients shown first

### Auto-Focus Mode (Optional)
- Automatic focus switching based on active speaker
- Configurable auto-switch timeout
- Manual override support

### Advanced Focus Features (Optional, Low Priority)
- Picture-in-picture mode for focus cell
- Floating thumbnail positioning
- Thumbnail ordering preferences

## Conclusion

This architecture provides a practical, maintainable solution that handles N clients gracefully while maximizing terminal space usage. The two-mode system (Gallery with pagination, Focus with thumbnails) elegantly handles all client counts from 1 to 100+. By calculating capacity dynamically and using multi-factor scoring, the system adapts to any terminal size while maintaining quality thresholds.

**Implementation time: 4-5 weeks**
- Week 1: Core gallery mode + capacity calculation (20 hours)
- Week 2: Multi-factor scoring + terminal shape matching (20 hours)
- Week 3: Pagination system (20 hours)
- Week 4: Focus mode + thumbnail rendering (20 hours)
- Week 5: User controls + keyboard shortcuts + testing (20 hours)
