#include "FileWatcher.h"
#include "FileTypeDetector.h"
#include "JobQueue.h"
#include "InputProcessor.h"
#include "FileConverter.h"
#include "WorkerPool.h"
#include "DatabaseLoader.h"
#include "aggregation_manager.h"
#include <iostream>
#include <string>
#include <vector>
#include <csignal>
#include <unistd.h>
#include <atomic>
#include <filesystem>

// Global flag for signal handling
static std::atomic<bool> g_running{true};
static FileWatcher* g_inputWatcher = nullptr;
static FileWatcher* g_queueWatcher = nullptr;
static WorkerPool* g_workerPool = nullptr;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nReceived signal, shutting down..." << std::endl;
        g_running = false;
        if (g_workerPool) {
            g_workerPool->stop();
        }
        if (g_inputWatcher) {
            g_inputWatcher->stop();
        }
        if (g_queueWatcher) {
            g_queueWatcher->stop();
        }
    }
}

void printUsage(const char* progName) {
    std::cout << "FoxETL File Monitor\n"
              << "Usage: " << progName << " [options]\n\n"
              << "Options:\n"
              << "  -d, --directory PATH    Directory to watch (default: ./input)\n"
              << "  -p, --pattern PATTERN  File pattern to match (can specify multiple)\n"
              << "                          (default: watches all files)\n"
              << "  -h, --help             Show this help message\n\n"
              << "Examples:\n"
              << "  " << progName << " -d ./input\n"
              << "  " << progName << " -d ./input -p workstationOutputReport.xls\n"
              << "  " << progName << " -d ./input -p workstationOutputReport.xls -p \"Test board record report.xls\"\n";
}

// Helper function to extract filename from path
std::string extractFilename(const std::string& filepath) {
    std::filesystem::path p(filepath);
    return p.filename().string();
}

void printStatus(const FileWatcher& inputWatcher, const FileWatcher& queueWatcher, 
                 const InputProcessor& processor, const JobQueue& queue, const WorkerPool& workers) {
    auto queueStats = queue.getStatistics();
    auto procStats = processor.getStatistics();
    auto workerStats = workers.getStatistics();
    
    std::cout << "\n=== FoxETL Status ===\n"
              << "\n--- Input Processing ---\n"
              << "Input folder: " << inputWatcher.getWatchPath() << "\n"
              << "Files detected: " << inputWatcher.getFileCount() << "\n"
              << "Processed: " << procStats.filesProcessed << "\n"
              << "Converted: " << procStats.filesConverted << "\n"
              << "Moved to queue: " << procStats.filesMoved << "\n"
              << "Failed: " << procStats.filesFailed << "\n"
              << "\n--- Queue Processing ---\n"
              << "Queue folder: " << queueWatcher.getWatchPath() << "\n"
              << "Files detected: " << queueWatcher.getFileCount() << "\n"
              << "\n--- Job Queue ---\n"
              << "Pending: " << queueStats.pending << "\n"
              << "Retrying: " << queueStats.retrying << "\n"
              << "Active: " << queueStats.active << "\n"
              << "Total added: " << queueStats.totalAdded << "\n"
              << "Total completed: " << queueStats.totalCompleted << "\n"
              << "Total failed: " << queueStats.totalFailed << "\n"
              << "Total retried: " << queueStats.totalRetried << "\n"
              << "\n--- Worker Pool ---\n"
              << "Workers: " << workers.getWorkerCount() << "\n"
              << "Active workers: " << workerStats.activeWorkers << "\n"
              << "Total processed: " << workerStats.totalProcessed << "\n"
              << "Succeeded: " << workerStats.totalSucceeded << "\n"
              << "Failed: " << workerStats.totalFailed << "\n"
              << "=========================\n" << std::endl;
}

int main(int argc, char* argv[]) {
    // Configuration
    std::string inputDir = "./input";
    std::string queueDir = "./queue";

    // Parse command line arguments (simplified for now)
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--input-dir" && i + 1 < argc) {
            inputDir = argv[++i];
        } else if (arg == "--queue-dir" && i + 1 < argc) {
            queueDir = argv[++i];
        }
    }

    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Database configuration
    DatabaseLoader::DatabaseConfig dbConfig;
    dbConfig.host = "localhost";
    dbConfig.port = 5432;
    dbConfig.database = "fox_db";
    dbConfig.user = "gpu_user";
    dbConfig.password = ""; // Add password if needed

    // Create components
    JobQueue jobQueue(2); // Max 2 retries
    InputProcessor inputProcessor(inputDir, queueDir);
    WorkerPool workerPool(2, jobQueue, dbConfig); // 2 worker threads
    AggregationManager aggManager; // Aggregation manager for post-import processing
    g_workerPool = &workerPool;

    // Set up aggregation callback - triggered after successful imports
    workerPool.setSuccessCallback([&aggManager](FileType fileType) {
        // Map FileType to DataType and mark as pending
        if (fileType == FileType::WORKSTATION) {
            aggManager.mark_data_pending(DataType::WORKSTATION);
        } else if (fileType == FileType::TESTBOARD) {
            aggManager.mark_data_pending(DataType::TESTBOARD);
        } else if (fileType == FileType::SNFN) {
            // SNFN is a testboard aggregation type
            aggManager.mark_data_pending(DataType::TESTBOARD);
        }
        
        // Trigger aggregations (will run if not already running)
        aggManager.trigger_pending_aggregations();
    });

    // Create Input folder watcher (for conversion)
    FileWatcher inputWatcher(inputDir);
    g_inputWatcher = &inputWatcher;

    inputWatcher.setCallback([&inputProcessor](const std::string& filepath) {
        std::cout << "\n[INPUT] File detected: " << filepath << std::endl;
        
        // Process file: convert if needed, move to queue
        if (inputProcessor.processFile(filepath)) {
            std::cout << "  ✓ File processed and moved to queue" << std::endl;
        } else {
            std::cerr << "  ✗ Failed to process file: " << inputProcessor.getLastError() << std::endl;
        }
    });

    // Create Queue folder watcher (for job queue)
    FileWatcher queueWatcher(queueDir);
    g_queueWatcher = &queueWatcher;

    queueWatcher.setCallback([&jobQueue](const std::string& filepath) {
        std::cout << "\n[QUEUE] File detected: " << filepath << std::endl;
        
        // Only .csv files should be in queue folder
        if (!FileConverter::isCsvFile(filepath)) {
            std::cerr << "  ⚠ Warning: File is not CSV format, skipping: " << filepath << std::endl;
            return;
        }
        
        std::string filename = extractFilename(filepath);
        FileType type = FileTypeDetector::detect(filename);
        
        // Skip unknown files (shouldn't happen, but safety check)
        if (type == FileType::UNKNOWN) {
            std::cerr << "  ⚠ Warning: Unknown file type in queue, skipping" << std::endl;
            return;
        }
        
        // Add to JobQueue
        auto job = jobQueue.addJob(filepath, type);
        if (job) {
            std::cout << "  ✓ Job #" << job->jobId 
                      << " added: " << FileTypeDetector::toString(type)
                      << " -> " << filepath << std::endl;
        } else {
            std::cout << "  ⊗ Skipped (duplicate): " << filepath << std::endl;
        }
    });

    // Check if LibreOffice is available
    if (!FileConverter::isLibreOfficeAvailable()) {
        std::cerr << "Warning: LibreOffice not found. .xls files cannot be converted." << std::endl;
        std::cerr << "Please install LibreOffice for full functionality." << std::endl;
    }

    // Start components
    std::cout << "Starting FoxETL orchestrator..." << std::endl;
    std::cout << "Input directory: " << inputDir << std::endl;
    std::cout << "Queue directory: " << queueDir << std::endl;
    std::cout << "Database: " << dbConfig.user << "@" << dbConfig.host << ":" << dbConfig.port << "/" << dbConfig.database << std::endl;
    
    // Start worker pool first (so it's ready to process jobs)
    if (!workerPool.start()) {
        std::cerr << "Failed to start worker pool!" << std::endl;
        return 1;
    }
    
    if (!inputWatcher.start()) {
        std::cerr << "Failed to start input folder watcher!" << std::endl;
        workerPool.stop();
        return 1;
    }
    
    if (!queueWatcher.start()) {
        std::cerr << "Failed to start queue folder watcher!" << std::endl;
        inputWatcher.stop();
        workerPool.stop();
        return 1;
    }

    std::cout << "\n✓ FoxETL started. Press Ctrl+C to stop.\n" << std::endl;

    // Main loop - keep running and print status periodically
    while (g_running && (inputWatcher.isRunning() || queueWatcher.isRunning())) {
        sleep(5); // Print status every 5 seconds
        
        if (g_running) {
            printStatus(inputWatcher, queueWatcher, inputProcessor, jobQueue, workerPool);
        }
    }

    // Graceful shutdown
    std::cout << "\nShutting down..." << std::endl;
    workerPool.stop();
    inputWatcher.stop();
    queueWatcher.stop();
    
    std::cout << "FoxETL stopped." << std::endl;
    return 0;
}

