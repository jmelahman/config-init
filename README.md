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

Never migrated: `.git`, `.gitignore`, `.gitattributes`, `.gitmodules`, `.github`,
`.pre-commit-config.yaml`, `.pre-commit-hooks.yaml` — git, GitHub, and pre-commit read
these from the root before any hook can run. Skipped by the automatic scan (but migratable
explicitly): caches and machine state like `.venv`, `.mypy_cache`, `.terraform`, and
secrets like `.env`.

## Pre-commit hook

`config-init migrate` writes this configuration:

```yaml
default_install_hook_types: [pre-commit, post-checkout, post-merge, post-rewrite]
repos:
  - repo: https://github.com/jmelahman/config-init
    rev: v0.1.0
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
make build      # so build -o config-init ./so   (native binary, needs solod + a C compiler)
make test       # so test ./so
make sanitize   # solod tests under ASan/UBSan
make gobuild    # go build .                     (Go binary via gocompat, needs only Go)
make gotest     # the same test suite against gocompat
make check      # all of the above
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
