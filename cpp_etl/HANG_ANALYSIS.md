# C++ ETL Pipeline Hang Analysis

## Problem Summary
The C++ ETL pipeline hangs when connecting to the database, consuming 90% CPU and never exiting. The process must be manually killed.

## Execution Flow Analysis

### Main Entry Point: `main.cpp`
1. **Line 79-96**: Parse command line arguments
2. **Line 98-100**: Set up signal handlers (SIGINT, SIGTERM)
3. **Line 103**: Create `JobQueue` (max 2 retries)
4. **Line 104**: Create `InputProcessor`
5. **Line 107-119**: Create and start Input folder watcher
6. **Line 122-152**: Create and start Queue folder watcher
7. **Line 165-174**: Start both watchers
8. **Line 179-185**: Main loop - sleeps 5 seconds, prints status

**⚠️ CRITICAL OBSERVATION**: The main.cpp does NOT have worker threads that process jobs! Jobs are added to the queue, but there's no code that actually calls `DatabaseLoader::importCSV()`.

### Database Connection Flow: `DatabaseLoader.cpp`

#### `importCSV()` Method (Line 546-622)
This is where the database connection happens:

```
Line 560-561: Log "Importing [filename] into database..."
Line 564:      Parse CSV file
Line 570:      Log "Parsed [N] rows from CSV"
Line 573:      ⚠️ HANG POINT #1: connectToDatabase() - BLOCKING CALL
Line 574-576:  Check if connection failed
Line 582:      Log "Checking for existing records..."
Line 583-591: Loop through CSV rows checking for duplicates
Line 596-597: Log "Found [N] existing records, [N] new records"
Line 600-606: Insert new records (if any)
Line 608:      ⚠️ HANG POINT #2: closeConnection() - Should be safe
Line 611-618: Delete CSV file on success
Line 620:      Set success flag
```

#### `connectToDatabase()` Method (Line 28-48)
**THIS IS THE LIKELY HANG POINT:**

```cpp
Line 28: void* DatabaseLoader::connectToDatabase() const {
Line 29-37: Build connection string
Line 39:  ⚠️ CRITICAL: PGconn* conn = PQconnectdb(connInfo.c_str());
         // This is a BLOCKING synchronous call
         // If database is unreachable, this can hang indefinitely
         // No timeout is set!
Line 41-45: Check connection status and handle errors
Line 47:  Return connection handle
```

**PROBLEM IDENTIFIED:**
- `PQconnectdb()` is a **blocking synchronous call** with **NO TIMEOUT**
- If the database server is:
  - Not running
  - Not reachable (network issue)
  - Firewall blocking
  - Taking too long to respond
- The call will **hang indefinitely**, consuming CPU in a busy-wait or blocking state

### Memory Leak Analysis

#### Potential Memory Leaks:

1. **Database Connection (Line 39)**: 
   - If `PQconnectdb()` hangs, the connection is never created
   - If it succeeds but hangs later, connection is never closed
   - **FIX**: Need to ensure `PQfinish()` is always called

2. **Connection Not Closed on Error**:
   - Line 43: `PQfinish(conn)` is called on error (GOOD)
   - Line 608: `closeConnection(conn)` is called on success (GOOD)
   - **BUT**: If `importCSV()` throws an exception or returns early, connection might leak

3. **Query Results Not Cleared**:
   - Line 381: `PQexecParams()` returns `PGresult*`
   - Line 391: `PQclear(res)` is called (GOOD)
   - Line 495: `PQexec()` returns `PGresult*`
   - Line 521: `PQclear(res)` is called (GOOD)
   - **Status**: Query results appear to be properly cleaned up

## Recommended Logging Points

Add console logging at these critical points to identify where execution stops:

### In `connectToDatabase()`:
```cpp
Line 28: std::cout << "[DEBUG] connectToDatabase() - START" << std::endl;
Line 29: std::cout << "[DEBUG] Building connection string..." << std::endl;
Line 38: std::cout << "[DEBUG] About to call PQconnectdb()..." << std::endl;
Line 38: std::cout << "[DEBUG] Connection string: " << connInfo << std::endl;
Line 39: std::cout << "[DEBUG] Calling PQconnectdb() - THIS MAY HANG" << std::endl;
Line 39: PGconn* conn = PQconnectdb(connInfo.c_str());
Line 40: std::cout << "[DEBUG] PQconnectdb() returned" << std::endl;
Line 41: std::cout << "[DEBUG] Checking connection status..." << std::endl;
```

### In `importCSV()`:
```cpp
Line 546: std::cout << "[DEBUG] importCSV() - START" << std::endl;
Line 550: std::cout << "[DEBUG] Checking if file exists..." << std::endl;
Line 560: std::cout << "[DEBUG] About to parse CSV..." << std::endl;
Line 564: std::cout << "[DEBUG] CSV parsing complete" << std::endl;
Line 572: std::cout << "[DEBUG] About to connect to database..." << std::endl;
Line 573: std::cout << "[DEBUG] Calling connectToDatabase()..." << std::endl;
Line 573: void* conn = connectToDatabase();
Line 574: std::cout << "[DEBUG] connectToDatabase() returned" << std::endl;
Line 577: std::cout << "[DEBUG] Starting duplicate check loop..." << std::endl;
Line 583: std::cout << "[DEBUG] Processing row " << (i+1) << " of " << csvRows.size() << std::endl;
Line 599: std::cout << "[DEBUG] About to insert records..." << std::endl;
Line 608: std::cout << "[DEBUG] About to close connection..." << std::endl;
Line 620: std::cout << "[DEBUG] importCSV() - SUCCESS" << std::endl;
```

### In `recordExists()`:
```cpp
Line 265: std::cout << "[DEBUG] recordExists() - START" << std::endl;
Line 380: std::cout << "[DEBUG] About to execute PQexecParams()..." << std::endl;
Line 381: std::cout << "[DEBUG] PQexecParams() returned" << std::endl;
```

### In `insertRecords()`:
```cpp
Line 395: std::cout << "[DEBUG] insertRecords() - START" << std::endl;
Line 495: std::cout << "[DEBUG] About to execute PQexec()..." << std::endl;
Line 495: std::cout << "[DEBUG] PQexec() returned" << std::endl;
```

## Root Cause Hypothesis

**Most Likely**: The hang occurs at **Line 39 in `connectToDatabase()`**:
```cpp
PGconn* conn = PQconnectdb(connInfo.c_str());
```

This is a blocking call with no timeout. If the database:
- Is not running
- Is not reachable
- Has network issues
- Is overloaded

The call will hang indefinitely.

## Solutions to Consider (After Debugging Confirms)

1. **Add Connection Timeout**:
   - Use `PQconnectStart()` + `PQconnectPoll()` for non-blocking connection
   - Or use `PQsetnonblocking()` with timeout
   - Or add connection timeout to connection string: `connect_timeout=5`

2. **Add Timeout to Connection String**:
   ```cpp
   connInfo += " connect_timeout=5";  // 5 second timeout
   ```

3. **Use Non-Blocking Connection**:
   - Implement async connection with timeout
   - Check connection status periodically

4. **Add Signal Handling**:
   - Ensure SIGINT/SIGTERM can interrupt the connection attempt
   - Clean up resources on signal

5. **Add RAII Wrapper**:
   - Create a connection wrapper that automatically closes on destruction
   - Ensures no memory leaks even on exceptions

## Next Steps

1. **Add logging** at all critical points (especially around `PQconnectdb()`)
2. **Run the pipeline** and observe which log message is the last one printed
3. **Confirm the hang point** (likely Line 39 in `connectToDatabase()`)
4. **Implement timeout solution** based on confirmed hang point
5. **Test with database intentionally unreachable** to verify timeout works

## Files to Modify (When Ready)

1. `cpp_etl/src/DatabaseLoader.cpp` - Add logging and timeout
2. `cpp_etl/include/DatabaseLoader.h` - Add timeout configuration option

