#include "FileTypeDetector.h"
#include <algorithm>
#include <cctype>

FileType FileTypeDetector::detect(const std::string&  filename) {
    std::string lower;
    lower.resize(filename.size());
    std::transform(filename.begin(), filename.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (containsKeyword(lower, "snfn")) {
        return FileType::SNFN;
    }
    if (containsKeyword(lower, "test board") || containsKeyword(lower, "testboard") || 
        containsKeyword(lower, "test_board")) {
        return FileType::TESTBOARD;
    }
    if (containsKeyword(lower, "workstation")) {
        return FileType::WORKSTATION;
    }
    return FileType::UNKNOWN;
}

std::string FileTypeDetector::toString(FileType type) {
    switch (type) {
        case FileType::WORKSTATION:
            return "WORKSTATION";
        case FileType::TESTBOARD:
            return "TESTBOARD";
        case FileType::SNFN:
            return "SNFN";
        default:
            return "UNKNOWN";
    }
}

bool FileTypeDetector::containsKeyword(const std::string& filename,
                                       const std::string& keyword) {
    return filename.find(keyword) != std::string::npos;
}