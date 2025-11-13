# FoxETL Architecture - League of Legends Separation of Concerns

## 🎮 The Analogy

**Current Problem (Python ETL):** Champions trying to do everything - ADC trying to tank, support trying to jungle, etc. One script doing 9 different jobs = chaos.

**Solution (C++ ETL):** Each champion has ONE clear role. They work together as a team, but each does their specific job perfectly.

---

## 👥 Current Team Roster

### **FileWatcher** (Support/Vision Champion)
**Role:** Ward duty - watches for incoming files
**Abilities:**
- `start()` - Places ward (starts watching)
- `stop()` - Removes ward (stops watching)
- `setCallback()` - Sets up alert when enemy file detected
**Only Job:** Detect files, that's it!

---

### **FileTypeDetector** (Scout)
**Role:** Identify what enemy we're facing
**Abilities:**
- `detect()` - Analyzes filename to determine type
- `toString()` - Converts enum to string
**Only Job:** Read filename, return type. No file I/O, no queues, no processing!

---

## 🆕 Proposed Team Additions (Separation of Concerns)

### **1. Job** (Item/Stat Card)
**Role:** Data container - just holds information
**Location:** `include/Job.h` (no .cpp needed, just a struct!)
**Only Job:** Store job data (filepath, type, status, retries, etc.)
**Analogy:** Like a champion's stat card - it's just data, not a champion itself

```cpp
// include/Job.h - Just a struct, no abilities
struct Job {
    std::string filepath;
    FileType fileType;
    JobStatus status;
    // ... data fields only
};
```

---

### **2. JobQueue** (Jungle - Resource Coordinator)
**Role:** Manages the job queue, coordinates resources
**Location:** `include/JobQueue.h`, `src/JobQueue.cpp`
**Only Job:** 
- Add jobs to queue
- Get jobs from queue (thread-safe)
- Track pending/active jobs
- Prevent duplicates
**NOT Responsible For:**
- ❌ Executing Python scripts
- ❌ Writing logs
- ❌ Database operations
- ❌ Retry logic (just tracks retry count)

**Abilities:**
- `addJob()` - Adds job to queue
- `getNextJob()` - Worker pulls job (blocking)
- `markCompleted()` - Marks job done
- `markFailed()` - Marks job failed (but doesn't handle retry details)
- `getStatistics()` - Queue stats only

---

### **3. ErrorLogger** (Support - Documentation)
**Role:** Write error logs to disk
**Location:** `include/ErrorLogger.h`, `src/ErrorLogger.cpp`
**Only Job:** 
- Create error log files
- Format error messages
- Write to `./error_logs/` directory
**NOT Responsible For:**
- ❌ Managing queue
- ❌ Executing scripts
- ❌ Database operations
- ❌ Retry decisions

**Abilities:**
- `createErrorLog()` - Writes formatted error log file
- `getLogPath()` - Returns path to log directory

---

### **4. ScriptLauncher** (Assassin - Executes the Kill)
**Role:** Execute Python import scripts
**Location:** `include/ScriptLauncher.h`, `src/ScriptLauncher.cpp`
**Only Job:**
- Spawn Python process
- Capture stdout/stderr
- Handle timeouts
- Return execution result
**NOT Responsible For:**
- ❌ Managing queue
- ❌ Writing logs (passes output to ErrorLogger)
- ❌ Database operations
- ❌ Retry decisions

**Abilities:**
- `execute()` - Runs Python script, returns result
- `getScriptPath()` - Maps FileType → script path
- `setTimeout()` - Configures timeout

**Result Structure:**
```cpp
struct ScriptResult {
    bool success;
    int exitCode;
    std::string stdout;
    std::string stderr;
    std::string errorMessage;
};
```

---

### **5. DatabaseWriter** (Support - Persistent Storage)
**Role:** Write failed jobs to database
**Location:** `include/DatabaseWriter.h`, `src/DatabaseWriter.cpp`
**Only Job:**
- Connect to PostgreSQL
- Insert failed job records
- Query failed jobs
- Read error log paths
**NOT Responsible For:**
- ❌ Managing queue
- ❌ Executing scripts
- ❌ Writing log files
- ❌ Retry logic

**Abilities:**
- `writeFailedJob()` - Inserts into `etl_failed_jobs` table
- `getFailedJobs()` - Queries failed jobs by filter
- `getErrorLogPath()` - Gets log file path for a job ID

**Note:** For now, this might spawn a Python helper script to do DB operations, or use libpqxx (C++ PostgreSQL client)

---

### **6. RetryManager** (Strategic Planner)
**Role:** Decide when to retry failed jobs
**Location:** `include/RetryManager.h`, `src/RetryManager.cpp`
**Only Job:**
- Determine if job should retry
- Calculate retry delay (if needed)
- Manage retry state transitions
**NOT Responsible For:**
- ❌ Executing scripts
- ❌ Writing logs
- ❌ Database operations
- ❌ Queue management

**Abilities:**
- `shouldRetry()` - Checks if job can retry
- `getNextRetryTime()` - Calculates when to retry (future: exponential backoff)

---

### **7. WorkerPool** (Damage Dealers - Top/Mid)
**Role:** Worker threads that process jobs
**Location:** `include/WorkerPool.h`, `src/WorkerPool.cpp`
**Only Job:**
- Spawn N worker threads
- Each worker: pull job → launch script → handle result
- Coordinate with JobQueue, ScriptLauncher, ErrorLogger, etc.
**NOT Responsible For:**
- ❌ Managing queue internals (delegates to JobQueue)
- ❌ Writing logs (delegates to ErrorLogger)
- ❌ DB operations (delegates to DatabaseWriter)

**Abilities:**
- `start()` - Spawns worker threads
- `stop()` - Gracefully shuts down workers
- `getWorkerCount()` - Returns active worker count

**Worker Thread Loop:**
```
1. Pull job from JobQueue
2. Execute via ScriptLauncher
3. If success: JobQueue.markCompleted()
4. If fail: RetryManager.shouldRetry()?
   - Yes: Add back to queue (with retry count++)
   - No: ErrorLogger.createErrorLog() → DatabaseWriter.writeFailedJob()
```

---

### **8. Orchestrator** (Team Captain - main.cpp or separate class)
**Role:** Coordinates all champions, main game loop
**Location:** `src/main.cpp` or `include/Orchestrator.h`
**Only Job:**
- Initialize all champions
- Set up connections between them
- Run main loop
- Handle shutdown
**NOT Responsible For:**
- ❌ Individual champion logic (delegates to specialists)

**Flow:**
```cpp
// main.cpp - The Team Captain
int main() {
    // Initialize all champions
    JobQueue jobQueue;
    ErrorLogger logger("./error_logs");
    ScriptLauncher launcher;
    DatabaseWriter dbWriter;
    RetryManager retryManager;
    WorkerPool workers(4); // 4 worker threads
    
    // Set up FileWatcher callback
    FileWatcher watcher("./input");
    watcher.setCallback([&](const std::string& path) {
        FileType type = FileTypeDetector::detect(extractFilename(path));
        jobQueue.addJob(path, type);
    });
    
    // Connect champions together
    workers.setJobQueue(&jobQueue);
    workers.setScriptLauncher(&launcher);
    workers.setErrorLogger(&logger);
    workers.setDatabaseWriter(&dbWriter);
    workers.setRetryManager(&retryManager);
    
    // Start the game
    watcher.start();
    workers.start();
    
    // Main loop - just coordinate, don't do work!
    while (running) {
        sleep(1);
        // Maybe print stats periodically
    }
    
    // Shutdown gracefully
    workers.stop();
    watcher.stop();
}
```

---

## 🎯 Separation Principles

### **Single Responsibility Rule (LoL Style)**
Each champion does ONE thing well:
- **ADC:** Deal damage (doesn't tank, doesn't ward)
- **Support:** Help team (doesn't jungle, doesn't DPS)
- **Jungle:** Coordinate resources (doesn't last hit, doesn't ward lanes)

### **Delegation Pattern**
Champions work together but don't do each other's jobs:
- WorkerPool doesn't write logs → asks ErrorLogger
- JobQueue doesn't execute scripts → WorkerPool uses ScriptLauncher
- ScriptLauncher doesn't decide retries → WorkerPool asks RetryManager

### **Clear Boundaries**
Each champion has clear inputs and outputs:
- **Input:** What it needs to do its job
- **Output:** What it produces
- **Dependencies:** What other champions it needs (passed in constructor or setters)

---

## 📁 Proposed File Structure

```
cpp_etl/
├── CMakeLists.txt              # Team composition
├── include/                    # Champion roster (.h files)
│   ├── FileWatcher.h          # ✅ Support (vision)
│   ├── FileTypeDetector.h     # ✅ Scout
│   ├── Job.h                  # 🆕 Item/Stat card
│   ├── JobQueue.h             # 🆕 Jungle (coordination)
│   ├── ErrorLogger.h          # 🆕 Support (documentation)
│   ├── ScriptLauncher.h       # 🆕 Assassin (execution)
│   ├── DatabaseWriter.h       # 🆕 Support (storage)
│   ├── RetryManager.h         # 🆕 Strategic planner
│   └── WorkerPool.h           # 🆕 Damage dealers
├── src/                        # Champion abilities (.cpp files)
│   ├── main.cpp               # Team captain (orchestrator)
│   ├── FileWatcher.cpp
│   ├── FileTypeDetector.cpp
│   ├── JobQueue.cpp
│   ├── ErrorLogger.cpp
│   ├── ScriptLauncher.cpp
│   ├── DatabaseWriter.cpp
│   ├── RetryManager.cpp
│   └── WorkerPool.cpp
└── build/
```

---

## 🔄 Comparison: Monolithic vs. Separated

### ❌ **BAD: One Champion Doing Everything** (Like Old Python Scripts)
```cpp
class MegaChampion {
    // File watching
    void watchFiles();
    
    // Queue management
    void addJob();
    void getJob();
    
    // Script execution
    void runPython();
    
    // Logging
    void writeLog();
    
    // Database
    void writeToDB();
    
    // Retry logic
    void retry();
    
    // Worker management
    void spawnWorkers();
    
    // 9 different jobs = chaos!
};
```

### ✅ **GOOD: Specialized Champions** (Our Design)
```cpp
// Each champion does ONE job
FileWatcher watcher;        // Only watches
JobQueue queue;             // Only manages queue
ScriptLauncher launcher;    // Only executes scripts
ErrorLogger logger;         // Only writes logs
DatabaseWriter db;          // Only writes to DB
WorkerPool workers;         // Only coordinates workers
```

---

## 🎮 CMakeLists.txt as Team Composition

When you add a new champion (role), you add them to the team:

```cmake
# Source files (champion abilities)
set(SOURCES
    src/main.cpp              # Team captain
    src/FileWatcher.cpp       # Support
    src/FileTypeDetector.cpp  # Scout
    src/JobQueue.cpp          # 🆕 Jungle
    src/ErrorLogger.cpp       # 🆕 Support
    src/ScriptLauncher.cpp    # 🆕 Assassin
    src/DatabaseWriter.cpp    # 🆕 Support
    src/RetryManager.cpp      # 🆕 Strategic planner
    src/WorkerPool.cpp        # 🆕 Damage dealers
)

# Header files (champion roster)
set(HEADERS
    include/FileWatcher.h
    include/FileTypeDetector.h
    include/Job.h
    include/JobQueue.h
    include/ErrorLogger.h
    include/ScriptLauncher.h
    include/DatabaseWriter.h
    include/RetryManager.h
    include/WorkerPool.h
)
```

**Adding a new role:** Just add the .h and .cpp to SOURCES and HEADERS. CMake handles the rest!

---

## 🏆 Benefits of This Separation

1. **Easy to Test:** Test each champion in isolation (unit tests)
2. **Easy to Modify:** Change one champion without breaking others
3. **Easy to Understand:** Each file has ONE clear purpose
4. **Easy to Debug:** Know exactly where the problem is
5. **Reusable:** Can swap out champions (e.g., different logger, different DB client)

---

## 🎯 Next Steps

1. **Start with Job struct** (simplest - just data)
2. **Then JobQueue** (core coordination)
3. **Then ScriptLauncher** (execution - needed by workers)
4. **Then ErrorLogger** (error handling)
5. **Then WorkerPool** (brings it all together)
6. **Finally DatabaseWriter & RetryManager** (polish)

Each champion is independent and can be built/tested separately!

---

**Remember:** One champion, one job. That's how you win the game! 🏆

