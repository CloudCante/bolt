#ifndef FILEWATCHER_H
#define FILEWATCHER_H

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>

/**
 * FileWatcher - Monitors directory for file changes using Linux inotify
 * 
 * Usage:
 *   FileWatcher watcher("/path/to/watch");
 *   watcher.setCallback([](const std::string& filepath) {
 *       std::cout << "File detected: " << filepath << std::endl;
 *   });
 *   watcher.start();
 *   // ... do other work ...
 *   watcher.stop();
 */
class FileWatcher {
public:
    // Callback function type: void callback(const std::string& filepath)
    using Callback = std::function<void(const std::string& filepath)>;

    /**
     * Constructor
     * @param watchPath Directory to monitor
     */
    explicit FileWatcher(const std::string& watchPath);

    /**
     * Destructor - automatically stops watching
     */
    ~FileWatcher();

    // Disable copy (we use unique resources)
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    /**
     * Set callback function called when file is detected
     * @param callback Function to call when file detected
     */
    void setCallback(Callback callback);

    /**
     * Set file patterns to watch for (e.g., "workstationOutputReport.xls")
     * If empty, watches all files
     * @param patterns Vector of filename patterns
     */
    void setFilePatterns(const std::vector<std::string>& patterns);

    /**
     * Start watching (spawns background thread)
     * @return true if started successfully
     */
    bool start();

    /**
     * Stop watching
     */
    void stop();

    /**
     * Check if currently watching
     */
    bool isRunning() const { return running_; }

    /**
     * Get number of files detected since start
     */
    size_t getFileCount() const { return fileCount_; }

    /**
     * Get watch directory path
     */
    std::string getWatchPath() const { return watchPath_; }

private:
    std::string watchPath_;
    Callback callback_;
    std::vector<std::string> filePatterns_;
    
    int inotifyFd_;
    int watchDescriptor_;
    
    std::unique_ptr<std::thread> watchThread_;
    std::atomic<bool> running_;
    std::atomic<size_t> fileCount_;

    /**
     * Internal thread function that runs the inotify loop
     */
    void watchLoop();

    /**
     * Check if filename matches any pattern
     */
    bool matchesPattern(const std::string& filename) const;
};

#endif // FILEWATCHER_H

