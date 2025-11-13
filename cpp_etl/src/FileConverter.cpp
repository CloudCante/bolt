#include "FileConverter.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <vector>
#include <string>

FileConverter::FileConverter()
    : conversionTimeoutSeconds_(60)
{
}

bool FileConverter::isXlsFile(const std::string& filepath) {
    if (filepath.length() < 4) {
        return false;
    }
    
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Check if ends with .xls (not .xlsx)
    size_t pos = lower.rfind(".xls");
    if (pos == std::string::npos) {
        return false;
    }
    
    // Must be exactly .xls, not .xlsx
    return (pos + 4 == lower.length());
}

bool FileConverter::isXlsxFile(const std::string& filepath) {
    if (filepath.length() < 5) {
        return false;
    }
    
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return lower.substr(lower.length() - 5) == ".xlsx";
}

bool FileConverter::isCsvFile(const std::string& filepath) {
    if (filepath.length() < 4) {
        return false;
    }
    
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return lower.substr(lower.length() - 4) == ".csv";
}

bool FileConverter::isLibreOfficeAvailable() {
    return !findLibreOfficePath().empty();
}

std::string FileConverter::convertToCSV(const std::string& inputFilePath, const std::string& outputDir) {
    lastError_.clear();
    
    // Validate input file
    if (inputFilePath.empty()) {
        lastError_ = "Input file path is empty";
        return "";
    }
    
    // Check if file exists
    if (access(inputFilePath.c_str(), F_OK) != 0) {
        lastError_ = "Input file does not exist: " + inputFilePath;
        return "";
    }
    
    // Check if file is .xls or .xlsx
    if (!isXlsFile(inputFilePath) && !isXlsxFile(inputFilePath)) {
        lastError_ = "Input file is not .xls or .xlsx format: " + inputFilePath;
        return "";
    }
    
    // Validate output directory
    if (outputDir.empty()) {
        lastError_ = "Output directory is empty";
        return "";
    }
    
    // Check if output directory exists
    if (access(outputDir.c_str(), F_OK) != 0) {
        lastError_ = "Output directory does not exist: " + outputDir;
        return "";
    }
    
    // Find LibreOffice path
    std::string libreofficePath = findLibreOfficePath();
    if (libreofficePath.empty()) {
        lastError_ = "LibreOffice not found. Please install LibreOffice.";
        return "";
    }
    
    // Build output file path
    size_t lastSlash = inputFilePath.find_last_of("/");
    std::string filename = (lastSlash == std::string::npos) 
                          ? inputFilePath 
                          : inputFilePath.substr(lastSlash + 1);
    
    size_t dotPos = filename.find_last_of(".");
    std::string baseName = (dotPos == std::string::npos) 
                          ? filename 
                          : filename.substr(0, dotPos);
    
    std::string outputFilePath = outputDir + "/" + baseName + ".csv";
    
    // Build command: libreoffice --headless --convert-to "csv:Text - txt - csv (StarCalc):44,34,UTF8" --outdir <dir> <file>
    // 44 = comma delimiter, 34 = double quote, UTF8 = encoding
    std::vector<std::string> cmdArgs = {
        libreofficePath,
        "--headless",
        "--convert-to", "csv:Text - txt - csv (StarCalc):44,34,UTF8",
        "--outdir", outputDir,
        inputFilePath
    };
    
    // Convert to C-style arguments for exec
    std::vector<char*> argv;
    for (auto& arg : cmdArgs) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    
    // Fork and execute LibreOffice
    pid_t pid = fork();
    
    if (pid < 0) {
        lastError_ = "Failed to fork process: " + std::string(strerror(errno));
        return "";
    }
    
    if (pid == 0) {
        // Child process: execute LibreOffice
        // Redirect stdout/stderr to /dev/null
        int nullFd = open("/dev/null", O_WRONLY);
        if (nullFd >= 0) {
            dup2(nullFd, STDOUT_FILENO);
            dup2(nullFd, STDERR_FILENO);
            close(nullFd);
        }
        
        execvp(argv[0], argv.data());
        // If execvp returns, it failed
        _exit(1);
    } else {
        // Parent process: wait for child with timeout
        int status;
        int waitResult;
        int elapsedSeconds = 0;
        
        while (elapsedSeconds < conversionTimeoutSeconds_) {
            waitResult = waitpid(pid, &status, WNOHANG);
            
            if (waitResult > 0) {
                // Child process finished
                if (WIFEXITED(status)) {
                    int exitCode = WEXITSTATUS(status);
                    if (exitCode == 0) {
                        // Success - check if output file exists
                        if (access(outputFilePath.c_str(), F_OK) == 0) {
                            return outputFilePath;
                        } else {
                            lastError_ = "Conversion succeeded but output file not found: " + outputFilePath;
                            return "";
                        }
                    } else {
                        lastError_ = "LibreOffice conversion failed with exit code: " + std::to_string(exitCode);
                        return "";
                    }
                } else {
                    lastError_ = "LibreOffice process terminated abnormally";
                    return "";
                }
            } else if (waitResult < 0) {
                // Error waiting
                lastError_ = "Error waiting for LibreOffice process: " + std::string(strerror(errno));
                return "";
            }
            
            // Child still running, wait 1 second
            sleep(1);
            elapsedSeconds++;
        }
        
        // Timeout - kill the process
        kill(pid, SIGTERM);
        sleep(1);
        if (waitpid(pid, &status, WNOHANG) == 0) {
            // Still running, force kill
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
        
        lastError_ = "LibreOffice conversion timed out after " + std::to_string(conversionTimeoutSeconds_) + " seconds";
        return "";
    }
}

std::string FileConverter::findLibreOfficePath() {
    // Check common installation paths
    std::vector<std::string> possiblePaths = {
        "/usr/bin/libreoffice",
        "/usr/local/bin/libreoffice",
        "/opt/libreoffice*/program/soffice",  // Note: would need glob expansion
    };
    
    for (const auto& path : possiblePaths) {
        if (access(path.c_str(), X_OK) == 0) {
            return path;
        }
    }
    
    // Check PATH using 'which' command
    // For now, we'll assume 'libreoffice' is in PATH
    // Could use execvp to check, but simple approach: try running it
    return "libreoffice"; // Will check if it works when we try to use it
}

std::string FileConverter::convertToXlsx(const std::string& inputFilePath, const std::string& outputDir) {
    lastError_.clear();
    
    // Validate input file
    if (inputFilePath.empty()) {
        lastError_ = "Input file path is empty";
        return "";
    }
    
    // Check if file exists
    if (access(inputFilePath.c_str(), F_OK) != 0) {
        lastError_ = "Input file does not exist: " + inputFilePath;
        return "";
    }
    
    // Check if file is .xls
    if (!isXlsFile(inputFilePath)) {
        lastError_ = "Input file is not .xls format: " + inputFilePath;
        return "";
    }
    
    // Validate output directory
    if (outputDir.empty()) {
        lastError_ = "Output directory is empty";
        return "";
    }
    
    // Check if output directory exists
    if (access(outputDir.c_str(), F_OK) != 0) {
        lastError_ = "Output directory does not exist: " + outputDir;
        return "";
    }
    
    // Find LibreOffice path
    std::string libreofficePath = findLibreOfficePath();
    if (libreofficePath.empty()) {
        lastError_ = "LibreOffice not found. Please install LibreOffice.";
        return "";
    }
    
    // Build output file path
    // Extract filename from input path and change extension
    size_t lastSlash = inputFilePath.find_last_of("/");
    std::string filename = (lastSlash == std::string::npos) 
                          ? inputFilePath 
                          : inputFilePath.substr(lastSlash + 1);
    
    size_t dotPos = filename.find_last_of(".");
    std::string baseName = (dotPos == std::string::npos) 
                          ? filename 
                          : filename.substr(0, dotPos);
    
    std::string outputFilePath = outputDir + "/" + baseName + ".xlsx";
    
    // Build command
    // libreoffice --headless --convert-to xlsx --outdir <outputDir> <inputFile>
    std::vector<std::string> cmdArgs = {
        libreofficePath,
        "--headless",
        "--convert-to", "xlsx",
        "--outdir", outputDir,
        inputFilePath
    };
    
    // Convert to C-style arguments for exec
    std::vector<char*> argv;
    for (auto& arg : cmdArgs) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    
    // Fork and execute LibreOffice
    pid_t pid = fork();
    
    if (pid < 0) {
        lastError_ = "Failed to fork process: " + std::string(strerror(errno));
        return "";
    }
    
    if (pid == 0) {
        // Child process: execute LibreOffice
        // Redirect stdout/stderr to /dev/null
        int nullFd = open("/dev/null", O_WRONLY);
        if (nullFd >= 0) {
            dup2(nullFd, STDOUT_FILENO);
            dup2(nullFd, STDERR_FILENO);
            close(nullFd);
        }
        
        execvp(argv[0], argv.data());
        // If execvp returns, it failed
        _exit(1);
    } else {
        // Parent process: wait for child with timeout
        int status;
        int waitResult;
        int elapsedSeconds = 0;
        
        while (elapsedSeconds < conversionTimeoutSeconds_) {
            waitResult = waitpid(pid, &status, WNOHANG);
            
            if (waitResult > 0) {
                // Child process finished
                if (WIFEXITED(status)) {
                    int exitCode = WEXITSTATUS(status);
                    if (exitCode == 0) {
                        // Success - check if output file exists
                        if (access(outputFilePath.c_str(), F_OK) == 0) {
                            return outputFilePath;
                        } else {
                            lastError_ = "Conversion succeeded but output file not found: " + outputFilePath;
                            return "";
                        }
                    } else {
                        lastError_ = "LibreOffice conversion failed with exit code: " + std::to_string(exitCode);
                        return "";
                    }
                } else {
                    lastError_ = "LibreOffice process terminated abnormally";
                    return "";
                }
            } else if (waitResult < 0) {
                // Error waiting
                lastError_ = "Error waiting for LibreOffice process: " + std::string(strerror(errno));
                return "";
            }
            
            // Child still running, wait 1 second
            sleep(1);
            elapsedSeconds++;
        }
        
        // Timeout - kill the process
        kill(pid, SIGTERM);
        sleep(1);
        if (waitpid(pid, &status, WNOHANG) == 0) {
            // Still running, force kill
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
        
        lastError_ = "LibreOffice conversion timed out after " + std::to_string(conversionTimeoutSeconds_) + " seconds";
        return "";
    }
}

