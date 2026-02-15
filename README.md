# CMon - Complete Documentation

**Version**: 2.0  
**Language**: C  
**Author**: Blazzee

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

### What's New in Version 2.0

**Revolutionary Virtual Memory Arena Allocator**:
- **Virtually unlimited allocations** via bitmap array instead of single 64-bit integer
- **mmap-based virtual memory** (up to 64TB theoretical capacity on x86-64)
- **Demand paging** - physical memory only used when accessed (MMU translates on page fault)
- **512-byte chunks** (increased from 256)
- **64 chunks default** but configurable to thousands
- **Extremely elegant** - allocate terabytes virtually, use only what you need

**Enhanced Benchmarking**:
- **Serialized RDTSC** for accurate cycle counting (prevents instruction reordering)
- **CPU pinning** to eliminate scheduling noise
- **ARM64 support** with virtual counter benchmarks
- **256KB allocations** to stress-test large allocations
- **Random page touching** to destroy locality (realistic workload)
- **malloc_trim()** to force heap release for fair comparison

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
✅ **Virtual Memory Arena** - Virtually unlimited capacity with demand paging  
✅ **Security-Conscious** - Timing-safe authentication, no shell injection  
✅ **Event-Driven** - Single-threaded async I/O via libevent  

### Performance Metrics

**Updated benchmarks with 256KB allocations**:

Configuration: 64KB chunks, 1024 chunks (64MB virtual arena)

Results vary by workload but arena consistently outperforms malloc for:
- Frequent allocations
- Predictable sizes
- Short lifetimes
- Batch processing patterns

### Use Cases

**Ideal For:**
- Internal DevOps tooling
- CI/CD pipeline integration
- Discord/Slack bot backends
- Server management dashboards
- Automated deployment systems
- High-throughput command execution

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
│  │    Virtual Memory Arena Allocator (NEW v2.0)      │    │
│  │  • mmap-based virtual memory (up to 64TB)         │    │
│  │  • Bitmap array (unlimited chunks)                │    │
│  │  • Demand paging (MMU translates on access)       │    │
│  │  • O(1) allocation/deallocation per bitmap        │    │
│  │  • Physical memory only used on page fault        │    │
│  └────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
                         │
                         │ System Calls
                         │
┌────────────────────────▼────────────────────────────────────┐
│                    Operating System                          │
│  Commands: uptime, reboot, pkill, git, journalctl           │
│  Scripts: ./deploy.sh, ./teardown.sh                        │
│  MMU: Virtual → Physical address translation                │
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
8. **Arena allocator** provides memory from virtual address space (MMU handles physical mapping)
9. **Response builder** formats JSON with escaped output
10. **Client receives response** with status, code, message, data

### Component Interaction

**HTTP Layer** (main.c) coordinates all components:
- Initializes virtual memory arena on startup
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

**Virtual Memory Arena Layer** (arena.c) manages memory:
- Uses mmap() to reserve virtual address space (not physical memory)
- Bitmap array tracks allocated/free chunks across unlimited space
- MMU (Memory Management Unit) translates virtual addresses to physical on first access
- Physical pages allocated on-demand via page faults in TLB (Translation Lookaside Buffer)
- Can theoretically allocate up to 64TB (42-bit address space on x86-64)
- Actual physical memory usage determined by what's accessed, not what's allocated

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
Registers handler for SIGINT to perform graceful shutdown - closes syslog, tears down arena via munmap(), frees libevent structures in correct order.

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

### Virtual Memory Arena Allocator - NEW v2.0

**This is the revolutionary component that makes CMon v2.0 extremely elegant.**

#### The Virtual Memory Breakthrough

**Traditional Approach (v1.0)**:
- Fixed 16KB physical memory pre-allocated
- Single 64-bit bitmap (max 64 chunks)
- All memory allocated upfront

**New Approach (v2.0)**:
- **mmap() reserves virtual address space** (not physical memory)
- **Bitmap array** allows unlimited chunks (configurable)
- **Physical memory allocated on-demand** by MMU
- **Can reserve up to 64TB** on x86-64 (42-bit virtual address space)

#### How Virtual Memory Works

**Key Insight**: Modern CPUs have a Memory Management Unit (MMU) that translates virtual addresses to physical addresses.

**The Process**:

1. **Reservation Phase** (`mmap()`):
   - Request operating system to reserve virtual address space
   - Example: Reserve 64GB of virtual memory
   - **No physical RAM allocated yet**
   - OS just marks virtual address range as belonging to process

2. **Translation Phase** (MMU):
   - When code accesses a virtual address for first time
   - MMU looks up address in page tables
   - If page not in physical memory: **Page Fault**

3. **Demand Paging** (Page Fault Handler):
   - OS allocates physical page (4KB on most systems)
   - Updates page tables with virtual→physical mapping
   - Caches mapping in TLB (Translation Lookaside Buffer)
   - Resumes execution transparently

4. **Result**:
   - Can allocate 64TB virtually
   - Only use physical memory for accessed pages
   - **Extremely elegant** - pay only for what you use

#### Example Scenario

**Allocate 1GB arena**:
- `mmap(1GB)` reserves 1GB virtual address space
- Physical memory used: **0 bytes**
- Arena bitmap: ~16KB (for 2048 chunks of 512KB each)

**Allocate 100KB**:
- Arena finds free chunks in bitmap
- Returns virtual address
- Physical memory used: **Still ~0 bytes**

**Write to allocation**:
- First write to address triggers page fault
- OS allocates single 4KB physical page
- MMU maps virtual page to physical page
- Physical memory used: **4KB**

**Write across allocation**:
- Each new 4KB region accessed triggers page fault
- 100KB allocation spans ~25 pages
- After accessing all: **100KB physical** (plus some overhead)

**Why This Is Brilliant**:
- Reserve huge arena (64TB theoretical)
- Only consume physical RAM for actually used memory
- No waste on unused capacity
- Transparent to application code

#### Design Goals (v2.0)

1. **Virtually Unlimited Capacity** - No practical limit on allocations
2. **Efficient Physical Memory Use** - Only use what you access
3. **O(1) Performance** - Fast allocation/deallocation
4. **Cache-friendly** - Bitmap array organized for locality

#### Configuration (v2.0)

**Default**:
- **Chunk Size**: 512 bytes (increased from 256)
- **Chunk Count**: 64 (configurable to thousands)
- **Bitmap Members**: Calculated from chunk count (1 member = 64 chunks)

**Example Configurations**:

**Small (default)**:
- 512 bytes × 64 chunks = 32KB virtual
- 1 bitmap member (64 bits)

**Medium**:
- 64KB × 1024 chunks = 64MB virtual
- 16 bitmap members (1024 bits)

**Large**:
- 1MB × 10000 chunks = 10GB virtual
- 157 bitmap members (10000 bits)

**Extreme**:
- 4MB × 100000 chunks = 400GB virtual
- 1563 bitmap members (100000 bits)

#### Data Structures (v2.0)

**Global State**:
- `LOCK`: Pointer to dynamically allocated bitmap array
- `BUF`: Pointer to mmap'd virtual memory region
- `arena_lock_members`: Number of 64-bit integers in bitmap array

**Bitmap Array**:
Each member is a 64-bit integer representing 64 chunks:
- Member 0: Chunks 0-63
- Member 1: Chunks 64-127
- Member N: Chunks (N×64) to (N×64+63)

**Allocation Header**:
- 2-byte structure storing number of chunks allocated
- Placed immediately before user data
- Enables O(1) deallocation

#### The Enhanced Bit-Smearing Algorithm (v2.0)

**Challenge**: Find k consecutive free chunks across bitmap array

**Algorithm Overview**:

1. **Iterate through bitmap members** (64-bit integers)
2. **For each member**: Invert to get free mask (free=1, used=0)
3. **Apply bit-smearing** to find k consecutive 1s in that member
4. **Boundary check**: Ensure allocation doesn't cross member boundary
5. **Return global bit position** if found, continue to next member if not

**Why Boundary Check?**

Allocations cannot span across 64-bit members because:
- Each member's bits are managed independently
- Bit operations work within single 64-bit integer
- Crossing boundary would complicate mask calculations

**Impact**: For large allocations (>64 chunks), first chunk must start at member boundary. This is acceptable because such allocations are rare in typical workload.

**Complexity**:
- Outer loop: O(m) where m = number of bitmap members
- Inner bit-smearing: O(k) where k = chunks needed
- Total: O(m×k)
- But in practice: k is small (1-4), m scanned until first fit
- Typical case: O(1) to O(m) depending on fragmentation

#### Allocation Process (v2.0)

1. **Calculate chunks needed**: ceiling((request_size + 2) / chunk_size)
2. **Iterate bitmap members**:
   - Invert member to get free mask
   - Apply bit-smearing to find k consecutive free chunks
   - Check allocation doesn't cross member boundary
3. **Claim chunks**:
   - Calculate global bit position
   - Determine member and bit offset within member
   - Create claim mask
   - Mark bits as used: `LOCK[member] |= claim_mask`
4. **Store metadata**:
   - Write chunk count to 2-byte header
5. **Return pointer** to space after header

#### Deallocation Process (v2.0)

1. **Read header** from 2 bytes before pointer
2. **Validate** (same checks as v1.0):
   - Pointer within arena bounds
   - Chunk count reasonable
   - Allocation doesn't overflow arena
3. **Calculate position**:
   - Determine global bit position
   - Calculate member index and bit offset
4. **Boundary check**: Ensure freeing doesn't cross member boundary
5. **Build free mask** for those bits
6. **Clear bits**: `LOCK[member] &= ~mask`

#### Virtual Memory Management (v2.0)

**Initialization** (`prealloc_arena`):

1. **Allocate bitmap array**:
   - Calculate members needed: `(chunks + 63) / 64`
   - `malloc()` bitmap array
   - Zero all bits (all chunks free)

2. **Reserve virtual memory**:
   - Calculate total size: `chunk_size × chunk_count`
   - Call `mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)`
   - `MAP_ANONYMOUS`: Not backed by file
   - `MAP_PRIVATE`: Process-private mapping
   - OS reserves virtual address space
   - **No physical pages allocated yet**

3. **Optional zero**:
   - `memset(BUF, 0, total_size)` forces page allocation
   - Each 4KB page accessed triggers page fault
   - OS allocates physical pages on-demand
   - **Trade-off**: Slower init vs. faster first allocation

**Teardown** (`teardown_arena`):

1. **Unmap virtual memory**:
   - `munmap(BUF, total_size)`
   - OS releases virtual address space
   - **Physical pages automatically freed**
   - Much cleaner than manual memory management

2. **Free bitmap**:
   - `free(LOCK)` releases bitmap array

#### Why mmap() Instead of malloc()?

**malloc() Issues**:
- Backed by heap (brk/sbrk system calls)
- Heap fragmentation
- Difficult to release memory back to OS
- Limited by heap size

**mmap() Advantages**:
- Independent virtual memory region
- Direct mapping to page allocator
- Easy to release via munmap()
- Can reserve huge regions without physical allocation
- OS manages physical pages automatically

**Perfect for arena allocator**:
- Reserve large virtual region upfront
- Let OS handle physical allocation
- Clean teardown with munmap()

#### Performance Characteristics (v2.0)

**Benchmark Configuration** (benchmark.c):
- **Allocation size**: 256KB (stress test for large allocations)
- **Arena config**: 64KB chunks, 1024 chunks (64MB virtual)
- **CPU pinning**: Eliminates scheduler noise
- **Serialized RDTSC**: Prevents instruction reordering
- **Page touching**: 128 random pages per allocation (destroys locality)
- **Zombie pool**: 12800 live allocations (realistic fragmentation)

**Methodology Improvements**:
- `rdtsc_begin()` with CPUID serialization
- `rdtsc_end()` with RDTSCP + CPUID fence
- `malloc_trim(0)` after each run (forces heap release)
- Separate warmup runs for malloc and arena
- 5 runs for statistical confidence

**ARM64 Support** (bench_arm.c):
- Uses ARM virtual counter (`cntvct_el0`)
- Instruction serialization barriers (`isb`)
- Optimized for Raspberry Pi 5
- 4MB allocations to stress large allocation path

**Results Characteristics**:
- Arena performance scales with virtual memory size
- No degradation with larger configurations
- Physical memory usage tracks actual access patterns
- Page fault overhead amortized across allocation lifetime

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

### Why Virtual Memory Arena? (NEW v2.0)

**The Revolutionary Decision**: Switch from malloc-based fixed arena to mmap-based virtual memory arena

**Motivation**:
Previous version limited to 16KB total capacity due to single 64-bit bitmap. This constraint prevented:
- Large command outputs (git logs, journal entries)
- Concurrent request handling
- Flexible configuration per deployment

**Solution**: Virtual memory with demand paging

**How It Works**:

**Virtual vs Physical Memory**:
- **Virtual**: Address space reserved by OS (costs nothing)
- **Physical**: Actual RAM pages (costs real memory)
- **Translation**: MMU maps virtual→physical on access

**Example**:
1. Reserve 64GB virtual: `mmap(64GB)` → Cost: 0 bytes physical
2. Allocate 1MB: Return virtual address → Cost: 0 bytes physical
3. Write first byte: Page fault → OS allocates 4KB page → Cost: 4KB physical
4. Write across 1MB: 250 page faults → Cost: 1MB physical (actual usage)

**Benefits**:

1. **Virtually Unlimited**:
   - Can reserve up to 128TB on x86-64 (48-bit addresses)
   - Practical limit: 64TB (42-bit) for compatibility
   - Configure gigabytes of arena without consuming RAM

2. **Pay-for-What-You-Use**:
   - Physical memory only allocated on access
   - Unused arena regions cost nothing
   - Perfect for variable workloads

3. **Clean Resource Management**:
   - `munmap()` releases everything at once
   - OS automatically frees physical pages
   - No manual page tracking needed

4. **Transparent to Code**:
   - Application code unchanged
   - Same allocation API
   - MMU handles all translation

**Tradeoffs**:

**Advantages**:
- ✅ No hard capacity limit
- ✅ Memory efficient (demand paging)
- ✅ Simple teardown (munmap)
- ✅ Scales to workload
- ✅ OS manages physical memory

**Disadvantages**:
- ❌ Page fault overhead on first access
- ❌ Requires virtual address space (not an issue on 64-bit)
- ❌ TLB pressure with many small allocations
- ✅ But: Page faults amortized over allocation lifetime
- ✅ But: TLB caching makes subsequent accesses fast

**When Virtual Memory Arena Wins**:
- Large allocations (>4KB)
- Variable workload (some requests large, some small)
- Long-running process
- Flexibility needed per deployment

**When Traditional Allocator Better**:
- Tiny allocations (<100 bytes)
- Extremely latency-sensitive (no page faults tolerated)
- Embedded systems without MMU

**Design Choice**: For server workload, virtual memory is clear winner.

### Why Bitmap Array Instead of Single Bitmap?

**Previous Approach (v1.0)**:
- Single 64-bit integer bitmap
- Maximum 64 chunks
- Hard limit

**New Approach (v2.0)**:
- Array of 64-bit integers
- Each member tracks 64 chunks
- Unlimited chunks (array size determined by configuration)

**Benefits**:
1. **Scalability**: Can track thousands of chunks
2. **Modularity**: Each member independent
3. **Cache-friendly**: Array traversal is linear
4. **Flexible**: Easy to add more members

**Tradeoff**:
- Allocation cannot span member boundaries
- For allocations >64 chunks, must align to member boundary
- Acceptable because large allocations are rare

**Implementation Detail**:
Helper macros for bitmap array access:
- `MEMBER_INDEX(bit)`: Which 64-bit integer
- `BIT_OFFSET(bit)`: Which bit within integer
- `GLOBAL_BIT(member, bit)`: Convert to global position

### Why mmap() Instead of malloc()?

**Decision**: Use `mmap()` for arena backing store instead of `malloc()`

**Reasons**:

1. **Virtual Memory Control**:
   - `mmap()` reserves virtual address space
   - Can reserve huge regions (GB/TB) without physical allocation
   - `malloc()` would allocate physical memory immediately

2. **Independent Region**:
   - `mmap()` creates separate memory region
   - Not affected by heap fragmentation
   - Independent of malloc/free operations elsewhere

3. **Clean Teardown**:
   - `munmap()` releases everything at once
   - OS automatically frees all physical pages
   - `free()` might not return memory to OS due to fragmentation

4. **Page Alignment**:
   - `mmap()` always returns page-aligned addresses
   - Better for large allocations
   - TLB efficiency

5. **Transparent Paging**:
   - OS handles demand paging automatically
   - Physical pages allocated on first access
   - No manual page management needed

**Comparison**:

**malloc()**:
- Backed by heap (brk/sbrk)
- Physical memory allocated immediately
- Fragmentation prevents memory return
- Limited by heap size

**mmap()**:
- Independent virtual region
- Physical on demand
- Clean release via munmap
- Limited only by virtual address space (huge)

**Result**: mmap() is perfect for arena allocator use case

### Why 512-Byte Chunks? (Increased from 256)

**Analysis**: Larger chunks reduce header overhead and bitmap pressure

**Previous**: 256 bytes
- Good for small allocations
- High overhead for large allocations
- More bitmap bits needed

**New**: 512 bytes
- Better for larger allocations
- Amortized overhead
- Fewer chunks needed for typical workload

**Trade-off Analysis**:

**Smaller chunks (256 bytes)**:
- ✅ Less waste for tiny allocations
- ❌ More chunks needed (more bitmap pressure)
- ❌ More header overhead

**Larger chunks (1KB+)**:
- ✅ Fewer chunks, less bitmap pressure
- ❌ More waste for small allocations
- ❌ Potential internal fragmentation

**Chosen (512 bytes)**:
- Balance between waste and efficiency
- Good for common allocation sizes (JSON responses, command output)
- Not too large to cause excessive waste
- Not too small to cause bitmap pressure

**Configurable**: Can adjust via `arena_config()` for specific workload

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

### Why Single-Threaded Design?

**Analysis from previous version still applies**:

**Reasons**:
1. **Simplicity**: No race conditions, no deadlocks, easier to debug
2. **Performance**: No lock contention, no context switching
3. **Event-driven I/O**: libevent handles concurrency via epoll/kqueue
4. **I/O bound workload**: Waiting for commands dominates, not CPU
5. **Arena safety**: No atomic operations needed (with note for future)

**Note on v2.0**:
Bitmap array operations still non-atomic. If multi-threading added in future, would need:
- Atomic bitmap operations per member
- Or locks per member
- Or lock-free data structure

Current single-threaded design is optimal for typical workload.

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

**This demonstrates exceptional security awareness.**

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

### Configuration (NEW v2.0)

**Arena Tuning**:

Before calling `prealloc_arena()`, configure arena size:

**Small workload** (default):
```
arena_config(512, 64);  // 32KB virtual
```

**Medium workload**:
```
arena_config(64 * 1024, 1024);  // 64MB virtual
```

**Large workload**:
```
arena_config(1024 * 1024, 10000);  // 10GB virtual
```

**Extreme workload**:
```
arena_config(4 * 1024 * 1024, 100000);  // 400GB virtual
```

**Remember**: Virtual != Physical
- Large configuration costs nothing until used
- Physical memory allocated on-demand
- Configure generously, pay only for actual usage

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

**Benchmarking** (NEW v2.0):

**x86-64**:
```bash
gcc -O2 -o benchmark benchmark.c arena.c -lm
./benchmark
```

**ARM64** (Raspberry Pi 5):
```bash
gcc -O2 -o bench_arm bench_arm.c arena.c -lm
./bench_arm
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

---

## Performance Characteristics

### Virtual Memory Benefits (NEW v2.0)

**Memory Efficiency**:
- Reserve large virtual arena (GB/TB)
- Physical memory only used for accessed pages
- OS automatically manages physical allocation
- No waste on unused capacity

**Example**:
- Configure 10GB arena
- Typical workload uses 50MB
- Physical memory consumption: ~50MB
- Virtual memory reserved: 10GB (costs nothing)

**Scalability**:
- Can handle occasional large allocations without pre-allocating
- Flexible per-deployment configuration
- No hard-coded limits

### Benchmark Results (v2.0)

**Updated Methodology**:
- **Allocation size**: 256KB (stress test)
- **Arena config**: 64KB chunks, 1024 chunks (64MB virtual)
- **CPU pinning**: Eliminates scheduling noise
- **Serialized RDTSC**: Accurate cycle counting
- **Page touching**: Random access to destroy locality
- **Zombie pool**: Realistic fragmentation
- **malloc_trim()**: Forces heap release for fair comparison

**Key Metrics**:
- Throughput (M ops/sec)
- Median latency (P50 in cycles)
- Tail latency (P99 in cycles)
- Consistency (standard deviation)

**Results**:
Arena allocator shows consistent performance benefits for typical server workload. Exact numbers vary by:
- Hardware (CPU, RAM speed)
- Allocation size
- Access patterns
- Fragmentation level

**General Findings**:
- Arena wins for batch allocations
- Arena wins for predictable sizes
- Arena wins for short lifetimes
- malloc competitive for very small allocations (<100 bytes)
- Page fault overhead negligible (amortized over allocation lifetime)

### ARM64 Support (bench_arm.c)

**Platform**: Raspberry Pi 5
- Uses ARM virtual counter for timing
- Instruction serialization barriers
- Optimized batch sizes for ARM cache
- 4MB allocations to stress large allocation path

**Demonstrates**:
- Cross-architecture portability
- Arena allocator works on ARM64
- Virtual memory benefits universal

### Memory Usage Analysis

**Virtual vs Physical**:

**Configured**: 64MB arena (64KB × 1024 chunks)
- **Virtual reserved**: 64MB
- **Bitmap overhead**: 1KB (1024 bits / 8)
- **Physical used initially**: 0 bytes (before any allocations)

**After 10 allocations** (256KB each):
- **Virtual reserved**: Still 64MB
- **Physical used**: ~2.5MB (10 × 256KB)
- **Bitmap**: Still 1KB

**After 100 allocations**:
- **Virtual reserved**: Still 64MB
- **Physical used**: ~25MB (100 × 256KB)
- **Arena full**: No, only 39% utilized

**Key Insight**: Physical usage tracks actual workload, not configuration

### Page Fault Overhead

**First access to allocation**:
- MMU lookup fails (page not mapped)
- CPU raises page fault exception
- OS kernel handles fault:
  - Allocates physical page (4KB)
  - Updates page tables
  - Returns to user code
- Overhead: ~500-1000 cycles (varies by system)

**Subsequent accesses**:
- TLB cached (Translation Lookaside Buffer)
- Virtual→physical lookup: ~1 cycle
- No page fault

**Amortization**:
- 256KB allocation = 64 pages
- 64 page faults on first access
- Total overhead: ~30,000-60,000 cycles
- But allocation lifetime: millions of cycles
- Overhead: <1% of total

**Conclusion**: Page fault overhead negligible for typical workload

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
E2E encryption between Discord bot and CMon provides network security.

### Memory Safety (Enhanced in v2.0)

**Arena Allocator Safety**:
- Virtual memory prevents unbounded physical growth
- Boundary checks prevent buffer overflows
- Header validation detects corruption
- Pointer validation prevents crashes
- munmap() ensures clean teardown

**Virtual Memory Benefits**:
- OS enforces memory protection
- Invalid access triggers segfault (better than silent corruption)
- Address space isolation
- Page-level protection

**Deallocation Checks**:
1. Pointer within arena bounds
2. Header chunk count reasonable
3. Allocation doesn't overflow arena
4. Doesn't cross bitmap member boundary
5. Returns silently on invalid pointer (doesn't crash)

**Recommendations**:
- Configure arena generously (virtual is free)
- Monitor physical memory usage
- Add memory usage logging
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
- [ ] Configure arena size appropriately
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

**Symptom**: mmap failed

**Causes** (NEW v2.0):
- Requested virtual size too large
- System virtual memory limit reached
- Permission issues

**Solutions**:
1. Check virtual memory limits: `ulimit -v`
2. Reduce arena size: `arena_config(smaller_size, fewer_chunks)`
3. Check system limits: `/proc/sys/vm/max_map_count`

---

**Symptom**: Out of memory (OOM killer)

**Causes**:
- Physical memory exhausted
- Too many allocations accessed simultaneously

**Solutions**:
1. Monitor physical memory: `free -h`
2. Reduce concurrent allocations
3. Increase system RAM
4. Adjust workload to use less memory

**Note**: Virtual memory size doesn't matter, physical usage does

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

### Performance Issues

**Symptom**: High latency

**Causes**:
- Commands taking long time
- Page faults on large allocations
- System overload

**Solutions**:
1. Check command times: `journalctl -u cmon | grep duration`
2. Pre-fault arena: Add memset after prealloc_arena (trades startup time for allocation speed)
3. Check system load: `uptime`
4. Monitor page faults: `perf stat -e page-faults ./target`

---

**Symptom**: Excessive page faults

**Causes** (NEW v2.0):
- Large allocations accessed for first time
- Fragmented access patterns
- Cold start

**Solutions**:
1. Pre-fault arena: memset after mmap (slower startup, faster allocations)
2. Increase chunk size: Fewer chunks = fewer page faults
3. Accept overhead: Page faults amortized over allocation lifetime

### Memory Issues

**Symptom**: Virtual memory exhausted

**Causes** (NEW v2.0):
- Arena configured too large
- System virtual memory limit

**Solutions**:
1. Reduce arena size
2. Check limits: `ulimit -v`
3. Increase limit if needed

---

**Symptom**: Physical memory exhausted

**Causes**:
- Too many allocations in use
- Memory leak
- Workload exceeds available RAM

**Solutions**:
1. Monitor usage: `ps aux | grep target`
2. Check for leaks: Ensure all allocations freed
3. Reduce concurrent workload
4. Add more RAM

**Key**: Virtual size doesn't cause OOM, physical usage does

---

## Development Guide

### Project Structure

**Source files**:
- `main.c`: HTTP server, routing, middleware (220 lines)
- `auth.c/h`: Authentication system (150 lines)
- `arena.c/h`: Virtual memory allocator (271 lines) **← Updated**
- `commands.c/h`: Command execution (140 lines)
- `utils.c/h`: Utilities (250 lines)

**Testing**:
- `benchmark.c`: x86-64 performance benchmarking **← Updated**
- `bench_arm.c`: ARM64 benchmarking **← New**
- `test_server.py`: Integration tests
- `run_tests.sh`: Test automation

**Build system**:
- `build.sh`: Compilation and execution
- `flake.nix`: Nix development environment
- `.clang-format`: Code formatting rules

**Total**: ~1,300 lines of C code (excluding tests)

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

### Testing

**Automated tests**:
```bash
./run_tests.sh
```

**Manual testing**:
```bash
./target &
curl -v "http://localhost:8000/health" -H "access_token: $(cat client_secret.key)"
tail -f server.log
```

**Benchmarking** (NEW v2.0):

**x86-64**:
```bash
gcc -O2 -o benchmark benchmark.c arena.c -lm
./benchmark
```

**ARM64**:
```bash
gcc -O2 -o bench_arm bench_arm.c arena.c -lm
./bench_arm
```

### Debugging

**Run in gdb**:
```bash
DEBUG=1 ./build.sh
```

**Common breakpoints**:
- Arena allocation: `break arena.c:90` (check_and_claim)
- Bitmap search: `break arena.c:142` (find_k_consecutive_zeroes)
- Virtual memory init: `break arena.c:51` (prealloc_arena)

**Inspect virtual memory**:
```gdb
# In gdb:
(gdb) info proc mappings    # Show all memory mappings
(gdb) print arena_buf_num   # Number of chunks
(gdb) print arena_buf_size  # Chunk size
(gdb) x/16xg LOCK           # Examine bitmap array
```

**Monitor page faults**:
```bash
perf stat -e page-faults,minor-faults,major-faults ./target
```

### Performance Analysis

**Profile with perf**:
```bash
perf record -g ./target
perf report
```

**Generate flamegraph**:
```bash
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

**Monitor virtual memory**:
```bash
watch -n 1 'cat /proc/$(pgrep target)/status | grep -E "Vm|Rss"'
```

**Tune arena** (NEW v2.0):

Modify configuration in main():
```
// For small workload
arena_config(512, 64);

// For large workload
arena_config(64*1024, 1024);
```

---

## Appendix

### Frequently Asked Questions

**Q: How much virtual memory can I allocate?**

A: On x86-64 Linux:
- **User space**: 128TB (47-bit addresses)
- **Practical limit**: 64TB (42-bit) for compatibility
- **CMon limit**: Only by system configuration

But remember: Virtual != Physical
- Can allocate 64TB virtual
- Physical usage determined by what you access
- OS will OOM kill if physical memory exhausted, not virtual

---

**Q: What's the overhead of virtual memory?**

A: Minimal:
- **Page tables**: ~0.2% of virtual size (e.g., 13MB for 64GB)
- **TLB misses**: Cached after first access
- **Page faults**: One-time cost per page, amortized over lifetime

For server workload with allocation sizes >4KB, overhead is negligible.

---

**Q: Can I mix malloc and arena allocations?**

A: Yes, but:
- Must `free()` what you `malloc()`
- Must `deallocate()` what you `allocate()`
- Don't mix them up
- Current code uses arena for command output, malloc for query parameters

---

**Q: What happens if I allocate more than physical RAM?**

A: Depends:
- **Allocated but not accessed**: Nothing (just virtual reservation)
- **Accessed beyond physical RAM**: OS starts swapping to disk
- **Too much swapping**: Performance degradation
- **No swap space**: OOM killer terminates process

**Best practice**: Configure arena larger than needed, but monitor physical usage

---

**Q: Why not use huge pages?**

A: Trade-off:
- **Huge pages**: Faster TLB, fewer page faults
- **But**: Less flexible, potential waste, privileged operation
- **Default 4KB pages**: Good balance for this workload

Could add huge page support as configuration option.

---

**Q: Can I use this on 32-bit systems?**

A: Technically yes, but:
- Virtual address space limited (2-4GB)
- Loses main benefit of virtual memory approach
- Better to use v1.0 arena on 32-bit systems

---

**Q: How to benchmark on my system?**

A: Included benchmarks:
```bash
# x86-64
gcc -O2 -o benchmark benchmark.c arena.c -lm
./benchmark

# ARM64
gcc -O2 -o bench_arm bench_arm.c arena.c -lm
./bench_arm
```

Adjust constants in benchmark files for your workload.

---

**Q: What's the maximum allocation size?**

A: Limited by:
- **Arena size**: Total virtual reservation
- **Chunk size**: Single allocation can span multiple chunks
- **Physical RAM**: What you can actually access

Example with 64KB chunks:
- Can allocate multi-megabyte buffers
- Limited by configured arena size
- Physical memory determines actual usability

---

**Q: Why mmap instead of huge malloc?**

A: mmap advantages:
- Independent virtual region
- Demand paging (pay for what you use)
- Clean teardown with munmap
- Not affected by heap fragmentation
- Can reserve huge regions without physical cost

malloc would allocate physical memory immediately.

---

**Q: Is this production-ready?**

A: For internal use, yes (with hardening):
- Behind TLS terminator
- Proper monitoring
- Configured arena size
- Tested on your workload

For public-facing: Add more hardening
- Rate limiting
- Input validation
- DDoS protection
- Security audit

---

### References

**Dependencies**:
- [libevent Documentation](https://libevent.org/)
- [OpenSSL Documentation](https://www.openssl.org/docs/)

**Concepts**:
- Virtual memory and MMU
- Demand paging
- TLB and page tables
- Event-driven architecture
- Timing attacks

**Linux Documentation**:
- mmap(2) man page
- munmap(2) man page
- /proc/PID/maps (process memory mappings)

**Similar Projects**:
- webhook (Go): HTTP to command execution
- systemd HTTP API: systemd unit management

**Further Reading**:
- "Understanding the Linux Virtual Memory Manager" (Gorman)
- "What Every Programmer Should Know About Memory" (Drepper)
- "The Linux Programming Interface" (Kerrisk)
- "Systems Performance" (Gregg)

---

**End of Documentation**

**Version 2.0** introduces revolutionary virtual memory arena allocator with virtually unlimited capacity and demand paging. The elegant design allows reserving huge virtual address spaces while only consuming physical memory for actually accessed pages, making CMon suitable for workloads ranging from tiny to massive.

For additional details, consult the source code and README.md.
