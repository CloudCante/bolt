#ifndef WORKER_POOL_H
#define WORKER_POOL_H

#include "JobQueue.h"
#include "DatabaseLoader.h"
#include <vector>
#include <thread>
#include <atomic>
#include <memory>

/**
 * WorkerPool - Manages worker threads that process jobs from JobQueue
 * 
 * Role: Damage Dealers - Top/Mid (Processing Champions)
 * Only Job: Spawn worker threads that pull jobs and execute DatabaseLoader
 * 
 * Usage:
 *   WorkerPool pool(4, jobQueue, dbConfig);  // 4 worker threads
 *   pool.start();
 *   // ... jobs are processed ...
 *   pool.stop();
 */
class WorkerPool {
public:
    /**
     * Constructor
     * @param numWorkers Number of worker threads to spawn
     * @param jobQueue Reference to JobQueue to pull jobs from
     * @param dbConfig Database configuration for DatabaseLoader
     */
    WorkerPool(size_t numWorkers, JobQueue& jobQueue, const DatabaseLoader::DatabaseConfig& dbConfig);

    /**
     * Destructor - automatically stops workers
     */
    ~WorkerPool();

    // Disable copy
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    /**
     * Start all worker threads
     * @return true if started successfully
     */
    bool start();

    /**
     * Stop all worker threads gracefully
     * Waits for current jobs to complete
     */
    void stop();

    /**
     * Check if workers are running
     */
    bool isRunning() const { return running_; }

    /**
     * Get number of worker threads
     */
    size_t getWorkerCount() const { return numWorkers_; }

    /**
     * Get statistics
     */
    struct Statistics {
        size_t totalProcessed;
        size_t totalSucceeded;
        size_t totalFailed;
        size_t activeWorkers;
    };

    Statistics getStatistics() const;

private:
    size_t numWorkers_;
    JobQueue& jobQueue_;
    DatabaseLoader::DatabaseConfig dbConfig_;
    
    std::vector<std::unique_ptr<std::thread>> workerThreads_;
    std::atomic<bool> running_;
    
    // Statistics
    std::atomic<size_t> totalProcessed_{0};
    std::atomic<size_t> totalSucceeded_{0};
    std::atomic<size_t> totalFailed_{0};
    std::atomic<size_t> activeWorkers_{0};

    /**
     * Worker thread function
     * Pulls jobs from queue and processes them
     * @param workerId Worker thread ID (for logging)
     */
    void workerThread(size_t workerId);
};

#endif // WORKER_POOL_H

