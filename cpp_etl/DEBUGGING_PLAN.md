# C++ ETL Pipeline Debugging Plan

## Current Situation
- Pipeline hangs when connecting to database
- Process consumes 90% CPU and never exits
- Must be manually killed
- This is your first memory leak/hang debugging experience

## Evaluation Summary

### Where Database Connection Happens

**File**: `cpp_etl/src/DatabaseLoader.cpp`

**Method**: `connectToDatabase()` (Line 28-48)

**Critical Line**: **Line 39**
```cpp
PGconn* conn = PQconnectdb(connInfo.c_str());
```

This is a **blocking synchronous call** with **NO TIMEOUT**. If the database is unreachable, this will hang indefinitely.

### Execution Flow

When `importCSV()` is called:

1. ✅ Parse CSV file (Line 564)
2. ✅ Log "Parsed N rows" (Line 570)
3. ⚠️ **HANG POINT**: Connect to database (Line 573 → Line 39 in `connectToDatabase()`)
4. ❌ Never reaches: Check for duplicates
5. ❌ Never reaches: Insert records
6. ❌ Never reaches: Close connection

### Why It Hangs

`PQconnectdb()` is a blocking call that:
- Waits for TCP connection to establish
- Waits for PostgreSQL handshake
- Has **no built-in timeout**
- Will wait **forever** if database is:
  - Not running
  - Not reachable (network/firewall)
  - Overloaded and not responding
  - Incorrect host/port

### Why High CPU Usage?

The PostgreSQL libpq library might be:
- In a busy-wait loop trying to connect
- Polling network sockets continuously
- Retrying connection attempts rapidly

## Debugging Strategy

### Step 1: Add Strategic Logging

Add logging **before and after** every critical operation to see where execution stops:

**Priority 1 - Most Critical** (in `connectToDatabase()`):
- Before building connection string
- After building connection string (log the string)
- **BEFORE** `PQconnectdb()` call
- **AFTER** `PQconnectdb()` call (if it returns)
- After checking connection status

**Priority 2 - Important** (in `importCSV()`):
- At start of `importCSV()`
- After CSV parsing
- **BEFORE** calling `connectToDatabase()`
- **AFTER** `connectToDatabase()` returns
- Before duplicate checking loop
- After duplicate checking loop
- Before inserting records
- After inserting records
- Before closing connection
- At end of `importCSV()`

**Priority 3 - Secondary** (in other methods):
- `recordExists()` - before/after `PQexecParams()`
- `insertRecords()` - before/after `PQexec()`

### Step 2: Run and Observe

1. Run your test program (e.g., `test_database_loader`)
2. Watch console output
3. Identify the **last log message** printed
4. The hang occurs **after** that message but **before** the next one

### Step 3: Confirm Hypothesis

Based on the analysis, the hang is **most likely** at:
```
[DEBUG] About to call PQconnectdb()...
[HANGS HERE - no further output]
```

## Logging Format Recommendation

Use a consistent format for easy tracking:

```cpp
std::cout << "[DEBUG] [FunctionName] [StepDescription] - [Status]" << std::endl;
std::cout.flush();  // Force immediate output
```

Example:
```cpp
std::cout << "[DEBUG] [connectToDatabase] Building connection string - START" << std::endl;
std::cout.flush();
// ... code ...
std::cout << "[DEBUG] [connectToDatabase] About to call PQconnectdb() - CRITICAL POINT" << std::endl;
std::cout.flush();
PGconn* conn = PQconnectdb(connInfo.c_str());
std::cout << "[DEBUG] [connectToDatabase] PQconnectdb() returned - SUCCESS" << std::endl;
std::cout.flush();
```

**Important**: Use `std::cout.flush()` to ensure logs are written immediately, even if the program hangs.

## Expected Output When Hanging

If the hang is at `PQconnectdb()` (most likely), you'll see:

```
[DatabaseLoader] Importing file.csv into database...
[DatabaseLoader] Parsed 1000 rows from CSV
[DEBUG] [importCSV] About to connect to database...
[DEBUG] [connectToDatabase] Building connection string - START
[DEBUG] [connectToDatabase] Connection string: host=localhost port=5432 dbname=fox_db user=gpu_user
[DEBUG] [connectToDatabase] About to call PQconnectdb() - CRITICAL POINT
[HANGS HERE - NO MORE OUTPUT]
```

## Next Steps After Confirming Hang Point

Once you confirm where it hangs, we can implement:

1. **Connection Timeout**: Add `connect_timeout=5` to connection string
2. **Non-blocking Connection**: Use `PQconnectStart()` + `PQconnectPoll()` with timeout
3. **Signal Handling**: Ensure SIGINT can interrupt connection attempts
4. **RAII Wrapper**: Ensure connections are always closed, even on exceptions

## Memory Leak Notes

While debugging, also watch for:
- Connections that are never closed (if `PQconnectdb()` succeeds but hangs later)
- Query results that are never cleared (though current code looks good)
- Exceptions that bypass cleanup code

## Testing Strategy

1. **Test with database running**: Should work normally
2. **Test with database stopped**: Should hang at `PQconnectdb()` (confirms hypothesis)
3. **Test with wrong host/port**: Should hang at `PQconnectdb()` (confirms hypothesis)
4. **Test with firewall blocking**: Should hang at `PQconnectdb()` (confirms hypothesis)

After adding timeout, all of these should fail gracefully instead of hanging.

