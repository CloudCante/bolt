#include "aggregation_manager.h"
#include "aggregations.h"
#include <iostream>
#include <thread>
#include <chrono>

void AggregationManager::mark_data_pending(DataType type) {
    if (type == DataType::WORKSTATION) {
        workstation_pending = true;
        std::cout << "[AggregationManager] Workstation data marked as pending\n";
    } else if (type == DataType::TESTBOARD) {
        testboard_pending = true;
        std::cout << "[AggregationManager] Testboard data marked as pending\n";
    }
}

void AggregationManager::trigger_pending_aggregations() {
    // Check if we should run workstation aggregations
    if (workstation_pending.load() && !workstation_running.load()) {
        std::cout << "[AggregationManager] Triggering workstation aggregations...\n";
        workstation_running = true;
        workstation_pending = false;
        
        // Launch in a separate thread
        std::thread([this]() {
            run_workstation_aggregations_impl();
        }).detach();
    }
    
    // Check if we should run testboard aggregations
    if (testboard_pending.load() && !testboard_running.load()) {
        std::cout << "[AggregationManager] Triggering testboard aggregations...\n";
        testboard_running = true;
        testboard_pending = false;
        
        // Launch in a separate thread
        std::thread([this]() {
            run_testboard_aggregations_impl();
        }).detach();
    }
    
    // Check force flag
    if (force_aggregation.load()) {
        std::cout << "[AggregationManager] Force aggregation triggered - running all!\n";
        force_aggregation = false;
        
        // Mark both as pending and trigger them
        workstation_pending = true;
        testboard_pending = true;
        
        // Recursively call to trigger them
        trigger_pending_aggregations();
    }
}

void AggregationManager::run_workstation_aggregations_impl() {
    std::cout << "[AggregationManager] >>> Workstation aggregations started in thread\n";
    
    int result = 0;
    int total_errors = 0;
    
    try {
        // Run TPY Daily Metrics
        std::cout << "[AggregationManager]   - Running tpy_daily_metrics...\n";
        result = run_tpy_daily_metrics_aggregation();
        if (result != 0) {
            std::cerr << "[AggregationManager]   ✗ tpy_daily_metrics failed with code " << result << "\n";
            total_errors++;
        } else {
            std::cout << "[AggregationManager]   ✓ tpy_daily_metrics complete\n";
        }
        
        // Run TPY Weekly Metrics
        std::cout << "[AggregationManager]   - Running tpy_weekly_metrics...\n";
        result = run_tpy_weekly_metrics_aggregation();
        if (result != 0) {
            std::cerr << "[AggregationManager]   ✗ tpy_weekly_metrics failed with code " << result << "\n";
            total_errors++;
        } else {
            std::cout << "[AggregationManager]   ✓ tpy_weekly_metrics complete\n";
        }
        
        // Run PChart Data
        std::cout << "[AggregationManager]   - Running pchart_data...\n";
        result = run_pchart_data_aggregation();
        if (result != 0) {
            std::cerr << "[AggregationManager]   ✗ pchart_data failed with code " << result << "\n";
            total_errors++;
        } else {
            std::cout << "[AggregationManager]   ✓ pchart_data complete\n";
        }
        
        // Run Daily Packing
        std::cout << "[AggregationManager]   - Running daily_packing...\n";
        result = run_daily_packing_aggregation();
        if (result != 0) {
            std::cerr << "[AggregationManager]   ✗ daily_packing failed with code " << result << "\n";
            total_errors++;
        } else {
            std::cout << "[AggregationManager]   ✓ daily_packing complete\n";
        }
        
        if (total_errors == 0) {
            std::cout << "[AggregationManager] >>> Workstation aggregations completed successfully\n";
        } else {
            std::cout << "[AggregationManager] >>> Workstation aggregations completed with " 
                      << total_errors << " errors\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[AggregationManager] ERROR in workstation aggregations: " << e.what() << "\n";
        total_errors = 1;
    }
    
    on_workstation_complete(total_errors);
}

void AggregationManager::run_testboard_aggregations_impl() {
    std::cout << "[AggregationManager] >>> Testboard aggregations started in thread\n";
    
    int result = 0;
    int total_errors = 0;
    
    try {
        // Run Testboard Station Performance
        std::cout << "[AggregationManager]   - Running testboard_station_performance...\n";
        result = run_testboard_station_performance_aggregation();
        if (result != 0) {
            std::cerr << "[AggregationManager]   ✗ testboard_station_performance failed with code " << result << "\n";
            total_errors++;
        } else {
            std::cout << "[AggregationManager]   ✓ testboard_station_performance complete\n";
        }
        
        // Run Fixture Performance
        std::cout << "[AggregationManager]   - Running fixture_performance...\n";
        result = run_fixture_performance_aggregation();
        if (result != 0) {
            std::cerr << "[AggregationManager]   ✗ fixture_performance failed with code " << result << "\n";
            total_errors++;
        } else {
            std::cout << "[AggregationManager]   ✓ fixture_performance complete\n";
        }
        
        // Run SNFN Reports
        std::cout << "[AggregationManager]   - Running snfn_reports...\n";
        result = run_snfn_reports_aggregation();
        if (result != 0) {
            std::cerr << "[AggregationManager]   ✗ snfn_reports failed with code " << result << "\n";
            total_errors++;
        } else {
            std::cout << "[AggregationManager]   ✓ snfn_reports complete\n";
        }
        
        if (total_errors == 0) {
            std::cout << "[AggregationManager] >>> Testboard aggregations completed successfully\n";
        } else {
            std::cout << "[AggregationManager] >>> Testboard aggregations completed with " 
                      << total_errors << " errors\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[AggregationManager] ERROR in testboard aggregations: " << e.what() << "\n";
        total_errors = 1;
    }
    
    on_testboard_complete(total_errors);
}

void AggregationManager::on_workstation_complete(int result) {
    std::cout << "[AggregationManager] Workstation aggregations finished with code: " << result << "\n";
    
    workstation_running = false;
    
    // Check if more data arrived while we were running
    if (workstation_pending.load()) {
        std::cout << "[AggregationManager] New workstation data arrived during aggregation, re-running...\n";
        trigger_pending_aggregations();
    }
}

void AggregationManager::on_testboard_complete(int result) {
    std::cout << "[AggregationManager] Testboard aggregations finished with code: " << result << "\n";
    
    testboard_running = false;
    
    // Check if more data arrived while we were running
    if (testboard_pending.load()) {
        std::cout << "[AggregationManager] New testboard data arrived during aggregation, re-running...\n";
        trigger_pending_aggregations();
    }
}

bool AggregationManager::is_running() const {
    return workstation_running.load() || testboard_running.load();
}

void AggregationManager::force_run() {
    std::cout << "[AggregationManager] Force run requested\n";
    force_aggregation = true;
    trigger_pending_aggregations();
}

