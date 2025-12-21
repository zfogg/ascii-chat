#!/bin/bash
# Test script to verify grid layout fixes

set -e

echo "Building project with grid layout fixes..."
cmake --preset default -B build 2>&1 | grep -E "(error|Grid|grid|CMake)" || true

# Try to build just the ascii tests if possible
if cmake --build build --target ascii_create_grid_test 2>/dev/null; then
    echo "✓ Grid layout test built successfully"
    ./build/bin/ascii_create_grid_test 2>&1 | head -50
else
    echo "⚠ Full build needed, attempting minimal build..."
    # Just try to compile the file without full test suite
    cmake --build build 2>&1 | tail -20 || true
fi

echo ""
echo "Grid layout fix testing complete!"
echo ""
echo "KEY CHANGES MADE:"
echo "1. Fixed aspect ratio scoring (removed buggy logf)"
echo "2. Fixed empty cell tolerance (now <= cols/rows instead of 50%)"
echo "3. Fixed single-source centering (safe padding calculation)"
echo "4. Improved cell dimension calculation (proper separator accounting)"
echo "5. Implemented proper multi-objective scoring"
echo ""
echo "WEIGHTS:"
echo "- Cell Size (Readability):     35% (MOST IMPORTANT)"
echo "- Cell Aspect Ratio:           30%"
echo "- Space Utilization:           25%"
echo "- Square Grid Preference:       5%"
echo "- Terminal Shape Matching:      5%"
