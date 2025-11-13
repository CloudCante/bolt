# Exact Logging Locations for Debugging

This document shows **exactly where** to add logging statements to identify where the hang occurs.

## File: `cpp_etl/src/DatabaseLoader.cpp`

### Function: `connectToDatabase()` (Lines 28-48)

**This is the MOST LIKELY hang point!**

Add logging at these exact locations:

```cpp
void* DatabaseLoader::connectToDatabase() const {
    std::cout << "[DEBUG] [connectToDatabase] START - Building connection string" << std::endl;
    std::cout.flush();
    
    std::string connInfo = "host=" + config_.host +
                          " port=" + std::to_string(config_.port) +
                          " dbname=" + config_.database +
                          " user=" + config_.user;
    
    std::string password = config_.password;
    if (!password.empty()) {
        connInfo += " password=" + password;
    }
    
    std::cout << "[DEBUG] [connectToDatabase] Connection string built: " << connInfo << std::endl;
    std::cout.flush();
    std::cout << "[DEBUG] [connectToDatabase] ⚠️ ABOUT TO CALL PQconnectdb() - THIS MAY HANG" << std::endl;
    std::cout.flush();
    
    PGconn* conn = PQconnectdb(connInfo.c_str());  // LINE 39 - LIKELY HANG POINT
    
    std::cout << "[DEBUG] [connectToDatabase] ✓ PQconnectdb() RETURNED" << std::endl;
    std::cout.flush();
    std::cout << "[DEBUG] [connectToDatabase] Checking connection status..." << std::endl;
    std::cout.flush();
    
    if (PQstatus(conn) != CONNECTION_OK) {
        std::cout << "[DEBUG] [connectToDatabase] Connection status: FAILED" << std::endl;
        std::cout.flush();
        lastError_ = "Database connection failed: " + std::string(PQerrorMessage(conn));
        PQfinish(conn);
        return nullptr;
    }
    
    std::cout << "[DEBUG] [connectToDatabase] Connection status: OK" << std::endl;
    std::cout.flush();
    std::cout << "[DEBUG] [connectToDatabase] END - Returning connection handle" << std::endl;
    std::cout.flush();
    
    return conn;
}
```

### Function: `importCSV()` (Lines 546-622)

Add logging at these locations:

```cpp
bool DatabaseLoader::importCSV(const std::string& csvFilePath, FileType fileType) {
    std::cout << "[DEBUG] [importCSV] START" << std::endl;
    std::cout.flush();
    
    lastError_.clear();
    lastStats_ = {0, 0, 0, 0, false};
    
    std::cout << "[DEBUG] [importCSV] Checking if file exists..." << std::endl;
    std::cout.flush();
    
    if (!fs::exists(csvFilePath)) {
        lastError_ = "CSV file does not exist: " + csvFilePath;
        return false;
    }
    
    if (fileType == FileType::UNKNOWN) {
        lastError_ = "Unknown file type, cannot import";
        return false;
    }
    
    std::cout << "[DatabaseLoader] Importing " << fs::path(csvFilePath).filename().string()
              << " into database..." << std::endl;
    
    std::cout << "[DEBUG] [importCSV] About to parse CSV file..." << std::endl;
    std::cout.flush();
    
    // Parse CSV
    std::vector<std::map<std::string, std::string>> csvRows = parseCSV(csvFilePath);
    
    std::cout << "[DEBUG] [importCSV] CSV parsing complete" << std::endl;
    std::cout.flush();
    
    if (csvRows.empty()) {
        return false; // Error already set in parseCSV
    }
    
    lastStats_.totalRows = csvRows.size();
    std::cout << "[DatabaseLoader] Parsed " << csvRows.size() << " rows from CSV" << std::endl;
    
    std::cout << "[DEBUG] [importCSV] ⚠️ ABOUT TO CONNECT TO DATABASE" << std::endl;
    std::cout.flush();
    
    // Connect to database
    void* conn = connectToDatabase();
    
    std::cout << "[DEBUG] [importCSV] ✓ connectToDatabase() RETURNED" << std::endl;
    std::cout.flush();
    
    if (!conn) {
        std::cout << "[DEBUG] [importCSV] Connection failed, returning false" << std::endl;
        std::cout.flush();
        return false; // Error already set
    }
    
    std::cout << "[DEBUG] [importCSV] Connection successful, starting duplicate check" << std::endl;
    std::cout.flush();
    
    // Map rows and check for duplicates
    std::vector<std::map<std::string, std::string>> newRecords;
    size_t existingCount = 0;
    
    std::cout << "[DatabaseLoader] Checking for existing records..." << std::endl;
    
    std::cout << "[DEBUG] [importCSV] Starting loop through " << csvRows.size() << " rows" << std::endl;
    std::cout.flush();
    
    for (const auto& csvRow : csvRows) {
        std::map<std::string, std::string> mappedRecord = mapRowToDatabase(csvRow, fileType);
        
        if (recordExists(conn, mappedRecord, fileType)) {
            existingCount++;
        } else {
            newRecords.push_back(mappedRecord);
        }
    }
    
    std::cout << "[DEBUG] [importCSV] Duplicate check loop complete" << std::endl;
    std::cout.flush();
    
    lastStats_.existingRows = existingCount;
    lastStats_.newRows = newRecords.size();
    
    std::cout << "[DatabaseLoader] Found " << existingCount << " existing records, "
              << newRecords.size() << " new records to insert" << std::endl;
    
    // Insert new records
    if (!newRecords.empty()) {
        std::cout << "[DEBUG] [importCSV] About to insert " << newRecords.size() << " records" << std::endl;
        std::cout.flush();
        
        size_t inserted = insertRecords(conn, newRecords, fileType);
        
        std::cout << "[DEBUG] [importCSV] insertRecords() returned: " << inserted << std::endl;
        std::cout.flush();
        
        lastStats_.insertedRows = inserted;
        std::cout << "[DatabaseLoader] Inserted " << inserted << " new records" << std::endl;
    } else {
        std::cout << "[DatabaseLoader] No new records to insert (all records already exist)" << std::endl;
    }
    
    std::cout << "[DEBUG] [importCSV] About to close connection" << std::endl;
    std::cout.flush();
    
    closeConnection(conn);
    
    std::cout << "[DEBUG] [importCSV] Connection closed" << std::endl;
    std::cout.flush();
    
    // Delete CSV file on success
    if (lastStats_.insertedRows > 0 || newRecords.empty()) {
        try {
            fs::remove(csvFilePath);
            std::cout << "[DatabaseLoader] Deleted CSV file after successful import" << std::endl;
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[DatabaseLoader] Warning: Could not delete CSV file: " << e.what() << std::endl;
        }
    }
    
    lastStats_.success = true;
    
    std::cout << "[DEBUG] [importCSV] END - SUCCESS" << std::endl;
    std::cout.flush();
    
    return true;
}
```

### Function: `recordExists()` (Lines 265-393)

Add logging around the query execution:

```cpp
bool DatabaseLoader::recordExists(void* conn, const std::map<std::string, std::string>& record, FileType fileType) const {
    std::cout << "[DEBUG] [recordExists] START" << std::endl;
    std::cout.flush();
    
    PGconn* pgconn = static_cast<PGconn*>(conn);
    // ... build query ...
    
    std::cout << "[DEBUG] [recordExists] About to execute PQexecParams()" << std::endl;
    std::cout.flush();
    
    PGresult* res = PQexecParams(pgconn, query.c_str(), paramValues.size(), nullptr,
                                 params.data(), paramLengths.data(), paramFormats.data(), 0);
    
    std::cout << "[DEBUG] [recordExists] PQexecParams() returned" << std::endl;
    std::cout.flush();
    
    // ... rest of function ...
}
```

### Function: `insertRecords()` (Lines 395-524)

Add logging around the insert execution:

```cpp
size_t DatabaseLoader::insertRecords(void* conn, const std::vector<std::map<std::string, std::string>>& records, FileType fileType) const {
    std::cout << "[DEBUG] [insertRecords] START - " << records.size() << " records" << std::endl;
    std::cout.flush();
    
    // ... build query ...
    
    std::cout << "[DEBUG] [insertRecords] About to execute PQexec()" << std::endl;
    std::cout.flush();
    
    PGresult* res = PQexec(pgconn, fullQuery.c_str());
    
    std::cout << "[DEBUG] [insertRecords] PQexec() returned" << std::endl;
    std::cout.flush();
    
    // ... rest of function ...
}
```

## Expected Output When Hanging

If the hang is at `PQconnectdb()` (most likely scenario), you'll see:

```
[DatabaseLoader] Importing file.csv into database...
[DEBUG] [importCSV] About to parse CSV file...
[DatabaseLoader] Parsed 1000 rows from CSV
[DEBUG] [importCSV] ⚠️ ABOUT TO CONNECT TO DATABASE
[DEBUG] [connectToDatabase] START - Building connection string
[DEBUG] [connectToDatabase] Connection string built: host=localhost port=5432 dbname=fox_db user=gpu_user
[DEBUG] [connectToDatabase] ⚠️ ABOUT TO CALL PQconnectdb() - THIS MAY HANG
[HANGS HERE - NO MORE OUTPUT]
```

## What This Tells Us

- **If you see the last message**: Hang is confirmed at `PQconnectdb()` (Line 39)
- **If you see messages after**: Hang is somewhere else (less likely)
- **If you see no messages**: Hang is before logging starts (unlikely)

## Next Steps

1. Add the logging statements above
2. Recompile: `cd cpp_etl/build && make`
3. Run your test: `./test_database_loader /path/to/file.csv`
4. Observe the last log message printed
5. Report back with the last message you see

This will definitively identify where the hang occurs!

