# C++ ETL Orchestrator - Progress Report

## Session Summary
Built the foundation for a C++-based ETL orchestrator that will replace 21+ Python scripts with an efficient, event-driven system.

---

## ✅ Completed Today

### 1. FileWatcher
**Status:** ✅ Complete and Tested

**What it does:**
- Monitors directory for files using Linux `inotify`
- Instant file detection (no polling, 0% CPU when idle)
- Thread-safe background monitoring
- Detects files when they're fully written

**Files:**
- `include/FileWatcher.h`
- `src/FileWatcher.cpp`

**Key Features:**
- Event-driven file detection
- Pattern matching for specific files
- Graceful shutdown handling
- Low resource usage (~1MB)

---

### 2. FileTypeDetector
**Status:** ✅ Complete and Tested

**What it does:**
- Identifies file type from filename patterns
- Handles variations: `workstationOutputReport (2).xls`, dates, case-insensitive
- Returns: `WORKSTATION`, `TESTBOARD`, `SNFN`, or `UNKNOWN`

**Files:**
- `include/FileTypeDetector.h`
- `src/FileTypeDetector.cpp`

**Tested Patterns:**
- ✓ Basic filenames
- ✓ Duplicates: `file (2).xls`
- ✓ Case variations: `WORKSTATIONOUTPUTREPORT.xls`
- ✓ With dates: `workstationOutputReport 08181995.xls`
- ✓ Testboard variations (with/without spaces)

---

### 3. Project Structure
**Status:** ✅ Established

**Structure:**
```
cpp_etl/
├── CMakeLists.txt          # Build configuration
├── build.sh                # Quick build script
├── include/                # Header files
│   ├── FileWatcher.h
│   └── FileTypeDetector.h
├── src/                    # Implementation files
│   ├── main.cpp
│   ├── FileWatcher.cpp
│   └── FileTypeDetector.cpp
└── build/                  # Compiled binary
```

---

## ✅ Completed Today (Session 2 & 3)

### 3. Job Struct
**Status:** ✅ Complete

**What it does:**
- Data container for file processing jobs
- Tracks filepath, type, status, retry count, timestamps
- Just data, no methods (LoL: Item/Stat Card)

**Files:**
- `include/Job.h`

**Key Features:**
- JobStatus enum (PENDING, PROCESSING, COMPLETED, FAILED, RETRYING)
- Automatic timestamp on creation
- Configurable max retries (default: 2)

---

### 4. JobQueue
**Status:** ✅ Complete and Integrated

**What it does:**
- Thread-safe queue for file processing jobs
- Prevents duplicate processing
- Manages pending and retry queues
- Tracks active/completed/failed jobs with statistics

**Files:**
- `include/JobQueue.h`
- `src/JobQueue.cpp`

**Key Features:**
- Thread-safe operations (mutex + condition_variable)
- Duplicate prevention (normalized path checking)
- Separate retry queue (priority over new jobs)
- Statistics tracking (pending, active, completed, failed, retried)
- Integrated with FileWatcher + FileTypeDetector in main.cpp

**Integration:**
- FileWatcher callback → FileTypeDetector → JobQueue
- Status display shows queue statistics every 5 seconds

---

### 5. FileConverter
**Status:** ✅ Complete

**What it does:**
- Converts .xls files to .xlsx using LibreOffice headless mode
- Role: Support (Utility Champion)

**Files:**
- `include/FileConverter.h`
- `src/FileConverter.cpp`

**Key Features:**
- Spawns LibreOffice process with timeout (60 seconds)
- Handles process forking and waiting
- Checks for LibreOffice availability
- Returns path to converted file or empty string on failure

---

### 6. InputProcessor
**Status:** ✅ Complete

**What it does:**
- Processes files in input/ folder
- Converts .xls → .xlsx if needed
- Moves files to queue/ folder
- Role: Top Lane (Initial Processing)

**Files:**
- `include/InputProcessor.h`
- `src/InputProcessor.cpp`

**Key Features:**
- Uses FileConverter for conversion
- Handles both .xls (convert) and .xlsx (move directly)
- Deletes original .xls after conversion
- Tracks statistics (processed, converted, moved, failed)

---

### 7. Two-Folder System & Dual FileWatchers
**Status:** ✅ Complete

**What it does:**
- Two-folder workflow: `input/` → `queue/`
- Two independent FileWatcher instances
- Clear separation: raw input vs ready-to-process

**Workflow:**
```
1. File lands in input/ folder
2. Input FileWatcher detects → InputProcessor
3. InputProcessor converts (.xls → .xlsx) and moves to queue/
4. Queue FileWatcher detects → FileTypeDetector → JobQueue
5. JobQueue adds job for processing
```

**Files Updated:**
- `src/main.cpp` - Two watchers, two callbacks
- Created `queue/` folder structure

**Key Benefits:**
- Invalid files (conversion failures) don't enter queue
- Clear file lifecycle
- Easy debugging (separate input vs queue)
- Follows separation of concerns

---

## 🚧 Next Steps (In Order)

---

### 2. Worker Threads
**What we need:**
- Worker threads that pull from JobQueue
- Process one job at a time (or limited parallel)
- Spawn Python import scripts when needed

---

### 3. ScriptLauncher
**What we need:**
- Safely spawn Python scripts
- Handle timeouts, errors, output capture
- Return success/failure status
- Map FileType → Python script path

---

### 4. State Machine
**What we need:**
- Track current state: `IDLE`, `IMPORTING`, `AGGREGATING`
- Coordinate transitions
- Know when imports are complete

**States:**
```
IDLE → IMPORTING (file detected)
IMPORTING → AGGREGATING (queue empty)
AGGREGATING → IDLE (aggregation complete)
```

---

### 5. Aggregation Trigger
**What we need:**
- Listen for "queue empty" event
- Trigger aggregation scripts when imports complete
- Run aggregations in sequence or parallel
- Event-driven (not timer-based!)

**Logic:**
```
Queue hits 0 → pendingImports == 0 → Trigger aggregations
```

---

## 📊 Progress Estimate

**Completed:** ~45%
- ✅ File detection (FileWatcher)
- ✅ Type detection (FileTypeDetector)
- ✅ Foundation structure
- ✅ Job struct
- ✅ JobQueue (thread-safe queue with retry logic)
- ✅ Integration in main.cpp

**Next Milestone:** ~60%
- ⏳ ScriptLauncher (executes Python scripts)
- ⏳ WorkerPool (worker threads that pull from queue)
- ⏳ Basic job processing flow

**Complete System:** 100%
- ⏳ State machine
- ⏳ Aggregation trigger
- ⏳ Error handling
- ⏳ Terminal UI
- ⏳ Logging system

---

## 🎯 Architecture Goals

### Current System (Python):
- ❌ Timer-based (runs every 2 minutes, wasteful)
- ❌ 1.6GB RAM usage
- ❌ No coordination between scripts
- ❌ 21+ separate scripts

### Target System (C++):
- ✅ Event-driven (only runs when needed)
- ✅ ~50MB RAM usage (96% reduction)
- ✅ Coordinated (state-aware, queue-based)
- ✅ Single binary orchestrator

---

## 💡 Key Design Decisions

1. **Hybrid Approach:** C++ orchestrator + Python scripts (for now)
   - Keep existing Python import/aggregation scripts
   - Add C++ orchestration layer
   - Can migrate Python to C++ later if needed

2. **Event-Driven Aggregation:**
   - Wait for imports to complete (queue hits 0)
   - Then trigger aggregations
   - No timer waste!

3. **File Type Detection:**
   - Pattern-based (handles variations automatically)
   - Case-insensitive
   - Works with duplicates `(2)`, `(3)`, dates, etc.

---

## 🔧 Technical Notes

- **Build System:** CMake 3.10+
- **C++ Standard:** C++17
- **Threading:** std::thread for workers
- **File Watching:** Linux inotify
- **Database:** PostgreSQL (via Python scripts for now)

---

## 📝 Notes for Next Session

1. Start with Job struct (simple data container)
2. Then JobQueue class (thread-safe queue)
3. Integrate with existing FileWatcher + FileTypeDetector
4. Test with multiple files arriving at once

---

**Last Updated:** Session 3 - Two-Folder System + FileConverter + InputProcessor Complete

