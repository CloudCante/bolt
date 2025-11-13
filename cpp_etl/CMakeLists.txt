cmake_minimum_required(VERSION 3.10)
project(FoxETL VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Define to exclude main() from aggregation files when building combined executables
add_definitions(-DNO_MAIN_AGGREGATION)

# Source files
set(SOURCES
    src/main.cpp
    src/FileWatcher.cpp
    src/FileTypeDetector.cpp
    src/JobQueue.cpp
    src/FileConverter.cpp
    src/InputProcessor.cpp
    src/DataProcessor.cpp
    src/DatabaseLoader.cpp
    src/WorkerPool.cpp
    src/aggregation_manager.cpp
)

# Aggregation source files
set(AGGREGATION_SOURCES
    src/aggregations/workstation/tpy_daily_metrics.cpp
    src/aggregations/workstation/tpy_weekly_metrics.cpp
    src/aggregations/workstation/pchart_data.cpp
    src/aggregations/workstation/daily_packing.cpp
    src/aggregations/testboard/testboard_station_performance.cpp
    src/aggregations/testboard/fixture_performance.cpp
    src/aggregations/testboard/snfn_reports.cpp
)

# Header files
set(HEADERS
    include/FileWatcher.h
    include/FileTypeDetector.h
    include/Job.h
    include/JobQueue.h
    include/FileConverter.h
    include/InputProcessor.h
    include/DataProcessor.h
    include/DatabaseLoader.h
    include/WorkerPool.h
    include/aggregation_manager.h
    include/aggregations.h
    include/config.h
)

# Create executable (includes aggregations)
add_executable(foxetl ${SOURCES} ${AGGREGATION_SOURCES} ${HEADERS})

# Find PostgreSQL
find_path(POSTGRESQL_INCLUDE_DIR libpq-fe.h
    PATHS
    /usr/include/postgresql
    /usr/include
    /usr/local/include/postgresql
    /usr/local/include
)

# Find PostgreSQL library
find_library(POSTGRESQL_LIBRARY pq
    PATHS
    /usr/lib
    /usr/lib64
    /usr/local/lib
    /usr/local/lib64
)

# Find libpqxx (C++ wrapper for PostgreSQL)
find_library(PQXX_LIBRARY pqxx
    PATHS
    /usr/lib
    /usr/lib64
    /usr/local/lib
    /usr/local/lib64
)

# Find pqxx headers
find_path(PQXX_INCLUDE_DIR pqxx/pqxx
    PATHS
    /usr/include
    /usr/local/include
)

# Test executable for DatabaseLoader
set(TEST_DB_SOURCES
    src/FileTypeDetector.cpp
    src/DatabaseLoader.cpp
)
add_executable(test_database_loader
    test_database_loader.cpp
    ${TEST_DB_SOURCES}
    ${HEADERS}
)

# Test executable for AggregationManager (includes aggregations)
add_executable(test_aggregation_manager
    test_aggregation_manager.cpp
    src/aggregation_manager.cpp
    ${AGGREGATION_SOURCES}
)

# Include directories
target_include_directories(foxetl PRIVATE include)
target_include_directories(test_database_loader PRIVATE include)
target_include_directories(test_aggregation_manager PRIVATE include)
if(POSTGRESQL_INCLUDE_DIR)
    target_include_directories(foxetl PRIVATE ${POSTGRESQL_INCLUDE_DIR})
    target_include_directories(test_database_loader PRIVATE ${POSTGRESQL_INCLUDE_DIR})
    target_include_directories(test_aggregation_manager PRIVATE ${POSTGRESQL_INCLUDE_DIR})
    add_definitions(-DHAVE_LIBPQ=1)
endif()
if(PQXX_INCLUDE_DIR)
    target_include_directories(foxetl PRIVATE ${PQXX_INCLUDE_DIR})
    target_include_directories(test_aggregation_manager PRIVATE ${PQXX_INCLUDE_DIR})
endif()

# Link libraries
if(POSTGRESQL_LIBRARY)
    target_link_libraries(foxetl ${POSTGRESQL_LIBRARY})
    target_link_libraries(test_database_loader ${POSTGRESQL_LIBRARY})
    
    # Link libpqxx for aggregations
    if(PQXX_LIBRARY)
        target_link_libraries(foxetl ${PQXX_LIBRARY})
        target_link_libraries(test_aggregation_manager ${PQXX_LIBRARY})
        target_link_libraries(test_aggregation_manager ${POSTGRESQL_LIBRARY})
        message(STATUS "Found libpqxx: ${PQXX_LIBRARY}")
    else()
        message(WARNING "libpqxx not found. Aggregations will not work.")
        message(WARNING "Install with: dnf install libpqxx-devel")
    endif()
else()
    message(WARNING "PostgreSQL libraries not found. DatabaseLoader will not work.")
    message(WARNING "Install with: dnf install postgresql-devel")
endif()

