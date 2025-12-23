#include "InputProcessor.h"
#include <iostream>
#include <filesystem>
#include <cstring>
#include <unistd.h>
#include <ctime>
#include <cerrno>

namespace fs = std::filesystem;

InputProcessor::InputProcessor(const std::string& inputDir, const std::string& queueDir)
    : inputDir_(inputDir)
    , queueDir_(queueDir)
    , stats_{0, 0, 0, 0}
{
}

bool InputProcessor::processFile(const std::string& filepath) {
    lastError_.clear();
    
    // Validate file exists
    if (access(filepath.c_str(), F_OK) != 0) {
        lastError_ = "Cannot access: " + filepath + " (" + std::string(std::strerror(errno))+")";
        stats_.filesFailed++;
        return false;
    }
    
    // Extract filename
    fs::path path(filepath);
    std::string filename = path.filename().string();
    
    // Determine if we need conversion
    bool needsConversion = FileConverter::isXlsFile(filepath) || FileConverter::isXlsxFile(filepath);
    bool isCsv = FileConverter::isCsvFile(filepath);
    
    if (!needsConversion && !isCsv) {
        lastError_ = "File is not .xls, .xlsx, or .csv: " + filepath;
        stats_.filesFailed++;
        return false;
    }
    
    std::string finalFilePath;
    
    if (needsConversion) {
        // Convert .xls or .xlsx to CSV
        std::cout << "[InputProcessor] Converting " << filename << " to CSV..." << std::endl;
        
        // Convert to CSV (temporarily in input dir, then we'll move it)
        std::string convertedPath = converter_.convertToCSV(filepath, inputDir_);
        
        if (convertedPath.empty()) {
            lastError_ = "Conversion failed: " + converter_.getLastError();
            stats_.filesFailed++;
            return false;
        }
        
        // Check if converted file exists
        if (access(convertedPath.c_str(), F_OK) != 0) {
            lastError_ = "Converted file not found: " + convertedPath;
            stats_.filesFailed++;
            return false;
        }
        
        finalFilePath = convertedPath;
        stats_.filesConverted++;
        
        // Delete original .xls/.xlsx file
        if (!removeFile(filepath)) {
            std::cerr << "[InputProcessor] Warning: Could not delete original file: " 
                      << filepath << std::endl;
            // Continue anyway - converted file exists
        }
    } else {
        // Already .csv, use original path
        finalFilePath = filepath;
    }
    
    // Clean CSV columns (remove unused columns based on file type)
    std::cout << "[InputProcessor] Cleaning CSV columns..." << std::endl;
    FileType fileType = FileTypeDetector::detect(filename);
    
    if (fileType != FileType::UNKNOWN) {
        if (!dataProcessor_.cleanCSV(finalFilePath, fileType)) {
            std::cerr << "[InputProcessor] Warning: CSV cleaning failed: " 
                      << dataProcessor_.getLastError() << std::endl;
            std::cerr << "[InputProcessor] Continuing with uncleaned CSV..." << std::endl;
            // Continue anyway - proceed with uncleaned CSV
        } else {
            auto cleanStats = dataProcessor_.getLastCleanStats();
            if (cleanStats.removedColumns > 0) {
                std::cout << "[InputProcessor]   Removed " << cleanStats.removedColumns 
                          << " unused columns" << std::endl;
            }
        }
    } else {
        std::cerr << "[InputProcessor] Warning: Unknown file type, skipping column cleaning" << std::endl;
    }
    
    // Move file to queue directory with unique filename
    fs::path finalPath(finalFilePath);
    std::string baseFilename = finalPath.filename().string();
    std::string queueFilePath = generateUniqueFilename(baseFilename, queueDir_);
    
    std::cout << "[InputProcessor] Moving " << baseFilename;
    if (queueFilePath != (queueDir_ + "/" + baseFilename)) {
        // File was renamed, show the new name
        fs::path newPath(queueFilePath);
        std::cout << " -> " << newPath.filename().string();
    }
    std::cout << " to queue..." << std::endl;
    
    if (!moveFile(finalFilePath, queueFilePath)) {
        lastError_ = "Failed to move file to queue: " + lastError_;
        stats_.filesFailed++;
        return false;
    }
    
    stats_.filesProcessed++;
    stats_.filesMoved++;
    
    std::cout << "[InputProcessor] Successfully processed: " << filename << std::endl;
    return true;
}

bool InputProcessor::moveFile(const std::string& sourcePath, const std::string& destPath) {
    try {
        // Use filesystem library to move
        fs::rename(sourcePath, destPath);
        return true;
    } catch (const fs::filesystem_error& e) {
        lastError_ = "Filesystem error: " + std::string(e.what());
        return false;
    }
}

bool InputProcessor::removeFile(const std::string& filepath) {
    try {
        return fs::remove(filepath);
    } catch (const fs::filesystem_error& e) {
        lastError_ = "Failed to remove file: " + std::string(e.what());
        return false;
    }
}

std::string InputProcessor::generateUniqueFilename(const std::string& baseFilename, const std::string& destDir) const {
    // Extract name and extension
    fs::path basePath(baseFilename);
    std::string name = basePath.stem().string();  // filename without extension
    std::string extension = basePath.extension().string();  // .csv, .xlsx, etc.
    
    // First, try the original filename
    std::string candidatePath = destDir + "/" + baseFilename;
    if (!fs::exists(candidatePath)) {
        return candidatePath;
    }
    
    // If exists, append (1), (2), etc.
    int counter = 1;
    while (true) {
        std::string newName = name + " (" + std::to_string(counter) + ")" + extension;
        candidatePath = destDir + "/" + newName;
        
        if (!fs::exists(candidatePath)) {
            return candidatePath;
        }
        
        counter++;
        
        // Safety limit (should never hit this)
        if (counter > 10000) {
            // Fallback: use timestamp
            std::time_t now = std::time(nullptr);
            newName = name + "_" + std::to_string(now) + extension;
            return destDir + "/" + newName;
        }
    }
}

