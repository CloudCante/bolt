# Performance Issue: N+1 Query Problem

## Current Problem

The `importCSV()` function checks for duplicates by calling `recordExists()` **once per CSV row**. 

For a file with **18,636 rows**, this means:
- **18,636 individual database queries**
- Each query scans the entire table (unless indexed)
- Each query is a separate network round-trip
- **Estimated time: Hours** (depending on table size and indexes)

## Current Code Flow

```
importCSV()
  ├─ Parse CSV (18,636 rows)
  ├─ For each row:
  │   ├─ mapRowToDatabase()
  │   └─ recordExists()  ← ONE QUERY PER ROW!
  │       └─ SELECT COUNT(*) FROM table WHERE ... (13-17 conditions)
  └─ Insert new records
```

## Why It's Slow

1. **Network Latency**: Each query = 1 network round-trip
   - 18,636 queries × 1ms latency = 18.6 seconds just in network overhead
   - But queries take much longer (table scans)

2. **Table Scans**: Each query scans the entire table
   - Without proper indexes, each query is O(n) where n = table size
   - If table has 1 million rows, that's 18,636 × 1,000,000 = 18.6 billion row comparisons!

3. **No Batching**: All queries are sequential, no parallelization

## Solution: Batch Duplicate Checking

Instead of checking one-by-one, we should:

### Option 1: Temporary Table + JOIN (Best Performance)

```sql
-- Create temporary table with all CSV records
CREATE TEMP TABLE csv_records AS (
  VALUES 
    ($1, $2, $3, ...),  -- row 1
    ($14, $15, $16, ...), -- row 2
    ...
);

-- Single query to find all existing records
SELECT csv.* FROM csv_records csv
INNER JOIN workstation_master_log db
  ON csv.sn IS NOT DISTINCT FROM db.sn
  AND csv.pn IS NOT DISTINCT FROM db.pn
  ... (all 13 conditions)
```

**Benefits:**
- 1-2 queries total instead of 18,636
- Database can optimize the JOIN
- Much faster (seconds instead of hours)

### Option 2: Batch Check in Chunks

Check 1000 records at a time using a single query with multiple OR conditions or VALUES clause.

### Option 3: Use Database UNIQUE Constraint

If the database has a UNIQUE constraint, we can:
1. Try to insert all records
2. Catch duplicate key errors
3. Retry only the failed ones

**But**: This requires proper error handling and might be slower if many duplicates exist.

## Recommended Approach

**Use Option 1 (Temporary Table + JOIN)** because:
- Single optimized query
- Database handles the matching efficiently
- Works well with indexes
- Scales to any number of rows

## Implementation Plan

1. Create a new method: `checkDuplicatesBatch()`
2. Build a temporary table with all CSV records
3. Execute a single JOIN query to find existing records
4. Map results back to CSV rows
5. Replace the loop in `importCSV()` with batch check

## Performance Comparison

| Method | Queries | Estimated Time (18,636 rows) |
|--------|---------|------------------------------|
| Current (one-by-one) | 18,636 | **Hours** |
| Batch (temp table) | 2 | **Seconds** |

## Next Steps

1. Implement `checkDuplicatesBatch()` method
2. Replace the loop in `importCSV()` 
3. Test with your 18,636 row file
4. Should complete in seconds instead of hours!

