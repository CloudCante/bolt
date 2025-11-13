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
    std::cout << "TESTING OPERATOR COLUMN IMPORT" << std::endl;
    std::cout << "=" << std::string(80, '=') << std::endl;
    std::cout << "File: " << csvFile << std::endl << std::endl;
    
    // Step 1: Clean CSV (simulate what InputProcessor does)
    std::string filename = fs::path(csvFile).filename().string();
    FileType fileType = FileTypeDetector::detect(filename);
    
    std::cout << "[1] File type: " << FileTypeDetector::toString(fileType) << std::endl;
    
    if (fileType == FileType::WORKSTATION || fileType == FileType::TESTBOARD) {
        DataProcessor processor;
        std::cout << "[2] Cleaning CSV columns..." << std::endl;
        
        // Make a copy to clean
        std::string cleanedFile = csvFile + ".cleaned_test";
        fs::copy_file(csvFile, cleanedFile, fs::copy_options::overwrite_existing);
        
        if (processor.cleanCSV(cleanedFile, fileType)) {
            auto stats = processor.getLastCleanStats();
            std::cout << "   ✓ Cleaned: " << stats.originalColumns 
                      << " -> " << stats.cleanedColumns 
                      << " columns (removed " << stats.removedColumns << ")" << std::endl;
            
            csvFile = cleanedFile;
        } else {
            std::cerr << "   ✗ Cleaning failed: " << processor.getLastError() << std::endl;
            return 1;
        }
    }
    
    // Step 2: Parse CSV using DatabaseLoader's private method via importCSV
    // Actually, let's use importCSV but we need to check the database or logs
    // For now, let's manually parse to check
    
    std::cout << "\n[3] Parsing CSV and checking operator column..." << std::endl;
    
    // Read the cleaned CSV manually to verify
    std::ifstream file(csvFile);
    if (!file.is_open()) {
        std::cerr << "   ✗ Failed to open cleaned CSV" << std::endl;
        return 1;
    }
    
    // Read header
    std::string headerLine;
    std::getline(file, headerLine);
    
    // Parse header to find operator index
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
    // Last field
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
    
    // Find operator column index
    size_t operatorIdx = SIZE_MAX;
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == "operator") {
            operatorIdx = i;
            break;
        }
    }
    
    if (operatorIdx == SIZE_MAX) {
        std::cerr << "   ✗ ERROR: 'operator' column NOT FOUND in cleaned CSV!" << std::endl;
        std::cout << "   Available columns: ";
        for (const auto& h : headers) {
            std::cout << h << " ";
        }
        std::cout << std::endl;
        return 1;
    }
    
    std::cout << "   ✓ Found 'operator' column at index " << operatorIdx << std::endl;
    
    // Read and check first 10 rows
    std::cout << "\n[4] Checking operator values in first 10 rows:" << std::endl;
    std::string line;
    size_t rowNum = 0;
    size_t emptyCount = 0;
    size_t nonEmptyCount = 0;
    
    while (std::getline(file, line) && rowNum < 10) {
        if (line.empty()) continue;
        
        // Parse row
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
        // Last field
        if (colIndex < headers.size()) {
            row.push_back(currentField);
        }
        
        // Check operator value
        if (operatorIdx < row.size()) {
            std::string operatorValue = row[operatorIdx];
            // Trim whitespace
            size_t first = operatorValue.find_first_not_of(" \t\n\r");
            if (first != std::string::npos) {
                operatorValue.erase(0, first);
                size_t last = operatorValue.find_last_not_of(" \t\n\r");
                if (last != std::string::npos) {
                    operatorValue.erase(last + 1);
                } else {
                    operatorValue.clear();
                }
            } else {
                operatorValue.clear();
            }
            
            if (operatorValue.empty()) {
                emptyCount++;
                std::cout << "   Row " << std::setw(2) << (rowNum + 1) << ": ✗ EMPTY" << std::endl;
            } else {
                nonEmptyCount++;
                std::cout << "   Row " << std::setw(2) << (rowNum + 1) << ": ✓ '" << operatorValue << "'" << std::endl;
            }
        } else {
            emptyCount++;
            std::cout << "   Row " << std::setw(2) << (rowNum + 1) << ": ✗ MISSING (row too short)" << std::endl;
        }
        
        rowNum++;
    }
    
    file.close();
    
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "SUMMARY:" << std::endl;
    std::cout << "  Total rows checked: " << rowNum << std::endl;
    std::cout << "  Non-empty operator values: " << nonEmptyCount << std::endl;
    std::cout << "  Empty/missing operator values: " << emptyCount << std::endl;
    
    if (emptyCount > 0) {
        std::cout << "\n  ⚠ WARNING: Found " << emptyCount << " empty operator values!" << std::endl;
        return 1;
    } else {
        std::cout << "\n  ✓ SUCCESS: All operator values are present!" << std::endl;
        return 0;
    }
}

