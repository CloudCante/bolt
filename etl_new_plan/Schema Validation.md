[[C++ ETL Project Hub|← Back to Hub]]

  

## Overview

Database-driven schema registry with automatic drift detection. Prevents silent failures when report formats change.

  

**Goal:** Zero silent failures. Always know when schemas change.

  

---

  

## The Problem

  

Reports from wareconn.com can change at any time:

- ✅ New columns added

- ✅ Columns removed

- ✅ Column names changed

- ✅ Data types changed

- ✅ Column order changed

  

Without validation:

- ❌ Crashes if required column missing

- ❌ Silently ignores new columns (data loss)

- ❌ Imports garbage if column renamed

- ❌ Creates duplicates if dedup columns change

  

---

  

## Database Schema

  

### schema_registry Table

```sql

CREATE TABLE schema_registry (

id SERIAL PRIMARY KEY,

file_type VARCHAR(50) NOT NULL, -- 'testboard', 'workstation', 'snfn'

column_name VARCHAR(255) NOT NULL,

column_order INTEGER NOT NULL,

is_required BOOLEAN DEFAULT false, -- Must be present in file

skip_import BOOLEAN DEFAULT false, -- Don't import this column

ignore_for_dedup BOOLEAN DEFAULT false, -- Don't use for duplicate detection

data_type VARCHAR(50), -- 'string', 'timestamp', 'integer'

added_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

added_by VARCHAR(100), -- Who approved this column

notes TEXT,

UNIQUE(file_type, column_name)

);

```

  

### schema_change_log Table

```sql

CREATE TABLE schema_change_log (

id SERIAL PRIMARY KEY,

file_type VARCHAR(50) NOT NULL,

change_type VARCHAR(50) NOT NULL, -- 'column_added', 'column_removed', 'order_changed'

column_name VARCHAR(255),

old_value TEXT,

new_value TEXT,

detected_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

approved_at TIMESTAMP,

approved_by VARCHAR(100),

status VARCHAR(50) DEFAULT 'pending', -- 'pending', 'approved', 'rejected'

notes TEXT

);

```

  

### quarantined_files Table

```sql

CREATE TABLE quarantined_files (

id SERIAL PRIMARY KEY,

file_type VARCHAR(50) NOT NULL,

original_filename VARCHAR(255) NOT NULL,

quarantine_path VARCHAR(500) NOT NULL,

reason TEXT NOT NULL,

detected_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

reviewed_at TIMESTAMP,

reviewed_by VARCHAR(100),

status VARCHAR(50) DEFAULT 'pending', -- 'pending', 'approved', 'rejected'

action_taken TEXT

);

```

  

---

  

## Validation Flow

  

### Step 1: Read CSV Headers

```cpp

std::vector<std::string> csv_headers = read_csv_headers(csv_path);

// Example: ["sn", "pn", "model", "workstation_name", ...]

```

  

### Step 2: Get Expected Schema from DB

```cpp

std::vector<std::string> expected_schema = get_schema_from_db(file_type);

// Query: SELECT column_name FROM schema_registry

// WHERE file_type = 'testboard'

// ORDER BY column_order

```

  

### Step 3: Compare

```cpp

auto missing_columns = find_missing(expected_schema, csv_headers);

auto extra_columns = find_extra(expected_schema, csv_headers);

```

  

### Step 4: Determine Action

  

| Scenario | Action | Reason |

| --- | --- | --- |

| Exact match | ✅ Proceed | Schema unchanged |

| Extra columns | ⚠️ Quarantine | New columns need approval |

| Missing required columns | 🛑 HALT | Data loss would occur |

| Missing optional columns | ⚠️ Log warning, proceed | Acceptable |

  

---

  

## Validation Results

  

### EXACT_MATCH

```

✅ Schema validated

Proceeding with import

```

  

### COMPATIBLE_SUPERSET (New columns)

```

⚠️ NEW COLUMNS DETECTED in testboard

File: test_board_record_report_10_27_2025.xls

New columns: new_error_field, test_duration_ms

Action: File quarantined for review

Quarantine ID: 42

Review with: [q] command in control interface

```

  

### INCOMPATIBLE (Missing columns)

```

🛑 SCHEMA INCOMPATIBLE - IMPORT HALTED!

File: test_board_record_report_10_27_2025.xls

File type: testboard

Missing required columns:

- fixture_no

- operator

Action: File moved to quarantine

Quarantine ID: 43

All future testboard imports PAUSED until resolved.

```

  

### UNKNOWN (First time)

```

ℹ️ First time seeing testboard schema, learning...

Columns detected: sn, pn, model, workstation_name, ...

Schema saved to database.

Proceeding with import.

```

  

---

  

## Interactive Approval Workflow

  

When a file is quarantined, user can review:

  

```

╔════════════════════════════════════════╗

║ 🛑 SCHEMA MISMATCH DETECTED 🛑 ║

╚════════════════════════════════════════╝

  

File: test_board_record_report_10_27_2025.xls

Type: testboard

Detected: 2025-10-27 16:45:30

  

New columns detected:

+ test_duration_ms

+ temperature_reading

  

Actions:

[a] Approve and add new columns to schema

[i] Import anyway (ignore schema changes)

[r] Reject and delete file

[v] View file contents (first 10 rows)

[q] Keep in quarantine (review later)

  

Choice: _

```

  

### If user presses 'a' (Approve):

```sql

-- Add new columns to schema_registry

INSERT INTO schema_registry (file_type, column_name, column_order, is_required, added_by, notes)

VALUES ('testboard', 'test_duration_ms', 17, false, 'admin', 'Approved from quarantine'),

('testboard', 'temperature_reading', 18, false, 'admin', 'Approved from quarantine');

  

-- Update change log

UPDATE schema_change_log

SET status = 'approved', approved_at = NOW(), approved_by = 'admin'

WHERE file_type = 'testboard' AND status = 'pending';

  

-- Update quarantine record

UPDATE quarantined_files

SET status = 'approved', reviewed_at = NOW(), reviewed_by = 'admin',

action_taken = 'Columns added to schema, file imported'

WHERE id = 42;

```

  

Then proceed with import.

  

---

  

## Schema Registry Population

  

### Initial Setup (One-time)

```sql

-- Populate with current known schemas

INSERT INTO schema_registry (file_type, column_name, column_order, is_required, data_type, skip_import, ignore_for_dedup) VALUES

-- Testboard

('testboard', 'sn', 1, true, 'string', false, false),

('testboard', 'pn', 2, false, 'string', false, false),

('testboard', 'model', 3, false, 'string', false, false),

('testboard', 'work_station_process', 4, false, 'string', false, false),

('testboard', 'baseboard_sn', 5, false, 'string', false, false),

('testboard', 'baseboard_pn', 6, false, 'string', false, false),

('testboard', 'workstation_name', 7, true, 'string', false, false),

('testboard', 'history_station_start_time', 8, true, 'timestamp', false, false),

('testboard', 'history_station_end_time', 9, true, 'timestamp', false, false),

('testboard', 'history_station_passing_status', 10, false, 'string', false, false),

('testboard', 'operator', 11, false, 'string', false, false),

('testboard', 'failure_reasons', 12, false, 'string', false, false),

('testboard', 'failure_note', 13, false, 'string', false, false),

('testboard', 'failure_code', 14, false, 'string', false, false),

('testboard', 'diag_version', 15, false, 'string', false, false),

('testboard', 'fixture_no', 16, false, 'string', false, false);

  

-- Workstation (similar pattern)

-- SNFN (similar pattern)

```

  

---

  

## C++ Implementation Pseudocode

  

```cpp

class SchemaValidator {

public:

enum class ValidationResult {

EXACT_MATCH,

COMPATIBLE_SUPERSET,

INCOMPATIBLE,

UNKNOWN

};

ValidationResult validate_csv(const std::string& file_type,

const std::string& csv_path) {

auto csv_headers = read_csv_headers(csv_path);

if (!known_schemas.contains(file_type)) {

learn_schema(file_type, csv_headers);

return ValidationResult::UNKNOWN;

}

auto expected = known_schemas[file_type];

auto missing = find_missing(expected, csv_headers);

auto extra = find_extra(expected, csv_headers);

if (missing.empty() && extra.empty()) {

return ValidationResult::EXACT_MATCH;

}

if (!missing.empty()) {

log_schema_mismatch(file_type, missing, extra);

quarantine_file(csv_path, "Missing columns: " + join(missing));

return ValidationResult::INCOMPATIBLE;

}

if (!extra.empty()) {

log_schema_mismatch(file_type, missing, extra);

quarantine_file(csv_path, "New columns: " + join(extra));

return ValidationResult::COMPATIBLE_SUPERSET;

}

}

};

```

  

---

  

## Benefits

  

✅ **Zero silent failures** - Never import bad data

✅ **Self-documenting** - Schema history tracked automatically

✅ **Proactive alerts** - Know immediately when reports change

✅ **Safe evolution** - Can add new columns without breaking

✅ **Prevents duplicates** - Catches renamed columns that would cause dupes

✅ **Audit trail** - Every schema change logged with timestamp and approver

  

---

  

## Future Enhancements

  

### Ideas to Add Later

- [ ] Fuzzy column name matching (detect "Serial Number" → "SN" rename)

- [ ] Auto-suggest schema migrations (SQL ALTER TABLE commands)

- [ ] Predict schema changes based on historical patterns

- [ ] Email/Slack alerts for schema drift

- [ ] Web UI for schema approval (not just terminal)

- [ ] Schema versioning (track multiple versions over time)

- [ ] Rollback capability (revert to previous schema)

  

---

  

## Related Documents

- [[Import Pipeline Design]] - Full import workflow

- [[CSV Preprocessing]] - Column filtering and deduplication

- [[Problematic Columns]] - outbound_version case study

  

---

  

## Tags

#schema-validation #etl #database #design