#pragma once
#include <string>

namespace config {
    // Database Configuration
    const std::string DB_HOST = "localhost";
    const int DB_PORT = 5432;
    const std::string DB_NAME = "fox_db";
    const std::string DB_USER = "gpu_user";
    const std::string DB_PASSWORD = "";
    
    // Aggregation Configuration
    // Mode: "all_time" = aggregate all historical data
    //       "last_N_days" = only aggregate last N days
    const std::string AGGREGATION_MODE = "last_N_days";
    
    // If AGGREGATION_MODE = "last_N_days", this defines how many days back
    const int AGGREGATION_DAYS_BACK = 7;  // Last 7 days for testing
    
    // If AGGREGATION_MODE = "last_N_days", this defines how many weeks back for weekly aggregations
    const int AGGREGATION_WEEKS_BACK = 2;  // Last 2 weeks for testing
}