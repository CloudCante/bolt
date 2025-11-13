#include <iostream>
#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <map>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <set>
#include "config.h"

struct WeeklyFPY {
    int parts_started;
    int first_pass_success;
    double first_pass_yield;
    int active_parts;
    double completed_only_fpy;
    int parts_completed;
    int parts_failed;
    int parts_stuck;
};

struct StationMetrics {
    int total_parts;
    int passed_parts;
    int failed_parts;
    double throughput_yield;
};

struct ModelTPY {
    std::map<std::string, double> stations;
    double tpy;
    int station_count;
};

std::pair<std::string, std::string> get_week_date_range(const std::string& week_id) {
    // Parse week_id like "2025-W22"
    size_t pos = week_id.find("-W");
    int year = std::stoi(week_id.substr(0, pos));
    int week = std::stoi(week_id.substr(pos + 2));
    
    // Use PostgreSQL to calculate ISO week dates
    std::string query = "SELECT "
        "  (DATE '" + std::to_string(year) + "-01-04' - (EXTRACT(ISODOW FROM DATE '" + std::to_string(year) + "-01-04')::int - 1) * INTERVAL '1 day' + (" + std::to_string(week - 1) + " * INTERVAL '1 week'))::date AS week_start, "
        "  (DATE '" + std::to_string(year) + "-01-04' - (EXTRACT(ISODOW FROM DATE '" + std::to_string(year) + "-01-04')::int - 1) * INTERVAL '1 day' + (" + std::to_string(week - 1) + " * INTERVAL '1 week') + INTERVAL '6 days')::date AS week_end";
    
    return {week_id, week_id}; // Placeholder - we'll calculate in SQL
}

WeeklyFPY calculate_weekly_first_pass_yield(pqxx::connection& conn, const std::string& week_start, const std::string& week_end) {
    pqxx::work txn(conn);
    
    auto result = txn.exec_params(R"SQL(
        WITH part_analysis AS (
            SELECT 
                sn,
                model,
                COUNT(CASE WHEN workstation_name = 'PACKING' THEN 1 END) as reached_packing,
                COUNT(CASE WHEN history_station_passing_status != 'Pass' THEN 1 END) as failure_count
            FROM workstation_master_log
            WHERE history_station_end_time >= $1::date
                AND history_station_end_time < ($2::date + INTERVAL '1 day')
                AND service_flow NOT IN ('NC Sort', 'RO')
                AND service_flow IS NOT NULL
            GROUP BY sn, model
        )
        SELECT 
            COUNT(*) as parts_started,
            COUNT(CASE WHEN reached_packing > 0 AND failure_count = 0 THEN 1 END) as first_pass_success,
            COUNT(CASE WHEN reached_packing > 0 THEN 1 END) as parts_completed,
            COUNT(CASE WHEN failure_count > 0 THEN 1 END) as parts_failed,
            COUNT(CASE WHEN reached_packing = 0 AND failure_count = 0 THEN 1 END) as parts_stuck_in_limbo
        FROM part_analysis
    )SQL", week_start, week_end);
    
    WeeklyFPY fpy = {};
    
    if (!result.empty() && result[0][0].as<int>() > 0) {
        fpy.parts_started = result[0][0].as<int>();
        fpy.first_pass_success = result[0][1].as<int>();
        fpy.parts_completed = result[0][2].as<int>();
        fpy.parts_failed = result[0][3].as<int>();
        fpy.parts_stuck = result[0][4].as<int>();
        
        fpy.first_pass_yield = fpy.parts_started > 0 ? 
            (fpy.first_pass_success * 100.0) / fpy.parts_started : 0.0;
        
        fpy.active_parts = fpy.parts_completed + fpy.parts_failed;
        fpy.completed_only_fpy = fpy.active_parts > 0 ?
            (fpy.first_pass_success * 100.0) / fpy.active_parts : 0.0;
    }
    
    txn.commit();
    return fpy;
}

std::map<std::string, std::map<std::string, StationMetrics>> calculate_model_specific_yields(
    pqxx::connection& conn, 
    const std::string& week_start, 
    const std::string& week_end
) {
    pqxx::work txn(conn);
    
    auto results = txn.exec_params(R"SQL(
        SELECT 
            model,
            workstation_name,
            COUNT(*) as total_parts,
            COUNT(CASE WHEN history_station_passing_status = 'Pass' THEN 1 END) as passed_parts,
            COUNT(CASE WHEN history_station_passing_status != 'Pass' THEN 1 END) as failed_parts
        FROM workstation_master_log 
        WHERE history_station_end_time >= $1::date
            AND history_station_end_time < ($2::date + INTERVAL '1 day')
            AND service_flow NOT IN ('NC Sort', 'RO')
            AND service_flow IS NOT NULL
            AND (model IN ('Tesla SXM4', 'Tesla SXM5') OR model = 'SXM6')
        GROUP BY model, workstation_name
        HAVING COUNT(*) >= 1
        ORDER BY model, total_parts DESC
    )SQL", week_start, week_end);
    
    std::map<std::string, std::map<std::string, StationMetrics>> model_yields;
    model_yields["overall"] = {};
    
    for (auto row : results) {
        std::string model = row["model"].c_str();
        std::string station = row["workstation_name"].c_str();
        int total = row["total_parts"].as<int>();
        int passed = row["passed_parts"].as<int>();
        int failed = row["failed_parts"].as<int>();
        
        double yield = total > 0 ? (passed * 100.0) / total : 0.0;
        
        model_yields[model][station] = {total, passed, failed, yield};
        
        // Update overall
        if (model_yields["overall"].find(station) == model_yields["overall"].end()) {
            model_yields["overall"][station] = {0, 0, 0, 0.0};
        }
        model_yields["overall"][station].total_parts += total;
        model_yields["overall"][station].passed_parts += passed;
        model_yields["overall"][station].failed_parts += failed;
    }
    
    // Calculate overall yields
    for (auto& [station, metrics] : model_yields["overall"]) {
        metrics.throughput_yield = metrics.total_parts > 0 ?
            (metrics.passed_parts * 100.0) / metrics.total_parts : 0.0;
    }
    
    txn.commit();
    return model_yields;
}

std::map<std::string, ModelTPY> calculate_hardcoded_tpy(
    const std::map<std::string, std::map<std::string, StationMetrics>>& model_yields
) {
    std::map<std::string, ModelTPY> hardcoded_tpy;
    
    // SXM4: VI2 × ASSY2 × FI × FQC
    std::vector<std::string> sxm4_stations = {"VI2", "ASSY2", "FI", "FQC"};
    ModelTPY sxm4 = {{}, 0.0, 0};
    
    if (model_yields.find("Tesla SXM4") != model_yields.end()) {
        double tpy_value = 1.0;
        int count = 0;
        
        for (const auto& station : sxm4_stations) {
            auto it = model_yields.at("Tesla SXM4").find(station);
            if (it != model_yields.at("Tesla SXM4").end()) {
                double yield_pct = it->second.throughput_yield;
                sxm4.stations[station] = yield_pct;
                tpy_value *= (yield_pct / 100.0);
                count++;
            }
        }
        
        if (count == 4) {
            sxm4.tpy = std::round(tpy_value * 10000) / 100.0;
        }
    }
    hardcoded_tpy["SXM4"] = sxm4;
    
    // SXM5/6: BBD × ASSY2 × FI × FQC
    std::vector<std::string> sxm5_stations = {"BBD", "ASSY2", "FI", "FQC"};
    
    for (const auto& model_short : {"SXM5", "SXM6"}) {
        std::string model_key = std::string(model_short) == "SXM5" ? "Tesla SXM5" : "SXM6";
        ModelTPY model_tpy = {{}, 0.0, 0};
        
        if (model_yields.find(model_key) != model_yields.end()) {
            double tpy_value = 1.0;
            int count = 0;
            
            for (const auto& station : sxm5_stations) {
                auto it = model_yields.at(model_key).find(station);
                if (it != model_yields.at(model_key).end()) {
                    double yield_pct = it->second.throughput_yield;
                    model_tpy.stations[station] = yield_pct;
                    tpy_value *= (yield_pct / 100.0);
                    count++;
                }
            }
            
            if (count == 4) {
                model_tpy.tpy = std::round(tpy_value * 10000) / 100.0;
            }
        }
        hardcoded_tpy[model_short] = model_tpy;
    }
    
    return hardcoded_tpy;
}

std::map<std::string, ModelTPY> calculate_dynamic_tpy(
    const std::map<std::string, std::map<std::string, StationMetrics>>& model_yields
) {
    std::map<std::string, ModelTPY> dynamic_tpy;
    
    std::map<std::string, std::string> model_mappings = {
        {"SXM4", "Tesla SXM4"},
        {"SXM5", "Tesla SXM5"},
        {"SXM6", "SXM6"}
    };
    
    for (const auto& [model_short, model_full] : model_mappings) {
        ModelTPY model_tpy = {{}, 0.0, 0};
        
        if (model_yields.find(model_full) != model_yields.end()) {
            double tpy_value = 1.0;
            
            for (const auto& [station, metrics] : model_yields.at(model_full)) {
                model_tpy.stations[station] = metrics.throughput_yield;
                tpy_value *= (metrics.throughput_yield / 100.0);
            }
            
            model_tpy.station_count = model_tpy.stations.size();
            if (!model_tpy.stations.empty()) {
                model_tpy.tpy = std::round(tpy_value * 10000) / 100.0;
            }
        }
        
        dynamic_tpy[model_short] = model_tpy;
    }
    
    return dynamic_tpy;
}

std::string build_json_station_metrics(const std::map<std::string, StationMetrics>& metrics) {
    std::ostringstream json;
    json << "{";
    bool first = true;
    for (const auto& [station, m] : metrics) {
        if (!first) json << ",";
        first = false;
        json << "\"" << station << "\":{";
        json << "\"totalParts\":" << m.total_parts << ",";
        json << "\"passedParts\":" << m.passed_parts << ",";
        json << "\"failedParts\":" << m.failed_parts << ",";
        json << "\"throughputYield\":" << std::fixed << std::setprecision(2) << m.throughput_yield;
        json << "}";
    }
    json << "}";
    return json.str();
}

std::string build_json_model_stations(const std::map<std::string, double>& stations) {
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);
    json << "{";
    bool first = true;
    for (const auto& [station, yield] : stations) {
        if (!first) json << ",";
        first = false;
        json << "\"" << station << "\":" << yield;
    }
    json << "}";
    return json.str();
}

void aggregate_weekly_tpy_for_week(pqxx::connection& conn, const std::string& week_id) {
    // Reduced logging - only calculate, don't print details
    
    // Calculate proper week dates
    size_t pos = week_id.find("-W");
    std::string year = week_id.substr(0, pos);
    std::string week_num = week_id.substr(pos + 2);
    
    std::string week_start, week_end;
    {
        pqxx::work txn_calc(conn);
        std::string jan4 = year + "-01-04";
        auto calc_result = txn_calc.exec_params(
            "SELECT "
            "  (DATE($1) - (EXTRACT(ISODOW FROM DATE($1))::int - 1) * INTERVAL '1 day' + ($2::int - 1) * INTERVAL '1 week')::date AS week_start, "
            "  (DATE($1) - (EXTRACT(ISODOW FROM DATE($1))::int - 1) * INTERVAL '1 day' + ($2::int - 1) * INTERVAL '1 week' + INTERVAL '6 days')::date AS week_end",
            jan4, week_num
        );
        
        week_start = calc_result[0]["week_start"].c_str();
        week_end = calc_result[0]["week_end"].c_str();
        txn_calc.commit();
    }
    
    // Calculate metrics
    WeeklyFPY weekly_fpy = calculate_weekly_first_pass_yield(conn, week_start, week_end);
    auto model_yields = calculate_model_specific_yields(conn, week_start, week_end);
    auto hardcoded_tpy = calculate_hardcoded_tpy(model_yields);
    auto dynamic_tpy = calculate_dynamic_tpy(model_yields);
    
    // Get daily aggregates
    int total_parts_overall, total_passed_parts;
    {
        pqxx::work txn_daily(conn);
        auto daily_result = txn_daily.exec_params(
            "SELECT SUM(total_parts) as total_parts, SUM(passed_parts) as passed_parts "
            "FROM daily_tpy_metrics WHERE date_id >= $1 AND date_id <= $2",
            week_start, week_end
        );
        
        total_parts_overall = daily_result[0][0].is_null() ? 0 : daily_result[0][0].as<int>();
        total_passed_parts = daily_result[0][1].is_null() ? 0 : daily_result[0][1].as<int>();
        txn_daily.commit();
    }
    
    double overall_yield = total_parts_overall > 0 ? 
        (total_passed_parts * 100.0) / total_parts_overall : 0.0;
    
    // Calculate average throughput yield
    auto& station_metrics = model_yields["overall"];
    double avg_yield = 0.0;
    if (!station_metrics.empty()) {
        double sum = 0.0;
        for (const auto& [_, metrics] : station_metrics) {
            sum += metrics.throughput_yield;
        }
        avg_yield = sum / station_metrics.size();
    }
    
    // Find best/worst stations
    std::string best_station_name, worst_station_name;
    double best_station_yield = 0.0, worst_station_yield = 100.0;
    
    if (!station_metrics.empty()) {
        for (const auto& [station, metrics] : station_metrics) {
            if (metrics.throughput_yield > best_station_yield) {
                best_station_yield = metrics.throughput_yield;
                best_station_name = station;
            }
            if (metrics.throughput_yield < worst_station_yield) {
                worst_station_yield = metrics.throughput_yield;
                worst_station_name = station;
            }
        }
    }
    
    // Build JSON strings
    std::string station_metrics_json = build_json_station_metrics(station_metrics);
    
    // Insert main weekly metrics
    pqxx::work txn(conn);
    txn.exec_params(R"SQL(
        INSERT INTO weekly_tpy_metrics (
            week_id, week_start, week_end, days_in_week,
            weekly_first_pass_yield_traditional_parts_started,
            weekly_first_pass_yield_traditional_first_pass_success,
            weekly_first_pass_yield_traditional_first_pass_yield,
            weekly_first_pass_yield_completed_only_active_parts,
            weekly_first_pass_yield_completed_only_first_pass_success,
            weekly_first_pass_yield_completed_only_first_pass_yield,
            weekly_first_pass_yield_breakdown_parts_completed,
            weekly_first_pass_yield_breakdown_parts_failed,
            weekly_first_pass_yield_breakdown_parts_stuck_in_limbo,
            weekly_first_pass_yield_breakdown_total_parts,
            weekly_overall_yield_total_parts,
            weekly_overall_yield_completed_parts,
            weekly_overall_yield_overall_yield,
            weekly_throughput_yield_station_metrics,
            weekly_throughput_yield_average_yield,
            total_stations,
            best_station_name,
            best_station_yield,
            worst_station_name,
            worst_station_yield
        ) VALUES (
            $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22, $23, $24
        ) ON CONFLICT (week_id) DO UPDATE SET
            week_start = EXCLUDED.week_start,
            week_end = EXCLUDED.week_end,
            days_in_week = EXCLUDED.days_in_week,
            weekly_first_pass_yield_traditional_parts_started = EXCLUDED.weekly_first_pass_yield_traditional_parts_started,
            weekly_first_pass_yield_traditional_first_pass_success = EXCLUDED.weekly_first_pass_yield_traditional_first_pass_success,
            weekly_first_pass_yield_traditional_first_pass_yield = EXCLUDED.weekly_first_pass_yield_traditional_first_pass_yield,
            weekly_first_pass_yield_completed_only_active_parts = EXCLUDED.weekly_first_pass_yield_completed_only_active_parts,
            weekly_first_pass_yield_completed_only_first_pass_success = EXCLUDED.weekly_first_pass_yield_completed_only_first_pass_success,
            weekly_first_pass_yield_completed_only_first_pass_yield = EXCLUDED.weekly_first_pass_yield_completed_only_first_pass_yield,
            weekly_first_pass_yield_breakdown_parts_completed = EXCLUDED.weekly_first_pass_yield_breakdown_parts_completed,
            weekly_first_pass_yield_breakdown_parts_failed = EXCLUDED.weekly_first_pass_yield_breakdown_parts_failed,
            weekly_first_pass_yield_breakdown_parts_stuck_in_limbo = EXCLUDED.weekly_first_pass_yield_breakdown_parts_stuck_in_limbo,
            weekly_first_pass_yield_breakdown_total_parts = EXCLUDED.weekly_first_pass_yield_breakdown_total_parts,
            weekly_overall_yield_total_parts = EXCLUDED.weekly_overall_yield_total_parts,
            weekly_overall_yield_completed_parts = EXCLUDED.weekly_overall_yield_completed_parts,
            weekly_overall_yield_overall_yield = EXCLUDED.weekly_overall_yield_overall_yield,
            weekly_throughput_yield_station_metrics = EXCLUDED.weekly_throughput_yield_station_metrics,
            weekly_throughput_yield_average_yield = EXCLUDED.weekly_throughput_yield_average_yield,
            total_stations = EXCLUDED.total_stations,
            best_station_name = EXCLUDED.best_station_name,
            best_station_yield = EXCLUDED.best_station_yield,
            worst_station_name = EXCLUDED.worst_station_name,
            worst_station_yield = EXCLUDED.worst_station_yield
    )SQL",
        week_id, week_start, week_end, 7,
        weekly_fpy.parts_started,
        weekly_fpy.first_pass_success,
        std::round(weekly_fpy.first_pass_yield * 100) / 100,
        weekly_fpy.active_parts,
        weekly_fpy.first_pass_success,
        std::round(weekly_fpy.completed_only_fpy * 100) / 100,
        weekly_fpy.parts_completed,
        weekly_fpy.parts_failed,
        weekly_fpy.parts_stuck,
        weekly_fpy.parts_started,
        total_parts_overall,
        total_passed_parts,
        std::round(overall_yield * 100) / 100,
        station_metrics_json,
        std::round(avg_yield * 100) / 100,
        (int)station_metrics.size(),
        best_station_name,
        best_station_yield,
        worst_station_name,
        worst_station_yield
    );
    
    // Insert model-specific metrics
    for (const auto& [model_name, model_data] : hardcoded_tpy) {
        if (!model_data.stations.empty()) {
            std::string full_model = model_name == "SXM6" ? "SXM6" : "Tesla " + model_name;
            
            txn.exec_params(R"SQL(
                INSERT INTO weekly_tpy_model_metrics (
                    week_id, model,
                    hardcoded_stations, hardcoded_tpy,
                    dynamic_stations, dynamic_tpy, dynamic_station_count
                ) VALUES ($1, $2, $3, $4, $5, $6, $7)
                ON CONFLICT (week_id, model) DO UPDATE SET
                    hardcoded_stations = EXCLUDED.hardcoded_stations,
                    hardcoded_tpy = EXCLUDED.hardcoded_tpy,
                    dynamic_stations = EXCLUDED.dynamic_stations,
                    dynamic_tpy = EXCLUDED.dynamic_tpy,
                    dynamic_station_count = EXCLUDED.dynamic_station_count
            )SQL",
                week_id, full_model,
                build_json_model_stations(model_data.stations),
                model_data.tpy,
                build_json_model_stations(dynamic_tpy[model_name].stations),
                dynamic_tpy[model_name].tpy,
                dynamic_tpy[model_name].station_count
            );
        }
    }
    
    txn.commit();
    // Reduced logging - no per-week output
}

std::vector<std::string> get_all_available_weeks(pqxx::connection& conn) {
    std::cout << "Finding weeks with test activity..." << std::endl;
    
    pqxx::work txn(conn);
    
    // Check config for aggregation mode
    std::string date_filter = "";
    if (config::AGGREGATION_MODE == "last_N_days") {
        std::cout << "  Mode: Last " << config::AGGREGATION_WEEKS_BACK << " weeks" << std::endl;
        date_filter = " AND DATE(history_station_end_time) >= CURRENT_DATE - INTERVAL '" 
                    + std::to_string(config::AGGREGATION_WEEKS_BACK * 7) + " days'";
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
    
    std::set<std::string> weeks_set;
    
    for (auto row : results) {
        std::string date_str = row["test_date"].c_str();
        
        // Get week ID for this date
        auto week_result = txn.exec_params(
            "SELECT TO_CHAR($1::date, 'IYYY-\"W\"IW') as week_id",
            date_str
        );
        
        if (!week_result.empty()) {
            weeks_set.insert(week_result[0]["week_id"].c_str());
        }
    }
    
    std::vector<std::string> weeks(weeks_set.begin(), weeks_set.end());
    
    if (!weeks.empty()) {
        std::cout << "  Found " << weeks.size() << " weeks with test activity from " 
                  << weeks.front() << " to " << weeks.back() << std::endl;
    }
    
    txn.commit();
    return weeks;
}

// Callable function for aggregation manager
int run_tpy_weekly_metrics_aggregation() {
    try {
        std::string conn_str = "dbname=" + config::DB_NAME + 
                                " user=" + config::DB_USER + 
                                " host=" + config::DB_HOST + 
                                " port=" + std::to_string(config::DB_PORT);
        
        if (!config::DB_PASSWORD.empty()) {
            conn_str += " password=" + config::DB_PASSWORD;
        }
        
        pqxx::connection conn(conn_str);
        
        std::cout << "WEEKLY TPY METRICS ALL-TIME AGGREGATOR" << std::endl;
        std::cout << std::string(50, '=') << std::endl;
        
        auto all_weeks = get_all_available_weeks(conn);
        
        if (all_weeks.empty()) {
            std::cout << "No valid weeks found in the dataset" << std::endl;
            return 0;  // Not an error, just no data
        }
        
        std::cout << "\nProcessing ALL " << all_weeks.size() << " historical weeks..." << std::endl;
        
        int success_count = 0;
        int error_count = 0;
        
        for (size_t i = 0; i < all_weeks.size(); ++i) {
            try {
                // Only log progress every 10 weeks to reduce output
                if (i % 10 == 0 || i == all_weeks.size() - 1) {
                    std::cout << "Processing weeks: " << (i + 1) << "/" << all_weeks.size() 
                              << " (" << all_weeks[i] << ")" << std::endl;
                }
                
                aggregate_weekly_tpy_for_week(conn, all_weeks[i]);
                success_count++;
                
            } catch (const std::exception& e) {
                std::cerr << "ERROR processing " << all_weeks[i] << ": " << e.what() << std::endl;
                error_count++;
            }
        }
        
        std::cout << "\nWEEKLY TPY ALL-TIME AGGREGATION COMPLETE!" << std::endl;
        std::cout << "Successfully processed: " << success_count << " weeks" << std::endl;
        std::cout << "Errors: " << error_count << " weeks" << std::endl;
        
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
    return run_tpy_weekly_metrics_aggregation();
}
#endif

