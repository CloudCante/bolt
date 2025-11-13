#include "DatabaseLoader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <set>

#include <libpq-fe.h>

namespace fs = std::filesystem;

DatabaseLoader::DatabaseLoader(const DatabaseConfig& config)
    : config_(config)
    , lastStats_{0, 0, 0, 0, false}
{
}

DatabaseLoader::DatabaseLoader()
    : config_{"localhost", 5432, "fox_db", "gpu_user", ""}
    , lastStats_{0, 0, 0, 0, false}
{
}

DatabaseLoader::~DatabaseLoader() {
}

void* DatabaseLoader::connectToDatabase() const {
    std::cout << "[DEBUG] [connectToDatabase] START - Building connection string" << std::endl;
    std::cout.flush();
    
    std::string connInfo = "host=" + config_.host +
                          " port=" + std::to_string(config_.port) +
                          " dbname=" + config_.database +
                          " user=" + config_.user;
    
    std::string password = config_.password;
    if (!password.empty()) {
        connInfo += " password=" + password;
    }
    
    std::cout << "[DEBUG] [connectToDatabase] Connection string built: " << connInfo << std::endl;
    std::cout.flush();
    std::cout << "[DEBUG] [connectToDatabase] ⚠️ ABOUT TO CALL PQconnectdb() - THIS MAY HANG" << std::endl;
    std::cout.flush();
    
    PGconn* conn = PQconnectdb(connInfo.c_str());
    
    std::cout << "[DEBUG] [connectToDatabase] ✓ PQconnectdb() RETURNED" << std::endl;
    std::cout.flush();
    std::cout << "[DEBUG] [connectToDatabase] Checking connection status..." << std::endl;
    std::cout.flush();
    
    if (PQstatus(conn) != CONNECTION_OK) {
        std::cout << "[DEBUG] [connectToDatabase] Connection status: FAILED" << std::endl;
        std::cout.flush();
        lastError_ = "Database connection failed: " + std::string(PQerrorMessage(conn));
        PQfinish(conn);
        return nullptr;
    }
    
    std::cout << "[DEBUG] [connectToDatabase] Connection status: OK" << std::endl;
    std::cout.flush();
    std::cout << "[DEBUG] [connectToDatabase] END - Returning connection handle" << std::endl;
    std::cout.flush();
    
    return conn;
}

void DatabaseLoader::closeConnection(void* conn) const {
    if (conn) {
        PQfinish(static_cast<PGconn*>(conn));
    }
}

std::vector<std::map<std::string, std::string>> DatabaseLoader::parseCSV(const std::string& csvFilePath) const {
    std::vector<std::map<std::string, std::string>> rows;
    
    std::ifstream file(csvFilePath);
    if (!file.is_open()) {
        lastError_ = "Failed to open CSV file: " + csvFilePath;
        return rows;
    }
    
    // Read header
    std::string headerLine;
    if (!std::getline(file, headerLine)) {
        lastError_ = "CSV file is empty or has no header";
        return rows;
    }
    
    // Parse header and normalize column names (lowercase, replace spaces/dashes with underscores)
    std::vector<std::string> headers;
    std::stringstream ss(headerLine);
    std::string col;
    bool inQuotes = false;
    std::string currentField;
    
    auto normalizeColumnName = [](const std::string& name) -> std::string {
        std::string normalized = name;
        // Convert to lowercase
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        // Replace spaces and dashes with underscores
        std::replace(normalized.begin(), normalized.end(), ' ', '_');
        std::replace(normalized.begin(), normalized.end(), '-', '_');
        return normalized;
    };
    
    for (char c : headerLine) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            // Trim whitespace before normalizing
            size_t first = currentField.find_first_not_of(" \t\n\r");
            if (first != std::string::npos) {
                currentField.erase(0, first);
                size_t last = currentField.find_last_not_of(" \t\n\r");
                if (last != std::string::npos) {
                    currentField.erase(last + 1);
                }
            }
            if (!currentField.empty()) {
                headers.push_back(normalizeColumnName(currentField));
            }
            currentField.clear();
        } else {
            currentField += c;
        }
    }
    // Last field - trim whitespace/newline
    if (!currentField.empty()) {
        size_t first = currentField.find_first_not_of(" \t\n\r");
        if (first != std::string::npos) {
            currentField.erase(0, first);
            size_t last = currentField.find_last_not_of(" \t\n\r");
            if (last != std::string::npos) {
                currentField.erase(last + 1);
            }
        }
        if (!currentField.empty()) {
            headers.push_back(normalizeColumnName(currentField));
        }
    }
    
    // Read data rows
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::map<std::string, std::string> row;
        std::stringstream rowStream(line);
        std::string field;
        inQuotes = false;
        currentField.clear();
        size_t colIndex = 0;
        
        for (char c : line) {
            if (c == '"') {
                inQuotes = !inQuotes;
            } else if (c == ',' && !inQuotes) {
                if (colIndex < headers.size()) {
                    row[headers[colIndex]] = currentField;
                }
                currentField.clear();
                colIndex++;
            } else {
                currentField += c;
            }
        }
        // Last field
        if (colIndex < headers.size()) {
            row[headers[colIndex]] = currentField;
        }
        
        rows.push_back(row);
    }
    
    file.close();
    return rows;
}

std::map<std::string, std::string> DatabaseLoader::mapRowToDatabase(const std::map<std::string, std::string>& row, FileType fileType) const {
    std::map<std::string, std::string> mapped;
    
    switch (fileType) {
        case FileType::WORKSTATION: {
            // Map fields - match Python behavior: empty/whitespace -> empty string (not NULL for most fields)
            // Only customer_pn can be NULL
            // IMPORTANT: Normalize 'nan' to NULL to match database records that have 'nan' strings
            auto getField = [&row](const std::string& key, bool canBeNull = false) -> std::string {
                if (!row.count(key)) {
                    return canBeNull ? "__NULL__" : "";
                }
                std::string val = row.at(key);
                // Trim whitespace - handle npos correctly to avoid undefined behavior
                size_t first = val.find_first_not_of(" \t\n\r");
                if (first != std::string::npos) {
                    val.erase(0, first);
                    size_t last = val.find_last_not_of(" \t\n\r");
                    if (last != std::string::npos) {
                        val.erase(last + 1);
                    } else {
                        val.clear();  // All whitespace
                    }
                } else {
                    val.clear();  // All whitespace or empty
                }
                // Normalize 'nan' (case-insensitive) to NULL for nullable fields
                // This fixes the issue where database has 'nan' strings but CSV has empty/NULL
                // Without this, NULL != 'nan' so UNIQUE constraint doesn't match duplicates
                if (canBeNull) {
                    std::string valLower = val;
                    std::transform(valLower.begin(), valLower.end(), valLower.begin(), ::tolower);
                    if (val.empty() || valLower == "nan" || valLower == "null" || valLower == "none") {
                        return "__NULL__";  // NULL marker for nullable fields
                    }
                }
                return val;
            };
            
            mapped["sn"] = getField("sn");
            mapped["pn"] = getField("pn");
            mapped["customer_pn"] = getField("customer_pn", true);  // Can be NULL
            mapped["workstation_name"] = getField("workstation_name");
            mapped["history_station_start_time"] = getField("history_station_start_time");
            mapped["history_station_end_time"] = getField("history_station_end_time");
            mapped["hours"] = getField("hours");
            mapped["service_flow"] = getField("service_flow");
            mapped["model"] = getField("model");
            mapped["history_station_passing_status"] = getField("history_station_passing_status");
            mapped["passing_station_method"] = getField("passing_station_method");
            mapped["operator"] = getField("operator");
            mapped["data_source"] = "workstation";
            break;
        }
        
        case FileType::TESTBOARD: {
            // Map fields - match Python behavior: empty/whitespace -> NULL for nullable fields
            // Nullable fields: work_station_process, baseboard_sn, baseboard_pn, failure_reasons,
            //                  failure_note, failure_code, diag_version, fixture_no
            // IMPORTANT: Normalize 'nan' to NULL to match database records that have 'nan' strings
            auto getField = [&row](const std::string& key, bool canBeNull = false) -> std::string {
                if (!row.count(key)) {
                    return canBeNull ? "__NULL__" : "";
                }
                std::string val = row.at(key);
                // Trim whitespace - handle npos correctly to avoid undefined behavior
                size_t first = val.find_first_not_of(" \t\n\r");
                if (first != std::string::npos) {
                    val.erase(0, first);
                    size_t last = val.find_last_not_of(" \t\n\r");
                    if (last != std::string::npos) {
                        val.erase(last + 1);
                    } else {
                        val.clear();  // All whitespace
                    }
                } else {
                    val.clear();  // All whitespace or empty
                }
                // Normalize 'nan' (case-insensitive) to NULL for nullable fields
                // This fixes the issue where database has 'nan' strings but CSV has empty/NULL
                if (canBeNull) {
                    std::string valLower = val;
                    std::transform(valLower.begin(), valLower.end(), valLower.begin(), ::tolower);
                    if (val.empty() || valLower == "nan" || valLower == "null" || valLower == "none") {
                        return "__NULL__";  // NULL marker for nullable fields
                    }
                }
                return val;
            };
            
            mapped["sn"] = getField("sn");
            mapped["pn"] = getField("pn");
            mapped["model"] = getField("model");
            mapped["work_station_process"] = getField("work_station_process", true);  // Can be NULL
            mapped["baseboard_sn"] = getField("baseboard_sn", true);  // Can be NULL
            mapped["baseboard_pn"] = getField("baseboard_pn", true);  // Can be NULL
            mapped["workstation_name"] = getField("workstation_name");
            mapped["history_station_start_time"] = getField("history_station_start_time");
            mapped["history_station_end_time"] = getField("history_station_end_time");
            mapped["history_station_passing_status"] = getField("history_station_passing_status");
            mapped["operator"] = getField("operator");
            mapped["failure_reasons"] = getField("failure_reasons", true);  // Can be NULL
            mapped["failure_note"] = getField("failure_note", true);  // Can be NULL
            mapped["failure_code"] = getField("failure_code", true);  // Can be NULL
            mapped["diag_version"] = getField("diag_version", true);  // Can be NULL
            mapped["fixture_no"] = getField("fixture_no", true);  // Can be NULL
            mapped["data_source"] = "testboard";
            break;
        }
        
        default:
            break;
    }
    
    return mapped;
}

bool DatabaseLoader::recordExists(void* conn, const std::map<std::string, std::string>& record, FileType fileType) const {
    std::cout << "[DEBUG] [recordExists] START" << std::endl;
    std::cout.flush();
    
    PGconn* pgconn = static_cast<PGconn*>(conn);
    std::string query;
    std::vector<std::string> paramValues;
    
    if (fileType == FileType::WORKSTATION) {
        // Build query with proper NULL handling using IS NOT DISTINCT FROM
        // This treats NULL = NULL as true (unlike regular =)
        // NOTE: first_station_start_time column has been removed from database
        // Database enforces uniqueness via UNIQUE constraint on the fields below
        query = "SELECT COUNT(*) FROM workstation_master_log WHERE "
                "sn IS NOT DISTINCT FROM $1 AND "
                "pn IS NOT DISTINCT FROM $2 AND "
                "customer_pn IS NOT DISTINCT FROM $3 AND "
                "workstation_name IS NOT DISTINCT FROM $4 AND "
                "history_station_start_time IS NOT DISTINCT FROM $5 AND "
                "history_station_end_time IS NOT DISTINCT FROM $6 AND "
                "hours IS NOT DISTINCT FROM $7 AND "
                "service_flow IS NOT DISTINCT FROM $8 AND "
                "model IS NOT DISTINCT FROM $9 AND "
                "history_station_passing_status IS NOT DISTINCT FROM $10 AND "
                "passing_station_method IS NOT DISTINCT FROM $11 AND "
                "operator IS NOT DISTINCT FROM $12 AND "
                "data_source IS NOT DISTINCT FROM $13";
        
        // Convert NULL markers to empty strings for parameterized query
        // PostgreSQL will handle empty strings, and COALESCE will match NULL to empty
        auto handleNull = [](const std::string& val) -> std::string {
            return (val == "__NULL__") ? "" : val;
        };
        
        paramValues.push_back(handleNull(record.at("sn")));
        paramValues.push_back(handleNull(record.at("pn")));
        paramValues.push_back(handleNull(record.at("customer_pn")));
        paramValues.push_back(handleNull(record.at("workstation_name")));
        paramValues.push_back(handleNull(record.at("history_station_start_time")));
        paramValues.push_back(handleNull(record.at("history_station_end_time")));
        paramValues.push_back(handleNull(record.at("hours")));
        paramValues.push_back(handleNull(record.at("service_flow")));
        paramValues.push_back(handleNull(record.at("model")));
        paramValues.push_back(handleNull(record.at("history_station_passing_status")));
        paramValues.push_back(handleNull(record.at("passing_station_method")));
        paramValues.push_back(handleNull(record.at("operator")));
        paramValues.push_back(handleNull(record.at("data_source")));
    } else if (fileType == FileType::TESTBOARD) {
        // Build query with proper NULL handling using IS NOT DISTINCT FROM
        // This treats NULL = NULL as true (unlike regular =)
        query = "SELECT COUNT(*) FROM testboard_master_log WHERE "
                "sn IS NOT DISTINCT FROM $1 AND "
                "pn IS NOT DISTINCT FROM $2 AND "
                "model IS NOT DISTINCT FROM $3 AND "
                "work_station_process IS NOT DISTINCT FROM $4 AND "
                "baseboard_sn IS NOT DISTINCT FROM $5 AND "
                "baseboard_pn IS NOT DISTINCT FROM $6 AND "
                "workstation_name IS NOT DISTINCT FROM $7 AND "
                "history_station_start_time IS NOT DISTINCT FROM $8 AND "
                "history_station_end_time IS NOT DISTINCT FROM $9 AND "
                "history_station_passing_status IS NOT DISTINCT FROM $10 AND "
                "operator IS NOT DISTINCT FROM $11 AND "
                "failure_reasons IS NOT DISTINCT FROM $12 AND "
                "failure_note IS NOT DISTINCT FROM $13 AND "
                "failure_code IS NOT DISTINCT FROM $14 AND "
                "diag_version IS NOT DISTINCT FROM $15 AND "
                "fixture_no IS NOT DISTINCT FROM $16 AND "
                "data_source IS NOT DISTINCT FROM $17";
        
        // Convert NULL markers to empty strings for parameterized query
        // PostgreSQL will handle empty strings, and IS NOT DISTINCT FROM will match NULL to NULL
        auto handleNull = [](const std::string& val) -> std::string {
            return (val == "__NULL__") ? "" : val;
        };
        
        paramValues.push_back(handleNull(record.at("sn")));
        paramValues.push_back(handleNull(record.at("pn")));
        paramValues.push_back(handleNull(record.at("model")));
        paramValues.push_back(handleNull(record.at("work_station_process")));
        paramValues.push_back(handleNull(record.at("baseboard_sn")));
        paramValues.push_back(handleNull(record.at("baseboard_pn")));
        paramValues.push_back(handleNull(record.at("workstation_name")));
        paramValues.push_back(handleNull(record.at("history_station_start_time")));
        paramValues.push_back(handleNull(record.at("history_station_end_time")));
        paramValues.push_back(handleNull(record.at("history_station_passing_status")));
        paramValues.push_back(handleNull(record.at("operator")));
        paramValues.push_back(handleNull(record.at("failure_reasons")));
        paramValues.push_back(handleNull(record.at("failure_note")));
        paramValues.push_back(handleNull(record.at("failure_code")));
        paramValues.push_back(handleNull(record.at("diag_version")));
        paramValues.push_back(handleNull(record.at("fixture_no")));
        paramValues.push_back(handleNull(record.at("data_source")));
    } else {
        return false;
    }
    
    // Convert to const char* array for PQexecParams with NULL handling
    // Note: paramValues must stay in scope during PQexecParams call
    std::vector<const char*> params;
    std::vector<int> paramLengths;
    std::vector<int> paramFormats;
    
    params.reserve(paramValues.size());
    paramLengths.reserve(paramValues.size());
    paramFormats.reserve(paramValues.size());
    
    for (const auto& val : paramValues) {
        if (val.empty()) {
            // Empty string means NULL - pass NULL pointer and length 0
            params.push_back(nullptr);
            paramLengths.push_back(0);
        } else {
            params.push_back(val.c_str());
            paramLengths.push_back(val.length());
        }
        paramFormats.push_back(0);  // 0 = text format
    }
    
    // Execute query - paramValues must remain valid during this call
    // Log a sample of the record being checked (first few fields for debugging)
    std::string sampleKey = (fileType == FileType::WORKSTATION) ? "sn" : "sn";
    std::string sampleValue = record.count(sampleKey) ? record.at(sampleKey).substr(0, 20) : "N/A";
    std::cout << "[DEBUG] [recordExists] About to execute PQexecParams() - checking record with " << sampleKey << "=" << sampleValue << "..." << std::endl;
    std::cout.flush();
    
    PGresult* res = PQexecParams(pgconn, query.c_str(), paramValues.size(), nullptr,
                                 params.data(), paramLengths.data(), paramFormats.data(), 0);
    
    std::cout << "[DEBUG] [recordExists] ✓ PQexecParams() RETURNED" << std::endl;
    std::cout.flush();
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cout << "[DEBUG] [recordExists] Query status: FAILED" << std::endl;
        std::cout.flush();
        lastError_ = "Query failed: " + std::string(PQerrorMessage(pgconn));
        PQclear(res);
        return false;
    }
    
    int count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    std::cout << "[DEBUG] [recordExists] END - count=" << count << " (exists=" << (count > 0) << ")" << std::endl;
    std::cout.flush();
    
    return count > 0;
}

size_t DatabaseLoader::insertRecords(void* conn, const std::vector<std::map<std::string, std::string>>& records, FileType fileType) const {
    if (records.empty()) {
        return 0;
    }
    
    PGconn* pgconn = static_cast<PGconn*>(conn);
    std::string insertQuery;
    
    if (fileType == FileType::WORKSTATION) {
        insertQuery = "INSERT INTO workstation_master_log ("
                     "sn, pn, model, workstation_name, "
                     "history_station_start_time, history_station_end_time, "
                     "history_station_passing_status, operator, customer_pn, "
                     "hours, service_flow, passing_station_method, data_source"
                     ") VALUES ";
    } else if (fileType == FileType::TESTBOARD) {
        insertQuery = "INSERT INTO testboard_master_log ("
                     "sn, pn, model, work_station_process, baseboard_sn, baseboard_pn, "
                     "workstation_name, history_station_start_time, history_station_end_time, "
                     "history_station_passing_status, operator, "
                     "failure_reasons, failure_note, failure_code, diag_version, fixture_no, data_source"
                     ") VALUES ";
    } else {
        return 0;
    }
    
    // Build VALUES clause for batch insert
    std::stringstream valuesStream;
    for (size_t i = 0; i < records.size(); ++i) {
        if (i > 0) valuesStream << ", ";
        valuesStream << "(";
        
        if (fileType == FileType::WORKSTATION) {
            const auto& r = records[i];
            auto getValue = [&r, this](const std::string& key, bool isTimestamp = false, bool normalizeNull = false) -> std::string {
                std::string val = r.at(key);
                if (val == "__NULL__") {
                    // For customer_pn, normalize NULL to empty string so UNIQUE constraint works
                    // (PostgreSQL UNIQUE treats NULLs as distinct, but empty strings are equal)
                    if (normalizeNull) {
                        return "E''";  // Empty string instead of NULL
                    }
                    return "NULL";
                }
                if (isTimestamp) {
                    return "'" + val + "'";
                }
                return "E'" + escapeString(val) + "'";
            };
            
            valuesStream << getValue("sn") << ", "
                        << getValue("pn") << ", "
                        << getValue("model") << ", "
                        << getValue("workstation_name") << ", "
                        << getValue("history_station_start_time", true) << ", "
                        << getValue("history_station_end_time", true) << ", "
                        << getValue("history_station_passing_status") << ", "
                        << getValue("operator") << ", "
                        << getValue("customer_pn", false, true) << ", "  // Normalize NULL to empty string
                        << getValue("hours") << ", "
                        << getValue("service_flow") << ", "
                        << getValue("passing_station_method") << ", "
                        << getValue("data_source");
        } else if (fileType == FileType::TESTBOARD) {
            const auto& r = records[i];
            auto getValue = [&r, this](const std::string& key, bool isTimestamp = false) -> std::string {
                std::string val = r.at(key);
                if (val == "__NULL__") {
                    return "NULL";
                }
                if (isTimestamp) {
                    return "'" + val + "'";
                }
                return "E'" + escapeString(val) + "'";
            };
            
            valuesStream << getValue("sn") << ", "
                        << getValue("pn") << ", "
                        << getValue("model") << ", "
                        << getValue("work_station_process") << ", "
                        << getValue("baseboard_sn") << ", "
                        << getValue("baseboard_pn") << ", "
                        << getValue("workstation_name") << ", "
                        << getValue("history_station_start_time", true) << ", "
                        << getValue("history_station_end_time", true) << ", "
                        << getValue("history_station_passing_status") << ", "
                        << getValue("operator") << ", "
                        << getValue("failure_reasons") << ", "
                        << getValue("failure_note") << ", "
                        << getValue("failure_code") << ", "
                        << getValue("diag_version") << ", "
                        << getValue("fixture_no") << ", "
                        << getValue("data_source");
        }
        
        valuesStream << ")";
    }
    
    // Add ON CONFLICT DO NOTHING to skip duplicates automatically (much faster than checking first)
    // Use the unique index name (which properly handles NULLs via COALESCE)
    // Use RETURNING to get the serial numbers of actually inserted records (for debugging)
    std::string conflictClause;
    std::string returningClause;
    if (fileType == FileType::WORKSTATION) {
        // Simplified constraint: same serial number + same station + same start/end time = duplicate
        conflictClause = " ON CONFLICT (sn, workstation_name, history_station_start_time, history_station_end_time) DO NOTHING";
        returningClause = " RETURNING sn, pn, workstation_name, history_station_start_time, history_station_end_time";
    } else if (fileType == FileType::TESTBOARD) {
        // Simplified constraint: same serial number + same station + same start/end time = duplicate
        conflictClause = " ON CONFLICT (sn, workstation_name, history_station_start_time, history_station_end_time) DO NOTHING";
        returningClause = " RETURNING sn, pn, workstation_name, history_station_start_time, history_station_end_time";
    } else {
        conflictClause = " ON CONFLICT DO NOTHING";  // Fallback
        returningClause = "";
    }
    std::string fullQuery = insertQuery + valuesStream.str() + conflictClause + returningClause;
    
    // Debug: log query size for large inserts
    if (records.size() > 1000) {
        std::cout << "[DatabaseLoader] Executing batch insert of " << records.size() << " records (with ON CONFLICT DO NOTHING)..." << std::endl;
    }
    
    // Debug: log a sample of the query (first record only) to verify format
    if (!records.empty()) {
        std::string sampleQuery = insertQuery;
        if (fileType == FileType::WORKSTATION) {
            const auto& r = records[0];
            auto getValue = [&r, this](const std::string& key, bool isTimestamp = false) -> std::string {
                std::string val = r.at(key);
                if (val == "__NULL__") {
                    return "NULL";
                }
                if (isTimestamp) {
                    return "'" + val + "'";
                }
                return "E'" + escapeString(val) + "'";
            };
            std::stringstream sampleValues;
            sampleValues << "("
                        << getValue("sn") << ", "
                        << getValue("pn") << ", "
                        << getValue("model") << ", "
                        << getValue("workstation_name") << ", "
                        << getValue("history_station_start_time", true) << ", "
                        << getValue("history_station_end_time", true) << ", "
                        << getValue("history_station_passing_status") << ", "
                        << getValue("operator") << ", "
                        << getValue("customer_pn") << ", "
                        << getValue("hours") << ", "
                        << getValue("service_flow") << ", "
                        << getValue("passing_station_method") << ", "
                        << getValue("data_source")
                        << ")";
            sampleQuery += sampleValues.str() + conflictClause;
        }
        std::cout << "[DEBUG] [insertRecords] Sample query (first record): " << sampleQuery.substr(0, 500) << "..." << std::endl;
        std::cout.flush();
    }
    
    std::cout << "[DEBUG] [insertRecords] START - " << records.size() << " records" << std::endl;
    std::cout.flush();
    std::cout << "[DEBUG] [insertRecords] About to execute PQexec()" << std::endl;
    std::cout.flush();
    
    PGresult* res = PQexec(pgconn, fullQuery.c_str());
    
    std::cout << "[DEBUG] [insertRecords] PQexec() returned" << std::endl;
    std::cout.flush();
    
    // Check result status - with RETURNING, we get PGRES_TUPLES_OK instead of PGRES_COMMAND_OK
    ExecStatusType expectedStatus = returningClause.empty() ? PGRES_COMMAND_OK : PGRES_TUPLES_OK;
    
    if (PQresultStatus(res) != expectedStatus) {
        std::string errorMsg = PQerrorMessage(pgconn);
        lastError_ = "Insert failed: " + errorMsg;
        std::cerr << "[DatabaseLoader] Insert error: " << errorMsg << std::endl;
        // Try to get more details
        std::cerr << "[DatabaseLoader] Query length: " << fullQuery.length() << " chars" << std::endl;
        if (fullQuery.length() > 1000) {
            std::cerr << "[DatabaseLoader] First 500 chars: " << fullQuery.substr(0, 500) << "..." << std::endl;
        }
        PQclear(res);
        return 0;
    }
    
    size_t inserted = 0;
    
    // If we used RETURNING, count the returned rows (these are the records actually inserted)
    // Database returns 0 rows if all were duplicates, N rows if N were inserted
    if (!returningClause.empty() && PQresultStatus(res) == PGRES_TUPLES_OK) {
        inserted = PQntuples(res);  // Database tells us exactly how many were inserted
        
        if (inserted > 0) {
            // Log first 20 inserted records for debugging
            int logCount = (inserted < 20) ? inserted : 20;
            std::cout << "[DatabaseLoader] Database inserted " << inserted << " records. First " << logCount << " serial numbers:" << std::endl;
            for (int i = 0; i < logCount; ++i) {
                std::string sn = PQgetvalue(res, i, 0);
                std::string pn = PQgetvalue(res, i, 1);
                std::string workstation = PQgetvalue(res, i, 2);
                std::string startTime = PQgetvalue(res, i, 3);
                std::string endTime = PQgetvalue(res, i, 4);
                std::cout << "  [" << (i+1) << "] SN=" << sn << " PN=" << pn 
                          << " Station=" << workstation << " Start=" << startTime 
                          << " End=" << endTime << std::endl;
            }
            if (inserted > 20) {
                std::cout << "  ... and " << (inserted - 20) << " more records" << std::endl;
            }
        }
        // If inserted == 0, database already told us (0 rows returned = all duplicates)
    } else {
        // Fallback: use PQcmdTuples for INSERT without RETURNING
        const char* tuplesStr = PQcmdTuples(res);
        if (tuplesStr && tuplesStr[0] != '\0') {
            inserted = static_cast<size_t>(atoi(tuplesStr));
        } else {
            std::cerr << "[DatabaseLoader] Warning: PQcmdTuples returned empty, cannot verify insert count" << std::endl;
            inserted = records.size(); // Assume all were inserted if status is OK
        }
    }
    PQclear(res);
    
    return inserted;
}

std::string DatabaseLoader::escapeString(const std::string& str) const {
    std::string escaped;
    for (char c : str) {
        if (c == '\'') {
            escaped += "''";  // Escape single quote
        } else if (c == '\\') {
            escaped += "\\\\";  // Escape backslash
        } else {
            escaped += c;
        }
    }
    return escaped;
}

std::string DatabaseLoader::parseTimestamp(const std::string& timestampStr) const {
    // Parse various timestamp formats and convert to PostgreSQL format
    // For now, return as-is (assuming CSV already has proper format)
    return timestampStr;
}

std::vector<std::map<std::string, std::string>> DatabaseLoader::deduplicateCSVRows(
    const std::vector<std::map<std::string, std::string>>& records, 
    FileType fileType) const {
    
    (void)fileType; // Not needed for deduplication, but kept for API consistency
    
    std::vector<std::map<std::string, std::string>> deduplicated;
    std::set<std::string> seenHashes;
    
    // Create a hash string from all fields in the record
    auto createHash = [](const std::map<std::string, std::string>& record) -> std::string {
        std::string hash;
        for (const auto& pair : record) {
            hash += pair.first + "=" + pair.second + "|";
        }
        return hash;
    };
    
    for (const auto& record : records) {
        std::string hash = createHash(record);
        if (seenHashes.find(hash) == seenHashes.end()) {
            seenHashes.insert(hash);
            deduplicated.push_back(record);
        }
    }
    
    return deduplicated;
}

std::set<size_t> DatabaseLoader::checkDuplicatesBatch(
    void* conn, 
    const std::vector<std::map<std::string, std::string>>& records, 
    FileType fileType) const {
    
    std::cout << "[DEBUG] [checkDuplicatesBatch] START - " << records.size() << " records (using one-by-one parameterized queries like Python)" << std::endl;
    std::cout.flush();
    
    if (records.empty()) {
        return std::set<size_t>();
    }
    
    PGconn* pgconn = static_cast<PGconn*>(conn);
    std::set<size_t> existingIndices;
    
    // Build parameterized query (like Python) - no type casting needed, libpq handles it
    std::string checkQuery;
    if (fileType == FileType::WORKSTATION) {
        checkQuery = 
            "SELECT COUNT(*) FROM workstation_master_log WHERE "
            "sn IS NOT DISTINCT FROM $1 AND "
            "pn IS NOT DISTINCT FROM $2 AND "
            "customer_pn IS NOT DISTINCT FROM $3 AND "
            "workstation_name IS NOT DISTINCT FROM $4 AND "
            "history_station_start_time IS NOT DISTINCT FROM $5 AND "
            "history_station_end_time IS NOT DISTINCT FROM $6 AND "
            "hours IS NOT DISTINCT FROM $7 AND "
            "service_flow IS NOT DISTINCT FROM $8 AND "
            "model IS NOT DISTINCT FROM $9 AND "
            "history_station_passing_status IS NOT DISTINCT FROM $10 AND "
            "passing_station_method IS NOT DISTINCT FROM $11 AND "
            "operator IS NOT DISTINCT FROM $12 AND "
            "data_source IS NOT DISTINCT FROM $13";
    } else if (fileType == FileType::TESTBOARD) {
        checkQuery = 
            "SELECT COUNT(*) FROM testboard_master_log WHERE "
            "sn IS NOT DISTINCT FROM $1 AND "
            "pn IS NOT DISTINCT FROM $2 AND "
            "model IS NOT DISTINCT FROM $3 AND "
            "work_station_process IS NOT DISTINCT FROM $4 AND "
            "baseboard_sn IS NOT DISTINCT FROM $5 AND "
            "baseboard_pn IS NOT DISTINCT FROM $6 AND "
            "workstation_name IS NOT DISTINCT FROM $7 AND "
            "history_station_start_time IS NOT DISTINCT FROM $8 AND "
            "history_station_end_time IS NOT DISTINCT FROM $9 AND "
            "history_station_passing_status IS NOT DISTINCT FROM $10 AND "
            "operator IS NOT DISTINCT FROM $11 AND "
            "failure_reasons IS NOT DISTINCT FROM $12 AND "
            "failure_note IS NOT DISTINCT FROM $13 AND "
            "failure_code IS NOT DISTINCT FROM $14 AND "
            "diag_version IS NOT DISTINCT FROM $15 AND "
            "fixture_no IS NOT DISTINCT FROM $16 AND "
            "data_source IS NOT DISTINCT FROM $17";
    } else {
        return existingIndices;
    }
    
    // Process records one-by-one (like Python) - simple and fast with indexes
    std::cout << "[DEBUG] [checkDuplicatesBatch] Checking records one-by-one using parameterized queries..." << std::endl;
    std::cout.flush();
    
    for (size_t i = 0; i < records.size(); ++i) {
        // Log progress every 1000 records
        if (i > 0 && i % 1000 == 0) {
            std::cout << "[DEBUG] [checkDuplicatesBatch] Processed " << i << " of " << records.size() << " records (found " << existingIndices.size() << " existing so far)" << std::endl;
            std::cout.flush();
        }
        
        const auto& record = records[i];
        
        // Prepare parameters - convert NULL markers to empty strings for parameterized query
        std::vector<std::string> paramValues;
        auto getParam = [&record](const std::string& key) -> std::string {
            if (!record.count(key)) return "";
            std::string val = record.at(key);
            if (val == "__NULL__") return "";
            return val;
        };
        
        if (fileType == FileType::WORKSTATION) {
            paramValues.push_back(getParam("sn"));
            paramValues.push_back(getParam("pn"));
            paramValues.push_back(getParam("customer_pn"));
            paramValues.push_back(getParam("workstation_name"));
            paramValues.push_back(getParam("history_station_start_time"));
            paramValues.push_back(getParam("history_station_end_time"));
            paramValues.push_back(getParam("hours"));
            paramValues.push_back(getParam("service_flow"));
            paramValues.push_back(getParam("model"));
            paramValues.push_back(getParam("history_station_passing_status"));
            paramValues.push_back(getParam("passing_station_method"));
            paramValues.push_back(getParam("operator"));
            paramValues.push_back(getParam("data_source"));
        } else if (fileType == FileType::TESTBOARD) {
            paramValues.push_back(getParam("sn"));
            paramValues.push_back(getParam("pn"));
            paramValues.push_back(getParam("model"));
            paramValues.push_back(getParam("work_station_process"));
            paramValues.push_back(getParam("baseboard_sn"));
            paramValues.push_back(getParam("baseboard_pn"));
            paramValues.push_back(getParam("workstation_name"));
            paramValues.push_back(getParam("history_station_start_time"));
            paramValues.push_back(getParam("history_station_end_time"));
            paramValues.push_back(getParam("history_station_passing_status"));
            paramValues.push_back(getParam("operator"));
            paramValues.push_back(getParam("failure_reasons"));
            paramValues.push_back(getParam("failure_note"));
            paramValues.push_back(getParam("failure_code"));
            paramValues.push_back(getParam("diag_version"));
            paramValues.push_back(getParam("fixture_no"));
            paramValues.push_back(getParam("data_source"));
        }
        
        // Convert to parameter arrays for PQexecParams
        std::vector<const char*> params;
        std::vector<int> paramLengths;
        std::vector<int> paramFormats;
        
        params.reserve(paramValues.size());
        paramLengths.reserve(paramValues.size());
        paramFormats.reserve(paramValues.size());
        
        for (const auto& val : paramValues) {
            if (val.empty()) {
                // Empty string means NULL - pass NULL pointer
                params.push_back(nullptr);
                paramLengths.push_back(0);
            } else {
                params.push_back(val.c_str());
                paramLengths.push_back(val.length());
            }
            paramFormats.push_back(0);  // 0 = text format
        }
        
        // Execute parameterized query (like Python's cursor.execute)
        PGresult* res = PQexecParams(pgconn, checkQuery.c_str(), paramValues.size(), nullptr,
                                     params.data(), paramLengths.data(), paramFormats.data(), 0);
        
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            // Error on this record - log and continue
            std::string errorMsg = PQerrorMessage(pgconn);
            if (i < 5) {  // Only log first few errors to avoid spam
                std::cout << "[DEBUG] [checkDuplicatesBatch] Query error for record " << i << ": " << errorMsg << std::endl;
                std::cout.flush();
            }
            PQclear(res);
            continue;
        }
        
        // Check if record exists (COUNT > 0)
        int count = atoi(PQgetvalue(res, 0, 0));
        if (count > 0) {
            existingIndices.insert(i);
        }
        
        PQclear(res);
    }
    
    std::cout << "[DEBUG] [checkDuplicatesBatch] END - found " << existingIndices.size() << " existing records out of " << records.size() << std::endl;
    std::cout.flush();
    
    return existingIndices;
}

bool DatabaseLoader::importCSV(const std::string& csvFilePath, FileType fileType) {
    std::cout << "[DEBUG] [importCSV] START" << std::endl;
    std::cout.flush();
    
    lastError_.clear();
    lastStats_ = {0, 0, 0, 0, false};
    
    std::cout << "[DEBUG] [importCSV] Checking if file exists..." << std::endl;
    std::cout.flush();
    
    if (!fs::exists(csvFilePath)) {
        lastError_ = "CSV file does not exist: " + csvFilePath;
        return false;
    }
    
    if (fileType == FileType::UNKNOWN) {
        lastError_ = "Unknown file type, cannot import";
        return false;
    }
    
    std::cout << "[DatabaseLoader] Importing " << fs::path(csvFilePath).filename().string()
              << " into database..." << std::endl;
    
    std::cout << "[DEBUG] [importCSV] About to parse CSV file..." << std::endl;
    std::cout.flush();
    
    // Parse CSV
    std::vector<std::map<std::string, std::string>> csvRows = parseCSV(csvFilePath);
    
    std::cout << "[DEBUG] [importCSV] CSV parsing complete" << std::endl;
    std::cout.flush();
    
    if (csvRows.empty()) {
        return false; // Error already set in parseCSV
    }
    
    lastStats_.totalRows = csvRows.size();
    std::cout << "[DatabaseLoader] Parsed " << csvRows.size() << " rows from CSV" << std::endl;
    
    // STEP 1: Map all CSV rows to database format
    std::cout << "[DEBUG] [importCSV] Mapping CSV rows to database format..." << std::endl;
    std::cout.flush();
    
    std::vector<std::map<std::string, std::string>> mappedRecords;
    for (const auto& csvRow : csvRows) {
        mappedRecords.push_back(mapRowToDatabase(csvRow, fileType));
    }
    
    std::cout << "[DEBUG] [importCSV] Mapped " << mappedRecords.size() << " records" << std::endl;
    std::cout.flush();
    
    // STEP 2: Deduplicate within CSV (remove duplicate rows in the file itself)
    std::cout << "[DatabaseLoader] Deduplicating CSV rows (removing duplicates within file)..." << std::endl;
    size_t beforeDedup = mappedRecords.size();
    mappedRecords = deduplicateCSVRows(mappedRecords, fileType);
    size_t csvDuplicates = beforeDedup - mappedRecords.size();
    std::cout << "[DatabaseLoader] Removed " << csvDuplicates << " duplicate rows from CSV" << std::endl;
    
    std::cout << "[DEBUG] [importCSV] ⚠️ ABOUT TO CONNECT TO DATABASE" << std::endl;
    std::cout.flush();
    
    // Connect to database
    void* conn = connectToDatabase();
    
    std::cout << "[DEBUG] [importCSV] ✓ connectToDatabase() RETURNED" << std::endl;
    std::cout.flush();
    
    if (!conn) {
        std::cout << "[DEBUG] [importCSV] Connection failed, returning false" << std::endl;
        std::cout.flush();
        return false; // Error already set
    }
    
    std::cout << "[DEBUG] [importCSV] Connection successful, starting duplicate check" << std::endl;
    std::cout.flush();
    
    // STEP 3: Insert all records - let database UNIQUE constraint handle duplicates
    // We normalize NULL customer_pn to empty string so UNIQUE constraint works properly
    std::cout << "[DatabaseLoader] Inserting all records (database will skip duplicates via UNIQUE constraint)..." << std::endl;
    
    // Insert all records - ON CONFLICT DO NOTHING will skip duplicates
    // The database tells us exactly how many were actually inserted
    size_t inserted = insertRecords(conn, mappedRecords, fileType);
    
    // Statistics: trust what the database says
    lastStats_.totalRows = mappedRecords.size();
    lastStats_.insertedRows = inserted;  // What database actually inserted
    lastStats_.newRows = inserted;        // Same as inserted
    lastStats_.existingRows = mappedRecords.size() - inserted;  // Total - inserted = duplicates
    
    if (inserted == 0) {
        std::cout << "[DatabaseLoader] All " << mappedRecords.size() << " records already exist in database (0 new inserts)" << std::endl;
    } else {
        std::cout << "[DatabaseLoader] Database inserted " << inserted << " new records, skipped " 
                  << (mappedRecords.size() - inserted) << " duplicates" << std::endl;
    }
    
    std::cout << "[DEBUG] [importCSV] About to close connection" << std::endl;
    std::cout.flush();
    
    closeConnection(conn);
    
    std::cout << "[DEBUG] [importCSV] Connection closed" << std::endl;
    std::cout.flush();
    
    // Move CSV file to archive instead of deleting (if any records were inserted or all were duplicates)
    if (lastStats_.insertedRows > 0 || inserted == 0) {
        try {
            fs::path csvPath(csvFilePath);
            fs::path archiveDir = csvPath.parent_path() / "archive";
            
            // Create archive directory if it doesn't exist
            if (!fs::exists(archiveDir)) {
                fs::create_directories(archiveDir);
            }
            
            // Generate unique filename with timestamp to avoid conflicts
            std::string timestamp = std::to_string(std::time(nullptr));
            std::string archiveFilename = csvPath.stem().string() + "_" + timestamp + csvPath.extension().string();
            fs::path archivePath = archiveDir / archiveFilename;
            
            // Move file to archive
            fs::rename(csvFilePath, archivePath);
            std::cout << "[DatabaseLoader] Archived CSV file to: " << archivePath.filename().string() << std::endl;
        } catch (const fs::filesystem_error& e) {
            std::cerr << "[DatabaseLoader] Warning: Could not archive CSV file: " << e.what() << std::endl;
            // Fallback: try to delete if archive fails
            try {
                fs::remove(csvFilePath);
                std::cout << "[DatabaseLoader] Deleted CSV file after archive failed" << std::endl;
            } catch (const fs::filesystem_error& e2) {
                std::cerr << "[DatabaseLoader] Error: Could not delete CSV file either: " << e2.what() << std::endl;
            }
        }
    }
    
    lastStats_.success = true;
    
    std::cout << "[DEBUG] [importCSV] END - SUCCESS" << std::endl;
    std::cout.flush();
    
    return true;
}

std::string DatabaseLoader::getLastError() const {
    return lastError_;
}

DatabaseLoader::ImportStats DatabaseLoader::getLastImportStats() const {
    return lastStats_;
}

