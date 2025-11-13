#include <iostream>
#include <pqxx/pqxx>
#include <string>
#include "config.h"

static const std::string CREATE_TABLE_SQL = R"SQL(
CREATE TABLE IF NOT EXISTS snfn_aggregate_daily (
    fixture_no TEXT NOT NULL,
    workstation_name TEXT NOT NULL,
    sn TEXT NOT NULL,
    pn TEXT,
    model TEXT NOT NULL,
    error_code TEXT NOT NULL,
    error_disc TEXT,
    history_station_end_time TIMESTAMP NOT NULL,
    PRIMARY KEY (sn, fixture_no, model, workstation_name, error_code, history_station_end_time)
);
)SQL";

static const std::string AGGREGATE_SQL = R"SQL(
INSERT INTO snfn_aggregate_daily (
    fixture_no, workstation_name, sn, pn, model, error_code, error_disc, history_station_end_time
)
SELECT DISTINCT
    fixture_no,
    workstation_name,
    sn,
    pn,
    model,
    CONCAT('EC', RIGHT(failure_reasons, 3)) AS error_code,
    failure_note AS error_disc,
    history_station_end_time
FROM testboard_master_log
WHERE history_station_end_time IS NOT NULL
    AND history_station_passing_status = 'Fail'
    AND fixture_no NOT IN ('NCS039-01', 'NCS039-02', 'NCS039-03', 'NCS039-04',
                           'NCS040-01', 'NCS040-02', 'NCS040-03', 'NCS040-04',
                           'NCS041-01', 'NCS041-02', 'NCS041-03', 'NCS041-04',
                           'NCS042-01', 'NCS042-02', 'NCS042-03', 'NCS042-04',
                           'NCS043-01', 'NCS043-02', 'NCS043-03', 'NCS043-04')
ORDER BY history_station_end_time DESC
ON CONFLICT (sn, fixture_no, model, workstation_name, error_code, history_station_end_time)
DO UPDATE SET
    error_code = EXCLUDED.error_code,
    error_disc = EXCLUDED.error_disc,
    pn = EXCLUDED.pn;
)SQL";

// Callable function for aggregation manager
int run_snfn_reports_aggregation() {
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
        
        std::cout << "Creating snfn_aggregate_daily table if not exists..." << std::endl;
        txn.exec(CREATE_TABLE_SQL);
        
        // Build date filter based on config
        std::string date_filter = "";
        if (config::AGGREGATION_MODE == "last_N_days") {
            std::cout << "Aggregating SNFN report data (last " << config::AGGREGATION_DAYS_BACK << " days)..." << std::endl;
            date_filter = " AND DATE(history_station_end_time) >= CURRENT_DATE - INTERVAL '" 
                        + std::to_string(config::AGGREGATION_DAYS_BACK) + " days'";
        } else {
            std::cout << "Aggregating SNFN report data..." << std::endl;
        }
        
        std::string aggregate_sql = R"SQL(
INSERT INTO snfn_aggregate_daily (
    fixture_no, workstation_name, sn, pn, model, error_code, error_disc, history_station_end_time
)
SELECT DISTINCT
    fixture_no, workstation_name, sn, pn, model,
    CONCAT('EC', RIGHT(failure_reasons, 3)) AS error_code,
    failure_note AS error_disc,
    history_station_end_time
FROM testboard_master_log
WHERE history_station_end_time IS NOT NULL
    AND history_station_passing_status = 'Fail'
    AND fixture_no NOT IN ('NCS039-01', 'NCS039-02', 'NCS039-03', 'NCS039-04',
                           'NCS040-01', 'NCS040-02', 'NCS040-03', 'NCS040-04',
                           'NCS041-01', 'NCS041-02', 'NCS041-03', 'NCS041-04',
                           'NCS042-01', 'NCS042-02', 'NCS042-03', 'NCS042-04',
                           'NCS043-01', 'NCS043-02', 'NCS043-03', 'NCS043-04')
)SQL" + date_filter + R"SQL(
ORDER BY history_station_end_time DESC
ON CONFLICT (sn, fixture_no, model, workstation_name, error_code, history_station_end_time)
DO UPDATE SET
    error_code = EXCLUDED.error_code, error_disc = EXCLUDED.error_disc, pn = EXCLUDED.pn;
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
    return run_snfn_reports_aggregation();
}
#endif

