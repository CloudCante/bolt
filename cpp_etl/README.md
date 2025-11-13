# FoxETL C++ File Monitor

A lightweight C++ file monitor using Linux inotify to detect files in real-time.

## Building

### Prerequisites
- CMake 3.10 or higher
- GCC with C++17 support
- Linux system with inotify support

### Build Steps

```bash
# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake ..

# Build
make

# Run
./foxetl
```

## Usage

### Basic Usage (watch all files in ./input)
```bash
./foxetl -d ./input
```

### Watch for specific files
```bash
./foxetl -d ./input -p workstationOutputReport.xls -p "Test board record report.xls"
```

### Options
- `-d, --directory PATH` : Directory to watch (default: `./input`)
- `-p, --pattern PATTERN` : File pattern to match (can specify multiple)
- `-h, --help` : Show help message

## How It Works

1. Uses Linux `inotify` API for instant file detection (no polling!)
2. Watches for `IN_CLOSE_WRITE` events (file written and closed)
3. Runs in background thread, 0% CPU when idle
4. Calls callback function when matching files detected

## Architecture

```
main.cpp          -> CLI interface, signal handling
FileWatcher.h/cpp -> Core file watching logic
```

## Next Steps

- [ ] Add Python script spawning when files detected
- [ ] Add job queue for managing multiple files
- [ ] Add state machine
- [ ] Add terminal UI for control

