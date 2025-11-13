[[C++ ETL Project Hub|← Back to Hub]]

  

## Overview

Single long-running C++ application that replaces 21+ Python scripts with event-driven, resource-efficient architecture.

  

**Philosophy:** One process, always running, zero waste.

  

---

  

## Current Python Architecture (Problems)

  

```

File_Monitor.py (polls every 10s)

↓

Spawns import_testboard_file.py

- Python interpreter: ~40MB

- pandas: ~150MB

- Processes file, exits

↓

Spawns import_workstation_file.py

- Python interpreter: ~40MB

- pandas: ~150MB

- Processes file, exits

↓

(2 minutes pass)

↓

AutoAggregator_Recent.py (timer-based)

↓

Spawns 10 aggregation scripts sequentially

- Each: ~60MB RAM

- Each: ~200ms startup overhead

- Total: ~600MB RAM, ~2s wasted on startup

↓

AutoAggregator_Historical.py (timer-based)

↓

Spawns 10 more aggregation scripts

- Another ~600MB RAM

- Another ~2s startup overhead

```

  

### Problems:

- ❌ **Wasteful:** Runs on timer even when no data

- ❌ **Slow:** Process creation overhead × 20+

- ❌ **Memory hungry:** ~1.6GB peak usage

- ❌ **No coordination:** Scripts don't know about each other

- ❌ **Blind:** No state awareness

- ❌ **Fragile:** 21+ scripts to maintain

  

---

  

## New C++ Architecture (Solution)

  

```

┌─────────────────────────────────────────┐

│ FoxETLApp (Single Process) │

│ │

│ ┌────────────────────────────────────┐ │

│ │ File Watcher (inotify) │ │

│ │ - Instant detection │ │

│ │ - 0% CPU when idle │ │

│ └────────────┬───────────────────────┘ │

│ │ │

│ ▼ │

│ ┌────────────────────────────────────┐ │

│ │ Job Queue (thread-safe) │ │

│ │ - Import jobs │ │

│ │ - Aggregation jobs │ │

│ │ - Tracks dependencies │ │

│ └────────────┬───────────────────────┘ │

│ │ │

│ ▼ │

│ ┌────────────────────────────────────┐ │

│ │ Worker Threads │ │

│ │ - Process jobs in parallel │ │

│ │ - Shared connection pool │ │

│ └────────────┬───────────────────────┘ │

│ │ │

│ ▼ │

│ ┌────────────────────────────────────┐ │

│ │ Database Connection Pool │ │

│ │ - 5 persistent connections │ │

│ │ - No connect/disconnect overhead │ │

│ └────────────┬───────────────────────┘ │

│ │ │

│ ▼ │

│ PostgreSQL Database │

└─────────────────────────────────────────┘

```

  

### Benefits:

- ✅ **Efficient:** Only runs when data arrives

- ✅ **Fast:** Zero process creation overhead

- ✅ **Lean:** ~50MB peak usage (96% less than Python)

- ✅ **Coordinated:** Shared state, job dependencies

- ✅ **Aware:** Knows exactly what's happening

- ✅ **Simple:** One binary to maintain

  

---

  

## Core Components

  

### 1. File Watcher

```cpp

class FileWatcher {

// Linux inotify for instant file detection

int inotify_fd;

std::map<int, std::string> watch_descriptors;

void watch_directory(const std::string& path) {

// Add inotify watch for IN_CLOSE_WRITE events

// Triggers callback when file fully written

}

void on_file_detected(const std::string& filepath) {

auto file_type = detect_file_type(filepath);

job_queue.enqueue(ImportJob{file_type, filepath});

}

};

```

  

**Performance:**

- Detection latency: <10ms (vs 10s polling)

- CPU usage (idle): 0%

- Memory: ~1MB

  

---

  

### 2. Job Queue

```cpp

class JobQueue {

std::queue<Job> pending_jobs;

std::mutex queue_mutex;

std::condition_variable queue_cv;

std::atomic<int> pending_imports{0};

void enqueue(Job job) {

std::lock_guard lock(queue_mutex);

pending_jobs.push(job);

queue_cv.notify_one();

if (job.type == JobType::IMPORT) {

pending_imports++;

}

}

void on_import_complete() {

if (--pending_imports == 0) {

// Queue empty, trigger aggregation

enqueue(AggregationJob{current_mode});

}

}

};

```

  

**Features:**

- Thread-safe queue

- Dependency tracking

- Automatic aggregation trigger

- Priority support (future)

  

---

  

### 3. Database Connection Pool

```cpp

class DatabasePool {

std::vector<PGconn*> connections;

std::queue<PGconn*> available;

std::mutex pool_mutex;

std::condition_variable pool_cv;

PGconn* acquire() {

std::unique_lock lock(pool_mutex);

pool_cv.wait(lock, [this]() {

return !available.empty();

});

auto conn = available.front();

available.pop();

return conn;

}

void release(PGconn* conn) {

std::lock_guard lock(pool_mutex);

available.push(conn);

pool_cv.notify_one();

}

};

```

  

**Benefits:**

- No connection overhead (persistent connections)

- Thread-safe access

- Automatic connection recycling

- Health checks (future)

  

---

  

### 4. State Machine

```cpp

enum class State {

IDLE, // Nothing happening

IMPORTING, // Processing imports

AGGREGATING, // Running aggregations

RECONFIGURING, // Changing settings

DEBUG_MODE // Verbose logging

};

  

class StateMachine {

std::atomic<State> current_state{State::IDLE};

std::mutex state_mutex;

std::condition_variable state_cv;

void transition(State new_state) {

std::lock_guard lock(state_mutex);

logger.info("State: {} → {}",

state_name(current_state),

state_name(new_state));

current_state = new_state;

state_cv.notify_all();

}

void wait_for_idle() {

std::unique_lock lock(state_mutex);

state_cv.wait(lock, [this]() {

return current_state == State::IDLE;

});

}

};

```

  

**State Transitions:**

```

IDLE → IMPORTING (file detected)

IMPORTING → IDLE (queue empty, no aggregation needed)

IMPORTING → AGGREGATING (queue empty, trigger aggregation)

AGGREGATING → IDLE (aggregation complete)

Any → RECONFIGURING (user requests config change, waits for idle)

RECONFIGURING → IDLE (config applied)

```

  

---

  

### 5. Configuration Manager

```cpp

struct Config {

enum AggregationMode {

ALL_TIME,

LAST_7_DAYS,

LAST_3_DAYS,

LAST_24_HOURS

};

AggregationMode mode = LAST_7_DAYS;

bool auto_aggregate = true;

int aggregation_delay_seconds = 30;

bool parallel_aggregation = true;

};

  

class ConfigManager {

std::atomic<Config> current_config;

void change_mode(Config::AggregationMode new_mode) {

state_machine.wait_for_idle(); // Wait for safe moment

auto cfg = current_config.load();

cfg.mode = new_mode;

current_config.store(cfg);

logger.info("Aggregation mode changed to: {}", mode_name(new_mode));

}

};

```

  

**Features:**

- Runtime configuration changes (no restart)

- Safe updates (waits for idle state)

- Atomic reads (lock-free)

  

---

  

## Resource Comparison

  

| Component | Python | C++ | Improvement |

| --- | --- | --- | --- |

| **File Monitor** | 50MB (polling) | 1MB (inotify) | 98% less |

| **Import Process** | 260-410MB each | 10MB shared | 96% less |

| **Aggregation** | 600MB (10 scripts) | 5MB shared | 99% less |

| **Total (idle)** | 140MB | 25MB | 82% less |

| **Total (peak)** | 1.6GB | 50MB | 96% less |

| **CPU (idle)** | 2-5% | 0% | 100% less |

| **Startup time** | 2-3s/cycle | 0ms | 100% less |

  

---

  

## Threading Model

  

```

Main Thread

- Event loop

- Control interface (terminal UI)

- State management

  

File Watcher Thread

- inotify monitoring

- File detection

- Job enqueueing

  

Job Processor Threads (pool of 4)

- Dequeue jobs

- Execute imports

- Execute aggregations

- Release resources

  

Logger Thread (optional)

- Async logging

- File rotation

- Log compression

```

  

**Thread Safety:**

- Job queue: mutex + condition variable

- Connection pool: mutex + condition variable

- State machine: atomic + condition variable

- Config: atomic (lock-free reads)

  

---

  

## Deployment Model

  

### Single Server Deployment

```

Server (4GB RAM, 4 CPU cores)

├─ PostgreSQL (1.5GB)

├─ C++ ETL Engine (50MB)

├─ Web Dashboard (500MB)

└─ Available for ML (2GB)

```

  

### Future: Distributed Deployment

```

Server 1: C++ ETL Engine + PostgreSQL

Server 2: Python ML Services

Server 3: Web Dashboard + API

```

  

---

  

## Startup Sequence

  

```

1. Load configuration from config file

2. Initialize logger

3. Connect to PostgreSQL (create pool)

4. Load schema registry from DB

5. Start file watcher thread

6. Start job processor threads

7. Start control interface

8. Enter event loop (IDLE state)

```

  

**Startup time:** <100ms

  

---

  

## Shutdown Sequence

  

```

1. User presses 'x' or SIGTERM received

2. Stop accepting new files (disable inotify)

3. Wait for job queue to drain

4. Wait for all threads to finish

5. Close database connections

6. Flush logs

7. Exit cleanly

```

  

**Shutdown time:** <5s (waits for in-progress jobs)

  

---

  

## Error Recovery

  

### Transient Errors

- Database connection lost → Retry with exponential backoff

- File conversion timeout → Retry once, then quarantine

- Disk full → Alert, pause imports until resolved

  

### Permanent Errors

- Schema mismatch → Quarantine, alert, wait for approval

- Corrupted file → Quarantine, alert

- Invalid data → Log, skip row, continue

  

### Crash Recovery

- State persisted to disk (future)

- Resume from last checkpoint (future)

- Reprocess quarantined files on startup (future)

  

---

  

## Monitoring & Observability

  

### Metrics (Future)

- Import throughput (rows/second)

- Aggregation duration (per table)

- Memory usage (peak/average)

- CPU usage (per operation)

- Queue depth over time

- Error rates

  

### Logging

- Structured logs (JSON format)

- Multiple log levels (SILENT, NORMAL, VERBOSE, DEBUG)

- Runtime log level changes

- Log rotation (daily)

- Log compression (gzip)

  

### Alerts

- Schema drift detected

- Import failures

- Database connection issues

- Disk space low

- Performance degradation

  

---

  

## Future Enhancements

  

### High Availability

- Multiple ETL instances (active/standby)

- Leader election (only one processes files)

- Automatic failover

- Distributed file locking

  

### Scalability

- Horizontal scaling (multiple workers)

- Partitioned job queue

- Sharded database connections

- Load balancing

  

### Observability

- Prometheus metrics endpoint

- Grafana dashboard

- Distributed tracing (OpenTelemetry)

- APM integration

  

---

  

## Related Documents

- [[Import Pipeline Design]] - Detailed import workflow

- [[State Management]] - State machine details

- [[Database Schema Extensions]] - Schema registry tables

- [[Control Interface]] - Terminal UI design

  

---

  

## Tags

#architecture #etl #cpp #design #system-design