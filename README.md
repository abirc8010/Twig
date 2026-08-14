# Twig

Twig is a lightweight custom version control system written in modern C++20. The goal is to model the same core ideas behind Git: content-addressable objects, a Merkle-style DAG, a working tree, and a branch-based commit history.

## Project philosophy

Twig is intentionally designed as a learning and portfolio project. It avoids paid services and external cloud dependencies. The implementation focuses on:

- a hidden `.twig/` repository directory
- object storage under `.twig/objects/`
- tree and commit metadata
- efficient local file hashing and staging
- a clean CLI built around `twig <command>`

## Command surface

The first version focuses on the repository lifecycle and the VCS building blocks:

- `twig init`
- `twig status`
- `twig add <file...>`
- `twig log`
- `twig help`

## Core object model

Twig follows the same conceptual object model as Git:

- Blob: file content stored by hash
- Tree: directory snapshot and references to child objects
- Commit: metadata + parent pointer + tree root
- Branch: lightweight pointer to a commit

This is the foundation for the roadmap below.

## Roadmap

### Phase 1: repository bootstrapping
- create `.twig/`
- create `objects/`, `refs/heads/`, and `HEAD`
- track current branch as `main`
- implement repository initialization and filesystem checks

### Phase 2: index and trees
- stage file paths and hashes
- create tree structures from the index
- support directory snapshots and path tracking

### Phase 3: commit history
- write commit objects with parent links and metadata
- update branch refs
- walk the history DAG in `twig log`

### Phase 4: checkout and branching
- switch branches and restore snapshots
- map commit IDs to tree roots
- verify working-tree safety

### Phase 5: diff and merge
- line-based diffing using Myers algorithm or a simpler fallback
- LCA-based merge logic for non-conflicting changes
- conflict reporting and branch safety

## Local build

```bash
cmake -S . -B build
cmake --build build
./build/twig init
```

## Notes

This repository begins with a clean C++20 scaffold and intentionally keeps the implementation understandable. The object and commit pipeline is designed to be expanded toward a real Git-like VCS without requiring large frameworks or proprietary tooling.
