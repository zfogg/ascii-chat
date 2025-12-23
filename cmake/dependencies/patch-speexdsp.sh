#!/bin/sh
# Patch script for speexdsp - generates configure script

SOURCE_DIR="$1"

if [ -z "$SOURCE_DIR" ]; then
    echo "Error: SOURCE_DIR not provided"
    exit 1
fi

cd "$SOURCE_DIR" || exit 1

echo "Generating speexdsp configure script in $SOURCE_DIR"

# Run autogen.sh to create configure script
if [ -f "autogen.sh" ]; then
    echo "Running autogen.sh..."
    sh autogen.sh || {
        echo "autogen.sh failed, trying autoreconf..."
        autoreconf -i || exit 1
    }
else
    echo "autogen.sh not found, running autoreconf..."
    autoreconf -i || exit 1
fi

# Verify configure was created
if [ ! -f "configure" ]; then
    echo "Error: configure script was not created"
    ls -la
    exit 1
fi

echo "Configure script created successfully"
ls -la configure
