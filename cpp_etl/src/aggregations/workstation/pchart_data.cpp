#include <iostream>
#include <pqxx/pqxx>
#include <string>
#include "config.h"

static const std::string CREATE_TABLE_SQL = R"SQL(
CREATE TABLE IF NOT EXISTS workstation_pchart_daily (
    date DATE NOT NULL,
    pn VARCHAR(255) NOT NULL,
    model VARCHAR(255),
    workstation_name VARCHAR(255) NOT NULL,
    service_flow VARCHAR(255),
    total_count INTEGER NOT NULL,
    pass_count INTEGER NOT NULL,
    fail_count INTEGER NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (date, pn, workstation_name, service_flow)
);
)SQL";

// Note: SQL is built dynamically based on config

// Callable function for aggregation manager
int run_pchart_data_aggregation() {
    try {
        // Build connection string from config
        std::string conn_str = "dbname=" + config::DB_NAME + 
                                " user=" + config::DB_USER + 
                                " host=" + config::DB_HOST + 
                                " port=" + std::to_string(config::DB_PORT);
        
        if (!config::DB_PASSWORD.empty()) {
            conn_str += " password=" + config::DB_PASSWORD;
        }
        
        pqxx::connection conn(conn_str);
        pqxx::work txn(conn);
        
        std::cout << "Creating workstation_pchart_daily table if not exists..." << std::endl;
        txn.exec(CREATE_TABLE_SQL);
        
        // Build date filter based on config
        std::string date_filter = "";
        if (config::AGGREGATION_MODE == "last_N_days") {
            std::cout << "Aggregating P-Chart daily data (last " << config::AGGREGATION_DAYS_BACK << " days)..." << std::endl;
            date_filter = " AND DATE(history_station_end_time) >= CURRENT_DATE - INTERVAL '" 
                        + std::to_string(config::AGGREGATION_DAYS_BACK) + " days'";
        } else {
            std::cout << "Aggregating P-Chart daily data (all-time)..." << std::endl;
        }
        
        std::string aggregate_sql = R"SQL(
INSERT INTO workstation_pchart_daily (
    date, pn, model, workstation_name, service_flow, total_count, pass_count, fail_count
)
SELECT 
    DATE(history_station_end_time) as date, pn, model, workstation_name, service_flow,
    COUNT(*) as total_count,
    COUNT(CASE WHEN history_station_passing_status = 'Pass' THEN 1 END) as pass_count,
    COUNT(CASE WHEN history_station_passing_status != 'Pass' THEN 1 END) as fail_count
FROM workstation_master_log
WHERE history_station_end_time IS NOT NULL
    AND service_flow NOT IN ('NC Sort', 'RO')
    AND service_flow IS NOT NULL
)SQL" + date_filter + R"SQL(
GROUP BY DATE(history_station_end_time), pn, model, workstation_name, service_flow
ORDER BY DATE(history_station_end_time), pn, workstation_name
ON CONFLICT (date, pn, workstation_name, service_flow)
DO UPDATE SET
    model = EXCLUDED.model, total_count = EXCLUDED.total_count,
    pass_count = EXCLUDED.pass_count, fail_count = EXCLUDED.fail_count;
)SQL";
        
        pqxx::result result = txn.exec(aggregate_sql);
        
        txn.commit();
        
        std::cout << "Aggregated and upserted " << result.affected_rows() << " records." << std::endl;
        
        // Clean exit - connection auto-closes via RAII
        return 0;
        
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;  // Clean exit on error
    }
}

// Keep main() for standalone testing
#ifndef NO_MAIN_AGGREGATION
int main() {
    return run_pchart_data_aggregation();
}
#endif

