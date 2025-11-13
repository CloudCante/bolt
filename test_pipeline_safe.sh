#!/bin/bash
# Safe pipeline test - only processes first 10 rows

set -e

echo "=========================================="
echo "SAFE PIPELINE TEST (10 rows only)"
echo "=========================================="
echo ""

TEST_FILE="test_import/workstationOutputReport_04_09_2025_to_04_12_2025.xls"
QUEUE_DIR="test_import/queue"
CLEANED_CSV=""

# Step 1: Convert XLS to CSV
echo "[1/4] Converting XLS to CSV..."
mkdir -p "$QUEUE_DIR"
libreoffice --headless --convert-to "csv:Text - txt - csv (StarCalc):44,34,UTF8" --outdir "$QUEUE_DIR" "$TEST_FILE" 2>&1 | tail -1
sleep 2

CSV_FILE=$(find "$QUEUE_DIR" -name "*.csv" -not -name "*.cleaned" | head -1)
if [ -z "$CSV_FILE" ]; then
    echo "❌ ERROR: CSV conversion failed!"
    exit 1
fi
echo "✓ Converted to: $CSV_FILE"
echo ""

# Step 2: Create small test CSV (first 11 lines = header + 10 rows)
echo "[2/4] Creating small test CSV (10 rows)..."
SMALL_CSV="${CSV_FILE}.small"
head -11 "$CSV_FILE" > "$SMALL_CSV"
echo "✓ Created: $SMALL_CSV ($(wc -l < "$SMALL_CSV") lines)"
CSV_FILE="$SMALL_CSV"
echo ""

# Step 3: Clean CSV using C++
echo "[3/4] Cleaning CSV columns (C++)..."
cd cpp_etl
./build.sh > /dev/null 2>&1
cd ..

# Compile cleaner if needed
if [ ! -f test_clean_csv ]; then
    g++ -std=c++17 -I./cpp_etl/include -o test_clean_csv test_clean_csv.cpp cpp_etl/src/DataProcessor.cpp cpp_etl/src/FileTypeDetector.cpp 2>&1 | head -5
fi

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

# Step 4: Check operator column
echo "[4/4] Checking operator column in cleaned CSV..."
python3 << PYEOF
import csv
import sys

csv_file = "$CSV_FILE"
print(f"Checking: {csv_file}")

with open(csv_file, 'r', encoding='utf-8') as f:
    reader = csv.DictReader(f)
    headers = reader.fieldnames
    print(f"Columns ({len(headers)}): {list(headers)}")
    
    if 'operator' not in headers:
        print("❌ ERROR: 'operator' column NOT FOUND!")
        sys.exit(1)
    
    print(f"✓ Found 'operator' column at index {list(headers).index('operator')}")
    
    # Check all rows
    row_count = 0
    empty_count = 0
    print("\nOperator values:")
    for row in reader:
        row_count += 1
        op = row.get('operator', '').strip()
        if op:
            print(f"  Row {row_count}: ✓ '{op}'")
        else:
            empty_count += 1
            print(f"  Row {row_count}: ⚠ EMPTY!")
    
    print(f"\nSummary: {row_count} rows, {empty_count} empty operator values")
    if empty_count > 0:
        print("❌ ERROR: Found empty operator values!")
        sys.exit(1)
    else:
        print("✓ All operator values are present!")
PYEOF

if [ $? -ne 0 ]; then
    echo "❌ CSV check failed!"
    exit 1
fi
echo ""

# Step 5: Test Python import (with small file)
echo "[5/5] Testing Python import (10 rows only)..."
echo "This will import to database - press Ctrl+C to cancel if needed"
sleep 2

python3 loaders/import_workstation_csv.py "$CSV_FILE" 2>&1

echo ""
echo "=========================================="
echo "PIPELINE TEST COMPLETE"
echo "=========================================="

