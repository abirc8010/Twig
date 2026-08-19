# Twig — Lightweight Content-Addressable Version Control System in C++20

Twig is a fast, zero-dependency, Git-inspired version control system written from scratch in modern **C++20**. It implements a content-addressable object database (`.twig/objects/`), a staging index, tree snapshots, parent-linked commit histories, branch pointers, Longest Common Subsequence (LCS) line diffing, and Lowest Common Ancestor (LCA) three-way branch merging.

---

## Key Features

- **Content-Addressable Object Store:** Hashes `blob`, `tree`, and `commit` objects using 64-bit FNV-1a and stores them in a 2-character sharded directory layout (`.twig/objects/<xx>/<remaining-hash>`).
- **Batch & Recursive Staging (`twig add`):** Stages individual files or entire directory trees recursively (`twig add .`) while pruning ignored directories and files via `.twigignore` wildcard glob matching.
- **Branching & Working-Tree Checkout:** Lightweight branch pointers under `.twig/refs/heads/` and full working-tree snapshot restoration via `twig checkout`.
- **Dynamic Programming Line Diff (`twig diff`):** Computes line-by-line additions (`+`) and deletions (`-`) between staged blobs and modified working tree files using an LCS dynamic programming matrix.
- **LCA Three-Way Merge (`twig merge`):** Traverses the commit Directed Acyclic Graph (DAG) to locate the Lowest Common Ancestor (LCA) commit, automatically resolving non-conflicting file edits/deletions and emitting standard `<<<<<<< HEAD` conflict markers on overlapping modifications.
- **Built-In Git Benchmark Harness (`twig benchmark`):** Measures end-to-end staging and commit throughput against the system `git` binary on identical synthetic workloads.

---

## Repository Architecture

```text
twig/
├── CMakeLists.txt
├── README.md
├── Architecture.md
├── include/
│   └── twig/
│       └── repository.hpp
├── src/
│   ├── main.cpp
│   └── repository.cpp
└── tests/
    └── repository_test.cpp
```

When initialized inside a project directory, Twig manages its state inside `.twig/`:

```text
<project-root>/
├── .twig/
│   ├── HEAD                 # Current symbolic ref (e.g., refs/heads/main)
│   ├── index                # Staged file path -> blob hash mapping
│   ├── objects/             # Sharded content-addressable blobs, trees, and commits
│   └── refs/
│       └── heads/           # Branch pointers (main, feature, etc.)
└── .twigignore              # Optional ignore patterns (supports * globs and dir/ prefixes)
```

---

## Build & Run Tests

### Prerequisites
- C++20 compatible compiler (`g++` >= 10 or `clang++` >= 12)
- CMake (>= 3.16)
- Git (optional, used only for side-by-side comparison in `twig benchmark`)

### Compile and Execute Unit Tests
```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## CLI Usage & Quickstart

```bash
# 1. Initialize a new Twig repository
./build/twig init

# 2. Stage individual files or recursively stage a directory
./build/twig add README.md src/
# or stage everything respecting .twigignore:
./build/twig add .

# 3. Check working tree and staging status
./build/twig status

# 4. Create a commit snapshot
./build/twig commit -m "Initial project snapshot" --author "Abir <abir@local>"

# 5. View commit history
./build/twig log

# 6. Inspect line-by-line changes before staging
./build/twig diff README.md

# 7. Create, list, and switch branches
./build/twig branch feature-x
./build/twig branch
./build/twig checkout feature-x

# 8. Merge a branch into the current branch using 3-way LCA merge
./build/twig checkout main
./build/twig merge feature-x
```

---

## Benchmarking Against Git

`twig benchmark [files=1000] [size=1024]` runs an isolated end-to-end staging + commit benchmark in temporary directories (`twig-bench` and `git-bench`). Repository initialization and synthetic file generation are excluded from both timers.

```bash
./build/twig benchmark 200 256
```

### Sample Verified Output
```text
benchmark: files=200, bytes_per_file=256, twig_elapsed_ms=12.840, git_elapsed_ms=16.579, throughput_mb_s=twig:3.988,git:3.088, git_status=available
```

| Metric (200 files x 256 B) | Twig (C++20) | Git |
| :--- | :--- | :--- |
| **End-to-End Elapsed Time** | **12.840 ms** | 16.579 ms |
| **Snapshot Throughput** | **3.988 MB/s** | 3.088 MB/s |

> **Note on Benchmark Scope:** Twig uses an uncompressed 64-bit FNV-1a object pipeline optimized with batched index writes, pre-allocated binary file buffers, and lexical path normalization, whereas Git performs cryptographic hashing and zlib compression. The benchmark serves as a reproducible systems-engineering throughput baseline.