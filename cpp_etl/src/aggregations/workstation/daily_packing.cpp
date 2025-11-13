#include <iostream>
#include <pqxx/pqxx>
#include <string>
#include "config.h"

// Connection string computed at runtime
namespace {
    std::string get_conn_str() {
        return "dbname=" + config::DB_NAME + 
               " user=" + config::DB_USER + 
               " password=" + config::DB_PASSWORD + 
               " host=" + config::DB_HOST + 
               " port=" + std::to_string(config::DB_PORT);
    }
}

static const std::string CREATE_TABLE_SQL = R"SQL(
CREATE TABLE IF NOT EXISTS packing_daily_summary (
    pack_date DATE NOT NULL,
    model TEXT NOT NULL,
    part_number TEXT NOT NULL,
    packed_count INTEGER NOT NULL,
    PRIMARY KEY (pack_date, model, part_number)
);
)SQL";

// Note: SQL is built dynamically based on config

// Callable function for aggregation manager
int run_daily_packing_aggregation() {
    try {
        pqxx::connection conn(get_conn_str());
        
        pqxx::work txn(conn);
        
        std::cout << "Creating packing_daily_summary table with primary key if not exists..." << std::endl;
        txn.exec(CREATE_TABLE_SQL);
        
        // Build date filter based on config
        std::string date_filter = "";
        if (config::AGGREGATION_MODE == "last_N_days") {
            std::cout << "Aggregating packing data (last " << config::AGGREGATION_DAYS_BACK << " days)..." << std::endl;
            date_filter = " AND history_station_end_time >= CURRENT_DATE - INTERVAL '" 
                        + std::to_string(config::AGGREGATION_DAYS_BACK) + " days'";
        } else {
            std::cout << "Aggregating all historical packing data..." << std::endl;
        }
        
        std::string aggregate_sql = R"SQL(
INSERT INTO packing_daily_summary (
    pack_date, model, part_number, packed_count
)
SELECT
    CASE
        WHEN EXTRACT(DOW FROM history_station_end_time) = 6 THEN DATE(history_station_end_time) - INTERVAL '1 day'
        WHEN EXTRACT(DOW FROM history_station_end_time) = 0 THEN DATE(history_station_end_time) - INTERVAL '2 days'
        ELSE DATE(history_station_end_time)
    END AS pack_date,
    model, pn AS part_number, COUNT(*) AS packed_count
FROM workstation_master_log
WHERE workstation_name = 'PACKING'
  AND history_station_passing_status = 'Pass'
)SQL" + date_filter + R"SQL(
GROUP BY pack_date, model, part_number
ORDER BY model, part_number, pack_date
ON CONFLICT (pack_date, model, part_number) DO UPDATE SET
    packed_count = EXCLUDED.packed_count;
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
    return run_daily_packing_aggregation();
}
#endif