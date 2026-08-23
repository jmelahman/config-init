# config-init

Declutter a project's root directory by organizing and initializing configuration files.

Tool config directories multiply at the repository root: `.claude/`, `.cursor/`, `.yarn/`,
`.husky/`, `.vscode/`, ... config-init moves them into a single `.config/` directory and
symlinks them back into the root, so the tools keep working while the root stays readable:

```
before                        after
──────                        ─────
.claude/                      .config/
.cursor/                      ├── .claude/
.yarn/                        ├── .cursor/
.github/                      └── .yarn/
src/                          .claude  -> .config/.claude   (symlink, gitignored)
                              .cursor  -> .config/.cursor   (symlink, gitignored)
                              .yarn    -> .config/.yarn     (symlink, gitignored)
                              .github/
                              src/
```

Only `.config/` is tracked by git. The root symlinks are gitignored and recreated after
`clone`/`checkout`/`merge`/`rebase` by a [pre-commit](https://pre-commit.com) hook (also
compatible with [prek](https://github.com/j178/prek)).

config-init is written in [Solod](https://github.com/solod-dev/solod), a subset of Go that
translates to C: the binary is a few hundred kilobytes, starts instantly, and has no runtime
dependencies.

## Install

The pre-commit hook (below) needs no separate install. For direct CLI use:

```sh
pip install config-init
```

PyPI wheels ship small static solod-built binaries for Linux and macOS (amd64 and
arm64); other POSIX platforms build from the sdist. Standalone binaries are also
attached to [GitHub releases](https://github.com/jmelahman/config-init/releases).
Windows is not supported: solod's os package is POSIX-only, and the workflow
depends on symlinks.

## Usage

```sh
config-init migrate              # migrate all root dotfiles (see exclusions below)
config-init migrate .claude .env # migrate specific entries only
config-init migrate --dry-run    # preview without changing anything
config-init init                 # (re)create root symlinks for everything in .config/
```

`migrate` does four things, each idempotent:

1. Moves root config entries into `.config/` and symlinks them back.
2. Adds anchored `/name` patterns for the symlinks to `.gitignore`. If existing ignore
   patterns reach inside a moved directory (like `.yarn/cache`), companion
   `/.config/...` patterns are added so nothing previously ignored becomes tracked.
3. Registers the `config-init` hook in `.pre-commit-config.yaml` (creating the file if
   needed) along with `default_install_hook_types`, so the post-checkout/post-merge/
   post-rewrite hooks get installed.
4. Runs the equivalent of `config-init init`.

Afterwards: `git add -A && git commit`, and `pre-commit install` if hooks weren't
installed already.

`init` walks `.config/` and ensures each entry has a root symlink. Correct links are left
alone (and nothing is printed), stale links are repaired, and real files or directories are
never touched — conflicts are reported and the exit code is non-zero.

### What migrate won't touch

- **Never migratable**: `.git`, `.gitignore`, `.gitattributes`, `.gitmodules` — git reads
  these from the worktree before anything can run; a symlink here would break the
  mechanism that restores symlinks.
- **Guarded (requires naming explicitly plus `--force`)**: `.github`,
  `.pre-commit-config.yaml`/`.yml`, `.pre-commit-hooks.yaml` — these bootstrap a fresh
  clone before any hook can run `config-init init`. Only force-migrate them if you know
  your hooks come from somewhere else.
- **Skipped by the scan (migratable by naming explicitly)**: machine state and secrets
  (`.venv`, `.mypy_cache`, `.terraform`, `.env`, ...) and configs that CI services read
  from hook-less checkouts (`.goreleaser.yaml`, `.golangci.yml`, `.codecov.yml`, ...).
  If you migrate a CI-read config, add a `config-init init` step (or `go run . init`)
  to the workflow before the tool that needs it.

### Ignore configuration

`.config/config-init.conf` (per repository, committed) and
`${XDG_CONFIG_HOME:-~/.config}/config-init.conf` (per user) tune what the automatic scan
ignores — one root-level name or shell glob per line:

```
# don't migrate the IDE config in this repo
.vscode
.idea*
# do migrate .env here even though it's skipped by default
!.env
```

The user file loads first, then the repository file, and the last matching rule wins, so
a repository can override a user-level ignore (and any rule overrides the built-in skip
list). The file itself is never symlinked into the root.

### A note on previously tracked files

For a migrated *directory*, `git add -A` records the moves and the new root symlink stays
untracked (it's gitignored). A previously tracked *file* is different: git keeps tracking
the root path even though it's now an ignored symlink, so also run
`git rm --cached <name>` — migrate prints the exact commands when this applies.

## Pre-commit hook

`config-init migrate` writes this configuration:

```yaml
default_install_hook_types: [pre-commit, post-checkout, post-merge, post-rewrite]
repos:
  - repo: https://github.com/jmelahman/config-init
    rev: v0.2.0
    hooks:
      - id: config-init
```

Two hook ids are available:

- `config-init` — built by pre-commit's `golang` language support (pre-commit and prek
  provision the Go toolchain themselves if needed). The build needs no network access:
  the repository's default modfile backs the solod APIs with `gocompat/`, a small
  Go-stdlib implementation, so the hook binary is regular, fully functional Go.
- `config-init-system` — runs `config-init init` from `PATH`; use it if you install a
  release binary yourself.

Because the symlinks are gitignored, a fresh clone starts without them; the post-checkout
hook restores them immediately.

## Why symlinks (and not hard links)?

Hard links can't reference directories at all (POSIX forbids it), and for files git breaks
the link on every checkout that rewrites the file. Relative symlinks (`.claude ->
.config/.claude`) survive moves of the repository, work for files and directories alike,
and every tool that resolves paths through the OS follows them transparently.

The symlinks themselves are gitignored rather than committed so that checkouts on
platforms without symlink support (Windows without developer mode) degrade gracefully:
you can still run `config-init init` — or read the configs directly in `.config/` —
instead of git materializing them as plain text files containing a path.

## Building from source

The source is written once, in the Solod subset of Go, and builds with either toolchain:

```sh
so build -o config-init ./so             # native binary (needs solod + a C compiler)
so test ./so                             # test suite under solod
so test -sanitize ./so                   # solod tests under ASan/UBSan
go build .                               # Go binary via gocompat (needs only Go)
go -C so run -modfile=gotest.mod ./test  # the same test suite against gocompat
scripts/check                            # builds + tests with both, plus version guards
```

How the two toolchains coexist:

- The root module's `go.mod` replaces `solod.dev` with `gocompat/`, a Go-stdlib-backed
  implementation of the solod APIs this tool uses. The real solod.dev packages are
  transpiler stubs that only work once translated to C, so this replacement is what makes
  a plain `go build` — and the pre-commit hook — produce a working binary.
- The nested module `so/` requires the real `solod.dev` and pulls in the same `cli/`
  package via a directory replace; `so build ./so` and `so test ./so` run there.
  (Replace directives only apply in the main module, so the root's gocompat replacement
  is inert during solod builds.)

`go install github.com/jmelahman/config-init@version` is refused by Go because of the
replace directive; that's intentional — use the pre-commit hook, a release binary, or
`go install ./...` from a checkout.

## Releasing

Releases are cut by pushing a `v*.*.*` tag. The tag must match `Version` in
`cli/cli.go` and `hookRev` in `cli/precommit.go` (`scripts/check-version.sh` guards
this — solod has no ldflags-style stamping, so the constants are the source of
truth). The workflow then publishes:

- a GitHub release with static binaries via GoReleaser (`scripts/sobuild` swaps
  `go build` for `so build` + `zig cc`, targeting musl for Linux), and
- PyPI wheels for linux/macOS × amd64/arm64 plus an sdist, built by the hatch hook
  in `hatch_build.py` with the same toolchain.

Build dependencies are pinned exactly — directly in `pyproject.toml` and
`hatch_build.py`, transitively (with hashes) in the generated `build-constraints.txt`
that CI passes to `uv build --build-constraints`. After changing a pin, regenerate it
with `scripts/gen-build-constraints.sh`; `scripts/check-version.sh` fails when it's
stale.

`goreleaser release --snapshot --clean --skip=publish` dry-runs the GitHub release
artifacts locally.
