#ifndef AGGREGATION_MANAGER_H
#define AGGREGATION_MANAGER_H

#include <string>
#include <atomic>
#include <mutex>

enum class DataType {
    WORKSTATION,
    TESTBOARD
};

class AggregationManager {
private:
    std::atomic<bool> workstation_running{false};
    std::atomic<bool> testboard_running{false};
    std::atomic<bool> workstation_pending{false};
    std::atomic<bool> testboard_pending{false};
    std::atomic<bool> force_aggregation{false};
    
    std::mutex state_mutex;
    
    // Private methods for running aggregations
    void run_workstation_aggregations_impl();
    void run_testboard_aggregations_impl();
    
    // Completion handlers
    void on_workstation_complete(int result);
    void on_testboard_complete(int result);
    
public:
    void mark_data_pending(DataType type);
    void trigger_pending_aggregations();
    bool is_running() const;
    
    // For debugging/CLI
    void force_run();
};

#endif

