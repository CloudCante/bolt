#ifndef DATABASE_LOADER_H
#define DATABASE_LOADER_H

#include "FileTypeDetector.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

/**
 * DatabaseLoader - Loads cleaned CSV data into PostgreSQL database
 * 
 * Role: ADC/DPS - Final data import
 * Only Job: Read CSV, check for duplicates, insert new records into database
 * 
 * Usage:
 *   DatabaseLoader loader;
 *   if (loader.importCSV("/path/to/file.csv", FileType::WORKSTATION)) {
 *       // Import successful
 *   }
 */
class DatabaseLoader {
public:
    /**
     * Database connection configuration
     */
    struct DatabaseConfig {
        std::string host = "localhost";
        int port = 5432;
        std::string database = "fox_db";
        std::string user = "gpu_user";
        std::string password = "";
    };

    /**
     * Constructor with default config
     */
    DatabaseLoader();

    /**
     * Constructor
     * @param config Database connection configuration
     */
    explicit DatabaseLoader(const DatabaseConfig& config);

    /**
     * Destructor
     */
    ~DatabaseLoader();

    // Disable copy
    DatabaseLoader(const DatabaseLoader&) = delete;
    DatabaseLoader& operator=(const DatabaseLoader&) = delete;

    /**
     * Import CSV file into database
     * - Reads CSV file
     * - Maps rows to database schema based on file type
     * - Checks for existing records (duplicates)
     * - Inserts only new records
     * - Deletes CSV file on success
     * 
     * @param csvFilePath Path to cleaned CSV file
     * @param fileType Type of file (WORKSTATION, TESTBOARD)
     * @return true if successful, false on error
     */
    bool importCSV(const std::string& csvFilePath, FileType fileType);

    /**
     * Get last error message
     * @return Error description or empty string if no error
     */
    std::string getLastError() const;

    /**
     * Get import statistics
     */
    struct ImportStats {
        size_t totalRows;
        size_t existingRows;
        size_t newRows;
        size_t insertedRows;
        bool success;
    };

    ImportStats getLastImportStats() const;

private:
    DatabaseConfig config_;
    mutable std::string lastError_;
    mutable ImportStats lastStats_;

    /**
     * Connect to PostgreSQL database
     * @return Connection handle (void* for libpq PGconn*)
     */
    void* connectToDatabase() const;

    /**
     * Close database connection
     * @param conn Connection handle
     */
    void closeConnection(void* conn) const;

    /**
     * Parse CSV file and return rows
     * @param csvFilePath Path to CSV file
     * @return Vector of rows, each row is a map of column name to value
     */
    std::vector<std::map<std::string, std::string>> parseCSV(const std::string& csvFilePath) const;

    /**
     * Map CSV row to database record based on file type
     * @param row CSV row data
     * @param fileType File type
     * @return Mapped record (map of database column to value)
     */
    std::map<std::string, std::string> mapRowToDatabase(const std::map<std::string, std::string>& row, FileType fileType) const;

    /**
     * Check if record exists in database
     * @param conn Database connection
     * @param record Mapped record
     * @param fileType File type
     * @return true if exists, false if new
     */
    bool recordExists(void* conn, const std::map<std::string, std::string>& record, FileType fileType) const;

    /**
     * Insert new records into database
     * @param conn Database connection
     * @param records Vector of new records to insert
     * @param fileType File type
     * @return Number of records inserted
     */
    size_t insertRecords(void* conn, const std::vector<std::map<std::string, std::string>>& records, FileType fileType) const;

    /**
     * Parse timestamp string to PostgreSQL format
     * @param timestampStr Timestamp string from CSV
     * @return PostgreSQL-formatted timestamp string
     */
    std::string parseTimestamp(const std::string& timestampStr) const;
    
    /**
     * Escape string for PostgreSQL
     * @param str String to escape
     * @return Escaped string
     */
    std::string escapeString(const std::string& str) const;

    /**
     * Deduplicate CSV rows (remove duplicates within the CSV file)
     * Uses a hash of all fields to identify duplicates
     * @param records Vector of records to deduplicate
     * @param fileType File type
     * @return Deduplicated vector of records
     */
    std::vector<std::map<std::string, std::string>> deduplicateCSVRows(
        const std::vector<std::map<std::string, std::string>>& records, 
        FileType fileType) const;

    /**
     * Batch check for existing records in database using temporary table
     * Much faster than checking one-by-one
     * @param conn Database connection
     * @param records Vector of records to check
     * @param fileType File type
     * @return Set of indices of records that already exist in database
     */
    std::set<size_t> checkDuplicatesBatch(
        void* conn, 
        const std::vector<std::map<std::string, std::string>>& records, 
        FileType fileType) const;
};

#endif // DATABASE_LOADER_H

