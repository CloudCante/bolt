#include <iostream>
#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <map>
#include <iomanip>
#include <sstream>
#include <cmath>
#include "config.h"

struct WeekData {
    std::string week_id;
    std::string week_start;
    std::string week_end;
    int total_starters;
    std::vector<std::string> week_starters;
    std::map<std::string, int> by_model;
};

struct DailyCompletions {
    int completed_today;
    int first_pass_today;
    double daily_fpy;
    std::map<std::string, std::pair<int, int>> by_model; // model -> (completed, first_pass)
};

WeekData calculate_weekly_starters_for_date(pqxx::connection& conn, const std::string& target_date) {
    WeekData result;
    
    pqxx::work txn(conn);
    
    // Get week bounds and ID from PostgreSQL
    auto week_info = txn.exec_params(
        "SELECT "
        "  TO_CHAR($1::date, 'IYYY-IW') as week_id, "
        "  DATE_TRUNC('week', $1::date)::date as week_start, "
        "  (DATE_TRUNC('week', $1::date) + INTERVAL '6 days')::date as week_end",
        target_date
    );
    
    result.week_id = week_info[0]["week_id"].c_str();
    result.week_start = week_info[0]["week_start"].c_str();
    result.week_end = week_info[0]["week_end"].c_str();
    
    // Get starters for the week
    auto starters = txn.exec_params(R"SQL(
        WITH first_activity AS (
            SELECT 
                sn,
                model,
                MIN(history_station_end_time) as first_activity_time
            FROM workstation_master_log
            WHERE service_flow NOT IN ('NC Sort', 'RO')
                AND service_flow IS NOT NULL
            GROUP BY sn, model
        )
        SELECT 
            model,
            COUNT(*) as count,
            ARRAY_AGG(sn) as parts
        FROM first_activity
        WHERE first_activity_time >= $1::date 
            AND first_activity_time < ($2::date + INTERVAL '1 day')
        GROUP BY model
        ORDER BY model
    )SQL", result.week_start, result.week_end);
    
    result.total_starters = 0;
    
    for (auto row : starters) {
        std::string model = row["model"].c_str();
        int count = row["count"].as<int>();
        
        result.total_starters += count;
        result.by_model[model] = count;
        
        // Extract serial numbers from array
        std::string parts_str = row["parts"].c_str();
        pqxx::array_parser parts_parser(parts_str);
        auto elem = parts_parser.get_next();
        while (elem.first != pqxx::array_parser::juncture::done) {
            if (elem.first == pqxx::array_parser::juncture::string_value) {
                result.week_starters.push_back(std::string(elem.second));
            }
            elem = parts_parser.get_next();
        }
    }
    
    txn.commit();
    return result;
}

DailyCompletions calculate_daily_completions_from_week_starters(
    pqxx::connection& conn, 
    const std::string& target_date,
    const std::vector<std::string>& week_starters
) {
    DailyCompletions result = {0, 0, 0.0, {}};
    
    if (week_starters.empty()) {
        return result;
    }
    
    pqxx::work txn(conn);
    
    // Convert vector to PostgreSQL array format
    std::string parts_array = "ARRAY[";
    for (size_t i = 0; i < week_starters.size(); ++i) {
        if (i > 0) parts_array += ",";
        parts_array += txn.quote(week_starters[i]);
    }
    parts_array += "]";
    
    std::string query = R"SQL(
        WITH completion_check AS (
            SELECT 
                sn,
                model,
                COUNT(CASE WHEN workstation_name = 'PACKING' THEN 1 END) as reached_packing,
                COUNT(CASE WHEN history_station_passing_status != 'Pass' THEN 1 END) as failure_count
            FROM workstation_master_log
            WHERE sn = ANY()SQL" + parts_array + R"SQL()
                AND history_station_end_time >= $1::date 
                AND history_station_end_time < ($1::date + INTERVAL '1 day')
                AND service_flow NOT IN ('NC Sort', 'RO')
                AND service_flow IS NOT NULL
            GROUP BY sn, model
        )
        SELECT 
            model,
            COUNT(*) as completed_today,
            COUNT(CASE WHEN reached_packing > 0 AND failure_count = 0 THEN 1 END) as first_pass_today
        FROM completion_check
        WHERE reached_packing > 0
        GROUP BY model
    )SQL";
    
    auto completions = txn.exec_params(query, target_date);
    
    for (auto row : completions) {
        std::string model = row["model"].c_str();
        int completed = row["completed_today"].as<int>();
        int first_pass = row["first_pass_today"].as<int>();
        
        result.completed_today += completed;
        result.first_pass_today += first_pass;
        result.by_model[model] = {completed, first_pass};
    }
    
    if (result.completed_today > 0) {
        result.daily_fpy = (result.first_pass_today * 100.0) / result.completed_today;
    }
    
    txn.commit();
    return result;
}

void aggregate_daily_tpy_for_date(pqxx::connection& conn, const std::string& target_date) {
    // Reduced logging - only calculate, don't print details
    WeekData week_data = calculate_weekly_starters_for_date(conn, target_date);
    
    DailyCompletions daily_completions = calculate_daily_completions_from_week_starters(
        conn, target_date, week_data.week_starters
    );
    
    pqxx::work txn(conn);
    
    auto station_results = txn.exec_params(R"SQL(
        SELECT 
            model,
            workstation_name,
            COUNT(*) as total_parts,
            COUNT(CASE WHEN history_station_passing_status = 'Pass' THEN 1 END) as passed_parts,
            COUNT(CASE WHEN history_station_passing_status != 'Pass' THEN 1 END) as failed_parts
        FROM workstation_master_log 
        WHERE history_station_end_time >= $1::date 
            AND history_station_end_time < ($1::date + INTERVAL '1 day')
            AND service_flow NOT IN ('NC Sort', 'RO')
            AND service_flow IS NOT NULL
            AND (model IN ('Tesla SXM4', 'Tesla SXM5') OR model = 'SXM6')
        GROUP BY model, workstation_name
        HAVING COUNT(*) >= 1
        ORDER BY model, total_parts DESC
    )SQL", target_date);
    
    int inserted_count = 0;
    
    for (auto row : station_results) {
        std::string model = row["model"].c_str();
        std::string workstation = row["workstation_name"].c_str();
        int total = row["total_parts"].as<int>();
        int passed = row["passed_parts"].as<int>();
        int failed = row["failed_parts"].as<int>();
        
        double throughput_yield = total > 0 ? (passed * 100.0) / total : 0.0;
        
        txn.exec_params(R"SQL(
            INSERT INTO daily_tpy_metrics 
                (date_id, model, workstation_name, total_parts, passed_parts, failed_parts, throughput_yield,
                 week_id, week_start, week_end, total_starters)
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
            ON CONFLICT (date_id, model, workstation_name) 
            DO UPDATE SET
                total_parts = EXCLUDED.total_parts,
                passed_parts = EXCLUDED.passed_parts,
                failed_parts = EXCLUDED.failed_parts,
                throughput_yield = EXCLUDED.throughput_yield,
                week_id = EXCLUDED.week_id,
                week_start = EXCLUDED.week_start,
                week_end = EXCLUDED.week_end,
                total_starters = EXCLUDED.total_starters,
                created_at = NOW()
        )SQL", 
            target_date, model, workstation, total, passed, failed, 
            std::round(throughput_yield * 100) / 100,
            week_data.week_id, week_data.week_start, week_data.week_end, 
            week_data.total_starters
        );
        
        inserted_count++;
    }
    
    txn.commit();
    // Reduced logging - no per-date output
}

std::vector<std::string> get_all_available_dates(pqxx::connection& conn) {
    std::cout << "Finding dates with actual test activity..." << std::endl;
    
    pqxx::work txn(conn);
    
    // Check config for aggregation mode
    std::string date_filter = "";
    if (config::AGGREGATION_MODE == "last_N_days") {
        std::cout << "  Mode: Last " << config::AGGREGATION_DAYS_BACK << " days" << std::endl;
        date_filter = " AND DATE(history_station_end_time) >= CURRENT_DATE - INTERVAL '" 
                    + std::to_string(config::AGGREGATION_DAYS_BACK) + " days'";
    } else {
        std::cout << "  Mode: All-time aggregation" << std::endl;
    }
    
    std::string query = R"SQL(
        SELECT DISTINCT DATE(history_station_end_time) as test_date
        FROM workstation_master_log
        WHERE history_station_end_time IS NOT NULL
            AND service_flow NOT IN ('NC Sort', 'RO')
            AND service_flow IS NOT NULL
    )SQL" + date_filter + R"SQL(
        ORDER BY test_date
    )SQL";
    
    auto results = txn.exec(query);
    
    std::vector<std::string> dates;
    for (auto row : results) {
        dates.push_back(row["test_date"].c_str());
    }
    
    if (!dates.empty()) {
        std::cout << "  Found " << dates.size() << " dates with test activity from " 
                  << dates.front() << " to " << dates.back() << std::endl;
    }
    
    txn.commit();
    return dates;
}

// Callable function for aggregation manager
int run_tpy_daily_metrics_aggregation() {
    try {
        std::string conn_str = "dbname=" + config::DB_NAME + 
                                " user=" + config::DB_USER + 
                                " host=" + config::DB_HOST + 
                                " port=" + std::to_string(config::DB_PORT);
        
        if (!config::DB_PASSWORD.empty()) {
            conn_str += " password=" + config::DB_PASSWORD;
        }
        
        pqxx::connection conn(conn_str);
        
        std::cout << "DAILY TPY METRICS ALL-TIME AGGREGATOR" << std::endl;
        std::cout << std::string(50, '=') << std::endl;
        
        auto all_dates = get_all_available_dates(conn);
        
        if (all_dates.empty()) {
            std::cout << "No valid dates found in the dataset" << std::endl;
            return 0;  // Not an error, just no data
        }
        
        std::cout << "\nProcessing ALL " << all_dates.size() << " historical dates..." << std::endl;
        
        int success_count = 0;
        int error_count = 0;
        
        for (size_t i = 0; i < all_dates.size(); ++i) {
            try {
                // Only log progress every 50 dates to reduce output
                if (i % 50 == 0 || i == all_dates.size() - 1) {
                    std::cout << "Processing dates: " << (i + 1) << "/" << all_dates.size() 
                              << " (" << all_dates[i] << ")" << std::endl;
                }
                
                aggregate_daily_tpy_for_date(conn, all_dates[i]);
                success_count++;
                
            } catch (const std::exception& e) {
                std::cerr << "ERROR processing " << all_dates[i] << ": " << e.what() << std::endl;
                error_count++;
            }
        }
        
        std::cout << "\nDAILY TPY ALL-TIME AGGREGATION COMPLETE!" << std::endl;
        std::cout << "Successfully processed: " << success_count << " dates" << std::endl;
        std::cout << "Errors: " << error_count << " dates" << std::endl;
        
        // Show sample results
        pqxx::work txn(conn);
        auto count_result = txn.exec("SELECT COUNT(*) FROM daily_tpy_metrics");
        int total_records = count_result[0][0].as<int>();
        std::cout << "Total records in daily_tpy_metrics: " << total_records << std::endl;
        
        if (total_records > 0) {
            auto samples = txn.exec(R"SQL(
                SELECT date_id, model, workstation_name, throughput_yield 
                FROM daily_tpy_metrics 
                ORDER BY date_id DESC, throughput_yield DESC 
                LIMIT 5
            )SQL");
            
            std::cout << "\nSAMPLE RESULTS:" << std::endl;
            for (auto row : samples) {
                std::cout << "  " << row["date_id"].c_str() << " " 
                          << row["model"].c_str() << " " 
                          << row["workstation_name"].c_str() << ": "
                          << std::setprecision(1) << row["throughput_yield"].as<double>() 
                          << "%" << std::endl;
            }
        }
        
        txn.commit();
        
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
    return run_tpy_daily_metrics_aggregation();
}
#endif

