# BuzzDB: A Lightweight In-Memory Database with Buffer Management

## Overview

**BuzzDB** is a simple database management system designed to implement efficient buffer management, persistent storage, and slotted page architecture. It supports multi-threaded operations, thread-safe page access, and FIFO/LRU page eviction policies, making it suitable for learning and experimenting with core database system concepts.

---

## Features

### Buffer Management
- Implements **FIFO** and **LRU** policies for managing pages in memory.
- Supports thread-safe operations with shared and exclusive locking mechanisms.
- Handles page eviction and buffer-full scenarios efficiently.

### Persistent Storage
- Utilizes a file-based storage system for durability.
- Flushes dirty pages to disk to ensure data persistence.

### Page Handling
- Provides APIs to **fix** and **unfix** pages in memory.
- Supports concurrent access with exclusive and shared locking.
- Maintains a clear separation between in-memory and persistent data.

### Slotted Pages
- Fixed-size pages with metadata to store records dynamically.
- Supports insertion and deletion of records within a page.

### Multi-threading
- Ensures thread safety for concurrent read and write operations.
- Handles synchronization effectively, even under high contention.

---

## Prerequisites

- A modern C++ compiler supporting C++17 or later.
- Libraries for multi-threading and file I/O (standard with most C++ toolchains).

---

## How to Build and Run

### Compile
Use a C++17-compliant compiler:
```bash
g++ -std=c++17 -pthread -o buzzdb buzzdb.cpp
