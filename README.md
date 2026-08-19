# Twig

Twig is a lightweight Git-like version control system implemented in modern C++20. It is built as a learning project with a focus on the core mechanics of a VCS: repositories, content-addressed storage, staging, commit history, branching, diffing, and basic merge workflows.

## What this project demonstrates

- repository initialization under a hidden `.twig/` directory
- working tree status detection
- index-based staging
- commit DAG with branch references
- branch creation and checkout
- minimal diff and merge flows
- `.twigignore` handling
- Git-vs-Twig benchmarking for hash throughput

## Quick start

```bash
cmake -S . -B build
cmake --build build
./build/twig init
```

## Common commands

```bash
./build/twig status
./build/twig add hello.txt
./build/twig commit -m "initial snapshot"
./build/twig branch feature
./build/twig checkout feature
./build/twig diff hello.txt
./build/twig merge main
./build/twig log
./build/twig benchmark 200 256
```

## Repository layout

```text
repo/
├── .twig/
│   ├── HEAD
│   ├── index
│   ├── objects/
│   └── refs/
│       └── heads/
├── .twigignore
├── tracked-file.txt
└── working-tree-files...
```

## Benchmarking against Git

The benchmark command compares a real Twig snapshot workload with a Git snapshot workload. Both repositories are initialized and receive the same synthetic files before timing begins. The timed work is Twig file-backed staging plus commit versus Git `add .` plus commit; repository initialization and input-file generation are excluded.

```bash
./build/twig benchmark 200 256
```

Output reports the file count, bytes per file, elapsed time for Twig and Git, both throughputs in the `throughput_mb_s=twig:<value>,git:<value>` field, and `git_status`. Benchmark staging output is suppressed. Git must be installed for the comparison; otherwise its timing and throughput are reported as unavailable.

Verified example from this run (timings vary by machine):

```text
benchmark: files=200, bytes_per_file=256, twig_elapsed_ms=12.840, git_elapsed_ms=16.579, throughput_mb_s=twig:3.988,git:3.088, git_status=available
```

This is a controlled synthetic add-and-snapshot comparison, not a production-grade performance claim. Twig and Git use different hashing and object-storage implementations.

## Design notes

Twig follows the same conceptual model as Git:

- Blob = file content addressed by hash
- Tree = staged snapshot of files
- Commit = metadata + parent + tree reference
- Branch = pointer to a commit

The implementation is intentionally small and readable so the core mechanics are easy to follow.

## Validation

```bash
ctest --test-dir build --output-on-failure
```

The project uses CMake and includes a basic repository regression test suite. A sanitizer-enabled configuration is also supported:

```bash
cmake -S . -B build -DTWIG_ENABLE_ASAN=ON
cmake --build build
```

## Current maturity

Twig is not a drop-in replacement for Git. It is a compact, working VCS prototype aimed at demonstrating how a Git-like system can be built and benchmarked in a single project. It is intentionally strong on concept clarity and extension potential, while staying small enough to understand in one pass.

See [Architecture.md](Architecture.md) for a deeper explanation of the repository model and execution flow.
