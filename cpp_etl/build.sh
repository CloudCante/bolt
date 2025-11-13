#!/bin/bash
# Quick build script for FoxETL file monitor

set -e  # Exit on error

echo "Building FoxETL file monitor..."

# Create build directory
mkdir -p build
cd build

# Configure and build
cmake ..
make

echo ""
echo "✓ Build successful!"
echo ""
echo "Run with: ./foxetl -d ../input"
echo "Or: ./foxetl -d ../input -p workstationOutputReport.xls"

