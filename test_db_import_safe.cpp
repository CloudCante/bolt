#include "DatabaseLoader.h"
#include "DataProcessor.h"
#include "FileConverter.h"
#include "FileTypeDetector.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <string>
#include <libpq-fe.h>

namespace fs = std::filesystem;

// Create a small test CSV with only first 10 rows
bool createSmallTestCSV(const std::string& inputFile, const std::string& outputFile, size_t maxRows = 10) {
    std::ifstream in(inputFile);
    if (!in.is_open()) {
        std::cerr << "Failed to open input CSV: " << inputFile << std::endl;
        return false;
    }
    
    std::ofstream out(outputFile);
    if (!out.is_open()) {
        std::cerr << "Failed to create output CSV: " << outputFile << std::endl;
        in.close();
        return false;
    }
    
    // Copy header
    std::string line;
    if (!std::getline(in, line)) {
        std::cerr << "Input CSV is empty" << std::endl;
        in.close();
        out.close();
        return false;
    }
    out << line << "\n";
    
    // Copy first maxRows data rows
    size_t rowCount = 0;
    while (std::getline(in, line) && rowCount < maxRows) {
        if (!line.empty()) {
            out << line << "\n";
            rowCount++;
        }
    }
    
    in.close();
    out.close();
    std::cout << "   Created test CSV with " << rowCount << " rows" << std::endl;
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv_file_path> [max_rows=10]" << std::endl;
        return 1;
    }
    
    std::string csvFile = argv[1];
    size_t maxRows = 10;
    if (argc >= 3) {
        maxRows = std::stoul(argv[2]);
    }
    
    if (!fs::exists(csvFile)) {
        std::cerr << "File not found: " << csvFile << std::endl;
        return 1;
    }
    
    std::cout << "=" << std::string(80, '=') << std::endl;
    std::cout << "SAFE DATABASE IMPORT TEST - OPERATOR COLUMN (Limited to " << maxRows << " rows)" << std::endl;
    std::cout << "=" << std::string(80, '=') << std::endl;
    std::cout << "File: " << csvFile << std::endl << std::endl;
    
    // Create a small test file to avoid memory issues
    std::string smallTestFile = csvFile + ".small_test";
    std::cout << "[0] Creating small test CSV (" << maxRows << " rows)..." << std::endl;
    if (!createSmallTestCSV(csvFile, smallTestFile, maxRows)) {
        std::cerr << "   ✗ Failed to create test CSV" << std::endl;
        return 1;
    }
    csvFile = smallTestFile;
    
    // Step 1: Clean CSV
    std::string filename = fs::path(csvFile).filename().string();
    FileType fileType = FileTypeDetector::detect(filename);
    
    std::cout << "\n[1] File type: " << FileTypeDetector::toString(fileType) << std::endl;
    
    if (fileType == FileType::WORKSTATION || fileType == FileType::TESTBOARD) {
        DataProcessor processor;
        std::cout << "[2] Cleaning CSV columns..." << std::endl;
        
        std::string cleanedFile = csvFile + ".cleaned";
        fs::copy_file(csvFile, cleanedFile, fs::copy_options::overwrite_existing);
        
        if (processor.cleanCSV(cleanedFile, fileType)) {
            auto stats = processor.getLastCleanStats();
            std::cout << "   ✓ Cleaned: " << stats.originalColumns 
                      << " -> " << stats.cleanedColumns 
                      << " columns" << std::endl;
            csvFile = cleanedFile;
        } else {
            std::cerr << "   ✗ Cleaning failed: " << processor.getLastError() << std::endl;
            return 1;
        }
    }
    
    // Step 2: Read CSV values before import
    std::cout << "\n[3] Reading operator values from CSV..." << std::endl;
    std::vector<std::string> csvOperatorValues;
    std::vector<std::string> csvSNValues;
    
    std::ifstream file(csvFile);
    if (!file.is_open()) {
        std::cerr << "   ✗ Failed to open CSV" << std::endl;
        return 1;
    }
    
    // Parse header
    std::string headerLine;
    std::getline(file, headerLine);
    
    std::vector<std::string> headers;
    std::string currentField;
    bool inQuotes = false;
    
    auto normalizeColumnName = [](const std::string& name) -> std::string {
        std::string normalized = name;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        std::replace(normalized.begin(), normalized.end(), ' ', '_');
        std::replace(normalized.begin(), normalized.end(), '-', '_');
        return normalized;
    };
    
    for (char c : headerLine) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            currentField.erase(0, currentField.find_first_not_of(" \t\n\r"));
            size_t last = currentField.find_last_not_of(" \t\n\r");
            if (last != std::string::npos) {
                currentField.erase(last + 1);
            }
            if (!currentField.empty()) {
                headers.push_back(normalizeColumnName(currentField));
            }
            currentField.clear();
        } else {
            currentField += c;
        }
    }
    if (!currentField.empty()) {
        currentField.erase(0, currentField.find_first_not_of(" \t\n\r"));
        size_t last = currentField.find_last_not_of(" \t\n\r");
        if (last != std::string::npos) {
            currentField.erase(last + 1);
        }
        if (!currentField.empty()) {
            headers.push_back(normalizeColumnName(currentField));
        }
    }
    
    size_t operatorIdx = SIZE_MAX;
    size_t snIdx = SIZE_MAX;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == "operator") operatorIdx = i;
        if (headers[i] == "sn") snIdx = i;
    }
    
    if (operatorIdx == SIZE_MAX || snIdx == SIZE_MAX) {
        std::cerr << "   ✗ operator or sn column not found" << std::endl;
        return 1;
    }
    
    // Read rows
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> row;
        currentField.clear();
        inQuotes = false;
        size_t colIndex = 0;
        
        for (char c : line) {
            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ',' && !inQuotes) {
                if (colIndex < headers.size()) {
                    row.push_back(currentField);
                }
                currentField.clear();
                colIndex++;
            } else {
                currentField += c;
            }
        }
        if (colIndex < headers.size()) {
            row.push_back(currentField);
        }
        
        if (operatorIdx < row.size() && snIdx < row.size()) {
            std::string op = row[operatorIdx];
            std::string sn = row[snIdx];
            
            // Trim
            auto trim = [](std::string& s) {
                size_t first = s.find_first_not_of(" \t\n\r");
                if (first != std::string::npos) {
                    s.erase(0, first);
                    size_t last = s.find_last_not_of(" \t\n\r");
                    if (last != std::string::npos) {
                        s.erase(last + 1);
                    } else {
                        s.clear();
                    }
                } else {
                    s.clear();
                }
            };
            
            trim(op);
            trim(sn);
            
            csvOperatorValues.push_back(op);
            csvSNValues.push_back(sn);
            
            std::cout << "   Row " << std::setw(2) << csvOperatorValues.size() 
                      << ": SN=" << sn << " operator='" << op << "'" << std::endl;
        }
    }
    file.close();
    
    // Step 3: Import to database (only small file, so safe)
    std::cout << "\n[4] Importing to database (" << csvOperatorValues.size() << " rows)..." << std::endl;
    DatabaseLoader::DatabaseConfig dbConfig;
    dbConfig.host = "localhost";
    dbConfig.port = 5432;
    dbConfig.database = "fox_db";
    dbConfig.user = "gpu_user";
    dbConfig.password = "";
    
    DatabaseLoader loader(dbConfig);
    
    if (!loader.importCSV(csvFile, fileType)) {
        std::cerr << "   ✗ Import failed: " << loader.getLastError() << std::endl;
        return 1;
    }
    
    auto stats = loader.getLastImportStats();
    std::cout << "   ✓ Import complete:" << std::endl;
    std::cout << "     Total rows: " << stats.totalRows << std::endl;
    std::cout << "     New rows: " << stats.newRows << std::endl;
    std::cout << "     Existing rows: " << stats.existingRows << std::endl;
    std::cout << "     Inserted: " << stats.insertedRows << std::endl;
    
    // Step 4: Query database
    std::cout << "\n[5] Querying database (matching by SN)..." << std::endl;
    
    PGconn* conn = PQconnectdb("host=localhost port=5432 dbname=fox_db user=gpu_user password=");
    if (PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "   ✗ Database connection failed: " << PQerrorMessage(conn) << std::endl;
        PQfinish(conn);
        return 1;
    }
    
    std::vector<std::string> dbOperatorValues;
    std::string tableName = (fileType == FileType::WORKSTATION) ? "workstation_master_log" : "testboard_master_log";
    
    for (size_t i = 0; i < csvSNValues.size(); ++i) {
        std::string query = "SELECT operator FROM " + tableName + " WHERE sn = $1 LIMIT 1";
        const char* paramValues[] = { csvSNValues[i].c_str() };
        int paramLengths[] = { static_cast<int>(csvSNValues[i].length()) };
        int paramFormats[] = { 0 };
        
        PGresult* res = PQexecParams(conn, query.c_str(), 1, nullptr, paramValues, paramLengths, paramFormats, 0);
        
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            const char* op = PQgetvalue(res, 0, 0);
            dbOperatorValues.push_back(op ? op : "");
        } else {
            dbOperatorValues.push_back("");
        }
        
        PQclear(res);  // CRITICAL: Always clear result
    }
    
    PQfinish(conn);  // CRITICAL: Always close connection
    
    // Step 5: Compare
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "COMPARISON: CSV vs DATABASE" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    
    size_t matchCount = 0;
    size_t emptyInDbCount = 0;
    
    for (size_t i = 0; i < csvOperatorValues.size() && i < dbOperatorValues.size(); ++i) {
        std::cout << "Row " << std::setw(2) << (i + 1) << " (SN=" << csvSNValues[i] << "): ";
        
        if (dbOperatorValues[i].empty()) {
            emptyInDbCount++;
            std::cout << "CSV='" << csvOperatorValues[i] << "' -> DB='' ⚠ EMPTY!" << std::endl;
        } else if (csvOperatorValues[i] == dbOperatorValues[i]) {
            matchCount++;
            std::cout << "CSV='" << csvOperatorValues[i] << "' -> DB='" << dbOperatorValues[i] << "' ✓" << std::endl;
        } else {
            std::cout << "CSV='" << csvOperatorValues[i] << "' -> DB='" << dbOperatorValues[i] << "' ⚠ MISMATCH!" << std::endl;
        }
    }
    
    std::cout << "\nSUMMARY: " << matchCount << " matches, " << emptyInDbCount << " empty in DB" << std::endl;
    
    // Cleanup
    try {
        fs::remove(smallTestFile);
        if (csvFile != smallTestFile) {
            fs::remove(csvFile);
        }
    } catch (...) {
        // Ignore cleanup errors
    }
    
    return (emptyInDbCount > 0) ? 1 : 0;
}

