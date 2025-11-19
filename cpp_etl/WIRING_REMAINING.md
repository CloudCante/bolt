# Remaining Wiring Tasks

## Overview

The ETL pipeline is mostly complete, but **AggregationManager is not yet connected** to the main pipeline. This document outlines what needs to be wired up.

---

## ✅ What's Already Wired

### Complete Pipeline Flow:
```
FileWatcher (input folder)
  → InputProcessor
    → FileConverter (converts .xls/.xlsx to CSV)
    → DataProcessor (cleans CSV columns) ✅
    → Move to queue/
      → FileWatcher (queue folder)
        → JobQueue.addJob() ✅
          → WorkerPool.getNextJob() ✅
            → DatabaseLoader.importCSV() ✅
              → JobQueue.markCompleted() ✅
```

**All components are wired and working:**
- ✅ FileWatcher (input & queue)
- ✅ InputProcessor
- ✅ FileConverter
- ✅ DataProcessor
- ✅ JobQueue
- ✅ WorkerPool
- ✅ DatabaseLoader

---

## ❌ What's Missing: AggregationManager Integration

### Current State:
- ✅ `AggregationManager` class exists and is fully implemented
- ✅ All 7 aggregation functions are implemented:
  - Workstation: `tpy_daily_metrics`, `tpy_weekly_metrics`, `pchart_data`, `daily_packing`
  - Testboard: `testboard_station_performance`, `fixture_performance`, `snfn_reports`
- ❌ **NOT instantiated in `main.cpp`**
- ❌ **NOT called when imports succeed**
- ❌ **No trigger mechanism**

### What Needs to Happen:
When a file is successfully imported to the database, we need to:
1. Detect the file type (WORKSTATION vs TESTBOARD)
2. Mark that data type as "pending" in AggregationManager
3. Trigger aggregations to run

---

## 🔧 Step-by-Step Wiring Instructions

### Step 1: Add AggregationManager to main.cpp

**File:** `cpp_etl/src/main.cpp`

**Add include at top:**
```cpp
#include "aggregation_manager.h"
```

**Add instance after other components (around line 126):**
```cpp
// Create components
JobQueue jobQueue(2); // Max 2 retries
InputProcessor inputProcessor(inputDir, queueDir);
WorkerPool workerPool(2, jobQueue, dbConfig); // 2 worker threads
AggregationManager aggManager;  // ← ADD THIS
g_workerPool = &workerPool;
```

---

### Step 2: Connect WorkerPool Success → AggregationManager

**Decision needed:** When should aggregations run?

**Option A: After each successful import** (immediate)
- Pros: Always up-to-date
- Cons: May run aggregations frequently if many files arrive

**Option B: When queue is empty** (batch)
- Pros: Runs once after batch completes
- Cons: May delay aggregations if queue never empties

**Option C: Periodic timer** (e.g., every 5 minutes)
- Pros: Predictable, batched
- Cons: May be delayed

**Recommendation:** Option A (after each success) + debounce logic (wait 30 seconds after last import before triggering)

---

### Step 3: Modify WorkerPool to Notify on Success

**File:** `cpp_etl/src/WorkerPool.cpp`

**Current code (around line 86-95):**
```cpp
if (success) {
    // Job succeeded
    auto stats = loader.getLastImportStats();
    std::cout << "[Worker #" << workerId << "] ✓ Job #" << job->jobId << " completed: "
              << stats.insertedRows << " new records inserted ("
              << stats.existingRows << " existing, "
              << stats.totalRows << " total rows)" << std::endl;
    
    jobQueue_.markCompleted(*job);
    totalSucceeded_.fetch_add(1);
}
```

**Need to add:**
- Way to notify AggregationManager when job succeeds
- Need to know the FileType from the Job

**Two approaches:**

#### Approach 1: Add callback to WorkerPool (Recommended)

**Modify `WorkerPool.h`:**
```cpp
class WorkerPool {
public:
    // ... existing code ...
    
    // Add callback type for successful imports
    using SuccessCallback = std::function<void(FileType fileType)>;
    
    // Set callback to call when import succeeds
    void setSuccessCallback(SuccessCallback callback);
    
private:
    // ... existing members ...
    SuccessCallback successCallback_;  // Add this
};
```

**Modify `WorkerPool.cpp`:**
```cpp
void WorkerPool::setSuccessCallback(SuccessCallback callback) {
    successCallback_ = callback;
}

// In workerThread(), after success:
if (success) {
    // ... existing success code ...
    jobQueue_.markCompleted(*job);
    totalSucceeded_.fetch_add(1);
    
    // Notify callback if set
    if (successCallback_) {
        successCallback_(job->fileType);
    }
}
```

**Then in `main.cpp`:**
```cpp
workerPool.setSuccessCallback([&aggManager](FileType fileType) {
    // Map FileType to DataType
    if (fileType == FileType::WORKSTATION) {
        aggManager.mark_data_pending(DataType::WORKSTATION);
    } else if (fileType == FileType::TESTBOARD) {
        aggManager.mark_data_pending(DataType::TESTBOARD);
    } else if (fileType == FileType::SNFN) {
        // SNFN is a testboard aggregation
        aggManager.mark_data_pending(DataType::TESTBOARD);
    }
    
    // Trigger aggregations (with debounce - see Step 4)
    aggManager.trigger_pending_aggregations();
});
```

#### Approach 2: Check JobQueue periodically in main loop

**In `main.cpp` main loop:**
```cpp
// Check for completed jobs and trigger aggregations
// (Less elegant, but simpler)
```

---

### Step 4: Add Debounce Logic (Optional but Recommended)

**Problem:** If 10 files arrive quickly, we don't want to run aggregations 10 times.

**Solution:** Add a debounce timer that waits 30 seconds after the last import before triggering.

**Modify `AggregationManager` or add debounce in main loop:**

**Option A: Add to main loop (simpler)**
```cpp
// In main.cpp, add:
std::time_t lastImportTime = 0;
const int AGGREGATION_DEBOUNCE_SECONDS = 30;

// In success callback:
lastImportTime = std::time(nullptr);

// In main loop:
if (lastImportTime > 0) {
    std::time_t now = std::time(nullptr);
    if (now - lastImportTime >= AGGREGATION_DEBOUNCE_SECONDS) {
        aggManager.trigger_pending_aggregations();
        lastImportTime = 0;  // Reset
    }
}
```

**Option B: Add debounce to AggregationManager (cleaner)**
- Modify `AggregationManager` to have internal debounce timer
- More complex but cleaner separation of concerns

---

### Step 5: Map FileType to DataType

**Decision needed:** How to map FileType enum to DataType enum?

**Current FileType enum:**
- `WORKSTATION`
- `TESTBOARD`
- `SNFN`
- `UNKNOWN`

**Current DataType enum:**
- `WORKSTATION`
- `TESTBOARD`

**Mapping:**
- `FileType::WORKSTATION` → `DataType::WORKSTATION` ✅
- `FileType::TESTBOARD` → `DataType::TESTBOARD` ✅
- `FileType::SNFN` → `DataType::TESTBOARD` (since SNFN is a testboard aggregation) ✅
- `FileType::UNKNOWN` → Skip (don't trigger aggregations)

---

## 📝 Code Changes Summary

### Files to Modify:

1. **`cpp_etl/src/main.cpp`**
   - Add `#include "aggregation_manager.h"`
   - Create `AggregationManager` instance
   - Set success callback on WorkerPool
   - Map FileType → DataType
   - Call `trigger_pending_aggregations()`

2. **`cpp_etl/include/WorkerPool.h`** (if using callback approach)
   - Add `SuccessCallback` type
   - Add `setSuccessCallback()` method
   - Add `successCallback_` member

3. **`cpp_etl/src/WorkerPool.cpp`** (if using callback approach)
   - Implement `setSuccessCallback()`
   - Call callback in `workerThread()` after successful import

---

## 🧪 Testing

After wiring:

1. **Test workstation file:**
   - Drop a workstation CSV in `./input`
   - Should: convert → clean → import → trigger workstation aggregations

2. **Test testboard file:**
   - Drop a testboard CSV in `./input`
   - Should: convert → clean → import → trigger testboard aggregations

3. **Test multiple files:**
   - Drop 5 files quickly
   - Should: import all, then run aggregations once (if debounce works)

4. **Check logs:**
   - Look for `[AggregationManager]` messages
   - Should see aggregation functions running

---

## ❓ Decisions to Make

Before implementing, decide:

1. **When to trigger aggregations?**
   - [ ] After each successful import (immediate)
   - [ ] When queue is empty (batch)
   - [ ] Periodic timer (every N minutes)
   - [ ] Debounced (wait 30s after last import)

2. **How to notify AggregationManager?**
   - [ ] Callback from WorkerPool (cleaner)
   - [ ] Poll JobQueue in main loop (simpler)

3. **SNFN file type:**
   - [ ] Map to `DataType::TESTBOARD` (recommended)
   - [ ] Skip (don't trigger aggregations)
   - [ ] Other?

---

## 📚 Reference

### AggregationManager API:
```cpp
class AggregationManager {
public:
    void mark_data_pending(DataType type);  // Mark that new data arrived
    void trigger_pending_aggregations();    // Run aggregations if pending
    bool is_running() const;                // Check if aggregations running
    void force_run();                       // Force run all (for testing)
};
```

### DataType enum:
```cpp
enum class DataType {
    WORKSTATION,
    TESTBOARD
};
```

### FileType enum:
```cpp
enum class FileType {
    UNKNOWN,
    WORKSTATION,
    TESTBOARD,
    SNFN
};
```

---

## ✅ Completion Checklist

- [ ] Add AggregationManager include to main.cpp
- [ ] Create AggregationManager instance
- [ ] Add success callback to WorkerPool (or polling logic)
- [ ] Map FileType → DataType in callback
- [ ] Call `mark_data_pending()` on successful import
- [ ] Call `trigger_pending_aggregations()` (with debounce if desired)
- [ ] Test with workstation file
- [ ] Test with testboard file
- [ ] Test with multiple files (verify debounce if implemented)
- [ ] Verify aggregations run and complete successfully

---

**Good luck! 🚀**

