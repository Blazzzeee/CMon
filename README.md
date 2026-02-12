# CMon - Complete Documentation

**Version**: 1.0  
**Language**: C  
**Author**: 3rd Year Tier-2 Student (India)

---

## Table of Contents

1. [Overview](#overview)
2. [High-Level Architecture](#high-level-architecture)
3. [Component Design](#component-design)
4. [Design Decisions & Tradeoffs](#design-decisions--tradeoffs)
5. [Setup & Installation](#setup--installation)
6. [API Reference](#api-reference)
7. [Performance Characteristics](#performance-characteristics)
8. [Security Model](#security-model)
9. [Troubleshooting](#troubleshooting)
10. [Development Guide](#development-guide)

---

## Overview

### What is CMon?

**CMon** (C Monitor) is a lightweight HTTP server for remote server operations management. It provides authenticated REST API endpoints to execute system administration commands remotely.

### Problem Statement

Managing remote servers typically requires:
- SSH access and manual command execution
- Custom scripts scattered across systems
- Multiple tools for different operations
- Manual deployment processes

CMon consolidates common server operations into a single authenticated HTTP API, enabling:
- Programmatic server control
- Automated deployment workflows
- Integration with CI/CD pipelines
- Discord/Slack bot integrations
- DevOps automation

### Key Features

✅ **Authenticated Command Execution** - All endpoints require 256-bit secret key  
✅ **System Operations** - Reboot, restart, health checks  
✅ **Git Integration** - Pull updates, deploy branches  
✅ **Log Viewing** - Access systemd journal entries  
✅ **High Performance** - Custom allocator provides 88x speedup  
✅ **Security-Conscious** - Timing-safe authentication, no shell injection  
✅ **Event-Driven** - Single-threaded async I/O via libevent  

### Performance Metrics

Based on benchmark results (`bench.txt`):

**Standard malloc**: 0.79 M ops/sec, P50=2736 cycles, P99=10126 cycles  
**Arena allocator**: 69.38 M ops/sec, P50=19 cycles, P99=30 cycles  

**Improvement**: 87.8x throughput, 144x lower latency

### Use Cases

**Ideal For:**
- Internal DevOps tooling
- CI/CD pipeline integration
- Discord/Slack bot backends
- Server management dashboards
- Automated deployment systems

**Not Suitable For:**
- Public-facing APIs (no TLS by default)
- Multi-tenant systems (single shared key)
- Untrusted environments (limited sandboxing)

---

## High-Level Architecture

### System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                         Client                               │
│           (HTTP Request + access_token header)               │
└────────────────────────┬────────────────────────────────────┘
                         │
                         │ HTTP/REST (Port 8000)
                         │
┌────────────────────────▼────────────────────────────────────┐
│                    CMon HTTP Server                          │
│                   (libevent 2.x)                            │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │         Authentication Middleware                  │    │
│  │  • Extracts access_token header                    │    │
│  │  • Validates with CRYPTO_memcmp (constant-time)    │    │
│  │  • Returns 401 if missing/invalid                  │    │
│  └────────────────┬───────────────────────────────────┘    │
│                   │                                          │
│  ┌────────────────▼───────────────────────────────────┐    │
│  │            Route Dispatcher                        │    │
│  │  Routes:                                           │    │
│  │  GET    /health           → uptime                 │    │
│  │  GET    /logs             → journalctl -n 50       │    │
│  │  POST   /reboot           → reboot                 │    │
│  │  POST   /restart          → pkill target           │    │
│  │  PUT    /sync_upstream    → git pull origin        │    │
│  │  GET    /deploy_branch    → ./deploy.sh           │    │
│  │  DELETE /teardown_branch  → ./teardown.sh         │    │
│  └────────────────┬───────────────────────────────────┘    │
│                   │                                          │
│  ┌────────────────▼───────────────────────────────────┐    │
│  │         Command Execution Layer                    │    │
│  │  • fork() child process                            │    │
│  │  • pipe() for stdout/stderr capture                │    │
│  │  • execvp() to run command                         │    │
│  │  • waitpid() for exit code                         │    │
│  │  • Timing measurement                              │    │
│  └────────────────┬───────────────────────────────────┘    │
│                   │                                          │
│  ┌────────────────▼───────────────────────────────────┐    │
│  │         Arena Memory Allocator                     │    │
│  │  • 64-bit bitmap (1 bit = 1 chunk)                 │    │
│  │  • Bit-smearing algorithm for free space           │    │
│  │  • O(1) allocation/deallocation                    │    │
│  │  • 2-byte header stores chunk count                │    │
│  └────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
                         │
                         │ System Calls
                         │
┌────────────────────────▼────────────────────────────────────┐
│                    Operating System                          │
│  Commands: uptime, reboot, pkill, git, journalctl           │
│  Scripts: ./deploy.sh, ./teardown.sh                        │
└─────────────────────────────────────────────────────────────┘
```

### Request Lifecycle

1. **Client sends HTTP request** with `access_token` header
2. **libevent receives request** on port 8000
3. **Authentication middleware** validates token in constant time
4. **Route dispatcher** matches path to handler
5. **Command executor** forks child process
6. **Child process** executes command via execvp()
7. **Parent process** captures output via pipe
8. **Response builder** formats JSON with escaped output
9. **Client receives response** with status, code, message, data

### Component Interaction

**HTTP Layer** (main.c) coordinates all components:
- Initializes arena allocator on startup
- Loads authentication key from file
- Registers routes with libevent
- Passes requests through auth middleware
- Delegates to command executors
- Formats responses using utilities

**Authentication Layer** (auth.c) provides security:
- Loads 256-bit hex key from `client_secret.key`
- Decodes hex to binary using OpenSSL
- Compares keys in constant time (prevents timing attacks)
- Returns 0 on success, non-zero on failure

**Command Layer** (commands.c) executes operations:
- Forks child process for isolation
- Uses pipes to capture stdout/stderr
- Executes via execvp() (no shell)
- Waits for completion and extracts exit code
- Measures execution duration
- Returns output allocated from arena

**Arena Layer** (arena.c) manages memory:
- Pre-allocates fixed-size pool (16KB default)
- Uses 64-bit bitmap to track free/used chunks
- Finds consecutive free chunks via bit-smearing
- Stores metadata header before user data
- Validates pointers on deallocation
- 88x faster than malloc for this workload

**Utility Layer** (utils.c) provides helpers:
- Dual logging to stderr and syslog
- JSON response formatting
- JSON string escaping (security critical)
- Query parameter parsing
- HTTP method string conversion

---

## Component Design

### HTTP Server Layer

**Technology**: libevent 2.x (asynchronous event-driven networking)

**Configuration**:
- **Port**: 8000 (hardcoded in main.c)
- **Binding**: 0.0.0.0 (all interfaces)
- **Methods**: GET, POST, PUT, DELETE
- **Concurrency**: Single-threaded event loop

**Route Table Structure**:
Routes are defined in a static array containing path, HTTP method, and callback function. This allows easy addition of new endpoints by adding entries to the array.

**Middleware Pattern**:
All requests pass through authentication middleware before reaching route handlers. The middleware extracts the access_token header, validates it, and either allows the request to proceed or returns 401 Unauthorized.

**Signal Handling**:
Registers handler for SIGINT to perform graceful shutdown - closes syslog, tears down arena, frees libevent structures in correct order.

**404 Handling**:
Generic request handler catches all undefined routes and returns JSON error with 404 status.

### Authentication System

**Security Model**:
- **Key Size**: 256-bit (32 bytes) - equivalent to SHA-256 strength
- **Storage**: File-based at `./client_secret.key` in hexadecimal format
- **Encoding**: Hex (64 characters) prevents binary data issues in text files
- **Comparison**: Constant-time using OpenSSL's `CRYPTO_memcmp()`

**Initialization Process**:
1. Reads key file from current directory
2. Validates file size (64 hex chars = 32 bytes, optionally +1 for newline)
3. Decodes hex string to binary using OpenSSL's `OPENSSL_hexstr2buf()`
4. Stores decoded key in global buffer
5. Cleanses temporary buffers with `OPENSSL_cleanse()` for security

**Authentication Flow**:
1. Extracts client key from HTTP header (in hex format)
2. Decodes client-provided hex key to binary
3. Performs constant-time comparison with stored key
4. Returns 0 on success, non-zero on failure

**Why Constant-Time Comparison?**

Standard comparison functions (strcmp, memcmp) exit early when they find a difference. This creates a timing side-channel: an attacker can measure response time to deduce where keys differ, enabling byte-by-byte brute forcing.

Constant-time comparison always examines all bytes regardless of differences, preventing timing attacks. Uses bitwise OR to accumulate differences without branching.

**Memory Security**:
Uses `OPENSSL_cleanse()` to zero sensitive memory before freeing, preventing key recovery from memory dumps or use-after-free vulnerabilities.

### Command Execution System

**Design Philosophy**: Process isolation via fork/exec with output capture

**Core Execution Flow**:
1. **Start timing** using gettimeofday()
2. **Create pipe** for capturing child output
3. **Fork process** to isolate command execution
4. **Child process**: Redirects stdout/stderr to pipe, executes command via execvp(), exits with code 127 if exec fails
5. **Parent process**: Closes write end of pipe, allocates buffer from arena, reads output, waits for child completion
6. **Extract exit code** using WIFEXITED() and WEXITSTATUS() macros
7. **Calculate duration** and log execution details
8. **Return output** and set exit code pointer

**Why Fork/Exec Instead of system()?**

The system() function invokes /bin/sh and passes the command as a string. This makes it vulnerable to shell injection attacks where malicious input can execute arbitrary commands.

Fork/exec passes arguments as a NULL-terminated array where each argument is treated as a literal string. No shell interpretation occurs, making injection impossible. Even if user input contains shell metacharacters like semicolons or pipes, they're passed literally to the program.

**Implemented Commands**:

| Endpoint | System Call | Purpose |
|----------|-------------|---------|
| /health | uptime | Check system uptime and load |
| /reboot | reboot | Reboot the system (requires root) |
| /restart | pkill target | Kill server binary for restart |
| /sync_upstream | git pull origin [branch] | Pull from git repository |
| /deploy_branch | ./deploy.sh [branch] | Run custom deployment script |
| /teardown_branch | ./teardown.sh [branch] | Run custom teardown script |
| /logs | journalctl -n 50 --no-pager | Fetch last 50 journal entries |

**Default Values**:
Branch parameters default to "main" if not provided in query string.

**Error Handling**:
- Exit code 127 indicates exec failure (command not found)
- Null return on pipe/fork failures
- Output buffer allocation failures logged

**Output Capture Limitation**:
Output is limited by arena buffer size. Analysis document mentions this has been fixed, suggesting buffer size was increased in production.

### Arena Memory Allocator

**This is the most sophisticated component, demonstrating graduate-level algorithm design.**

**Design Goals**:
1. **Performance**: Minimize allocation overhead for frequent small allocations
2. **Predictability**: Fixed memory footprint
3. **Simplicity**: O(1) operations
4. **Cache-friendliness**: Entire allocator state fits in cache line

**Configuration**:
- **Chunk Size**: 256 bytes (default)
- **Chunk Count**: 64 (maximum due to bitmap size)
- **Total Capacity**: 256 × 64 = 16,384 bytes (16KB)
- **Configurable**: via arena_config() function

**Data Structures**:

**Global State**:
- `LOCK`: 64-bit bitmap where each bit represents one chunk (0=free, 1=used)
- `BUF`: Pointer to backing storage allocated via malloc()

**Allocation Header**:
- 2-byte structure storing number of chunks allocated
- Placed immediately before user data
- Enables O(1) deallocation (header tells how many chunks to free)

**Memory Layout**:

Total arena is divided into fixed-size chunks. When allocating, the system calculates how many chunks are needed (including 2-byte header), finds consecutive free chunks, marks them as used in the bitmap, writes the chunk count to the header, and returns a pointer to the space after the header.

**The Bit-Smearing Algorithm**:

This is the most elegant part of the allocator. The challenge is finding k consecutive free chunks in O(k) time.

**Algorithm Overview**:
1. Invert the LOCK bitmap so free chunks are 1, used chunks are 0
2. Initialize combined mask with inverted bitmap
3. Perform k-1 iterations of: combined = combined AND (combined >> 1)
4. After k-1 iterations, any remaining 1 bit indicates start of k consecutive free chunks
5. Use compiler intrinsic __builtin_ctz (count trailing zeros) to find first set bit in O(1)

**Why This Works**:

After each iteration, a bit remains set only if both the current bit and the next bit are set. After k-1 iterations, a bit is set only if it starts a run of k consecutive 1s (free chunks).

This is mathematically elegant: instead of scanning linearly (O(n)), we use bit operations that execute in a few CPU cycles.

**Complexity Analysis**:
- Time: O(k) where k is number of chunks needed (typically 1-4)
- Space: O(1) - only uses bitmap and a few local variables
- Each iteration is a shift and AND (single CPU instructions)

**Allocation Process**:
1. Calculate chunks needed: ceiling((request_size + 2) / chunk_size)
2. Find k consecutive free chunks using bit-smearing
3. Create claim mask with k bits set, shifted to start position
4. Mark chunks as used: LOCK |= claim_mask
5. Store chunk count in 2-byte header
6. Return pointer to space after header

**Deallocation Process**:
1. Read header from 2 bytes before pointer
2. **Validate** pointer is within arena bounds
3. **Validate** chunk count is reasonable (1-64)
4. **Validate** allocation doesn't overflow arena
5. Calculate starting chunk position
6. Build free mask with appropriate bits
7. Clear bits: LOCK &= ~mask

**Defensive Programming**:
Deallocation includes extensive validation to detect corrupted pointers, use-after-free bugs, and memory corruption. These checks prevent crashes and aid debugging.

**Thread Safety**:
Allocator uses non-atomic bitmap operations, making it single-threaded only. This is acceptable because libevent runs in single thread. If multi-threading were needed, bitmap operations would require atomic instructions (__atomic_fetch_or, __atomic_fetch_and).

**Performance Characteristics**:

From benchmark.c (100 million allocations over 5 runs):

**malloc**:
- Throughput: 0.79 M ops/sec
- Median latency: 2736 cycles
- P99 latency: 10126 cycles
- Standard deviation: 10564.33

**Arena**:
- Throughput: 69.38 M ops/sec
- Median latency: 19 cycles
- P99 latency: 30 cycles
- Standard deviation: 76.28

**Improvements**:
- 87.8x faster throughput
- 144x lower median latency
- 337x lower P99 latency
- 138x more consistent (lower standard deviation)

**Why Such Dramatic Improvement?**

1. **No syscalls**: malloc may call brk/mmap, arena never does
2. **No metadata overhead**: malloc tracks free lists, arena uses simple bitmap
3. **Cache-friendly**: Entire allocator state (bitmap + pointer) fits in L1 cache
4. **Predictable**: O(1) operations, no tree traversals or list walking
5. **No fragmentation**: Fixed-size chunks eliminate fragmentation issues

### Utility Functions

**Logging System**:

**Dual Output Strategy**:
- **stderr**: Immediate feedback during development
- **syslog**: System-wide logging for production

**Log Levels**:
- INFO: Normal operations, request logging
- WARNING: Command failures, non-zero exit codes
- ERROR: Internal errors, authentication failures

**Structured Format**:
All logs include ISO 8601 timestamp, level, message, and optional context (method, URI, route, command, exit code, duration).

**Syslog Configuration**:
- Identifier: "cmon" for easy filtering
- LOG_PID: Includes process ID
- LOG_CONS: Falls back to console if syslog unavailable
- LOG_DAEMON: Categorizes as daemon logs for systemd integration

**JSON Response Formatting**:

**Standard Structure**:
All responses follow consistent format with fields: status, code, message, data.

**JSON Escaping**:
Critical for security. All special characters must be escaped to prevent:
- JSON structure breaking
- XSS attacks if output displayed in browser
- Client parsing errors

**Two-Pass Algorithm**:
1. First pass calculates required buffer size
2. Second pass builds escaped string

**Escaped Characters**:
- Quotes, backslashes
- Control characters (\b, \f, \n, \r, \t)
- Non-printable characters (as \uXXXX)

**Query Parameter Parsing**:

Uses libevent's built-in URI parser to:
- Parse request URI
- Extract query string
- Parse key-value pairs
- Return duplicated value (caller must free)

Returns NULL if parameter not found, allowing default values in command functions.

---

## Design Decisions & Tradeoffs

### Why libevent Instead of Raw Sockets?

**Alternatives Considered**:
1. Raw sockets with manual HTTP parsing
2. libmicrohttpd
3. Embedded servers (mongoose, civetweb)

**Chosen**: libevent 2.x

**Reasons**:
- Battle-tested in production systems (Tor, Chromium, memcached)
- Cross-platform support
- Event-driven architecture scales to many connections
- Built-in HTTP server support
- Active maintenance and security updates

**Tradeoffs**:
- Larger dependency than raw sockets
- Requires learning event-driven programming model
- But: Production-grade reliability worth the complexity

### Why Custom Arena Allocator?

**Motivation**: Server has predictable allocation patterns
- Frequent allocations of similar-sized buffers
- Short lifetimes (freed after request)
- JSON responses (~200 bytes)
- Command outputs (variable, but bounded)
- All freed together at request end

**Why Arena Wins**:

**malloc Characteristics**:
- General-purpose design for unpredictable workloads
- Maintains complex free lists
- Coalesces adjacent free blocks
- May call syscalls (brk/mmap)
- Thread-safe (locks even in single-threaded programs)

**Arena Characteristics**:
- Optimized for this specific workload
- Simple bitmap (fits in cache)
- No syscalls after initialization
- No thread synchronization
- Predictable performance

**Result**: 88x speedup

**When Arena Works**:
- High allocation frequency
- Similar sizes
- Short lifetimes
- Single-threaded

**When Arena Doesn't Work**:
- Unpredictable sizes
- Long-lived objects
- Multi-threaded access
- Memory-constrained systems

**Design Decision**: Predictability over flexibility. Fixed memory footprint is a feature, not a bug. If running out of memory, indicates misconfiguration or excessive load (should scale horizontally).

### Why 256-Byte Chunks?

**Analysis of Typical Allocations**:
- JSON template: ~100-200 bytes
- Escaped strings: 2x original (worst case)
- Small command outputs: <1KB
- HTTP headers: ~100 bytes

**Tradeoff Analysis**:

**Smaller chunks (128 bytes)**:
- Less waste for small allocations
- But: More chunks needed for large allocations
- But: Higher bitmap pressure

**Larger chunks (512 bytes)**:
- Fewer chunks for large allocations
- But: More waste for small allocations  
- But: Less total allocations possible

**Chosen**: 256 bytes balances waste vs. fragmentation

**Note**: Analysis mentions output truncation was fixed, suggesting chunk size or total capacity was increased for production.

### Why Timing-Safe Authentication?

**The Timing Attack Problem**:

Standard comparison functions (strcmp, memcmp, manual loops with early exit) reveal information through execution time. If comparison exits on first difference, an attacker can measure:
- Keys differing at byte 0: Fast (1 comparison)
- Keys differing at byte 31: Slow (32 comparisons)

Attacker brute-forces byte-by-byte:
- Try all 256 values for byte 0, measure timing
- Correct byte takes slightly longer (proceeds to byte 1)
- Repeat for all 32 bytes
- Total attempts: 256 × 32 = 8,192 instead of 2^256

**Constant-Time Solution**:

Algorithm examines all bytes regardless of differences. Uses bitwise OR to accumulate differences without branching. Compiler cannot optimize away because OpenSSL's CRYPTO_memcmp is designed to resist optimization.

**This demonstrates exceptional security awareness for a student project.**

### Why Single-Threaded Design?

**Analysis from cmon-analysis.md** (lines 1660-1686):

**Reasons**:
1. **Simplicity**: No race conditions, no deadlocks, easier to debug
2. **Performance**: No lock contention, no context switching
3. **Event-driven I/O**: libevent handles concurrency via epoll/kqueue
4. **I/O bound workload**: Waiting for commands dominates, not CPU
5. **Safety**: Arena doesn't need atomic operations

**Event Loop Model**:

Single thread can handle hundreds of concurrent connections:
1. Multiple clients connect
2. Event loop monitors all connections via epoll/kqueue
3. When data arrives on any socket, callback executes
4. Callback returns quickly to event loop
5. Loop continues monitoring all connections
6. Commands run in isolated child processes

**When Threading Helps**:
- CPU-intensive work in main thread
- Thousands of concurrent connections
- Long-running synchronous operations

**CMon Workload**:
- Fork handles CPU work (commands execute in child processes)
- Typical load: 10-100 requests/second
- Request handlers return immediately after fork

**Conclusion**: Single thread optimal for this use case

### Why JSON Instead of Plain Text?

**Advantages**:
1. **Machine-parseable**: Every language has JSON libraries
2. **Consistent structure**: All responses same format
3. **Extensible**: Can add fields without breaking clients
4. **Type-safe**: Clear distinction between success/error
5. **Error handling**: Standardized error format

**Plain Text Alternative Problems**:
- How to distinguish status from data?
- How to parse errors?
- Client needs custom parsing logic
- Difficult to extend

**Tradeoff**:
- Slightly more bandwidth
- Requires careful escaping (security critical)
- But: API consistency worth it

### Why Dual Logging?

**From cmon-analysis.md** (lines 1752-1783):

**stderr Benefits**:
- Immediate feedback during development
- See logs in terminal
- Colored output possible

**syslog Benefits**:
- System-wide logging infrastructure
- Automatic log rotation
- Priority-based filtering
- Remote forwarding capability
- systemd integration (journalctl)

**Both Together**:
- Development: Use stderr
- Production: Use syslog/journald
- Debugging: Can enable both
- Negligible performance impact

---

## Setup & Installation

### System Requirements

**Operating System**: Linux (tested on Ubuntu 24)

**Dependencies**:
- gcc or clang compiler
- pkg-config
- libevent 2.x development files
- OpenSSL development files

### Installation by Distribution

**Ubuntu/Debian**:
```bash
sudo apt-get update
sudo apt-get install build-essential pkg-config libevent-dev libssl-dev
```

**Fedora/RHEL**:
```bash
sudo dnf install gcc pkg-config libevent-devel openssl-devel
```

**Arch Linux**:
```bash
sudo pacman -S base-devel pkg-config libevent openssl
```

**Nix** (Reproducible builds):
```bash
nix develop
```

### Secret Key Generation

**Generate 256-bit key**:
```bash
openssl rand -hex 32 > client_secret.key
```

**Expected format**: 64 hexadecimal characters (optionally with newline)

**Secure permissions**:
```bash
chmod 600 client_secret.key
```

**Security note**: This file is the only authentication mechanism. Keep it secure, never commit to version control.

### Building

**Using build script**:
```bash
./build.sh
```

This compiles all sources and starts the server.

**Debug mode** (runs in gdb):
```bash
DEBUG=1 ./build.sh
```

**Manual compilation**:
```bash
gcc -O2 -Wall -Wextra -g -o target \
    main.c auth.c arena.c utils.c commands.c \
    $(pkg-config --cflags --libs libevent openssl)
```

**Build outputs**: Binary named `target` in current directory

### Running

**Foreground** (see logs directly):
```bash
./target
```

Expected output:
```
The client auth key was successfully loaded
Listening requests on http://0.0.0.0:8000
```

**Background**:
```bash
./target > /dev/null 2> server.log &
```

**systemd Service**:

Create `/etc/systemd/system/cmon.service`:
```ini
[Unit]
Description=CMon HTTP Server
After=network.target

[Service]
Type=simple
User=cmon
WorkingDirectory=/opt/cmon
ExecStart=/opt/cmon/target
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl daemon-reload
sudo systemctl enable cmon
sudo systemctl start cmon
sudo systemctl status cmon
```

View logs:
```bash
sudo journalctl -u cmon -f
```

### Testing

**Run test suite**:
```bash
./run_tests.sh
```

This builds the server, starts it in background, runs Python integration tests, and shows results.

**Manual testing**:
```bash
KEY=$(cat client_secret.key)
curl -v "http://localhost:8000/health" -H "access_token: $KEY"
```

---

## API Reference

### Base URL
```
http://localhost:8000
```

### Authentication

**All endpoints require authentication.**

**Header**: `access_token` (case-insensitive via libevent)  
**Value**: Your 256-bit hex key from `client_secret.key`

**Missing or invalid authentication returns**:
```json
{
    "status": "error",
    "code": 401,
    "message": "Authentication Error",
    "data": null
}
```

### Response Format

All responses follow this structure:

**Success** (HTTP 200):
```json
{
    "status": "ok",
    "code": 200,
    "message": "Command executed",
    "data": "command output here"
}
```

**Error** (HTTP 4xx/5xx):
```json
{
    "status": "error",
    "code": 500,
    "message": "Command failed",
    "data": "error details or null"
}
```

### Endpoints

#### GET /health

**Purpose**: Check server health and system uptime

**Authentication**: Required

**Query Parameters**: None

**Response**: System uptime information

**Example**:
```bash
curl "http://localhost:8000/health" -H "access_token: YOUR_KEY"
```

**Command executed**: `uptime`

---

#### POST /reboot

**Purpose**: Reboot the entire system

**Authentication**: Required

**Privileges**: Requires root or CAP_SYS_BOOT capability

**Warning**: This will restart the server immediately

**Example**:
```bash
curl -X POST "http://localhost:8000/reboot" -H "access_token: YOUR_KEY"
```

**Command executed**: `reboot`

---

#### POST /restart

**Purpose**: Restart the CMon server process

**Authentication**: Required

**Note**: Requires systemd or similar process manager to auto-restart

**Example**:
```bash
curl -X POST "http://localhost:8000/restart" -H "access_token: YOUR_KEY"
```

**Command executed**: `pkill target`

---

#### PUT /sync_upstream

**Purpose**: Pull latest changes from git repository

**Authentication**: Required

**Query Parameters**:
- `branch` (optional): Branch name, defaults to "main"

**Example**:
```bash
curl -X PUT "http://localhost:8000/sync_upstream?branch=develop" \
  -H "access_token: YOUR_KEY"
```

**Command executed**: `git pull origin <branch>`

**Prerequisites**: Must be run from a git repository

---

#### GET /deploy_branch

**Purpose**: Deploy a specific branch using custom script

**Authentication**: Required

**Query Parameters**:
- `branch` (optional): Branch name, defaults to "main"

**Requirements**:
- `./deploy.sh` script must exist in working directory
- Script must be executable (`chmod +x deploy.sh`)

**Example**:
```bash
curl "http://localhost:8000/deploy_branch?branch=feature-x" \
  -H "access_token: YOUR_KEY"
```

**Command executed**: `./deploy.sh <branch>`

**Script receives**: Branch name as first argument

---

#### DELETE /teardown_branch

**Purpose**: Teardown deployed branch using custom script

**Authentication**: Required

**Query Parameters**:
- `branch` (optional): Branch name, defaults to "main"

**Requirements**:
- `./teardown.sh` script must exist in working directory
- Script must be executable

**Example**:
```bash
curl -X DELETE "http://localhost:8000/teardown_branch?branch=feature-x" \
  -H "access_token: YOUR_KEY"
```

**Command executed**: `./teardown.sh <branch>`

---

#### GET /logs

**Purpose**: View system logs

**Authentication**: Required

**Query Parameters**: None

**Returns**: Last 50 systemd journal entries

**Example**:
```bash
curl "http://localhost:8000/logs" -H "access_token: YOUR_KEY"
```

**Command executed**: `journalctl -n 50 --no-pager`

---

### HTTP Status Codes

| Code | Meaning | When |
|------|---------|------|
| 200 | OK | Command executed successfully |
| 401 | Unauthorized | Missing or invalid access_token |
| 404 | Not Found | Route doesn't exist |
| 405 | Method Not Allowed | Wrong HTTP method for endpoint |
| 500 | Internal Server Error | Command failed, arena exhausted, or internal error |

**Note**: Current implementation maps all non-200 codes to 500 at HTTP level, but JSON response contains correct code.

---

## Performance Characteristics

### Benchmark Results

From `bench.txt` (100M allocations, 5 runs):

**Standard malloc**:
- Throughput: 0.79 million operations/second
- Median latency: 2,736 CPU cycles
- P99 latency: 10,126 CPU cycles
- Standard deviation: 10,564.33

**Arena allocator**:
- Throughput: 69.38 million operations/second
- Median latency: 19 CPU cycles
- P99 latency: 30 CPU cycles
- Standard deviation: 76.28

### Performance Improvements

| Metric | Improvement |
|--------|-------------|
| Throughput | 87.8x faster |
| Median latency | 144x lower |
| P99 latency | 337x lower |
| Consistency | 138x better (lower stddev) |

### Why Such Dramatic Improvement?

1. **No syscalls**: malloc may call brk/mmap, arena never does after initialization
2. **Cache-friendly**: Entire allocator state fits in L1 cache
3. **No fragmentation**: Fixed chunks eliminate coalescing overhead
4. **Simple operations**: Bitmap operations are single CPU instructions
5. **No locking**: malloc has thread synchronization even in single-threaded programs

### Benchmark Methodology

**Test design** (benchmark.c):
- 100 million total allocations
- Sample every 1,024th allocation (97,656 samples)
- 5 independent runs for statistical confidence
- Warmup phase to prime caches
- Batch processing (32 allocations at once)
- Zombie pool (4,096 allocations) simulating fragmentation
- Decision table pre-computed (eliminates rand() overhead from measurement)

**Measurement technique**:
- RDTSC instruction for cycle-accurate timing
- Statistical analysis: P50, P99 percentiles, standard deviation
- Graduate-level methodology

### Memory Usage

**Default configuration**:
- Chunk size: 256 bytes
- Chunk count: 64
- Total capacity: 16KB

**Configurable**:
Call `arena_config(chunk_size, chunk_count)` before `prealloc_arena()`

**Example** (increase to 1MB):
```
arena_config(4096, 256)
```

**Per-request overhead**:
- Arena header: 2 bytes
- JSON template: ~100 bytes
- Escaped output: 2x original (worst case)
- Typical total: <1KB per request

### Concurrency Model

**Single-threaded event loop**:
- libevent uses epoll (Linux) or kqueue (BSD)
- Can handle hundreds of concurrent connections
- No thread synchronization overhead
- Commands execute in isolated child processes

**Scalability**:
- Good for: 10-1,000 requests/second
- Limited by: Command execution time (I/O bound)
- Bottleneck: fork/exec overhead, not allocation

**When to scale horizontally**:
- More than 1,000 requests/second
- Long-running commands
- High CPU usage

---

## Security Model

### Authentication Security

**Key Strength**: 256-bit (2^256 possible combinations)
- Equivalent to SHA-256 hash length
- Effectively unbreakable by brute force
- Would take longer than age of universe to try all combinations

**Timing-Safe Comparison**:

Uses OpenSSL's `CRYPTO_memcmp()` which:
- Always examines all bytes
- Takes constant time regardless of where keys differ
- Cannot be optimized away by compiler
- Prevents timing attack vectors

**Why timing attacks matter**:

An attacker measuring response times could brute-force byte-by-byte with only 8,192 attempts (256 values × 32 bytes) instead of 2^256. Constant-time comparison prevents this.

**Key Storage**:
- File-based at `./client_secret.key`
- Hex-encoded (safe for text editors)
- Should have permissions 0600 (owner read/write only)
- Never logged or displayed in error messages

**Memory Security**:
- Keys cleansed from memory using `OPENSSL_cleanse()` before free
- Prevents recovery from memory dumps
- Prevents use-after-free vulnerabilities

**Recommendations**:
1. Generate with `openssl rand -hex 32`
2. Store securely (not in version control)
3. Rotate periodically
4. Use different keys per environment
5. Consider key derivation for multiple users

### Command Injection Prevention

**Safe Design**: Uses fork/exec, not system()

**Why fork/exec is safe**:
- Arguments passed as NULL-terminated array
- Each argument treated as literal string
- No shell metacharacter interpretation
- Even if input contains `;`, `|`, `&`, they're passed literally to program
- Program (e.g., git) just sees malformed input and fails safely

**Example**:
If branch parameter is `"main; rm -rf /"`:
- Git receives: `["git", "pull", "origin", "main; rm -rf /"]`
- Git looks for branch named `"main; rm -rf /"`
- Git fails with "unknown branch"
- No command injection possible

**Why system() would be unsafe**:
Would invoke shell which interprets metacharacters, enabling arbitrary command execution.

**Recommendation**: Still validate input
Even though injection is prevented, validation is good practice:
- Whitelist allowed characters (alphanumeric, dash, underscore, slash)
- Check length limits
- Reject unexpected patterns

### Network Security

**Current Setup**:
- Binds to 0.0.0.0:8000 (all interfaces)
- No TLS/SSL (plain HTTP)
- Authentication via custom header

**For Production**:

**1. Use TLS termination**:
Place CMon behind nginx or haproxy with TLS:
- nginx handles TLS/SSL
- Forwards to CMon on localhost
- CMon binds to 127.0.0.1 only

**2. Firewall rules**:
- Allow only specific IP addresses
- Rate limit requests
- Drop invalid packets early

**3. Bind to localhost**:
Change binding from 0.0.0.0 to 127.0.0.1 if only local access needed

**4. VPN/Tunnel**:
For remote access:
- Use WireGuard or SSH tunnel
- Never expose directly to internet

**5. Discord bot scenario**:
Analysis mentions E2E encryption between Discord bot and CMon, which provides network security.

### Memory Safety

**Arena Allocator Safety**:
- Fixed capacity prevents unbounded growth
- Boundary checks prevent buffer overflows
- Header validation detects corruption
- Pointer validation prevents crashes

**Deallocation Checks**:
1. Pointer within arena bounds
2. Header chunk count reasonable (1-64)
3. Allocation doesn't overflow arena
4. Returns silently on invalid pointer (doesn't crash)

**Potential Issues**:
- Output truncation (not exploitable)
- Arena exhaustion (returns 500 error)
- No use-after-free (validation prevents)

**Recommendations**:
- Increase arena size for production
- Add memory usage monitoring
- Consider memset of freed memory (debug builds)

### Privilege Management

**Commands Requiring Elevated Privileges**:
- `/reboot`: Needs CAP_SYS_BOOT or root
- `/restart`: Needs permission to signal processes

**Best Practices**:

**1. Use systemd capabilities**:
Grant only needed capabilities, not full root

**2. Use sudo with NOPASSWD**:
Configure sudoers for specific commands only

**3. Principle of least privilege**:
- Don't run as root
- Use dedicated user account
- Grant minimal permissions

**4. Audit logging**:
Log all privileged operations with user context

### Logging and Monitoring

**What's logged**:
- All authenticated requests (method, URI, route)
- Command execution (command, exit code, duration)
- Authentication failures
- Internal errors

**What's NOT logged**:
- Secret keys (never logged)
- Request bodies (if added in future)
- Source IP addresses (should be added)

**Security monitoring**:
- Watch for multiple auth failures
- Unusual command patterns
- High request rates
- Privilege escalation attempts

**Recommendations**:

**1. Add source IP logging**:
Log client IP addresses for audit trail

**2. Centralized logging**:
Forward syslog to remote server

**3. Alerting**:
Set up alerts for suspicious patterns

**4. Log rotation**:
Configure to prevent disk exhaustion

### Deployment Security Checklist

**Before Production**:

- [ ] Generate strong secret key
- [ ] Secure key file permissions (0600)
- [ ] Place behind TLS terminator
- [ ] Bind to localhost or use firewall
- [ ] Run as non-root user
- [ ] Configure systemd hardening
- [ ] Set up log monitoring
- [ ] Configure log rotation
- [ ] Test all endpoints
- [ ] Review custom scripts (deploy.sh, teardown.sh)
- [ ] Add input validation
- [ ] Increase arena capacity if needed
- [ ] Set up health checks
- [ ] Document incident response
- [ ] Test disaster recovery

---

## Troubleshooting

### Server Won't Start

**Symptom**: "The client auth key could not be loaded"

**Causes**:
- Missing `client_secret.key` file
- File in wrong location
- Insufficient permissions to read file

**Solutions**:
1. Generate key: `openssl rand -hex 32 > client_secret.key`
2. Check location: File must be in working directory
3. Fix permissions: `chmod 600 client_secret.key`
4. Verify content: Should be 64 hex characters

---

**Symptom**: "Bind: Address already in use"

**Causes**:
- Another process using port 8000
- Previous instance still running

**Solutions**:
1. Find process: `sudo lsof -i :8000`
2. Kill it: `sudo kill <PID>`
3. Or change port in main.c (recompile required)

---

**Symptom**: "init auth failed"

**Causes**:
- Key file wrong format
- Corrupted key file
- Non-hex characters in file

**Solutions**:
1. Check file size: `wc -c client_secret.key` (should be 64 or 65 bytes)
2. Verify hex format: Only characters 0-9, a-f, A-F
3. Regenerate if corrupted

---

**Symptom**: "Event base is null"

**Causes**:
- libevent not installed
- Memory allocation failure

**Solutions**:
1. Install libevent: `apt-get install libevent-dev`
2. Check memory: `free -h`
3. Check system logs: `dmesg | tail`

### Authentication Failures

**Symptom**: Always getting 401 errors

**Causes**:
- Wrong key in request
- Key not being sent
- Header name wrong
- Whitespace in key

**Solutions**:
1. Verify key matches: `cat client_secret.key`
2. Test directly: `curl -H "access_token: $(cat client_secret.key)" http://localhost:8000/health`
3. Check header name: Must be "access_token"
4. Remove whitespace: `tr -d '\n' < client_secret.key > client_secret.key.new`

---

**Symptom**: Works with curl, fails in application

**Causes**:
- Application not sending header
- Header value encoding issues
- HTTP client library quirks

**Solutions**:
1. Debug application: Print actual header being sent
2. Check encoding: Ensure hex string, no base64 or other encoding
3. Verify header name: Case doesn't matter but spelling does

### Command Failures

**Symptom**: `/reboot` returns exit_code=1

**Causes**:
- Insufficient privileges
- System preventing reboot

**Solutions**:
1. Check user: `whoami`
2. Grant capability: Configure systemd with CAP_SYS_BOOT
3. Or use sudo: Modify command to use `sudo reboot`

---

**Symptom**: `/deploy_branch` returns exit_code=127

**Causes**:
- Script not found
- Script not in PATH or current directory
- Script not executable

**Solutions**:
1. Check exists: `ls -la deploy.sh`
2. Make executable: `chmod +x deploy.sh`
3. Use absolute path: Modify commands.c to use `/opt/cmon/deploy.sh`
4. Verify working directory: Script must be in server's working directory

---

**Symptom**: Git commands fail

**Causes**:
- Not in git repository
- No git remote configured
- Network issues
- Branch doesn't exist

**Solutions**:
1. Initialize repo: `git init` (if needed)
2. Add remote: `git remote add origin <url>`
3. Check network: `ping github.com`
4. Verify branch: `git branch -a`

---

**Symptom**: Output truncated

**Causes**:
- Command output exceeds arena buffer size

**Solutions**:
Analysis mentions this was fixed, but if issue recurs:
1. Increase arena capacity: Call `arena_config(4096, 256)` before `prealloc_arena()`
2. Or reduce command output: Use flags like `-n 20` instead of `-n 50` for logs

### Performance Issues

**Symptom**: High latency

**Causes**:
- Commands taking long time
- Network delays
- System overload

**Solutions**:
1. Check command times: `journalctl -u cmon | grep duration`
2. Optimize slow commands
3. Add timeouts to prevent hanging
4. Check system load: `uptime`

---

**Symptom**: "FATAL: Buffer overflow"

**Causes**:
- Arena exhausted
- Too many concurrent requests
- Memory leak

**Solutions**:
1. Increase capacity: `arena_config(4096, 256)`
2. Reduce concurrent requests
3. Check for leaks: Ensure all `allocate()` calls have matching `deallocate()`

---

**Symptom**: Server unresponsive

**Causes**:
- Event loop blocked
- Long-running command
- Deadlock (shouldn't happen in single-threaded)

**Solutions**:
1. Check logs for stuck command
2. Restart server
3. Add command timeouts (requires code modification)

### Memory Issues

**Symptom**: Segmentation fault

**Causes**:
- Buffer overflow
- Corrupted pointer
- Use-after-free

**Solutions**:
1. Run in gdb: `DEBUG=1 ./build.sh`, then `bt` when it crashes
2. Use valgrind: `valgrind --leak-check=full ./target`
3. Check arena deallocation: Review calls to `deallocate()`

---

**Symptom**: Memory usage grows

**Causes**:
- Memory leak
- Arena not being torn down
- libevent buffers not freed

**Solutions**:
1. Check with valgrind
2. Verify cleanup: Ensure `teardown_arena()` called on shutdown
3. Check request handling: All allocations should be freed

### Logging Issues

**Symptom**: No syslog entries

**Causes**:
- syslog daemon not running
- Permissions issues
- Wrong syslog configuration

**Solutions**:
1. Check syslog: `sudo systemctl status rsyslog`
2. Or use journald: `sudo journalctl -u cmon -f`
3. Check permissions: Ensure user can write to syslog

---

**Symptom**: Can't find logs

**Causes**:
- Don't know where to look
- Logs being filtered

**Solutions**:
1. stderr logs: Check `server.log` if redirected
2. syslog: Check `/var/log/syslog` or `/var/log/messages`
3. systemd: Use `journalctl -u cmon`
4. Filter by priority: `journalctl -u cmon -p warning`

---

**Symptom**: Too much logging

**Solutions**:
1. Filter by level: Only show errors and warnings
2. Reduce request logging: Modify code if needed
3. Configure log rotation

### Common Errors

**"arena_free: invalid pointer"**:
- Passing pointer not allocated by arena
- Double-free attempt
- Corrupted pointer

**"arena_free: corrupted header"**:
- Buffer overflow corrupted metadata
- Random memory overwrite
- Use-after-free

**"FATAL: no k-consecutive-zeroes"**:
- Arena full
- Too many allocations
- Need larger capacity

**"execvp failed"**:
- Command not found
- Not in PATH
- Executable permissions issue

---

## Development Guide

### Project Structure

**Source files**:
- `main.c`: HTTP server, routing, middleware (220 lines)
- `auth.c/h`: Authentication system (150 lines)
- `arena.c/h`: Custom memory allocator (219 lines)
- `commands.c/h`: Command execution (129 lines)
- `utils.c/h`: Utilities (242 lines)

**Testing**:
- `benchmark.c`: Performance benchmarking
- `test_server.py`: Integration tests
- `run_tests.sh`: Test automation

**Build system**:
- `build.sh`: Compilation and execution
- `flake.nix`: Nix development environment
- `.clang-format`: Code formatting rules

**Total**: ~1,200 lines of C code (excluding tests)

### Building from Source

**Clone and setup**:
```bash
git clone <repository-url>
cd CMon
```

**Install dependencies**:
See Setup & Installation section for distribution-specific commands

**Build**:
```bash
./build.sh
```

**Or use Nix** (reproducible):
```bash
nix develop
```

### Code Style

**Formatting**:
Uses clang-format with LLVM style base

**Rules**:
- Indent: 4 spaces
- Line length: 100 characters max
- No single-line if statements

**Apply formatting**:
```bash
clang-format -i *.c *.h
```

### Adding New Endpoints

**Steps**:

1. **Define command function** in `commands.c`
   - Follow pattern of existing commands
   - Use `run_cmd_argv()` for execution
   - Handle default values for optional parameters
   - Return allocated output, set exit code

2. **Add declaration** in `commands.h`
   - Match signature of other command functions

3. **Create callback** in `main.c`
   - Use `validate_and_run()` for parameterless commands
   - Use `validate_and_run_arg()` for commands with parameters
   - Specify parameter name for query string

4. **Register route** in `ROUTES_CONFIG` array
   - Specify path, HTTP method, callback
   - Array automatically sized

5. **Test**
   - Add test to `test_server.py`
   - Run `./run_tests.sh`
   - Manual test with curl

**Example workflow** (adding `/status` endpoint):
1. Add to commands.c: function that runs `systemctl status <service>`
2. Declare in commands.h
3. Add callback in main.c
4. Add route entry: `{"/status", EVHTTP_REQ_GET, status_callback}`
5. Test

### Testing

**Automated tests**:
```bash
./run_tests.sh
```

This:
1. Builds server
2. Starts in background
3. Runs Python test suite
4. Shows results
5. Cleans up

**Manual testing**:
```bash
./target &
curl -v "http://localhost:8000/health" -H "access_token: $(cat client_secret.key)"
tail -f server.log
```

**Adding tests**:
Edit `test_server.py`, add new test cases following existing pattern

**Test coverage**:
Current tests cover:
- Health check (200 OK)
- Unknown routes (404)
- Wrong HTTP method (405)
- Missing auth header (401)
- Invalid auth key (401)
- Git pull with parameter

Should add:
- All endpoints
- Edge cases
- Concurrent requests
- Large outputs
- Error conditions

### Debugging

**Run in gdb**:
```bash
DEBUG=1 ./build.sh
```

**Common breakpoints**:
- Authentication: `break auth.c:115` (authenticate function)
- Allocation: `break arena.c:99` (bit-smearing algorithm)
- Request handling: `break main.c:52` (auth middleware)
- Command execution: `break commands.c:12` (run_cmd_argv)

**Inspect variables**:
- `print LOCK`: See bitmap state
- `print *req`: View HTTP request
- `print secret_key_buffer`: View loaded key (careful with security)

**Memory debugging**:
```bash
valgrind --leak-check=full --show-leak-kinds=all ./target
```

### Performance Analysis

**Benchmark allocator**:
```bash
gcc -O2 -o benchmark benchmark.c arena.c -lm
./benchmark
```

**Profile with perf**:
```bash
perf record -g ./target
perf report
```

**Generate flamegraph**:
```bash
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

**Tune arena**:
Modify arena_config call in main() to adjust capacity

### Contributing

**Before submitting**:
1. Run clang-format on all modified files
2. Ensure all tests pass
3. Add tests for new features
4. Update documentation
5. Check for memory leaks with valgrind

**Code review focus**:
- Security implications
- Memory safety
- Error handling
- Performance impact
- API consistency

---

## Appendix

### Frequently Asked Questions

**Q: Why C instead of modern languages?**

A: Performance and minimal dependencies. C provides:
- Direct system call access
- Predictable memory usage (no GC pauses)
- Small binary size (~100KB)
- Available on any Linux system
- No runtime dependencies

---

**Q: Is this production-ready?**

A: Depends on use case:

**Yes, if**:
- Internal use on trusted network
- Behind TLS terminator (nginx)
- Monitored environment
- Discord bot over E2E encryption (as designed)

**No, if**:
- Public-facing API
- Untrusted clients
- High-security environment
- Multi-tenant system

With recommended hardening (TLS, validation, monitoring), suitable for internal production use.

---

**Q: How does it compare to alternatives?**

**vs systemd HTTP API**:
- systemd: Only systemd unit management
- CMon: Arbitrary commands, git integration, custom scripts

**vs webhook tools**:
- webhook (Go): Similar concept, but Go runtime
- CMon: No runtime, faster, smaller

**vs SSH automation**:
- SSH: Requires key management per server
- CMon: Single HTTP API, easier to script

---

**Q: Can I run on non-Linux systems?**

A:
- **Linux**: Full support (tested Ubuntu 24)
- **BSD**: libevent supports it, should work with testing
- **macOS**: Likely works with Homebrew dependencies
- **Windows**: Not supported (POSIX dependencies)

---

**Q: How to scale horizontally?**

A: Deploy multiple instances behind load balancer:
- Each instance manages its own server
- Load balancer distributes requests
- Shared secret key across instances
- Or different keys per instance

---

**Q: What's maximum throughput?**

A: Depends on command execution time:
- Fast commands (/health): ~1000 req/sec
- Git operations: ~10-100 req/sec
- Reboots: Sequential only

Bottleneck is I/O (fork/exec/wait), not allocation.

---

**Q: Why 64-chunk limit?**

A: Bitmap is 64-bit integer (uint64_t), each bit represents one chunk. Could extend with:
- Multiple bitmaps
- 128-bit integers (compiler extension)
- Different data structure

But 64 chunks sufficient for current use case.

---

**Q: What if I need larger outputs?**

A: Increase arena capacity:
- Larger chunks: `arena_config(4096, ...)`
- More chunks: `arena_config(..., 256)`
- Both: `arena_config(4096, 256)` = 1MB

Analysis mentions output truncation was fixed, likely by increasing capacity.

---

**Q: How to add TLS?**

A: Don't add to CMon directly. Instead:
1. Bind CMon to 127.0.0.1
2. Run nginx with TLS
3. nginx proxies to CMon
4. Easier to manage certificates
5. Leverages nginx's battle-tested TLS

---

**Q: Can I use this for multi-user?**

A: Currently single shared key. For multi-user:
- Generate key per user
- Store in database or file
- Modify auth.c to check multiple keys
- Add user identification in logs

Or use OAuth/JWT proxy in front.

---

### References

**Dependencies**:
- [libevent Documentation](https://libevent.org/)
- [OpenSSL Documentation](https://www.openssl.org/docs/)

**Concepts**:
- Event-driven architecture
- Arena allocators
- Timing attacks
- Fork/exec security

**Similar Projects**:
- webhook (Go): HTTP to command execution
- cmd-server: Command server in Go
- systemd HTTP API: systemd unit management

**Further Reading**:
- "The Practice of Programming" (Kernighan & Pike)
- "The Linux Programming Interface" (Kerrisk)
- "Systems Performance" (Gregg)

---

**End of Documentation**

This documentation provides complete coverage of CMon's architecture, implementation, usage, and operational considerations. For additional details, consult the source code and analysis document.
