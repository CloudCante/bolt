#!/bin/bash
# Test duplicate detection by processing the same file twice

XLS_FILE="/home/cloud/projects/new_etl/input/data log/workstationreport/April_2025/workstationOutputReport_04_09_2025_to_04_12_2025.xls"
QUEUE_DIR="/tmp/test_queue"
TEST_DIR="/tmp/test_import"

mkdir -p "$QUEUE_DIR"
mkdir -p "$TEST_DIR"

cd /home/cloud/projects/new_etl/cpp_etl/build

echo "=== First Import (should insert some records) ==="
# Convert and clean
../../cpp_etl/build/foxetl --input-dir "$(dirname "$XLS_FILE")" --queue-dir "$QUEUE_DIR" &
FOXETL_PID=$!
sleep 5
# Copy file to trigger processing
cp "$XLS_FILE" "$TEST_DIR/test1.xls"
sleep 10
kill $FOXETL_PID 2>/dev/null

CSV_FILE=$(find "$QUEUE_DIR" -name "*.csv" | head -1)
if [ -z "$CSV_FILE" ]; then
    echo "Error: No CSV file found"
    exit 1
fi

echo "Testing with: $CSV_FILE"
./test_database_loader "$CSV_FILE"

echo ""
echo "=== Second Import (should be 100% duplicates) ==="
# Process the same file again
cp "$CSV_FILE" "$QUEUE_DIR/test2.csv"
./test_database_loader "$CSV_FILE"

