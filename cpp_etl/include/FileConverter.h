#ifndef FILE_CONVERTER_H
#define FILE_CONVERTER_H

#include <string>

/**
 * FileConverter - Converts .xls/.xlsx files to CSV using LibreOffice
 * 
 * Role: Support (Utility Champion)
 * Only Job: Convert .xls/.xlsx → CSV using LibreOffice headless mode
 * 
 * Usage:
 *   FileConverter converter;
 *   std::string csvPath = converter.convertToCSV("/path/to/file.xlsx", "/output/dir");
 *   if (!csvPath.empty()) {
 *       // Conversion successful
 *   }
 */
class FileConverter {
public:
    /**
     * Constructor
     */
    FileConverter();

    /**
     * Destructor
     */
    ~FileConverter() = default;

    // Disable copy
    FileConverter(const FileConverter&) = delete;
    FileConverter& operator=(const FileConverter&) = delete;

    /**
     * Convert .xls or .xlsx file to CSV format
     * 
     * @param inputFilePath Full path to input .xls or .xlsx file
     * @param outputDir Directory to place converted .csv file
     * @return Path to converted .csv file, or empty string on failure
     */
    std::string convertToCSV(const std::string& inputFilePath, const std::string& outputDir);

    /**
     * Convert .xls file to .xlsx format (deprecated - use convertToCSV instead)
     * 
     * @param inputFilePath Full path to input .xls file
     * @param outputDir Directory to place converted .xlsx file
     * @return Path to converted .xlsx file, or empty string on failure
     */
    std::string convertToXlsx(const std::string& inputFilePath, const std::string& outputDir);

    /**
     * Check if LibreOffice is available on the system
     * 
     * @return true if LibreOffice command is available
     */
    static bool isLibreOfficeAvailable();

    /**
     * Check if file is .xls format (needs conversion)
     * 
     * @param filepath Path to file
     * @return true if file is .xls (case-insensitive)
     */
    static bool isXlsFile(const std::string& filepath);

    /**
     * Check if file is .xlsx format
     * 
     * @param filepath Path to file
     * @return true if file is .xlsx (case-insensitive)
     */
    static bool isXlsxFile(const std::string& filepath);

    /**
     * Check if file is .csv format
     * 
     * @param filepath Path to file
     * @return true if file is .csv (case-insensitive)
     */
    static bool isCsvFile(const std::string& filepath);

    /**
     * Get last error message
     * 
     * @return Error message from last operation
     */
    std::string getLastError() const { return lastError_; }

private:
    std::string lastError_;
    int conversionTimeoutSeconds_;
    
    /**
     * Internal helper to find LibreOffice executable
     * Checks common paths and PATH environment variable
     * 
     * @return Path to LibreOffice executable, or empty string if not found
     */
    static std::string findLibreOfficePath();
};

#endif // FILE_CONVERTER_H

