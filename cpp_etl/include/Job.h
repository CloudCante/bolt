#ifndef JOB_H
#define JOB_H

#include <string>
#include <ctime>
#include "FileTypeDetector.h"

/**
 * Job Status Enum
 * Tracks the current state of a processing job
 */
enum class JobStatus {
    PENDING,        // Added to queue, waiting for worker
    PROCESSING,     // Currently being processed
    COMPLETED,      // Successfully processed
    FAILED,         // Failed after retries exhausted
    RETRYING        // Failed once, will retry
};

/**
 * Job - Data structure representing a file processing job
 * 
 * This is just a data container (like an item/stat card in LoL terms).
 * No methods, just holds information about a job.
 */
struct Job {
    std::string filepath;          // Full path to file
    FileType fileType;             // WORKSTATION, TESTBOARD, SNFN, UNKNOWN
    JobStatus status;              // Current status
    std::time_t timestamp;         // When job was created
    std::time_t lastAttemptTime;   // When last processing attempt started
    std::string errorMessage;      // Error from last attempt
    std::string logFilePath;       // Path to error log file (if failed)
    size_t jobId;                  // Unique job ID
    int retryCount;                // Number of attempts so far (0, 1, or 2)
    int maxRetries;                // Maximum retries allowed (default: 2)
    
    /**
     * Default constructor (needed for STL containers)
     */
    Job()
        : fileType(FileType::UNKNOWN)
        , status(JobStatus::PENDING)
        , timestamp(std::time(nullptr))
        , lastAttemptTime(0)
        , jobId(0)
        , retryCount(0)
        , maxRetries(2)
    {}
    
    /**
     * Constructor
     * @param path Full path to file
     * @param type File type (from FileTypeDetector)
     */
    Job(const std::string& path, FileType type) 
        : filepath(path)
        , fileType(type)
        , status(JobStatus::PENDING)
        , timestamp(std::time(nullptr))
        , lastAttemptTime(0)
        , jobId(0)
        , retryCount(0)
        , maxRetries(2)  // Try twice as requested
    {}
};

#endif // JOB_H

