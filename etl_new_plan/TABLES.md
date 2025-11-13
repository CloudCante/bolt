# Fox ETL - Database Schema Reference

  

## Overview

Three master log tables store raw data from production testing. All tables use `SERIAL PRIMARY KEY` for auto-incrementing IDs and have unique constraints to prevent duplicates.

  

---

  

## Master Log Tables

  

### testboard_master_log

**Purpose:** Stores GPU test board records from testing stations

  

#### Schema

| Column | Type | Constraints | Notes |

|--------|------|-------------|-------|

| `id` | SERIAL | PRIMARY KEY | Auto-increment |

| `sn` | VARCHAR(255) | NOT NULL | Serial number |

| `pn` | VARCHAR(255) | | Part number |

| `model` | VARCHAR(255) | | GPU model |

| `work_station_process` | VARCHAR(255) | | Process type |

| `baseboard_sn` | VARCHAR(255) | | Baseboard serial |

| `baseboard_pn` | VARCHAR(255) | | Baseboard part number |

| `workstation_name` | VARCHAR(255) | NOT NULL | Station identifier |

| `history_station_start_time` | TIMESTAMP | NOT NULL | Test start |

| `history_station_end_time` | TIMESTAMP | NOT NULL | Test end |

| `history_station_passing_status` | VARCHAR(255) | | Pass/Fail |

| `operator` | VARCHAR(255) | | Operator name |

| `failure_reasons` | TEXT | | Failure description |

| `failure_note` | TEXT | | Additional notes |

| `failure_code` | VARCHAR(255) | | Error code |

| `diag_version` | VARCHAR(255) | | Diagnostic version |

| `fixture_no` | VARCHAR(255) | | Test fixture ID |

| `data_source` | VARCHAR(50) | NOT NULL | Always "testboard" |

| `created_at` | TIMESTAMP | DEFAULT NOW() | Record creation |

| `updated_at` | TIMESTAMP | DEFAULT NOW() | Last update |

  

#### Unique Constraint

```sql

UNIQUE (sn, pn, model, work_station_process, baseboard_sn, baseboard_pn,

workstation_name, history_station_start_time, history_station_end_time,

history_station_passing_status, operator, failure_reasons, failure_note,

failure_code, diag_version, fixture_no, data_source)

```

  

#### Indexes

- `idx_testboard_end_time` on `history_station_end_time`

- `idx_testboard_fixture` on `fixture_no`

  

---

  

### workstation_master_log

**Purpose:** Stores workstation output reports for production flow

  

#### Schema

| Column | Type | Constraints | Notes |

|--------|------|-------------|-------|

| `id` | SERIAL | PRIMARY KEY | Auto-increment |

| `sn` | VARCHAR(255) | NOT NULL | Serial number |

| `pn` | VARCHAR(255) | | Part number |

| `model` | VARCHAR(255) | | GPU model |

| `workstation_name` | VARCHAR(255) | NOT NULL | Station name |

| `history_station_start_time` | TIMESTAMP | NOT NULL | Process start |

| `history_station_end_time` | TIMESTAMP | NOT NULL | Process end |

| `history_station_passing_status` | VARCHAR(255) | | Pass/Fail |

| `operator` | VARCHAR(255) | | Operator name |

| `customer_pn` | VARCHAR(255) | | Customer part number |

| `outbound_version` | VARCHAR(255) | | Version info |

| `hours` | VARCHAR(255) | | Hours logged |

| `service_flow` | VARCHAR(255) | | Service flow type |

| `passing_station_method` | VARCHAR(255) | | Method used |

| `first_station_start_time` | TIMESTAMP | | First station time |

| `data_source` | VARCHAR(50) | NOT NULL | Always "workstation" |

| `created_at` | TIMESTAMP | DEFAULT NOW() | Record creation |

| `updated_at` | TIMESTAMP | DEFAULT NOW() | Last update |

  

#### Unique Constraint

```sql

UNIQUE (sn, pn, customer_pn, outbound_version, workstation_name,

history_station_start_time, history_station_end_time, hours,

service_flow, model, history_station_passing_status,

passing_station_method, operator, first_station_start_time, data_source)

```

  

#### Indexes

- `idx_workstation_end_time` on `history_station_end_time`

  

---

  

### snfn_master_log

**Purpose:** Stores serial number/failure note reports

  

#### Schema

| Column | Type | Constraints | Notes |

|--------|------|-------------|-------|

| `id` | SERIAL | PRIMARY KEY | Auto-increment |

| `workstation_name` | VARCHAR(255) | NOT NULL | Station name |

| `fixture_no` | VARCHAR(255) | | Fixture identifier |

| `error_code` | VARCHAR(255) | | Error code |

| `error_disc` | TEXT | | Error description |

| `sn` | VARCHAR(255) | NOT NULL | Serial number |

| `pn` | VARCHAR(255) | | Part number |

| `history_station_start_time` | TIMESTAMP | NOT NULL | Event start |

| `history_station_end_time` | TIMESTAMP | NOT NULL | Event end |

| `data_source` | VARCHAR(50) | NOT NULL | Always "snfn" |

| `created_at` | TIMESTAMP | DEFAULT NOW() | Record creation |

| `updated_at` | TIMESTAMP | DEFAULT NOW() | Last update |

  

#### Unique Constraint

```sql

UNIQUE (workstation_name, fixture_no, error_code, error_disc,

sn, pn, history_station_start_time, history_station_end_time)

```

  

#### Indexes

- `idx_snfn_end_time` on `history_station_end_time`

  

---

  

## Data Validation Rules

  

### Common Rules (All Tables)

- ✅ Empty strings (`""`) → `NULL`

- ✅ Whitespace-only strings (`" "`) → `NULL`

- ✅ Invalid timestamps → `NULL` or skip row

- ✅ Column names: lowercase, underscores, alphanumeric only

  

### Testboard Validation

**Required Fields:**

- `sn` - must be non-empty

- `workstation_name` - must be non-empty

- `history_station_start_time` - valid timestamp

- `history_station_end_time` - valid timestamp

- `data_source` - hardcoded to `"testboard"`

  

**Optional but cleaned:**

- All other VARCHAR fields: trim whitespace, empty → NULL

- TEXT fields: empty → NULL

  

### Workstation Validation

**Required Fields:**

- `sn` - must be non-empty

- `workstation_name` - must be non-empty

- `history_station_start_time` - valid timestamp

- `history_station_end_time` - valid timestamp

- `data_source` - hardcoded to `"workstation"`

  

**Optional but cleaned:**

- All other VARCHAR fields: trim whitespace, empty → NULL

  

### SNFN Validation

**Required Fields:**

- `workstation_name` - must be non-empty

- `sn` - must be non-empty

- `history_station_start_time` - valid timestamp

- `history_station_end_time` - valid timestamp

- `data_source` - hardcoded to `"snfn"`

  

**Optional but cleaned:**

- All other VARCHAR fields: trim whitespace, empty → NULL

- `error_disc` TEXT: empty → NULL

  

---

  

## Complete SQL Schema

  

```sql

-- ============================================

-- FOX ETL - Master Log Tables

-- ============================================

  

-- Testboard Master Log

CREATE TABLE IF NOT EXISTS testboard_master_log (

id SERIAL PRIMARY KEY,

sn VARCHAR(255) NOT NULL,

pn VARCHAR(255),

model VARCHAR(255),

work_station_process VARCHAR(255),

baseboard_sn VARCHAR(255),

baseboard_pn VARCHAR(255),

workstation_name VARCHAR(255) NOT NULL,

history_station_start_time TIMESTAMP NOT NULL,

history_station_end_time TIMESTAMP NOT NULL,

history_station_passing_status VARCHAR(255),

operator VARCHAR(255),

failure_reasons TEXT,

failure_note TEXT,

failure_code VARCHAR(255),

diag_version VARCHAR(255),

fixture_no VARCHAR(255),

data_source VARCHAR(50) NOT NULL,

created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

CONSTRAINT testboard_unique_constraint UNIQUE (

sn, pn, model, work_station_process, baseboard_sn, baseboard_pn,

workstation_name, history_station_start_time, history_station_end_time,

history_station_passing_status, operator, failure_reasons, failure_note,

failure_code, diag_version, fixture_no, data_source

)

);

  

-- Workstation Master Log

CREATE TABLE IF NOT EXISTS workstation_master_log (

id SERIAL PRIMARY KEY,

sn VARCHAR(255) NOT NULL,

pn VARCHAR(255),

model VARCHAR(255),

workstation_name VARCHAR(255) NOT NULL,

history_station_start_time TIMESTAMP NOT NULL,

history_station_end_time TIMESTAMP NOT NULL,

history_station_passing_status VARCHAR(255),

operator VARCHAR(255),

customer_pn VARCHAR(255),

outbound_version VARCHAR(255),

hours VARCHAR(255),

service_flow VARCHAR(255),

passing_station_method VARCHAR(255),

first_station_start_time TIMESTAMP,

data_source VARCHAR(50) NOT NULL,

created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

CONSTRAINT workstation_unique_constraint UNIQUE (

sn, pn, customer_pn, outbound_version, workstation_name,

history_station_start_time, history_station_end_time, hours,

service_flow, model, history_station_passing_status,

passing_station_method, operator, first_station_start_time, data_source

)

);

  

-- SNFN Master Log

CREATE TABLE IF NOT EXISTS snfn_master_log (

id SERIAL PRIMARY KEY,

workstation_name VARCHAR(255) NOT NULL,

fixture_no VARCHAR(255),

error_code VARCHAR(255),

error_disc TEXT,

sn VARCHAR(255) NOT NULL,

pn VARCHAR(255),

history_station_start_time TIMESTAMP NOT NULL,

history_station_end_time TIMESTAMP NOT NULL,

data_source VARCHAR(50) NOT NULL,

created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,

CONSTRAINT snfn_unique_constraint UNIQUE (

workstation_name, fixture_no, error_code, error_disc,

sn, pn, history_station_start_time, history_station_end_time

)

);

  

-- Indexes for common queries

CREATE INDEX IF NOT EXISTS idx_testboard_end_time

ON testboard_master_log(history_station_end_time);

CREATE INDEX IF NOT EXISTS idx_testboard_fixture

ON testboard_master_log(fixture_no);

CREATE INDEX IF NOT EXISTS idx_workstation_end_time

ON workstation_master_log(history_station_end_time);

CREATE INDEX IF NOT EXISTS idx_snfn_end_time

ON snfn_master_log(history_station_end_time);

```

  

---

  

## Notes

  

### Critical Lessons Learned

> [!WARNING] SERIAL PRIMARY KEY

> Always use `SERIAL PRIMARY KEY` or manually set the sequence after bulk imports. Forgot this once and IDs didn't auto-increment. Had to go back to these scripts to remember the pattern.

  

### Deduplication Strategy

The unique constraints handle duplicates automatically via `ON CONFLICT DO NOTHING`. This is intentional - production testing generates duplicate records naturally.

  

### Reference Scripts

Original Python bulk upload scripts located at:

- `upload_testboard_master_log.py`

- `upload_workstation_master_log.py`

- `upload_snfn_master_log.py`

  

These are kept for disaster recovery and reference but not used in daily operations.

  

---

  

## Tags

#database #schema #etl #postgresql #fox-production

### workstation_master_log  | Table Schema
Purpose: Stores workstation output reports for production flow

| Column                         | Type         | Constraints   | Notes                |     |
| ------------------------------ | ------------ | ------------- | -------------------- | --- |
| id                             | SERIAL       | PRIMARY KEY   | Auto-increment       |     |
| sn                             | VARCHAR(255) | NOT NULL      | Serial number        |     |
| pn                             | VARCHAR(255) |               | Part number          |     |
| model                          | VARCHAR(255) |               | GPU model            |     |
| workstation_name               | VARCHAR(255) | NOT NULL      | Station name         |     |
| history_station_start_time     | TIMESTAMP    | NOT NULL      | Process start        |     |
| history_station_end_time       | TIMESTAMP    | NOT NULL      | Process end          |     |
| history_station_passing_status | VARCHAR(255) |               | Pass/Fail            |     |
| operator                       | VARCHAR(255) |               | Operator name        |     |
| customer_pn                    | VARCHAR(255) |               | Customer part number |     |
| outbound_version               | VARCHAR(255) |               | Version info         |     |
| hours                          | VARCHAR(255) |               | Hours logged         |     |
| service_flow                   | VARCHAR(255) |               | Service flow type    |     |
| passing_station_method         | VARCHAR(255) |               | Method used          |     |
| first_station_start_time       | TIMESTAMP    |               | First station time   |     |
| data_source                    | VARCHAR(50)  | NOT NULL      | Always "workstation" |     |
| created_at                     | TIMESTAMP    | DEFAULT NOW() | Record creation      |     |
| updated_at                     | TIMESTAMP    | DEFAULT NOW() | Last update          |     |

# testboard_master_log | Table Schema

  

**Purpose:** Stores GPU test board records from testing stations

| Column | Type | Constraints | Notes |
| --- | --- | --- | --- |
| id | SERIAL | PRIMARY KEY | Auto-increment |
| sn | VARCHAR(255) | NOT NULL | Serial number |
| pn | VARCHAR(255) | | Part number |
| model | VARCHAR(255) | | GPU model |
| work_station_process | VARCHAR(255) | | Process type |
| baseboard_sn | VARCHAR(255) | | Baseboard serial |
| baseboard_pn | VARCHAR(255) | | Baseboard part number |
| workstation_name | VARCHAR(255) | NOT NULL | Station identifier |
| history_station_start_time | TIMESTAMP | NOT NULL | Test start |
| history_station_end_time | TIMESTAMP | NOT NULL | Test end |
| history_station_passing_status | VARCHAR(255) | | Pass/Fail |
| operator | VARCHAR(255) | | Operator name |
| failure_reasons | TEXT | | Failure description |
| failure_note | TEXT | | Additional notes |
| failure_code | VARCHAR(255) | | Error code |
| diag_version | VARCHAR(255) | | Diagnostic version |
| fixture_no | VARCHAR(255) | | Test fixture ID |
| data_source | VARCHAR(50) | NOT NULL | Always "testboard" |
| created_at | TIMESTAMP | DEFAULT NOW() | Record creation |
| updated_at | TIMESTAMP | DEFAULT NOW() | Last update |


### snfn_master_log

Purpose: Stores serial number/failure note reports

#### Schema

|Column|Type|Constraints|Notes|
|---|---|---|---|
|id|SERIAL|PRIMARY KEY|Auto-increment|
|workstation_name|VARCHAR(255)|NOT NULL|Station name|
|fixture_no|VARCHAR(255)||Fixture identifier|
|error_code|VARCHAR(255)||Error code|
|error_disc|TEXT||Error description|
|sn|VARCHAR(255)|NOT NULL|Serial number|
|pn|VARCHAR(255)||Part number|
|history_station_start_time|TIMESTAMP|NOT NULL|Event start|
|history_station_end_time|TIMESTAMP|NOT NULL|Event end|
|data_source|VARCHAR(50)|NOT NULL|Always "snfn"|
|created_at|TIMESTAMP|DEFAULT NOW()|Record creation|
|updated_at|TIMESTAMP|DEFAULT NOW()|Last update|