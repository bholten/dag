# Versioning Policy

Dagwood follows [Semantic Versioning 2.0.0](https://semver.org/) with
one explicit pre-1.0 carve-out (see below).

## What gets versioned

Dagwood is one repository with two distinct surfaces, versioned
together under a single `MAJOR.MINOR.PATCH`. The *meaning* of
"breaking change" differs by surface.

### The `dag` CLI

The command-line interface: flags, exit codes, output format,
behavior of `--dot`/`--dry-run`/`--inspect`, the `KEY=value` override
syntax, and the stdout/stderr channel split (Dagwood chatter on
stderr, child task output prefixed on stdout).

### The Dagwood DSL

The embedded Lcl-based DSL in `lib/dagwood.lcl`: `project`, `task`,
`command`, `import`, `arg`, `def`, `config`, `task-override`,
`task-extend`, `task-disable`, `project-override`, `task-get`, and the
attribute names within `task` blocks (`inputs`, `outputs`,
`depends-on`, `run`, `always-run`, `description`, etc.).

The embedded Lcl interpreter itself versions independently and is
pinned by SHA via CMake's `FetchContent` in `CMakeLists.txt` -- see
[Lcl's versioning
policy](https://github.com/bholten/lcl/blob/master/VERSIONING.md).
Bumping the pin is a vendoring decision, recorded in Dagwood's release
notes when it happens.

## Pre-1.0 carve-out

Standard SemVer says pre-1.0 versions promise nothing. Dagwood is
slightly stricter than that:

- **PATCH** (`0.1.0` → `0.1.1`): bug fixes and internal changes only.
  No intentional breaking changes to either surface.
- **MINOR** (`0.1.0` → `0.2.0`): may include breaking changes to
  either surface. The release notes will call them out.
- **MAJOR** (`0.x` → `1.0`): this is the stability commitment, not a
  free pass for breakage. 1.0 says "we're done changing the CLI and
  DSL on a whim."

In practice that means: within an `0.x.y` line, upgrading is safe;
between `0.x` lines, read the release notes.

## Post-1.0 (the future)

Standard SemVer with no carve-outs:

- **PATCH**: bug fixes, no surface changes.
- **MINOR**: additive only. New CLI flags, new DSL forms, new task
  attributes. Existing surfaces unchanged.
- **MAJOR**: any breaking change to either surface.

## Release process

(Sketch -- to be filled in once 0.1.0 actually ships.)

1. Bump `PROJECT_VERSION` in `CMakeLists.txt`.
2. Reconfigure CMake (`cmake -S . -B build ...`) -- the generated
   version header picks up the new value.
3. Update release notes / `CHANGELOG.md`.
4. Tag the commit `vX.Y.Z`.
5. Publish.
