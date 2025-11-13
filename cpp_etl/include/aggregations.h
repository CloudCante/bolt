#ifndef AGGREGATIONS_H
#define AGGREGATIONS_H

// Workstation aggregation functions
// All return 0 on success, non-zero on error

int run_tpy_daily_metrics_aggregation();
int run_tpy_weekly_metrics_aggregation();
int run_pchart_data_aggregation();
int run_daily_packing_aggregation();

// Testboard aggregation functions
int run_testboard_station_performance_aggregation();
int run_fixture_performance_aggregation();
int run_snfn_reports_aggregation();

#endif

