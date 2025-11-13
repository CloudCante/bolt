#include "aggregation_manager.h"
#include <iostream>
#include <thread>
#include <chrono>

void wait_for_aggregations_to_complete(AggregationManager& mgr, const std::string& message) {
    std::cout << message << std::flush;
    while (mgr.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "." << std::flush;
    }
    std::cout << " DONE!\n\n";
}

int main() {
    std::cout << "=== AggregationManager Test ===" << std::endl;
    std::cout << std::endl;
    
    AggregationManager agg_manager;
    
    // Test 1: Basic workstation data flow
    std::cout << "TEST 1: Basic workstation aggregation\n";
    std::cout << "---------------------------------------\n";
    agg_manager.mark_data_pending(DataType::WORKSTATION);
    agg_manager.trigger_pending_aggregations();
    
    // Give it a moment to start
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Is running? " << (agg_manager.is_running() ? "YES" : "NO") << "\n";
    
    // Wait for Test 1 to complete fully
    wait_for_aggregations_to_complete(agg_manager, "Waiting for all 4 workstation aggregations to complete");
    
    // Test 2: Data arrives during aggregation
    std::cout << "TEST 2: Data arrives during aggregation (re-run detection)\n";
    std::cout << "---------------------------------------\n";
    agg_manager.mark_data_pending(DataType::WORKSTATION);
    agg_manager.trigger_pending_aggregations();
    
    // Let it start processing
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    std::cout << ">>> Simulating new workstation file arrival during aggregation...\n";
    agg_manager.mark_data_pending(DataType::WORKSTATION);
    std::cout << "Is running? " << (agg_manager.is_running() ? "YES (should re-run after)" : "NO") << "\n";
    
    // Wait for first run + re-run to complete
    wait_for_aggregations_to_complete(agg_manager, "Waiting for initial aggregation + re-run to complete");
    
    // Test 3: Mixed data types
    std::cout << "TEST 3: Mixed workstation + testboard data\n";
    std::cout << "---------------------------------------\n";
    agg_manager.mark_data_pending(DataType::WORKSTATION);
    agg_manager.mark_data_pending(DataType::TESTBOARD);
    agg_manager.trigger_pending_aggregations();
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Both types should be running now...\n";
    std::cout << "Is running? " << (agg_manager.is_running() ? "YES" : "NO") << "\n";
    
    // Wait for both to complete
    wait_for_aggregations_to_complete(agg_manager, "Waiting for workstation + testboard aggregations to complete");
    
    // Test 4: Force run
    std::cout << "TEST 4: Force run (manual trigger)\n";
    std::cout << "---------------------------------------\n";
    agg_manager.force_run();
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "Force triggered - running all aggregations\n";
    std::cout << "Is running? " << (agg_manager.is_running() ? "YES" : "NO") << "\n";
    
    // Wait for force run to complete
    wait_for_aggregations_to_complete(agg_manager, "Waiting for forced aggregation cycle to complete");
    
    std::cout << "\n=== All Tests Complete ===\n";
    std::cout << "Final state - Is running? " << (agg_manager.is_running() ? "YES" : "NO") << "\n";
    
    return 0;
}

