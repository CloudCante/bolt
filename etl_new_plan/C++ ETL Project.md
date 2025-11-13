
## Overview

Event-driven C++ ETL engine replacing 21+ Python scripts with a single efficient application.

  

**Status:** Planning Phase

**Target:** Production deployment

**Current:** Python-based (Fox_ETL)

  

---

  

## Core Documents

  

### Architecture

- [[ETL Architecture Overview]] - System design, components, resource targets

- [[State Management]] - Application states and transitions

- [[Database Schema Extensions]] - New tables for schema registry

  

### Import Pipeline

- [[Import Pipeline Design]] - Full import workflow (detection → cleanup)

- [[Schema Validation]] - Schema drift detection and quarantine

- [[CSV Preprocessing]] - Column removal and deduplication

- [[Database Import]] - PostgreSQL COPY and error handling

  

### Aggregation System

- [[Aggregation Design]] - Event-driven aggregation, modes, triggers

- [[Aggregation Functions]] - Station performance, fixture stats, TPY

  

### User Interface

- [[Control Interface]] - Terminal UI, keyboard controls, dashboard

- [[Logging System]] - Log levels, runtime control, output modes

  

### Reference

- [[Database Schema Reference]] - Master log table structures

- [[Problematic Columns]] - outbound_version issue, dedup rules

- [[Python ETL Analysis]] - Current system documentation

  

---

  

## Implementation Tracking

  

### Phase 1: Foundation

- [ ] Project setup (CMake, dependencies)

- [ ] Database connection pool

- [ ] File watcher (inotify)

- [ ] Job queue system

  

### Phase 2: Import Pipeline

- [ ] XLS to CSV conversion

- [ ] Schema validation

- [ ] CSV preprocessing

- [ ] Database import

  

### Phase 3: Aggregation

- [ ] Event-driven triggers

- [ ] Aggregation functions

- [ ] Mode switching

  

### Phase 4: Interface

- [ ] Terminal UI

- [ ] Logging system

- [ ] Control commands

  

---

  

## Quick Links

- [[Open Questions]] - Technical and operational decisions

- [[Migration Strategy]] - Cutover plan

- [[Future Enhancements]] - ML, monitoring, HA

  

---

  

## Resource Targets

  

| Metric | Python (Current) | C++ (Target) | Improvement |

| --- | --- | --- | --- |

| Idle RAM | 140MB | 25MB | 82% less |

| Peak RAM | 1.6GB | 50MB | 96% less |

| CPU (idle) | 2-5% | 0% | 100% less |

| Startup overhead | 2-3s/cycle | 0ms | 100% less |

| Import time (10k rows) | ~3s | ~0.5s | 6x faster |

  

---

  

## Tags

#etl #cpp #project-hub #fox-production