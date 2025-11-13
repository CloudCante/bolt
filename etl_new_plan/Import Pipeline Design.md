[[C++ ETL Project Hub|← Back to Hub]]

  

## Overview

Six-phase pipeline: Detection → Conversion → Validation → Preprocessing → Import → Cleanup

  

**Goal:** Fast, reliable, self-validating import process with zero silent failures.

  

---

  

## Phase 1: File Detection

**Status:** Design

**Priority:** High

  

### Requirements

- Linux `inotify` for instant file detection (no polling)

- Monitor input directory: `/input/`

- Detect file types by filename pattern:

- `workstationOutputReport.xls` → workstation

- `Test board record report.xls` → testboard

- `snfnReport.xls` → snfn

- Support both `.xls` and `.xlsx` formats

  

### Implementation Notes

```cpp

// Pseudo-code

FileWatcher watches /input/

→ File detected (inotify IN_CLOSE_WRITE event)

→ Determine type from filename

→ Enqueue import job

→ pending_imports++

```

  

### Performance

- Detection latency: <10ms (instant vs 10s polling in Python)

- Zero CPU usage when idle

  

---

  

## Phase 2: File Conversion

**Status:** Design

**Priority:** High

  

### Requirements

- Convert XLS/XLSX to CSV using LibreOffice headless

- CSV format enables fast C++ parsing

- Temporary file cleanup after processing

  

### Conversion Command

```bash

libreoffice --headless --convert-to csv --outdir /tmp file.xls

```

  

### Performance Target

- Conversion time: ~1-2 seconds per file

- CSV parsing: ~0.1-0.5 seconds (1M rows in 180ms proven)

  

### Error Handling

- LibreOffice timeout: 60 seconds

- Conversion failure → quarantine file

- Corrupted file → log error, alert

  

---

  

## Phase 3: Schema Validation

**Status:** Design

**Priority:** Critical

  

See [[Schema Validation]] for full details.

  

### Quick Summary

1. Read CSV headers

2. Compare to `schema_registry` table for file_type

3. Detect missing/extra/reordered columns

4. If mismatch → quarantine + alert

5. If match → proceed to preprocessing

  

### Validation Results

- **EXACT_MATCH:** Proceed with import

- **COMPATIBLE_SUPERSET:** New columns detected, quarantine for approval

- **INCOMPATIBLE:** Missing required columns, HALT import

- **UNKNOWN:** First time seeing this file type, learn schema

  

---

  

## Phase 4: CSV Preprocessing

**Status:** Design

**Priority:** High

  

See [[CSV Preprocessing]] for full details.

  

### Quick Summary

1. Remove columns marked `skip_import = true`

2. Deduplicate rows using columns where `ignore_for_dedup = false`

3. Fast in-memory processing (C++ unordered_set)

4. Output cleaned CSV for database import

  

### Performance Target

- 10k rows: ~100ms (30x faster than Python pandas)

- Memory: Streaming, minimal footprint

- Duplicates: O(1) hash set lookup

  

### Example Output

```

Original rows: 2,847

After dedup: 2,653

Duplicates removed: 194

Columns removed: 3 (outbound_version, day, tat)

```

  

---

  

## Phase 5: Database Import

**Status:** Design

**Priority:** High

  

See [[Database Import]] for full details.

  

### Quick Summary

1. Acquire connection from pool

2. Begin transaction

3. PostgreSQL COPY from cleaned CSV

4. Handle unique constraint violations (log as duplicates)

5. Commit transaction

6. Release connection back to pool

  

### Performance Target

- 10k rows: ~1-2 seconds

- Uses PostgreSQL COPY (fastest bulk insert)

- Connection pooling (no connect/disconnect overhead)

  

---

  

## Phase 6: Cleanup

**Status:** Design

**Priority:** Medium

  

### Requirements

- Delete original XLS/XLSX file after successful import

- Delete temporary CSV files

- Delete cleaned CSV files

- Preserve quarantined files for review

- Log all file operations

  

### Cleanup Flow

```

Import successful?

→ Delete original file

→ Delete temp CSV

→ Delete cleaned CSV

→ pending_imports--

  

Import failed?

→ Move to quarantine/

→ Preserve all files for debugging

→ pending_imports--

→ Alert user

```

  

---

  

## Complete Pipeline Flow

  

```

File arrives

↓

[Phase 1] Detect file type (inotify, instant)

↓

[Phase 2] Convert XLS → CSV (LibreOffice, ~1-2s)

↓

[Phase 3] Validate schema (DB lookup, ~10ms)

↓ (if valid)

[Phase 4] Preprocess CSV (remove columns, dedup, ~100ms)

↓

[Phase 5] Import to PostgreSQL (COPY, ~1-2s)

↓

[Phase 6] Cleanup files (delete temps, ~10ms)

↓

Queue empty? → Trigger aggregation

  

Total time: ~3-5 seconds (vs Python: ~10-20 seconds)

```

  

---

  

## Error Handling Strategy

  

### Recoverable Errors

- Duplicate records → Log, skip, continue

- Empty CSV → Log warning, skip file

- Timeout during conversion → Retry once, then quarantine

  

### Non-Recoverable Errors

- Schema mismatch → Quarantine, alert, HALT

- Database connection failure → Retry with backoff, alert if persistent

- Corrupted file → Quarantine, alert

  

### Alert Levels

- **INFO:** Duplicates skipped, normal operation

- **WARNING:** New columns detected, needs review

- **ERROR:** Import failed, file quarantined

- **CRITICAL:** Database down, schema incompatible

  

---

  

## Future Enhancements

  

### Ideas to Add Later

- [ ] Parallel file processing (multiple files at once)

- [ ] Incremental imports (track last processed timestamp)

- [ ] Import resume on failure (checkpoint system)

- [ ] Binary COPY format (faster than text CSV)

- [ ] Compression support (gzip CSV files)

- [ ] S3/cloud storage support (not just local files)

- [ ] Real-time import progress bar

- [ ] Import scheduling (process files at specific times)

  

---

  

## Related Documents

- [[Schema Validation]] - Schema drift detection details

- [[CSV Preprocessing]] - Deduplication and column filtering

- [[Database Import]] - PostgreSQL COPY implementation

- [[Problematic Columns]] - outbound_version and dedup rules

  

---

  

## Tags

#import-pipeline #etl #design #cpp