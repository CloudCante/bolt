#ifndef INPUT_PROCESSOR_H
#define INPUT_PROCESSOR_H

#include "FileConverter.h"
#include "DataProcessor.h"
#include "FileTypeDetector.h"
#include <string>

/**
 * InputProcessor - Processes files in input folder, converts and moves to queue folder
 * 
 * Role: Top Lane (Initial Processing)
 * Only Job: Monitor input/, convert files if needed, move to queue/
 * 
 * Usage:
 *   InputProcessor processor("./input", "./queue");
 *   processor.processFile("/path/to/file.xls");
 *   // File converted to .xlsx and moved to queue/ folder
 */
class InputProcessor {
public:
    /**
     * Constructor
     * @param inputDir Input directory where raw files land
     * @param queueDir Queue directory where converted files go
     */
    InputProcessor(const std::string& inputDir, const std::string& queueDir);

    /**
     * Destructor
     */
    ~InputProcessor() = default;

    // Disable copy
    InputProcessor(const InputProcessor&) = delete;
    InputProcessor& operator=(const InputProcessor&) = delete;

    /**
     * Process a file from input directory
     * - If .xls or .xlsx: Convert to CSV, clean columns, move to queue/
     * - If already .csv: Clean columns, move to queue/
     * 
     * @param filepath Full path to file in input directory
     * @return true if successfully processed and moved to queue, false on error
     */
    bool processFile(const std::string& filepath);

    /**
     * Get last error message
     * 
     * @return Error message from last operation
     */
    std::string getLastError() const { return lastError_; }

    /**
     * Get statistics
     */
    struct Statistics {
        size_t filesProcessed;
        size_t filesConverted;
        size_t filesMoved;
        size_t filesFailed;
    };

    Statistics getStatistics() const { return stats_; }

private:
    std::string inputDir_;
    std::string queueDir_;
    std::string lastError_;
    FileConverter converter_;
    DataProcessor dataProcessor_;
    
    Statistics stats_;
    
    /**
     * Move file from source to destination
     * 
     * @param sourcePath Source file path
     * @param destPath Destination file path
     * @return true if moved successfully
     */
    bool moveFile(const std::string& sourcePath, const std::string& destPath);
    
    /**
     * Remove file
     * 
     * @param filepath Path to file to remove
     * @return true if removed successfully
     */
    bool removeFile(const std::string& filepath);
    
    /**
     * Generate unique filename in destination directory
     * If filename exists, appends (1), (2), etc. like browser downloads
     * 
     * @param baseFilename Original filename
     * @param destDir Destination directory
     * @return Unique filename with full path
     */
    std::string generateUniqueFilename(const std::string& baseFilename, const std::string& destDir) const;
};

#endif // INPUT_PROCESSOR_H

