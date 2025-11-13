[[C++ ETL Project Hub|← Back to Hub]]

  

## Overview

Documentation of columns that cause issues during import and how to handle them.

  

---

  

## outbound_version (workstation_master_log)

  

**Status:** ⛔ REMOVED from database schema

  

### The Problem

  

**What it is:**

- Single-letter field (A, B, C, D, etc.)

- Changes EVERY time a part cycles through a station

- Increments with each pass through the production flow

  

**Why it's problematic:**

- Same serial number can have 100+ entries differing ONLY by this field

- Causes massive duplicate pollution in database

- Same part, same test, same timestamp, different letter = false duplicate

- Makes duplicate detection impossible

- Inflates database size unnecessarily

  

### Example of the Problem

  

```

SN: ABC123, PN: XYZ789, Station: Test1, Time: 10:00:00, outbound_version: A

SN: ABC123, PN: XYZ789, Station: Test1, Time: 10:00:00, outbound_version: B ← Duplicate!

SN: ABC123, PN: XYZ789, Station: Test1, Time: 10:00:00, outbound_version: C ← Duplicate!

SN: ABC123, PN: XYZ789, Station: Test1, Time: 10:00:00, outbound_version: D ← Duplicate!

```

  

All four rows are identical except for `outbound_version`. This creates:

- 4 database entries for 1 actual test

- False duplicate detection failures

- Bloated database (4x larger than needed)

  

### Real-World Impact

  

Checked serial number with 100+ entries:

- All entries were identical except `outbound_version`

- Part failed multiple times, cycled through station 100+ times

- Each cycle incremented the letter

- Database had 100 "unique" entries for what should be 1 entry

  

### The Solution

  

**Database schema:**

- ✅ Removed `outbound_version` column entirely

- ✅ Unique constraint does not include `outbound_version`

  

**Import script:**

- ✅ Skip this column during import (don't even read it)

- ✅ Mark as `skip_import = true` in schema_registry

  

**Schema registry:**

```sql

INSERT INTO schema_registry (file_type, column_name, skip_import, notes)

VALUES ('workstation', 'outbound_version', true,

'Changes every cycle, causes duplicate pollution. Removed from DB schema.');

```

  

### C++ Implementation

  

```cpp

// In CSV preprocessing

auto skip_columns = get_skip_columns("workstation");

// Returns: ["outbound_version", "day", "tat"]

  

// Filter these columns out before deduplication

auto cleaned_csv = remove_columns(csv_path, skip_columns);

  

// Now deduplication works correctly

auto deduped_csv = deduplicate(cleaned_csv);

```

  

---

  

## day (workstation_master_log)

  

**Status:** ⚠️ Metadata column, ignore for deduplication

  

### The Problem

  

**What it is:**

- Metadata column added by reporting system

- Represents the day the report was generated

- Not part of the actual test record

  

**Why it's problematic:**

- Same test can appear in multiple daily reports

- If included in deduplication, creates false duplicates

- Example: Test on 10/26 appears in both 10/26 and 10/27 reports

  

### The Solution

  

**Database schema:**

- ❌ Not imported (column doesn't exist in DB)

  

**Import script:**

- ✅ Ignore for deduplication

- ✅ Mark as `skip_import = true` in schema_registry

  

**Schema registry:**

```sql

INSERT INTO schema_registry (file_type, column_name, skip_import, ignore_for_dedup, notes)

VALUES ('workstation', 'day', true, true,

'Metadata column, not part of actual record. Report generation date.');

```

  

---

  

## tat (workstation_master_log)

  

**Status:** ⚠️ Metadata column, ignore for deduplication

  

### The Problem

  

**What it is:**

- TAT = Turnaround Time

- Calculated field added by reporting system

- Not part of the actual test record

  

**Why it's problematic:**

- Same as `day` - metadata, not actual data

- Can vary between reports for same test

- Including in deduplication creates false duplicates

  

### The Solution

  

**Database schema:**

- ❌ Not imported (column doesn't exist in DB)

  

**Import script:**

- ✅ Ignore for deduplication

- ✅ Mark as `skip_import = true` in schema_registry

  

**Schema registry:**

```sql

INSERT INTO schema_registry (file_type, column_name, skip_import, ignore_for_dedup, notes)

VALUES ('workstation', 'tat', true, true,

'Metadata column, calculated turnaround time. Not part of actual record.');

```

  

---

  

## Summary Table

  

| Column | File Type | Issue | Solution | DB Column Exists? |

| --- | --- | --- | --- | --- |

| `outbound_version` | workstation | Changes every cycle | Skip import | ❌ No |

| `day` | workstation | Report metadata | Skip import | ❌ No |

| `tat` | workstation | Calculated field | Skip import | ❌ No |

  

---

  

## Deduplication Strategy

  

### Columns to INCLUDE in deduplication:

- ✅ `sn` (serial number)

- ✅ `pn` (part number)

- ✅ `model`

- ✅ `workstation_name`

- ✅ `history_station_start_time`

- ✅ `history_station_end_time`

- ✅ `history_station_passing_status`

- ✅ `operator`

- ✅ All other actual data columns

  

### Columns to EXCLUDE from deduplication:

- ❌ `outbound_version` (changes per cycle)

- ❌ `day` (report metadata)

- ❌ `tat` (calculated field)

  

### Implementation

  

**Python (current):**

```python

# Hardcoded exclusion list

dedup_cols = [c for c in df.columns if c not in ['day', 'tat']]

df = df.drop_duplicates(subset=dedup_cols)

```

  

**C++ (proposed):**

```cpp

// Database-driven exclusion

auto dedup_columns = get_dedup_columns("workstation");

// Query: SELECT column_name FROM schema_registry

// WHERE file_type = 'workstation'

// AND skip_import = false

// AND ignore_for_dedup = false

  

auto dedup_key = generate_key(row, dedup_columns);

if (seen_keys.insert(dedup_key).second) {

// Not a duplicate, keep it

}

```

  

---

  

## Lessons Learned

  

### 1. Not all columns are data

Some columns are metadata added by reporting systems. These should be filtered out early.

  

### 2. Incremental fields break deduplication

Fields that change on every cycle (like `outbound_version`) make duplicate detection impossible.

  

### 3. Database schema should match reality

If a column doesn't represent actual test data, don't store it in the database.

  

### 4. Flexibility is key

Use database-driven configuration (schema_registry) instead of hardcoding column lists. Makes it easy to add/remove problematic columns without code changes.

  

---

  

## Future Problematic Columns

  

### How to Handle New Issues

  

When a new problematic column is discovered:

  

1. **Identify the issue:**

- Causes duplicates?

- Metadata vs actual data?

- Changes frequently?

  

2. **Update schema_registry:**

```sql

UPDATE schema_registry

SET skip_import = true,

ignore_for_dedup = true,

notes = 'Reason for exclusion'

WHERE file_type = 'workstation'

AND column_name = 'problematic_column';

```

  

3. **Test with sample data:**

- Import file with the column

- Verify it's skipped

- Verify deduplication works

  

4. **Document here:**

- Add section above

- Explain the problem

- Document the solution

  

5. **No code changes needed!**

- C++ reads from schema_registry

- Automatically applies new rules

  

---

  

## Related Documents

- [[CSV Preprocessing]] - Deduplication implementation

- [[Schema Validation]] - Schema registry details

- [[Import Pipeline Design]] - Full import workflow

  

---

  

## Tags

#data-quality #deduplication #schema #workstation