#include "FileWatcher.h"
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <iostream>
#include <cstring>
#include <algorithm>

FileWatcher::FileWatcher(const std::string& watchPath)
    : watchPath_(watchPath)
    , inotifyFd_(-1)
    , watchDescriptor_(-1)
    , running_(false)
    , fileCount_(0)
{
}

FileWatcher::~FileWatcher() {
    stop();
}

void FileWatcher::setCallback(Callback callback) {
    callback_ = std::move(callback);
}

void FileWatcher::setFilePatterns(const std::vector<std::string>& patterns) {
    filePatterns_ = patterns;
}

bool FileWatcher::start() {
    if (running_) {
        std::cerr << "FileWatcher: Already running!" << std::endl;
        return false;
    }

    // Initialize inotify
    inotifyFd_ = inotify_init1(IN_NONBLOCK);
    if (inotifyFd_ < 0) {
        std::cerr << "FileWatcher: Failed to initialize inotify: " 
                  << strerror(errno) << std::endl;
        return false;
    }

    // Add watch for directory
    // IN_CLOSE_WRITE: Trigger when file is closed after writing (file is complete)
    // IN_MOVED_TO: Trigger when file is moved/created in directory
    watchDescriptor_ = inotify_add_watch(inotifyFd_, watchPath_.c_str(),
                                          IN_CLOSE_WRITE | IN_MOVED_TO);
    
    if (watchDescriptor_ < 0) {
        std::cerr << "FileWatcher: Failed to add watch for " << watchPath_
                  << ": " << strerror(errno) << std::endl;
        close(inotifyFd_);
        inotifyFd_ = -1;
        return false;
    }

    running_ = true;
    fileCount_ = 0;

    // Start background thread
    watchThread_ = std::make_unique<std::thread>(&FileWatcher::watchLoop, this);

    std::cout << "FileWatcher: Started monitoring " << watchPath_ << std::endl;
    return true;
}

void FileWatcher::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    // Close inotify (this will wake up the read() in watchLoop)
    if (inotifyFd_ >= 0) {
        close(inotifyFd_);
        inotifyFd_ = -1;
    }

    // Wait for thread to finish
    if (watchThread_ && watchThread_->joinable()) {
        watchThread_->join();
    }

    std::cout << "FileWatcher: Stopped monitoring" << std::endl;
}

void FileWatcher::watchLoop() {
    // Buffer for inotify events (each event is at least sizeof(inotify_event) bytes)
    char buffer[4096];
    
    while (running_) {
        // Read events (non-blocking because we used IN_NONBLOCK)
        ssize_t length = read(inotifyFd_, buffer, sizeof(buffer));
        
        if (length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No events available, sleep briefly
                usleep(100000); // 100ms
                continue;
            } else {
                // Error reading
                if (running_) {
                    std::cerr << "FileWatcher: Error reading events: "
                              << strerror(errno) << std::endl;
                }
                break;
            }
        }

        // Process events
        int i = 0;
        while (i < length) {
            struct inotify_event* event = (struct inotify_event*)&buffer[i];
            
            if (event->len > 0) {
                std::string filename(event->name);
                
                // Check if filename matches our patterns
                if (filePatterns_.empty() || matchesPattern(filename)) {
                    std::string fullPath = watchPath_ + "/" + filename;
                    
                    // Call callback if set
                    if (callback_) {
                        callback_(fullPath);
                    }
                    
                    fileCount_++;
                    std::cout << "FileWatcher: Detected file: " << filename << std::endl;
                }
            }
            
            // Move to next event
            i += sizeof(struct inotify_event) + event->len;
        }
    }
}

bool FileWatcher::matchesPattern(const std::string& filename) const {
    // If no patterns specified, match everything
    if (filePatterns_.empty()) {
        return true;
    }

    // Check if filename matches any pattern (exact match for now)
    for (const auto& pattern : filePatterns_) {
        if (filename == pattern || filename.find(pattern) != std::string::npos) {
            return true;
        }
    }

    return false;
}

