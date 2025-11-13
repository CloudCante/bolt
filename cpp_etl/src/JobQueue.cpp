#include "JobQueue.h"
#include <iostream>
#include <algorithm>
#include <limits.h>
#include <unistd.h>
#include <chrono>

JobQueue::JobQueue(int maxRetries)
    : maxRetries_(maxRetries)
{
}

JobQueue::~JobQueue() {
    // All jobs should be completed by now, but clear anyway
    std::lock_guard<std::mutex> lock(queueMutex_);
    // Queues will be cleared automatically
}

std::unique_ptr<Job> JobQueue::addJob(const std::string& filepath, FileType fileType) {
    // Normalize path for duplicate checking
    std::string normalizedPath = normalizePath(filepath);
    
    // Check for duplicates
    {
        std::lock_guard<std::mutex> lock(processedMutex_);
        if (processedFiles_.find(normalizedPath) != processedFiles_.end()) {
            // File already processed or queued
            return nullptr;
        }
        // Reserve slot immediately to prevent race conditions
        processedFiles_.insert(normalizedPath);
    }
    
    // Create job with unique ID
    auto job = std::make_unique<Job>(filepath, fileType);
    job->jobId = nextJobId_.fetch_add(1);
    job->maxRetries = maxRetries_;
    
    // Add to pending queue
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        pendingJobs_.push(*job);
    }
    
    // Notify one waiting worker
    queueCondition_.notify_one();
    
    // Update statistics
    totalJobsAdded_.fetch_add(1);
    
    return job;
}

std::unique_ptr<Job> JobQueue::getNextJob(int timeout_ms) {
    std::unique_lock<std::mutex> lock(queueMutex_);
    
    // Helper lambda to check if queue has jobs
    auto hasJobs = [this]() {
        return !retryJobs_.empty() || !pendingJobs_.empty();
    };
    
    // Wait for job with timeout
    if (timeout_ms > 0) {
        // Convert timeout to duration
        auto timeout = std::chrono::milliseconds(timeout_ms);
        if (!queueCondition_.wait_for(lock, timeout, hasJobs)) {
            // Timeout
            return nullptr;
        }
    } else {
        // Wait forever
        queueCondition_.wait(lock, hasJobs);
    }
    
    // Priority: retry queue first, then pending queue
    Job job;
    if (!retryJobs_.empty()) {
        job = retryJobs_.front();
        retryJobs_.pop();
    } else if (!pendingJobs_.empty()) {
        job = pendingJobs_.front();
        pendingJobs_.pop();
    } else {
        // Should not happen (condition variable should prevent this)
        return nullptr;
    }
    
    // Update job status and add to active tracking
    job.status = JobStatus::PROCESSING;
    job.lastAttemptTime = std::time(nullptr);
    
    {
        std::lock_guard<std::mutex> activeLock(activeMutex_);
        activeJobs_[job.jobId] = job;
    }
    
    return std::make_unique<Job>(job);
}

void JobQueue::markCompleted(const Job& job) {
    std::lock_guard<std::mutex> lock(activeMutex_);
    
    auto it = activeJobs_.find(job.jobId);
    if (it != activeJobs_.end()) {
        activeJobs_.erase(it);
    }
    
    totalJobsCompleted_.fetch_add(1);
}

void JobQueue::markFailed(const Job& job, const std::string& errorMessage) {
    std::unique_lock<std::mutex> activeLock(activeMutex_);
    
    // Find and update job
    auto it = activeJobs_.find(job.jobId);
    if (it == activeJobs_.end()) {
        // Job not found in active jobs (shouldn't happen)
        return;
    }
    
    Job& jobRef = it->second;
    jobRef.retryCount++;
    jobRef.lastAttemptTime = std::time(nullptr);
    jobRef.errorMessage = errorMessage;
    
    // Check if we should retry
    if (jobRef.retryCount < jobRef.maxRetries) {
        // Add to retry queue
        jobRef.status = JobStatus::RETRYING;
        
        // Move to retry queue
        activeJobs_.erase(it);
        activeLock.unlock();
        
        {
            std::lock_guard<std::mutex> queueLock(queueMutex_);
            retryJobs_.push(jobRef);
        }
        
        // Notify workers
        queueCondition_.notify_one();
        
        totalJobsRetried_.fetch_add(1);
    } else {
        // Permanently failed - remove from active
        jobRef.status = JobStatus::FAILED;
        activeJobs_.erase(it);
        activeLock.unlock();
        
        totalJobsFailed_.fetch_add(1);
        
        // Note: ErrorLogger will handle creating log file
        // DatabaseWriter will handle DB write
        // JobQueue just tracks the failure
    }
}

bool JobQueue::isProcessed(const std::string& filepath) const {
    std::string normalizedPath = normalizePath(filepath);
    std::lock_guard<std::mutex> lock(processedMutex_);
    return processedFiles_.find(normalizedPath) != processedFiles_.end();
}

size_t JobQueue::getPendingCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return pendingJobs_.size();
}

size_t JobQueue::getRetryCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return retryJobs_.size();
}

size_t JobQueue::getActiveCount() const {
    std::lock_guard<std::mutex> lock(activeMutex_);
    return activeJobs_.size();
}

bool JobQueue::isEmpty() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return pendingJobs_.empty() && retryJobs_.empty();
}

JobQueue::Statistics JobQueue::getStatistics() const {
    Statistics stats;
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stats.pending = pendingJobs_.size();
        stats.retrying = retryJobs_.size();
    }
    
    {
        std::lock_guard<std::mutex> lock(activeMutex_);
        stats.active = activeJobs_.size();
    }
    
    stats.totalAdded = totalJobsAdded_.load();
    stats.totalCompleted = totalJobsCompleted_.load();
    stats.totalFailed = totalJobsFailed_.load();
    stats.totalRetried = totalJobsRetried_.load();
    
    return stats;
}

std::string JobQueue::normalizePath(const std::string& path) const {
    // Convert to absolute path for duplicate checking
    char resolved_path[PATH_MAX];
    if (realpath(path.c_str(), resolved_path) != nullptr) {
        return std::string(resolved_path);
    }
    // If realpath fails (file doesn't exist yet), return original
    // This is okay - we'll catch duplicates when file is actually processed
    return path;
}

