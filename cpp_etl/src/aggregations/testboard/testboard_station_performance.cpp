#include <iostream>
#include <pqxx/pqxx>
#include <string>
#include "config.h"

static const std::string CREATE_TABLE_SQL = R"SQL(
CREATE TABLE IF NOT EXISTS testboard_station_performance_daily (
    end_date DATE NOT NULL,
    model TEXT NOT NULL,
    work_station_process TEXT NOT NULL,
    workstation_name TEXT NOT NULL,
    pass INTEGER NOT NULL,
    fail INTEGER NOT NULL,
    total INTEGER NOT NULL,
    failurerate NUMERIC(5,3) NOT NULL,
    PRIMARY KEY (end_date, model, work_station_process, workstation_name)
);
)SQL";

// Note: SQL is built dynamically based on config
static const std::string BASE_AGGREGATE_SQL = R"SQL(
INSERT INTO testboard_station_performance_daily (
    end_date, model, work_station_process, workstation_name, pass, fail, total, failurerate
)
SELECT
    DATE(history_station_end_time) AS end_date,
    model,
    work_station_process,
    workstation_name,
    COUNT(CASE WHEN history_station_passing_status = 'Pass' THEN 1 END) AS pass,
    COUNT(CASE WHEN history_station_passing_status = 'Fail' THEN 1 END) AS fail,
    COUNT(*) AS total,
    ROUND(
        COUNT(CASE WHEN history_station_passing_status = 'Fail' THEN 1 END)::numeric /
        NULLIF(COUNT(*), 0), 3
    ) AS failurerate
FROM testboard_master_log
WHERE history_station_end_time IS NOT NULL
    AND fixture_no NOT IN ('NCS039-01', 'NCS039-02', 'NCS039-03', 'NCS039-04',
                           'NCS040-01', 'NCS040-02', 'NCS040-03', 'NCS040-04',
                           'NCS041-01', 'NCS041-02', 'NCS041-03', 'NCS041-04',
                           'NCS042-01', 'NCS042-02', 'NCS042-03', 'NCS042-04',
                           'NCS043-01', 'NCS043-02', 'NCS043-03', 'NCS043-04')
GROUP BY end_date, model, work_station_process, workstation_name
ORDER BY end_date DESC, model, work_station_process, workstation_name
ON CONFLICT (end_date, model, work_station_process, workstation_name)
DO UPDATE SET
    pass = EXCLUDED.pass,
    fail = EXCLUDED.fail,
    total = EXCLUDED.total,
    failurerate = EXCLUDED.failurerate;
)SQL";

// Callable function for aggregation manager
int run_testboard_station_performance_aggregation() {
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
        
        std::cout << "Creating testboard_station_performance_daily table if not exists..." << std::endl;
        txn.exec(CREATE_TABLE_SQL);
        
        // Build date filter based on config
        std::string date_filter = "";
        if (config::AGGREGATION_MODE == "last_N_days") {
            std::cout << "Aggregating testboard station performance data (last " << config::AGGREGATION_DAYS_BACK << " days)..." << std::endl;
            date_filter = " AND DATE(history_station_end_time) >= CURRENT_DATE - INTERVAL '" 
                        + std::to_string(config::AGGREGATION_DAYS_BACK) + " days'";
        } else {
            std::cout << "Aggregating testboard station performance data..." << std::endl;
        }
        
        std::string aggregate_sql = R"SQL(
INSERT INTO testboard_station_performance_daily (
    end_date, model, work_station_process, workstation_name, pass, fail, total, failurerate
)
SELECT
    DATE(history_station_end_time) AS end_date, model, work_station_process, workstation_name,
    COUNT(CASE WHEN history_station_passing_status = 'Pass' THEN 1 END) AS pass,
    COUNT(CASE WHEN history_station_passing_status = 'Fail' THEN 1 END) AS fail,
    COUNT(*) AS total,
    ROUND(COUNT(CASE WHEN history_station_passing_status = 'Fail' THEN 1 END)::numeric / NULLIF(COUNT(*), 0), 3) AS failurerate
FROM testboard_master_log
WHERE history_station_end_time IS NOT NULL
    AND fixture_no NOT IN ('NCS039-01', 'NCS039-02', 'NCS039-03', 'NCS039-04',
                           'NCS040-01', 'NCS040-02', 'NCS040-03', 'NCS040-04',
                           'NCS041-01', 'NCS041-02', 'NCS041-03', 'NCS041-04',
                           'NCS042-01', 'NCS042-02', 'NCS042-03', 'NCS042-04',
                           'NCS043-01', 'NCS043-02', 'NCS043-03', 'NCS043-04')
)SQL" + date_filter + R"SQL(
GROUP BY end_date, model, work_station_process, workstation_name
ORDER BY end_date DESC, model, work_station_process, workstation_name
ON CONFLICT (end_date, model, work_station_process, workstation_name)
DO UPDATE SET
    pass = EXCLUDED.pass, fail = EXCLUDED.fail, total = EXCLUDED.total, failurerate = EXCLUDED.failurerate;
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
    return run_testboard_station_performance_aggregation();
}
#endif

