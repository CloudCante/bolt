#ifndef FILE_TYPE_DETECTOR_H
#define FILE_TYPE_DETECTOR_H

#include <string>

/**
Detects the type of a file based on its filename.
examples:
- "workstationOutputReport.xls" -> WORKSTATION
- "Test board record report.xls" -> TESTBOARD
- "snfnReport.xls" -> SNFN
*/

enum class FileType {
    UNKNOWN,
    WORKSTATION,
    TESTBOARD,
    SNFN
};

class FileTypeDetector {
public:
    /**
    * Detect the type of a file based on its filename.
    * @param filename The name of the file to detect the type of.
    * @return The type of the file.
    */
    static FileType detect(const std::string& filename);


    static std::string toString(FileType type);

private:
    static bool containsKeyword(const std::string& filename,
                                const std::string& keyword);

};

#endif // FILE_TYPE_DETECTOR_H