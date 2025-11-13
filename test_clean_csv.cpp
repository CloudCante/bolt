#include "DataProcessor.h"
#include "FileTypeDetector.h"
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <csv_file> <output_file>" << std::endl;
        return 1;
    }
    
    std::string inputFile = argv[1];
    std::string outputFile = argv[2];
    
    // Copy input to output first
    fs::copy_file(inputFile, outputFile, fs::copy_options::overwrite_existing);
    
    // Detect file type
    std::string filename = fs::path(inputFile).filename().string();
    FileType fileType = FileTypeDetector::detect(filename);
    
    if (fileType == FileType::UNKNOWN) {
        std::cerr << "Unknown file type" << std::endl;
        return 1;
    }
    
    // Clean CSV
    DataProcessor processor;
    if (!processor.cleanCSV(outputFile, fileType)) {
        std::cerr << "Cleaning failed: " << processor.getLastError() << std::endl;
        return 1;
    }
    
    auto stats = processor.getLastCleanStats();
    std::cout << "Cleaned: " << stats.originalColumns << " -> " 
              << stats.cleanedColumns << " columns (removed " 
              << stats.removedColumns << ")" << std::endl;
    
    return 0;
}
