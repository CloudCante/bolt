#include "DatabaseLoader.h"
#include "DataProcessor.h"
#include "FileConverter.h"
#include "FileTypeDetector.h"
#include <iostream>
#include <filesystem>

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
    
    std::cout << "Testing import process for: " << csvFile << std::endl;
    std::cout << "=" << std::string(80, '=') << std::endl;
    
    // Step 1: Clean CSV (simulate what InputProcessor does)
    std::string filename = fs::path(csvFile).filename().string();
    FileType fileType = FileTypeDetector::detect(filename);
    
    std::cout << "\n[1] File type detected: " << FileTypeDetector::toString(fileType) << std::endl;
    
    if (fileType == FileType::WORKSTATION || fileType == FileType::TESTBOARD) {
        DataProcessor processor;
        std::cout << "[2] Cleaning CSV columns..." << std::endl;
        
        // Make a copy first
        std::string cleanedFile = csvFile + ".cleaned";
        fs::copy_file(csvFile, cleanedFile, fs::copy_options::overwrite_existing);
        
        if (processor.cleanCSV(cleanedFile, fileType)) {
            auto stats = processor.getLastCleanStats();
            std::cout << "   ✓ Cleaned CSV: " << stats.originalColumns 
                      << " -> " << stats.cleanedColumns 
                      << " columns (removed " << stats.removedColumns << ")" << std::endl;
            
            // Now test parsing with cleaned file
            csvFile = cleanedFile;
        } else {
            std::cerr << "   ✗ Cleaning failed: " << processor.getLastError() << std::endl;
            return 1;
        }
    }
    
    // Step 2: Parse CSV
    std::cout << "\n[3] Parsing CSV..." << std::endl;
    DatabaseLoader loader;
    auto rows = loader.parseCSV(csvFile);
    
    if (rows.empty()) {
        std::cerr << "   ✗ Failed to parse CSV: " << loader.getLastError() << std::endl;
        return 1;
    }
    
    std::cout << "   ✓ Parsed " << rows.size() << " rows" << std::endl;
    
    // Step 3: Check first few rows for operator column
    std::cout << "\n[4] Checking operator column in first 5 rows:" << std::endl;
    size_t rowsToCheck = std::min(rows.size(), size_t(5));
    
    for (size_t i = 0; i < rowsToCheck; ++i) {
        const auto& row = rows[i];
        std::cout << "\n   Row " << (i + 1) << ":" << std::endl;
        
        if (row.count("operator")) {
            std::string operatorValue = row.at("operator");
            std::cout << "     operator: '" << operatorValue << "'" << std::endl;
            if (operatorValue.empty()) {
                std::cout << "     ⚠ WARNING: operator is EMPTY!" << std::endl;
            }
        } else {
            std::cout << "     ✗ ERROR: 'operator' key NOT FOUND in row!" << std::endl;
            std::cout << "     Available keys: ";
            for (const auto& pair : row) {
                std::cout << pair.first << " ";
            }
            std::cout << std::endl;
        }
    }
    
    // Step 4: Test mapping
    std::cout << "\n[5] Testing mapRowToDatabase() for first row:" << std::endl;
    if (!rows.empty()) {
        auto mapped = loader.mapRowToDatabase(rows[0], fileType);
        
        if (mapped.count("operator")) {
            std::string operatorValue = mapped.at("operator");
            std::cout << "     operator: '" << operatorValue << "'" << std::endl;
            if (operatorValue.empty()) {
                std::cout << "     ⚠ WARNING: operator is EMPTY after mapping!" << std::endl;
            }
        } else {
            std::cout << "     ✗ ERROR: 'operator' key NOT FOUND in mapped row!" << std::endl;
        }
    }
    
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "Test complete!" << std::endl;
    
    return 0;
}

