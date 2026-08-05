# CMon - Complete Documentation

**Version**: 2.0  
**Language**: C  
**Author**: Blazzee

---


## Overview

### What is CMon?

**CMon** (C Monitor) is a lightweight HTTP server for remote server operations management. It provides authenticated REST API endpoints to execute system administration commands remotely.

### What's New in Version 2.0

**Virtual Memory Arena Allocator (10x to 150x faster than malloc)**:
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
