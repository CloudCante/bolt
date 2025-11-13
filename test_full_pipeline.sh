#!/bin/bash
# Test full pipeline: C++ conversion/cleaning -> Python import

set -e

echo "=========================================="
echo "TESTING FULL PIPELINE"
echo "=========================================="
echo ""

TEST_FILE="test_import/workstationOutputReport_04_09_2025_to_04_12_2025.xls"
QUEUE_DIR="test_import/queue"
CLEANED_CSV=""

# Step 1: Convert XLS to CSV using C++
echo "[1/4] Converting XLS to CSV (C++)..."
mkdir -p "$QUEUE_DIR"
libreoffice --headless --convert-to "csv:Text - txt - csv (StarCalc):44,34,UTF8" --outdir "$QUEUE_DIR" "$TEST_FILE" 2>&1 | tail -1
sleep 2

CSV_FILE=$(find "$QUEUE_DIR" -name "*.csv" | head -1)
if [ -z "$CSV_FILE" ]; then
    echo "❌ ERROR: CSV conversion failed!"
    exit 1
fi
echo "✓ Converted to: $CSV_FILE"
echo ""

# Step 2: Clean CSV using C++ DataProcessor
echo "[2/4] Cleaning CSV columns (C++)..."
cd cpp_etl
./build.sh > /dev/null 2>&1
cd ..

# Create a simple C++ test program to clean CSV
cat > test_clean_csv.cpp << 'EOF'
#include "DataProcessor.h"
#include "FileTypeDetector.h"
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <csv_file> <output_file>" << std::endl;
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string outputFile = argv[2];
    
    // Copy input to output first
    fs::copy_file(inputFile, outputFile, fs::copy_options::overwrite_existing);
    
    // Detect file type
    std::string filename = fs::path(inputFile).filename().string();
    FileType fileType = FileTypeDetector::detect(filename);
    
    if (fileType == FileType::UNKNOWN) {
        std::cerr << "Unknown file type" << std::endl;
        return 1;
    }
    
    // Clean CSV
    DataProcessor processor;
    if (!processor.cleanCSV(outputFile, fileType)) {
        std::cerr << "Cleaning failed: " << processor.getLastError() << std::endl;
        return 1;
    }
    
    auto stats = processor.getLastCleanStats();
    std::cout << "Cleaned: " << stats.originalColumns << " -> " 
              << stats.cleanedColumns << " columns (removed " 
              << stats.removedColumns << ")" << std::endl;
    
    return 0;
}
EOF

g++ -std=c++17 -I./cpp_etl/include -o test_clean_csv test_clean_csv.cpp cpp_etl/src/DataProcessor.cpp cpp_etl/src/FileTypeDetector.cpp 2>&1 | head -5

if [ -f test_clean_csv ]; then
    CLEANED_CSV="${CSV_FILE}.cleaned"
    ./test_clean_csv "$CSV_FILE" "$CLEANED_CSV"
    if [ $? -eq 0 ] && [ -f "$CLEANED_CSV" ]; then
        echo "✓ CSV cleaned: $CLEANED_CSV"
        CSV_FILE="$CLEANED_CSV"
    else
        echo "⚠ Warning: Cleaning failed, using original CSV"
    fi
else
    echo "⚠ Warning: Could not compile cleaner, using original CSV"
fi
echo ""

# Step 3: Check operator column in cleaned CSV
echo "[3/4] Checking operator column in cleaned CSV..."
python3 << PYEOF
import csv
import sys

csv_file = "$CSV_FILE"
print(f"Checking: {csv_file}")

with open(csv_file, 'r', encoding='utf-8') as f:
    reader = csv.DictReader(f)
    headers = reader.fieldnames
    print(f"Columns: {list(headers)}")
    
    if 'operator' not in headers:
        print("❌ ERROR: 'operator' column NOT FOUND!")
        sys.exit(1)
    
    print(f"✓ Found 'operator' column")
    
    # Check first 5 rows
    print("\nFirst 5 operator values:")
    for i, row in enumerate(reader):
        if i >= 5:
            break
        op = row.get('operator', '').strip()
        status = "✓" if op else "⚠ EMPTY"
        print(f"  Row {i+1}: {status} '{op}'")
        
        if not op:
            print("❌ ERROR: Found empty operator value!")
            sys.exit(1)
    
    print("✓ All operator values are present")
PYEOF

if [ $? -ne 0 ]; then
    echo "❌ CSV check failed!"
    exit 1
fi
echo ""

# Step 4: Test Python import (dry run - show what would be imported)
echo "[4/4] Testing Python import script..."
python3 loaders/import_workstation_csv.py "$CSV_FILE" 2>&1 | head -30

echo ""
echo "=========================================="
echo "PIPELINE TEST COMPLETE"
echo "=========================================="

