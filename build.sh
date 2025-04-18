#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# Define the build directory name
BUILD_DIR="build"

# Create the build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
  echo "--- Creating build directory: $BUILD_DIR ---"
  mkdir "$BUILD_DIR"
fi

# Navigate into the build directory
cd "$BUILD_DIR"

# Configure the project using CMake and Ninja generator
echo "--- Configuring project with CMake (using Ninja) ---"
cmake -G Ninja ..

# Build the project using Ninja
echo "--- Building project with Ninja ---"
ninja

echo "--- Build finished successfully! ---"

# Go back to the original directory if needed
 cd ..
