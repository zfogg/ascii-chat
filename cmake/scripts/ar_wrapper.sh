#!/bin/bash
# Wrapper to rename object files before archiving
set -e

# Extract the output archive and object directory from args
OUTPUT_ARCHIVE=""
for arg in "$@"; do
    if [[ "$arg" == *.a ]]; then
        OUTPUT_ARCHIVE="$arg"
        break
    fi
done

if [[ -n "$OUTPUT_ARCHIVE" ]]; then
    # Get the object directory from the archive path
    LIB_NAME="$(basename "$OUTPUT_ARCHIVE" .a)"
    # Strip 'lib' prefix if present
    LIB_NAME="${LIB_NAME#lib}"
    OBJ_DIR="/Users/loomen/src/github.com/zfogg/ascii-chat/build/CMakeFiles/${LIB_NAME}.dir"
    if [[ -d "$OBJ_DIR" ]]; then
        # Rename objects before archiving and update args
        /Users/loomen/src/github.com/zfogg/ascii-chat/cmake/scripts/rename_objects.sh "$OBJ_DIR" "/Users/loomen/src/github.com/zfogg/ascii-chat" >/dev/null 2>&1 || true
        
        # Update arguments to use renamed files
        NEW_ARGS=()
        for arg in "$@"; do
            if [[ "$arg" == *.o ]] && [[ "$arg" == CMakeFiles/* ]]; then
                # Get the relative path after CMakeFiles/target.dir/
                rel_path="${arg#CMakeFiles/*/}"
                new_name="${rel_path//\//_}"
                new_path="${OBJ_DIR}/${new_name}"
                if [[ -f "$new_path" ]]; then
                    NEW_ARGS+=("$new_path")
                elif [[ -f "$arg" ]]; then
                    NEW_ARGS+=("$arg")
                else
                    NEW_ARGS+=("$arg")
                fi
            else
                NEW_ARGS+=("$arg")
            fi
        done
        set -- "${NEW_ARGS[@]}"
    fi
fi

# Run the actual archiver (use REAL_AR from environment or default)
REAL_AR="${REAL_AR:-/usr/local/opt/llvm/bin/llvm-ar}"
exec "$REAL_AR" "$@"
