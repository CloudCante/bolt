#include "DataProcessor.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

DataProcessor::DataProcessor() {
    lastStats_ = {0, 0, 0, 0, 0};
}

DataProcessor::~DataProcessor() {
}

std::string DataProcessor::normalizeColumnName(const std::string& colName) const {
    std::string normalized = colName;
    
    // Convert to lowercase
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    // Replace spaces and dashes with underscores
    std::replace(normalized.begin(), normalized.end(), ' ', '_');
    std::replace(normalized.begin(), normalized.end(), '-', '_');
    
    return normalized;
}

std::set<std::string> DataProcessor::getColumnsToKeep(FileType fileType) const {
    std::set<std::string> columns;
    
    switch (fileType) {
        case FileType::WORKSTATION: {
            // Columns used in workstation_master_log mapping
            // Any column NOT in this list will be automatically removed (e.g., 'day', 'tat', 'outbound_version')
            columns.insert("sn");
            columns.insert("pn");
            columns.insert("customer_pn");
            columns.insert("workstation_name");
            columns.insert("history_station_start_time");
            columns.insert("history_station_end_time");
            columns.insert("hours");
            columns.insert("service_flow");
            columns.insert("model");
            columns.insert("history_station_passing_status");
            columns.insert("passing_station_method");
            columns.insert("operator");
            // Note: 'first_station_start_time' column has been removed from database
            // Note: 'data_source' is added programmatically, not from CSV
            break;
        }
        
        case FileType::TESTBOARD: {
            // Columns used in testboard_master_log mapping
            columns.insert("sn");
            columns.insert("pn");
            columns.insert("model");
            columns.insert("work_station_process");
            columns.insert("baseboard_sn");
            columns.insert("baseboard_pn");
            columns.insert("workstation_name");
            columns.insert("history_station_start_time");
            columns.insert("history_station_end_time");
            columns.insert("history_station_passing_status");
            columns.insert("operator");
            columns.insert("failure_reasons");
            columns.insert("failure_note");
            columns.insert("failure_code");
            columns.insert("diag_version");
            columns.insert("fixture_no");
            // Note: 'data_source' is added programmatically, not from CSV
            break;
        }
        
        case FileType::SNFN: {
            // SNFN is now an aggregation, but if we still need to process it:
            columns.insert("workstation_name");
            columns.insert("fixture_no");
            columns.insert("error_code");
            columns.insert("error_disc");
            columns.insert("model");
            columns.insert("sn");
            columns.insert("pn");
            columns.insert("history_station_start_time");
            columns.insert("history_station_end_time");
            break;
        }
        
        case FileType::UNKNOWN:
        default:
            // For unknown types, keep all columns (don't filter)
            break;
    }
    
    return columns;
}

std::vector<std::string> DataProcessor::parseCSVHeader(const std::string& headerLine) const {
    return parseCSVRow(headerLine);
}

std::vector<std::string> DataProcessor::parseCSVRow(const std::string& rowLine) const {
    std::vector<std::string> fields;
    std::stringstream ss(rowLine);
    std::string field;
    bool inQuotes = false;
    char currentChar;
    
    // Simple CSV parser that handles quoted fields
    for (size_t i = 0; i < rowLine.length(); ++i) {
        currentChar = rowLine[i];
        
        if (currentChar == '"') {
            inQuotes = !inQuotes;
        } else if (currentChar == ',' && !inQuotes) {
            fields.push_back(field);
            field.clear();
        } else {
            field += currentChar;
        }
    }
    
    // Add last field
    if (!field.empty() || rowLine.back() == ',') {
        fields.push_back(field);
    }
    
    return fields;
}

bool DataProcessor::writeCleanedCSV(const std::string& outputPath,
                                    const std::vector<std::string>& headerColumns,
                                    const std::vector<size_t>& columnIndices,
                                    const std::string& inputPath) const {
    std::ifstream inputFile(inputPath);
    if (!inputFile.is_open()) {
        lastError_ = "Failed to open input file: " + inputPath;
        return false;
    }
    
    std::ofstream outputFile(outputPath);
    if (!outputFile.is_open()) {
        lastError_ = "Failed to create output file: " + outputPath;
        inputFile.close();
        return false;
    }
    
    // Write header
    for (size_t i = 0; i < headerColumns.size(); ++i) {
        if (i > 0) outputFile << ",";
        outputFile << headerColumns[i];
    }
    outputFile << "\n";
    
    // Skip original header line
    std::string line;
    if (!std::getline(inputFile, line)) {
        lastError_ = "Input file is empty or has no header";
        inputFile.close();
        outputFile.close();
        return false;
    }
    
    // Process data rows
    size_t rowCount = 0;
    while (std::getline(inputFile, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> row = parseCSVRow(line);
        
        // Write only selected columns
        for (size_t i = 0; i < columnIndices.size(); ++i) {
            if (i > 0) outputFile << ",";
            
            size_t colIndex = columnIndices[i];
            std::string field;
            if (colIndex < row.size()) {
                field = row[colIndex];
            } else {
                // Row is shorter than expected - write empty string to maintain alignment
                field = "";
            }
            
            // Escape field if it contains comma or quote
            if (field.find(',') != std::string::npos || 
                field.find('"') != std::string::npos ||
                field.find('\n') != std::string::npos) {
                // Escape quotes and wrap in quotes
                std::string escaped;
                escaped += '"';
                for (char c : field) {
                    if (c == '"') escaped += '"';  // Double quote
                    escaped += c;
                }
                escaped += '"';
                outputFile << escaped;
            } else {
                outputFile << field;
            }
        }
        outputFile << "\n";
        rowCount++;
    }
    
    inputFile.close();
    outputFile.close();
    
    // Update stats
    lastStats_.cleanedRows = rowCount;
    
    return true;
}

bool DataProcessor::cleanCSV(const std::string& csvFilePath, FileType fileType) {
    lastError_.clear();
    lastStats_ = {0, 0, 0, 0, 0};
    
    // Validate file exists
    if (!fs::exists(csvFilePath)) {
        lastError_ = "CSV file does not exist: " + csvFilePath;
        return false;
    }
    
    // For UNKNOWN file types, don't clean (keep all columns)
    if (fileType == FileType::UNKNOWN) {
        std::cout << "[DataProcessor] Unknown file type, skipping column cleaning" << std::endl;
        return true;
    }
    
    // Open and read header
    std::ifstream file(csvFilePath);
    if (!file.is_open()) {
        lastError_ = "Failed to open CSV file: " + csvFilePath;
        return false;
    }
    
    std::string headerLine;
    if (!std::getline(file, headerLine)) {
        lastError_ = "CSV file is empty or has no header";
        file.close();
        return false;
    }
    file.close();
    
    // Parse header
    std::vector<std::string> originalColumns = parseCSVHeader(headerLine);
    lastStats_.originalColumns = originalColumns.size();
    lastStats_.originalRows = 0;  // Will be updated in writeCleanedCSV
    
    // Normalize column names
    std::vector<std::string> normalizedColumns;
    for (const auto& col : originalColumns) {
        normalizedColumns.push_back(normalizeColumnName(col));
    }
    
    // Get columns to keep for this file type
    std::set<std::string> columnsToKeep = getColumnsToKeep(fileType);
    
    // Find indices of columns to keep
    std::vector<std::string> keptColumns;
    std::vector<size_t> keptIndices;
    
    for (size_t i = 0; i < normalizedColumns.size(); ++i) {
        const std::string& normCol = normalizedColumns[i];
        
        // Keep column if it's in our "to keep" list
        if (columnsToKeep.find(normCol) != columnsToKeep.end()) {
            keptColumns.push_back(normalizedColumns[i]);  // Use normalized name
            keptIndices.push_back(i);
        }
    }
    
    lastStats_.cleanedColumns = keptColumns.size();
    lastStats_.removedColumns = originalColumns.size() - keptColumns.size();
    
    // If no columns were removed, nothing to do
    if (lastStats_.removedColumns == 0) {
        std::cout << "[DataProcessor] No columns to remove, file already clean" << std::endl;
        return true;
    }
    
    std::cout << "[DataProcessor] Cleaning CSV: " << fs::path(csvFilePath).filename().string() << std::endl;
    std::cout << "[DataProcessor]   Original columns: " << lastStats_.originalColumns << std::endl;
    std::cout << "[DataProcessor]   Kept columns: " << lastStats_.cleanedColumns << std::endl;
    std::cout << "[DataProcessor]   Removed columns: " << lastStats_.removedColumns << std::endl;
    
    // Create temporary file path
    std::string tempPath = csvFilePath + ".tmp";
    
    // Write cleaned CSV to temporary file
    if (!writeCleanedCSV(tempPath, keptColumns, keptIndices, csvFilePath)) {
        return false;
    }
    
    // Replace original file with cleaned version
    try {
        fs::rename(tempPath, csvFilePath);
    } catch (const fs::filesystem_error& e) {
        lastError_ = "Failed to replace original file: " + std::string(e.what());
        // Clean up temp file
        try {
            fs::remove(tempPath);
        } catch (...) {
            // Ignore cleanup errors
        }
        return false;
    }
    
    std::cout << "[DataProcessor]   ✓ CSV cleaned successfully" << std::endl;
    
    return true;
}

std::string DataProcessor::getLastError() const {
    return lastError_;
}

DataProcessor::CleanStats DataProcessor::getLastCleanStats() const {
    return lastStats_;
}

