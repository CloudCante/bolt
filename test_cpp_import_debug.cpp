// Quick test to debug operator column issue
// This simulates what the C++ code does

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>

// Simulate the logic
std::set<std::string> getColumnsToKeep() {
    std::set<std::string> columns;
    columns.insert("sn");
    columns.insert("pn");
    columns.insert("customer_pn");
    columns.insert("workstation_name");
    columns.insert("history_station_start_time");
    columns.insert("history_station_end_time");
    columns.insert("hours");
    columns.insert("service_flow");
    columns.insert("model");
    columns.insert("history_station_passing_status");
    columns.insert("passing_station_method");
    columns.insert("operator");
    // Note: first_station_start_time is NOT in this list
    return columns;
}

std::string normalizeColumnName(const std::string& name) {
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::replace(normalized.begin(), normalized.end(), ' ', '_');
    std::replace(normalized.begin(), normalized.end(), '-', '_');
    return normalized;
}

int main() {
    // Simulate original CSV columns (typical workstation file)
    std::vector<std::string> originalColumns = {
        "SN", "PN", "Model", "Workstation Name",
        "History station start time", "History station end time",
        "Hours", "Service Flow", "History station passing status",
        "Passing station method", "First station start time",  // <-- This will be removed
        "Operator", "Customer PN"
    };
    
    std::cout << "Original columns:" << std::endl;
    for (size_t i = 0; i < originalColumns.size(); ++i) {
        std::cout << "  [" << i << "] " << originalColumns[i] << std::endl;
    }
    
    // Normalize
    std::vector<std::string> normalizedColumns;
    for (const auto& col : originalColumns) {
        normalizedColumns.push_back(normalizeColumnName(col));
    }
    
    std::cout << "\nNormalized columns:" << std::endl;
    for (size_t i = 0; i < normalizedColumns.size(); ++i) {
        std::cout << "  [" << i << "] " << originalColumns[i] << " -> " << normalizedColumns[i] << std::endl;
    }
    
    // Find columns to keep
    std::set<std::string> columnsToKeep = getColumnsToKeep();
    std::vector<std::string> keptColumns;
    std::vector<size_t> keptIndices;
    
    for (size_t i = 0; i < normalizedColumns.size(); ++i) {
        const std::string& normCol = normalizedColumns[i];
        if (columnsToKeep.find(normCol) != columnsToKeep.end()) {
            keptColumns.push_back(normalizedColumns[i]);
            keptIndices.push_back(i);
        }
    }
    
    std::cout << "\nColumns to keep (after filtering):" << std::endl;
    for (size_t i = 0; i < keptColumns.size(); ++i) {
        std::cout << "  [" << i << "] " << keptColumns[i] 
                  << " (from original index " << keptIndices[i] << ")" << std::endl;
    }
    
    // Check if operator is in kept columns
    bool foundOperator = false;
    size_t operatorIndex = 0;
    for (size_t i = 0; i < keptColumns.size(); ++i) {
        if (keptColumns[i] == "operator") {
            foundOperator = true;
            operatorIndex = i;
            std::cout << "\n✓ Found 'operator' at cleaned CSV index " << i 
                      << " (from original index " << keptIndices[i] << ")" << std::endl;
            break;
        }
    }
    
    if (!foundOperator) {
        std::cout << "\n✗ ERROR: 'operator' NOT FOUND in kept columns!" << std::endl;
        std::cout << "Looking for variations..." << std::endl;
        for (size_t i = 0; i < keptColumns.size(); ++i) {
            if (keptColumns[i].find("operator") != std::string::npos) {
                std::cout << "  Found similar: [" << i << "] " << keptColumns[i] << std::endl;
            }
        }
    }
    
    return 0;
}

