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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv_file_path>" << std::endl;
        return 1;
    }
    
    std::string csvFile = argv[1];
    
    if (!fs::exists(csvFile)) {
        std::cerr << "File not found: " << csvFile << std::endl;
        return 1;
    }
    
    std::cout << "=" << std::string(80, '=') << std::endl;
    std::cout << "TESTING FULL DATABASE IMPORT - OPERATOR COLUMN" << std::endl;
    std::cout << "=" << std::string(80, '=') << std::endl;
    std::cout << "File: " << csvFile << std::endl << std::endl;
    
    // Step 1: Clean CSV first (simulate full pipeline)
    std::string filename = fs::path(csvFile).filename().string();
    FileType fileType = FileTypeDetector::detect(filename);
    
    std::cout << "[1] File type: " << FileTypeDetector::toString(fileType) << std::endl;
    
    if (fileType == FileType::WORKSTATION || fileType == FileType::TESTBOARD) {
        DataProcessor processor;
        std::cout << "[2] Cleaning CSV columns..." << std::endl;
        
        std::string cleanedFile = csvFile + ".db_test";
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
    
    // Step 2: Read CSV values before import to compare
    std::cout << "\n[3] Reading operator values from CSV before import..." << std::endl;
    std::vector<std::string> csvOperatorValues;
    
    std::ifstream file(csvFile);
    if (!file.is_open()) {
        std::cerr << "   ✗ Failed to open CSV" << std::endl;
        return 1;
    }
    
    // Skip header
    std::string headerLine;
    std::getline(file, headerLine);
    
    // Find operator column index
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
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == "operator") {
            operatorIdx = i;
            break;
        }
    }
    
    if (operatorIdx == SIZE_MAX) {
        std::cerr << "   ✗ operator column not found in CSV" << std::endl;
        return 1;
    }
    
    // Read first 10 rows
    std::string line;
    size_t rowCount = 0;
    while (std::getline(file, line) && rowCount < 10) {
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
        
        if (operatorIdx < row.size()) {
            std::string op = row[operatorIdx];
            // Trim
            size_t first = op.find_first_not_of(" \t\n\r");
            if (first != std::string::npos) {
                op.erase(0, first);
                size_t last = op.find_last_not_of(" \t\n\r");
                if (last != std::string::npos) {
                    op.erase(last + 1);
                } else {
                    op.clear();
                }
            } else {
                op.clear();
            }
            csvOperatorValues.push_back(op);
            std::cout << "   Row " << std::setw(2) << (rowCount + 1) << ": '" << op << "'" << std::endl;
        }
        rowCount++;
    }
    file.close();
    
    // Step 3: Store SN values for matching
    std::vector<std::string> csvSNValues;
    file.open(csvFile);
    std::getline(file, headerLine); // Skip header again
    
    // Find SN column index
    size_t snIdx = SIZE_MAX;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == "sn") {
            snIdx = i;
            break;
        }
    }
    
    // Read SN values for first 10 rows
    rowCount = 0;
    while (std::getline(file, line) && rowCount < 10) {
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
        
        if (snIdx < row.size()) {
            std::string sn = row[snIdx];
            size_t first = sn.find_first_not_of(" \t\n\r");
            if (first != std::string::npos) {
                sn.erase(0, first);
                size_t last = sn.find_last_not_of(" \t\n\r");
                if (last != std::string::npos) {
                    sn.erase(last + 1);
                } else {
                    sn.clear();
                }
            } else {
                sn.clear();
            }
            csvSNValues.push_back(sn);
        }
        rowCount++;
    }
    file.close();
    
    // Step 4: Import to database
    std::cout << "\n[4] Importing to database..." << std::endl;
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
    
    // Step 5: Query database to check operator values - MATCH BY SN
    std::cout << "\n[5] Querying database to check operator values (matching by SN)..." << std::endl;
    
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
        int paramFormats[] = { 0 }; // text format
        
        PGresult* res = PQexecParams(conn, query.c_str(), 1, nullptr, paramValues, paramLengths, paramFormats, 0);
        
        if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
            const char* op = PQgetvalue(res, 0, 0);
            std::string operatorStr = op ? op : "";
            dbOperatorValues.push_back(operatorStr);
            
            std::cout << "   Row " << std::setw(2) << (i + 1) << ": SN=" << csvSNValues[i] 
                      << " operator='" << operatorStr << "'";
            if (operatorStr.empty()) {
                std::cout << " ⚠ EMPTY!";
            }
            std::cout << std::endl;
        } else {
            dbOperatorValues.push_back("");
            std::cout << "   Row " << std::setw(2) << (i + 1) << ": SN=" << csvSNValues[i] 
                      << " ⚠ NOT FOUND IN DB!" << std::endl;
        }
        
        PQclear(res);
    }
    
    PQfinish(conn);
    
    // Step 6: Compare CSV vs Database - ROW BY ROW
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "COMPARISON: CSV vs DATABASE (ROW BY ROW MATCHING)" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    
    size_t matchCount = 0;
    size_t mismatchCount = 0;
    size_t emptyInDbCount = 0;
    
    for (size_t i = 0; i < csvOperatorValues.size() && i < dbOperatorValues.size(); ++i) {
        std::cout << "Row " << std::setw(2) << (i + 1) << " (SN=" << csvSNValues[i] << "): ";
        
        if (dbOperatorValues[i].empty()) {
            emptyInDbCount++;
            std::cout << "CSV='" << csvOperatorValues[i] 
                      << "' -> DB='' ⚠ EMPTY IN DB!" << std::endl;
            mismatchCount++;
        } else if (csvOperatorValues[i] == dbOperatorValues[i]) {
            matchCount++;
            std::cout << "CSV='" << csvOperatorValues[i] 
                      << "' -> DB='" << dbOperatorValues[i] << "' ✓ MATCH" << std::endl;
        } else {
            mismatchCount++;
            std::cout << "CSV='" << csvOperatorValues[i] 
                      << "' -> DB='" << dbOperatorValues[i] << "' ⚠ MISMATCH!" << std::endl;
        }
    }
    
    std::cout << "\nSUMMARY:" << std::endl;
    size_t compareCount = std::min(csvOperatorValues.size(), dbOperatorValues.size());
    std::cout << "  Compared rows: " << compareCount << std::endl;
    std::cout << "  Matches: " << matchCount << std::endl;
    std::cout << "  Mismatches: " << mismatchCount << std::endl;
    std::cout << "  Empty in DB: " << emptyInDbCount << std::endl;
    
    if (emptyInDbCount > 0) {
        std::cout << "\n  ✗ ISSUE FOUND: Operator values are EMPTY in database!" << std::endl;
        std::cout << "     This indicates a DATABASE insertion issue, not a code parsing issue." << std::endl;
        return 1;
    } else if (mismatchCount > 0) {
        std::cout << "\n  ⚠ WARNING: Some operator values don't match!" << std::endl;
        return 1;
    } else {
        std::cout << "\n  ✓ SUCCESS: All operator values match between CSV and database!" << std::endl;
        return 0;
    }
}

