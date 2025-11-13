#ifndef DATA_PROCESSOR_H
#define DATA_PROCESSOR_H

#include "FileTypeDetector.h"
#include <string>
#include <vector>
#include <set>
#include <memory>

/**
 * DataProcessor - Cleans CSV files by removing unused columns
 * 
 * Role: ADC/DPS - Processes CSV data and removes columns not needed for mapping
 * Only Job: Clean CSV files by filtering out unused columns based on file type
 * 
 * Usage:
 *   DataProcessor processor;
 *   if (processor.cleanCSV("/path/to/file.csv", FileType::WORKSTATION)) {
 *       // CSV cleaned successfully
 *   }
 */
class DataProcessor {
public:
    /**
     * Constructor
     */
    DataProcessor();

    /**
     * Destructor
     */
    ~DataProcessor();

    /**
     * Clean CSV file by removing unused columns
     * Reads CSV, filters columns based on file type, writes back cleaned CSV
     * 
     * @param csvFilePath Path to CSV file to clean
     * @param fileType Type of file (WORKSTATION, TESTBOARD, etc.)
     * @return true if successful, false on error
     */
    bool cleanCSV(const std::string& csvFilePath, FileType fileType);

    /**
     * Get last error message
     * @return Error description or empty string if no error
     */
    std::string getLastError() const;

    /**
     * Get statistics about last cleaning operation
     */
    struct CleanStats {
        size_t originalColumns;
        size_t cleanedColumns;
        size_t removedColumns;
        size_t originalRows;
        size_t cleanedRows;
    };

    CleanStats getLastCleanStats() const;

private:
    /**
     * Normalize column name (lowercase, replace spaces/dashes with underscores)
     * @param colName Original column name
     * @return Normalized column name
     */
    std::string normalizeColumnName(const std::string& colName) const;

    /**
     * Get set of columns to keep for a specific file type
     * @param fileType File type
     * @return Set of column names to keep (normalized)
     */
    std::set<std::string> getColumnsToKeep(FileType fileType) const;

    /**
     * Parse CSV header line
     * @param headerLine CSV header line
     * @return Vector of column names
     */
    std::vector<std::string> parseCSVHeader(const std::string& headerLine) const;

    /**
     * Parse CSV row (handles quoted fields)
     * @param rowLine CSV row line
     * @return Vector of field values
     */
    std::vector<std::string> parseCSVRow(const std::string& rowLine) const;

    /**
     * Write cleaned CSV to file
     * @param outputPath Output file path
     * @param headerColumns Columns to write in header
     * @param columnIndices Indices of columns to keep from original
     * @param inputPath Original CSV file path
     * @return true if successful
     */
    bool writeCleanedCSV(const std::string& outputPath,
                        const std::vector<std::string>& headerColumns,
                        const std::vector<size_t>& columnIndices,
                        const std::string& inputPath) const;

    // Error tracking
    mutable std::string lastError_;

    // Statistics from last operation
    mutable CleanStats lastStats_;
};

#endif // DATA_PROCESSOR_H

