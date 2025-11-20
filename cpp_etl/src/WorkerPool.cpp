#include "WorkerPool.h"
#include <iostream>
#include <chrono>

WorkerPool::WorkerPool(size_t numWorkers, JobQueue& jobQueue, const DatabaseLoader::DatabaseConfig& dbConfig)
    : numWorkers_(numWorkers)
    , jobQueue_(jobQueue)
    , dbConfig_(dbConfig)
    , running_(false)
{
}

WorkerPool::~WorkerPool() {
    stop();
}

bool WorkerPool::start() {
    if (running_) {
        std::cerr << "WorkerPool already running!" << std::endl;
        return false;
    }

    std::cout << "Starting WorkerPool with " << numWorkers_ << " workers..." << std::endl;
    running_ = true;

    // Spawn worker threads
    for (size_t i = 0; i < numWorkers_; i++) {
        workerThreads_.push_back(
            std::make_unique<std::thread>(&WorkerPool::workerThread, this, i + 1)
        );
    }

    std::cout << "WorkerPool started with " << numWorkers_ << " workers" << std::endl;
    return true;
}

void WorkerPool::stop() {
    if (!running_) {
        return;
    }

    std::cout << "Stopping WorkerPool..." << std::endl;
    running_ = false;

    // Wait for all worker threads to finish
    for (auto& thread : workerThreads_) {
        if (thread && thread->joinable()) {
            thread->join();
        }
    }

    workerThreads_.clear();
    std::cout << "WorkerPool stopped" << std::endl;
}

void WorkerPool::setSuccessCallback(SuccessCallback callback) {
    successCallback_ = callback;
}

void WorkerPool::workerThread(size_t workerId) {
    std::cout << "[Worker #" << workerId << "] Started" << std::endl;

    // Create DatabaseLoader for this worker (not thread-safe to share)
    DatabaseLoader loader(dbConfig_);

    while (running_) {
        // Get next job from queue (with timeout to check if still running)
        auto job = jobQueue_.getNextJob(1000); // 1 second timeout

        if (!job) {
            // No job available, check if we should exit
            if (!running_) {
                break;
            }
            continue;
        }

        // Mark this worker as active
        activeWorkers_.fetch_add(1);

        std::cout << "[Worker #" << workerId << "] Processing Job #" << job->jobId 
                  << ": " << job->filepath 
                  << " (" << FileTypeDetector::toString(job->fileType) << ")" << std::endl;

        // Process the job - import CSV to database
        bool success = loader.importCSV(job->filepath, job->fileType);

        totalProcessed_.fetch_add(1);

        if (success) {
            // Job succeeded
            auto stats = loader.getLastImportStats();
            std::cout << "[Worker #" << workerId << "] ✓ Job #" << job->jobId << " completed: "
                      << stats.insertedRows << " new records inserted ("
                      << stats.existingRows << " existing, "
                      << stats.totalRows << " total rows)" << std::endl;
            
            jobQueue_.markCompleted(*job);
            totalSucceeded_.fetch_add(1);
            
            // Notify callback if set
            if (successCallback_) {
                successCallback_(job->fileType);
            }
        } else {
            // Job failed
            std::string error = loader.getLastError();
            std::cerr << "[Worker #" << workerId << "] ✗ Job #" << job->jobId << " failed: " 
                      << error << std::endl;
            
            jobQueue_.markFailed(*job, error);
            totalFailed_.fetch_add(1);
        }

        // Mark this worker as idle
        activeWorkers_.fetch_sub(1);
    }

    std::cout << "[Worker #" << workerId << "] Stopped" << std::endl;
}

WorkerPool::Statistics WorkerPool::getStatistics() const {
    Statistics stats;
    stats.totalProcessed = totalProcessed_.load();
    stats.totalSucceeded = totalSucceeded_.load();
    stats.totalFailed = totalFailed_.load();
    stats.activeWorkers = activeWorkers_.load();
    return stats;
}

