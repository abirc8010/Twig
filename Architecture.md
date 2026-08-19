# Twig Architecture

Twig is a compact Git-like version control system written in modern C++20. It is designed to be understandable enough for learning, but still realistic enough to demonstrate the core mechanics of a VCS: staged files, commit history, branch pointers, tree snapshots, and basic merge behavior.

## Goals

- model Git concepts without depending on Git internals
- keep the code readable and educational
- provide a working local repository workflow
- benchmark Twig's hashing workflow against Git's own hashing path
- remain buildable on a normal Linux toolchain with CMake

## High-level layout

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
├── tests/
│   └── repository_test.cpp
└── build/
```

## Repository model

A Twig repository creates a hidden `.twig/` directory inside the working tree. The repository is intentionally simple but follows the same conceptual structure as Git:

```text
repo/
├── .twig/
│   ├── HEAD
│   ├── index
│   ├── objects/
│   └── refs/
│       └── heads/
│           ├── main
│           └── feature
├── .twigignore
├── tracked-file.txt
└── working-tree-files...
```

### Core concepts

- Blob: file contents are hashed and stored by content identity
- Tree: a snapshot of staged files and their paths
- Commit: metadata, parent pointer, authored message, tree root
- Branch: a pointer to a commit hash
- Index: the current staging area
- Working tree: files on disk that may be modified or untracked

## Main flow

### 1. Initialization

`twig init` creates the repository metadata and default branch structure. The repository is initialized with a branch ref for `main` and a blank index file.

### 2. Staging and snapshotting

Files are added to the index, then a tree snapshot is produced from the staged state. This makes the repository behave like a light Git workflow without requiring a full object database implementation.

### 3. Commit creation

A commit object stores:

- tree hash
- parent hash
- author info
- timestamp
- message

The current branch ref is updated to point at the new commit.

### 4. Branching and checkout

Branch creation stores a ref under `.twig/refs/heads/<name>`. Checkout restores files from the target snapshot and switches `HEAD` to the selected branch.

### 5. Status and diff

`twig status` compares the working tree against the index and the last recorded snapshot. It reports:

- staged changes
- modified tracked files
- untracked files
- ignored files

`twig diff <file>` compares the file content in the index with the current working tree content using a basic line-diff approach.

### 6. Merge

Twig supports a lightweight merge path that attempts to merge branch content into the current branch using a simplified three-way strategy and basic conflict handling. It is intentionally smaller than Git's full merge engine, but it demonstrates the same conceptual flow.

## Benchmarking against Git

`twig benchmark <files> <bytes-per-file>` measures an end-to-end snapshot workload. Setup is excluded from both timed sections:

1. Remove and recreate separate `twig-bench` and `git-bench` directories.
2. Initialize the Twig and Git repositories.
3. Generate the same deterministic synthetic files on disk in both working trees.
4. Time Twig staging every file from disk, writing blob objects and updating the index, followed by creating a commit.
5. Time Git's `add .` plus quiet commit as one shell invocation. Git stdout and stderr are redirected so benchmark output stays clean.

The output includes file count, bytes per file, Twig and Git elapsed milliseconds, both throughputs in decimal MB/s as `throughput_mb_s=twig:<value>,git:<value>`, and Git availability/status. Git initialization and file generation are not included in either measurement. Twig staging progress output is disabled for this operation. Twig batch staging reads the index and ignore rules once and writes the updated index once.

This is a comparable synthetic add-and-snapshot workload, not a claim of equivalent internals or a production-grade performance conclusion. Twig uses its own FNV-1a-based content hash and straightforward object writes; Git uses its configured object format and compression.

Verified example from this run (`./build/twig benchmark 200 256`; timings vary by machine):

```text
benchmark: files=200, bytes_per_file=256, twig_elapsed_ms=12.840, git_elapsed_ms=16.579, throughput_mb_s=twig:3.988,git:3.088, git_status=available
```

## Key implementation details

### Repository class

The `Repository` class owns most of the VCS logic:

- initialization
- branch and HEAD management
- status checks
- add/commit flow
- diff and merge operations
- benchmark harness

### CLI layer

The command parser in `src/main.cpp` exposes the user-facing commands:

- init
- status
- add
- commit
- branch
- checkout
- diff
- merge
- benchmark
- log
- help

## Tradeoffs

Twig intentionally favors clarity over Git parity. The project does not try to match Git's full object format, index semantics, or advanced patch algorithms. Instead, it surfaces the important concepts in a compact and approachable implementation that can be extended later.

## Future extensions

- more faithful `.gitignore` matching
- stronger diff output and patch generation
- real three-way merge resolution with conflict markers
- repository compression and object packing
- performance comparisons against optimized hash libraries
- CI validation with sanitizers and benchmark smoke tests
