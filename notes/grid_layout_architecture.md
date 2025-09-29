# ASCII-Chat Adaptive Grid Layout Architecture

## Overview
A sophisticated grid layout system that dynamically adapts to terminal dimensions, maximizing space usage while preserving aspect ratios. The system uses a data-driven architecture with layout descriptors, fitness scoring, and extensible patterns.

## Core Problems Solved

### 1. Vertical Column Bug (FIXED)
**Issue**: Missing pixels in first client of multiclient mode
**Cause**: Stride calculation using `composite_width_px` instead of `composite->w`
**Fix**: Changed pixel copying loops to use actual composite dimensions:
```c
// BEFORE (buggy):
int dst_idx = (dst_y * composite_width_px) + dst_x;

// AFTER (fixed):
int dst_idx = (dst_y * composite->w) + dst_x;
```

### 2. Static Layouts Waste Space
Current fixed grids (2x2, 3x3) don't adapt to terminal shape, leaving unnecessary borders and failing to maximize available space.

### 3. Poor Aspect Ratio Handling
No consideration for terminal character dimensions (~2:1 height:width ratio) leading to distorted video in certain configurations.

## Architecture Components

### Grid Cell Structure
```c
typedef struct {
    int id;                    // Unique cell identifier
    float x_percent;           // Position as % of container
    float y_percent;
    float width_percent;       // Dimensions as % of container
    float height_percent;
    int pixel_x;              // Calculated pixel position
    int pixel_y;
    int pixel_width;          // Calculated pixel dimensions
    int pixel_height;
    bool is_active;           // Whether cell contains content
    int client_id;            // Which client occupies this cell
} grid_cell_t;
```

### Layout Descriptor
```c
typedef enum {
    LAYOUT_SINGLE,          // 1x1 full screen
    LAYOUT_HORIZONTAL,      // 1xN horizontal strip
    LAYOUT_VERTICAL,        // Nx1 vertical strip
    LAYOUT_GRID,           // NxM regular grid
    LAYOUT_L_SHAPE,        // L-shaped for 3 clients
    LAYOUT_FOCUS_PLUS      // Large main + thumbnails
} layout_type_t;

typedef struct layout_descriptor {
    const char *name;
    layout_type_t type;
    int min_clients, max_clients;
    float min_aspect_ratio, max_aspect_ratio;
    float space_efficiency;    // 0.0-1.0
    int priority;              // Higher = preferred

    union {
        struct { int cols, rows; } grid;
        struct {
            int primary_cells, secondary_cells;
            bool horizontal_split;
        } l_shape;
    } params;

    // Function pointers
    void (*calculate_cells)(struct layout_descriptor*, grid_cell_t*, int, int, int);
    float (*score_fitness)(struct layout_descriptor*, int, float, float);
} layout_descriptor_t;
```

### Grid Manager
```c
typedef struct {
    layout_descriptor_t *current_layout;
    grid_cell_t *cells;
    int active_clients;
    int terminal_width, terminal_height;
    float terminal_aspect_ratio;

    // Layout registry
    layout_descriptor_t *available_layouts;
    int layout_count;

    // Scoring weights (user configurable)
    float weight_space_usage;
    float weight_aspect_preserve;
    float weight_balance;
} grid_manager_t;
```

## Layout Selection Algorithm

### Terminal-Aware Selection for N Clients
```c
grid_layout_t calculate_optimal_grid_layout(
    int terminal_width, int terminal_height, int client_count
) {
    // Account for character aspect ratio (~2:1 height:width)
    float terminal_ar = (float)(terminal_width * 2) / terminal_height;
    grid_layout_t layout = {0};

    // Special cases for small N (optimized layouts)
    if (client_count == 1) {
        layout.cols = 1;
        layout.rows = 1;
        return layout;
    }

    if (client_count == 3) {
        // L-shape for 3 clients in square terminals
        if (terminal_ar > 0.8 && terminal_ar < 1.5) {
            layout.use_l_shape = true;
            layout.cols = 2;
            layout.rows = 2;
            layout.primary_count = (terminal_ar > 1.0) ? 2 : 1;
            return layout;
        }
    }

    // General N-client algorithm: find optimal grid
    float best_score = -1.0;
    int best_cols = 1;
    int best_rows = client_count;

    // Try all possible arrangements
    for (int cols = 1; cols <= client_count; cols++) {
        int rows = (client_count + cols - 1) / cols;  // Ceiling division

        // Skip arrangements with too many empty cells
        int empty_cells = (cols * rows) - client_count;
        if (empty_cells > cols) continue;  // Don't waste more than one row

        // Calculate cell dimensions
        float cell_width = (float)terminal_width / cols;
        float cell_height = (float)terminal_height / rows;

        // Warn about very small cells but still allow them
        // Server should limit connections based on terminal capabilities
        if (cell_width < 10 || cell_height < 4) {
            // Cells are getting critically small - log warning
            if (cell_width < 10 && cell_height < 4) {
                log_warn("Grid cells critically small: %dx%d chars for %d clients",
                         (int)cell_width, (int)cell_height, client_count);
            }
            // Apply penalty to score but don't skip
            float size_penalty = 0.5;  // 50% penalty for tiny cells
            if (cell_width < 5 || cell_height < 2) {
                size_penalty = 0.1;  // 90% penalty for unusable cells
            }
            score *= size_penalty;
        }

        // Calculate aspect ratio of each cell
        float cell_ar = (cell_width * 2) / cell_height;  // Account for char dimensions

        // Score this arrangement (prefer ~2:1 aspect ratio for video)
        float target_ar = 2.0;
        float ar_score = 1.0 / (1.0 + fabs(cell_ar - target_ar));

        // Penalize wasted space
        float space_score = (float)client_count / (float)(cols * rows);

        // Prefer more square-like grids over extreme rectangles
        float balance_score = 1.0 / (1.0 + fabs(cols - rows));

        // Combined score with weights
        float score = (ar_score * 0.5) + (space_score * 0.3) + (balance_score * 0.2);

        // Bonus for arrangements that match terminal shape
        if ((terminal_ar > 1.5 && cols > rows) ||   // Wide terminal, wide grid
            (terminal_ar < 0.7 && rows > cols)) {    // Tall terminal, tall grid
            score *= 1.2;
        }

        if (score > best_score) {
            best_score = score;
            best_cols = cols;
            best_rows = rows;
        }
    }

    layout.cols = best_cols;
    layout.rows = best_rows;
    return layout;
}
```

### Fitness Scoring
```c
float score_layout_fitness(
    layout_descriptor_t *layout,
    int client_count,
    float terminal_ar,
    float video_ar
) {
    float score = 0.0;

    // Space utilization (how much terminal is used)
    float space_used = calculate_coverage(layout, client_count);
    score += space_used * 0.4;

    // Aspect preservation (how well video fits)
    float aspect_match = 1.0 - fabs(terminal_ar - video_ar) / video_ar;
    score += aspect_match * 0.4;

    // Visual balance (symmetry and arrangement)
    float balance = calculate_balance(layout, client_count);
    score += balance * 0.2;

    // Apply priority boost
    score *= (1.0 + layout->priority / 100.0);

    return score;
}
```

## L-Shape Layout Implementation

For 3 clients in square-ish terminals:
```
Wide L-Shape (2+1):        Tall L-Shape (1+2):
┌───────┬───────┐          ┌───────┬───────┐
│   1   │   2   │          │       │   2   │
├───────┴───────┤          │   1   ├───────┤
│       3       │          │       │   3   │
└───────────────┘          └───────┴───────┘
```

```c
void calculate_l_shape_layout(
    layout_descriptor_t *self,
    grid_cell_t *cells,
    int client_count,
    int width, int height
) {
    bool horizontal = self->params.l_shape.horizontal_split;

    if (horizontal) {
        // Two cells on top (66.7% height)
        cells[0] = (grid_cell_t){
            .x_percent = 0.0, .y_percent = 0.0,
            .width_percent = 0.5, .height_percent = 0.667
        };
        cells[1] = (grid_cell_t){
            .x_percent = 0.5, .y_percent = 0.0,
            .width_percent = 0.5, .height_percent = 0.667
        };
        // One cell below (33.3% height)
        cells[2] = (grid_cell_t){
            .x_percent = 0.0, .y_percent = 0.667,
            .width_percent = 1.0, .height_percent = 0.333
        };
    }
    // Similar for vertical L-shape...

    // Convert percentages to pixels
    for (int i = 0; i < client_count; i++) {
        cells[i].pixel_x = (int)(cells[i].x_percent * width);
        cells[i].pixel_y = (int)(cells[i].y_percent * height);
        cells[i].pixel_width = (int)(cells[i].width_percent * width);
        cells[i].pixel_height = (int)(cells[i].height_percent * height);
    }
}
```

## Layout Registry

Pre-defined layouts with priorities and constraints:
```c
static layout_descriptor_t g_layout_registry[] = {
    {
        .name = "Single Fullscreen",
        .type = LAYOUT_SINGLE,
        .min_clients = 1, .max_clients = 1,
        .space_efficiency = 1.0,
        .priority = 100,
        .calculate_cells = calculate_single_layout,
        .score_fitness = score_single_layout
    },
    {
        .name = "Horizontal Split",
        .type = LAYOUT_HORIZONTAL,
        .min_clients = 2, .max_clients = 2,
        .min_aspect_ratio = 1.5,  // Wide terminals only
        .space_efficiency = 0.95,
        .priority = 90,
        .params.grid = { .cols = 2, .rows = 1 }
    },
    {
        .name = "L-Shape Wide",
        .type = LAYOUT_L_SHAPE,
        .min_clients = 3, .max_clients = 3,
        .min_aspect_ratio = 1.0, .max_aspect_ratio = 1.8,
        .space_efficiency = 0.98,
        .priority = 95,
        .params.l_shape = { .primary_cells = 2, .horizontal_split = true }
    },
    // ... more layouts
};
```

## Integration with Stream Processing

```c
char* create_mixed_ascii_frame_with_grid_manager(
    grid_manager_t *manager,
    client_source_t *sources,
    int source_count,
    uint32_t target_client_id,
    size_t *out_size
) {
    // Select optimal layout
    layout_descriptor_t *layout = select_optimal_layout(manager, source_count);

    // Switch layouts if needed
    if (layout != manager->current_layout) {
        log_info("Switching to '%s' layout", layout->name);
        manager->current_layout = layout;
    }

    // Calculate cell positions
    layout->calculate_cells(layout, manager->cells, source_count,
                           manager->terminal_width, manager->terminal_height);

    // Create composite and render each client into their cell
    image_t *composite = image_new_from_pool(
        manager->terminal_width, manager->terminal_height);

    for (int i = 0; i < source_count; i++) {
        grid_cell_t *cell = &manager->cells[i];

        // Resize with aspect preservation
        image_t *fitted = resize_with_aspect_preserve(
            sources[i].image,
            cell->pixel_width,
            cell->pixel_height);

        // Copy to composite at cell position
        copy_image_to_composite(fitted, composite,
                              cell->pixel_x, cell->pixel_y,
                              cell->pixel_width, cell->pixel_height);

        image_destroy_to_pool(fitted);
    }

    // Convert to ASCII...
}
```

## Implementation Phases

### Phase 1: Bug Fixes ✅
- Fix vertical column stride bug
- Fix single client full-terminal usage
- Add diagnostic logging

### Phase 2: Core Algorithm (Current)
- Implement `calculate_optimal_grid_layout()`
- Add terminal aspect ratio calculation
- Create basic unit tests

### Phase 3: L-Shape Support
- Implement L-shape layout logic
- Handle 3-client special cases
- Test with various terminal sizes

### Phase 4: Data Structure Migration
- Convert to layout descriptor system
- Implement fitness scoring
- Add layout registry

### Phase 5: Polish & Optimization
- Cache layout calculations
- Add user preferences
- Performance tuning

## Testing Strategy

### Unit Tests
- Layout selection for various terminal dimensions
- Aspect ratio calculations
- Cell dimension calculations
- L-shape geometry validation

### Integration Tests
- Single client full-screen
- 2 clients (horizontal/vertical based on terminal shape)
- 3 clients (row/column/L-shape based on aspect ratio)
- 4-9 clients (optimal grid selection)
- 10-16 clients (4x4 grid max)
- 17-25 clients (5x5 grid)
- N clients (scales to any number)
- Terminal resize handling
- Dynamic client join/leave

### Visual Validation
- No missing pixels or artifacts
- Proper aspect ratio preservation
- Maximum space utilization
- Smooth layout transitions

## Success Metrics

- **Space Utilization**: >95% of terminal space used
- **Aspect Ratio**: No visible distortion
- **Performance**: <5ms layout calculation
- **Visual Quality**: No artifacts or missing pixels
- **Adaptability**: Correct layout selection for any terminal size

## Scalability for N Clients

The algorithm scales to any number of clients with intelligent degradation:

### Connection Limit Calculation
```c
int calculate_max_clients_for_terminal(int width, int height) {
    // Minimum usable cell size for video
    const int MIN_USABLE_WIDTH = 10;
    const int MIN_USABLE_HEIGHT = 4;

    // Calculate maximum grid that fits
    int max_cols = width / MIN_USABLE_WIDTH;
    int max_rows = height / MIN_USABLE_HEIGHT;
    int max_clients = max_cols * max_rows;

    // Apply reasonable limits
    if (max_clients > 100) {
        max_clients = 100;  // Sanity limit
        log_info("Terminal could support %d clients but limiting to 100",
                 max_cols * max_rows);
    }

    return max_clients;
}

// Server should check before accepting connections
bool can_accept_new_client(grid_manager_t *manager) {
    int max_allowed = calculate_max_clients_for_terminal(
        manager->terminal_width,
        manager->terminal_height);

    if (manager->active_clients >= max_allowed) {
        log_warn("Rejecting connection: %d clients already connected (max %d for %dx%d terminal)",
                 manager->active_clients, max_allowed,
                 manager->terminal_width, manager->terminal_height);
        return false;
    }
    return true;
}
```

### Cell Size Thresholds
- **20x8+**: Excellent quality, fully readable
- **15x6**: Good quality, readable
- **10x4**: Minimum usable, recognizable shapes
- **5x2**: Emergency mode, barely visible
- **<5x2**: Unusable but still rendered

### Example Scenarios
- **40x20 terminal, 20 clients**: Creates 5x4 grid with 8x5 char cells (usable but tiny)
- **80x24 terminal, 30 clients**: Creates 6x5 grid with 13x4 char cells (readable)
- **120x40 terminal, 50 clients**: Creates 8x7 grid with 15x5 char cells (good)
- **200x60 terminal, 100 clients**: Creates 10x10 grid with 20x6 char cells (excellent)

The algorithm always finds a solution but the server should limit connections based on quality thresholds.

## Separation of Concerns: Rendering vs Connection Policy

A critical architectural decision is the complete separation between rendering capability and connection acceptance policy. This ensures system robustness while maintaining quality standards.

### Rendering Layer (Always Works)
The grid layout algorithm is **unconditionally stable** - it will always produce a valid layout regardless of input:
- 100 clients in a 10x10 terminal? Creates a 10x10 grid with 1x1 cells
- 50 clients in a 40x20 terminal? Creates a 8x7 grid with 5x2 cells
- The renderer never refuses to draw - it simply does its best with available space

This guarantees the system never crashes or fails to display something, even in extreme conditions.

### Connection Policy Layer (Enforces Quality)
The server implements quality standards through connection limits:
```c
// In server.c accept_new_client()
if (!can_accept_new_client(&grid_manager)) {
    send_rejection_packet(client_socket, "Terminal too small for additional clients");
    close(client_socket);
    return;
}
```

This separation provides multiple benefits:
1. **Robustness**: Rendering never fails, system stays stable
2. **Flexibility**: Quality thresholds can be configured without touching render code
3. **Debugging**: Can force-test extreme scenarios by bypassing connection limits
4. **User Control**: Could add `--force-accept` flag to override limits for testing
5. **Graceful Degradation**: System degrades predictably rather than failing suddenly

### Policy Configuration Options
```c
typedef struct {
    int min_cell_width;     // Default: 10 chars
    int min_cell_height;    // Default: 4 chars
    int max_clients;        // Default: 100
    bool enforce_limits;    // Default: true
    bool warn_on_small;     // Default: true
} connection_policy_t;
```

This architecture pattern - separating "what's possible" from "what's acceptable" - ensures ASCII-Chat remains stable under any conditions while still maintaining a quality user experience.

## Key Benefits

1. **Intelligent**: Adapts to terminal dimensions automatically
2. **Scalable**: Works for any N clients, not hardcoded limits
3. **Efficient**: Maximizes space usage while preserving aspect ratios
4. **Extensible**: Easy to add new layouts via registry
5. **Testable**: Clear separation of concerns
6. **Performant**: O(N) complexity, cacheable calculations

## Next Steps

1. Complete Phase 2 - implement core algorithm
2. Test with real multi-client scenarios
3. Begin L-shape implementation for 3-client case
4. Profile performance with 9 clients
5. Consider WebSocket support for browser clients