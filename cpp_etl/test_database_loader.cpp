#include "DatabaseLoader.h"
#include "FileTypeDetector.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_csv_file>" << std::endl;
        std::cerr << "Example: " << argv[0] << " /home/cloud/projects/new_etl/queue/workstationOutputReport.csv" << std::endl;
        return 1;
    }
    
    std::string csvPath = argv[1];
    
    if (!fs::exists(csvPath)) {
        std::cerr << "Error: CSV file does not exist: " << csvPath << std::endl;
        return 1;
    }
    
    std::cout << "=== Testing DatabaseLoader ===" << std::endl;
    std::cout << "CSV file: " << csvPath << std::endl;
    
    // Detect file type from filename
    std::string filename = fs::path(csvPath).filename().string();
    FileType fileType = FileTypeDetector::detect(filename);
    
    if (fileType == FileType::UNKNOWN) {
        std::cerr << "Error: Could not detect file type from filename: " << filename << std::endl;
        std::cerr << "Please ensure filename contains 'workstation' or 'testboard'" << std::endl;
        return 1;
    }
    
    std::cout << "Detected file type: " << FileTypeDetector::toString(fileType) << std::endl;
    
    // Create DatabaseLoader with default config (from config.py)
    DatabaseLoader::DatabaseConfig config;
    config.host = "localhost";
    config.port = 5432;
    config.database = "fox_db";
    config.user = "gpu_user";
    config.password = "";
    
    DatabaseLoader loader(config);
    
    std::cout << "\n--- Importing CSV to Database ---" << std::endl;
    std::cout << "Database: " << config.database << "@" << config.host << ":" << config.port << std::endl;
    std::cout << "User: " << config.user << std::endl;
    
    // Import CSV
    if (!loader.importCSV(csvPath, fileType)) {
        std::cerr << "\n❌ Import failed: " << loader.getLastError() << std::endl;
        return 1;
    }
    
    // Show statistics
    auto stats = loader.getLastImportStats();
    std::cout << "\n--- Import Statistics ---" << std::endl;
    std::cout << "Total rows in CSV: " << stats.totalRows << std::endl;
    std::cout << "Existing records: " << stats.existingRows << std::endl;
    std::cout << "New records: " << stats.newRows << std::endl;
    std::cout << "Inserted records: " << stats.insertedRows << std::endl;
    std::cout << "Success: " << (stats.success ? "Yes" : "No") << std::endl;
    
    if (stats.existingRows > 0) {
        double duplicatePercentage = (static_cast<double>(stats.existingRows) / stats.totalRows) * 100.0;
        std::cout << "\n📊 Duplicate rate: " << duplicatePercentage << "% (" 
                  << stats.existingRows << " of " << stats.totalRows << " records already existed)" << std::endl;
    }
    
    if (stats.insertedRows > 0) {
        std::cout << "\n✅ Successfully inserted " << stats.insertedRows << " new records" << std::endl;
    } else if (stats.existingRows == stats.totalRows) {
        std::cout << "\n✅ All records already exist in database (no duplicates inserted)" << std::endl;
    }
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    
    return 0;
}

