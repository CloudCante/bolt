#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include "Job.h"
#include <queue>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>

/**
 * JobQueue - Thread-safe queue for file processing jobs
 * 
 * This class is responsible for managing the job queue, preventing duplicates, and tracking pending/active jobs.
 * Only Job: Manage job queue, prevent duplicates, track pending/active jobs
 * 
 * Usage:
 *   JobQueue queue;
 *   auto job = queue.addJob("/path/to/file.xls", FileType::WORKSTATION);
 *   auto nextJob = queue.getNextJob(1000); // 1 second timeout
 *   queue.markCompleted(*nextJob);
 */
class JobQueue {
public:
    /**
     * Constructor
     * @param maxRetries Maximum number of retry attempts (default: 2)
     */
    explicit JobQueue(int maxRetries = 2);

    /**
     * Destructor
     */
    ~JobQueue();

    // Disable copy (thread-safe resource)
    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    /**
     * Add a job to the queue
     * Prevents duplicates by checking filepath
     * 
     * @param filepath Full path to file
     * @param fileType Type of file (from FileTypeDetector)
     * @return Pointer to Job if added successfully, nullptr if duplicate
     */
    std::unique_ptr<Job> addJob(const std::string& filepath, FileType fileType);

    /**
     * Get next job from queue (blocking)
     * Checks retry queue first, then pending queue
     * Worker threads call this to get work
     * 
     * @param timeout_ms Timeout in milliseconds (0 = wait forever)
     * @return Job if available, nullptr if timeout
     */
    std::unique_ptr<Job> getNextJob(int timeout_ms = 0);

    /**
     * Mark job as completed
     * Removes from active tracking
     * 
     * @param job The job that completed
     */
    void markCompleted(const Job& job);

    /**
     * Mark job as failed (called after processing attempt)
     * If retry count < maxRetries, adds to retry queue
     * Otherwise marks as permanently failed
     * 
     * @param job The job that failed
     * @param errorMessage Error description
     */
    void markFailed(const Job& job, const std::string& errorMessage);

    /**
     * Check if a file was already processed
     * 
     * @param filepath Path to check
     * @return true if file was processed
     */
    bool isProcessed(const std::string& filepath) const;

    // ========================================================================
    // Statistics & Status
    // ========================================================================

    /**
     * Get number of pending jobs in queue
     */
    size_t getPendingCount() const;

    /**
     * Get number of jobs waiting to retry
     */
    size_t getRetryCount() const;

    /**
     * Get number of jobs currently being processed
     */
    size_t getActiveCount() const;

    /**
     * Check if queue is empty (pending + retry)
     */
    bool isEmpty() const;

    /**
     * Statistics structure
     */
    struct Statistics {
        size_t pending;
        size_t retrying;
        size_t active;
        size_t totalAdded;
        size_t totalCompleted;
        size_t totalFailed;
        size_t totalRetried;
    };

    /**
     * Get current statistics
     */
    Statistics getStatistics() const;

private:
    // Thread-safe queues
    std::queue<Job> pendingJobs_;      // New jobs waiting to be processed
    std::queue<Job> retryJobs_;        // Jobs to retry (failed once)
    mutable std::mutex queueMutex_;     // Protects queue operations
    std::condition_variable queueCondition_; // Notifies workers when jobs available

    // Duplicate prevention
    std::set<std::string> processedFiles_;   // Set of filepaths already processed/reserved
    mutable std::mutex processedMutex_;       // Protects processedFiles_

    // Job tracking
    std::map<size_t, Job> activeJobs_;  // Jobs currently being processed (by jobId)
    mutable std::mutex activeMutex_;     // Protects activeJobs_

    // Statistics
    std::atomic<size_t> totalJobsAdded_{0};
    std::atomic<size_t> totalJobsCompleted_{0};
    std::atomic<size_t> totalJobsFailed_{0};
    std::atomic<size_t> totalJobsRetried_{0};
    std::atomic<size_t> nextJobId_{1};

    // Configuration
    int maxRetries_;

    /**
     * Normalize filepath for duplicate checking
     * Resolves to absolute path
     * 
     * @param path File path to normalize
     * @return Normalized absolute path
     */
    std::string normalizePath(const std::string& path) const;
};

#endif // JOB_QUEUE_H

