# config-init

Declutter a project's root directory by organizing and initializing configuration files.

```
before              after
──────              ─────
.claude/            .config/
.cursor/            .github/
.editorconfig       src/
.github/
.npmrc
.yarn/
src/
```

Every tool wants a dotfile at the repository root. config-init moves them into a single
tracked `.config/` directory; locally, gitignored symlinks point back so every tool keeps
working. A [pre-commit](https://pre-commit.com) hook (or [prek](https://github.com/j178/prek))
recreates the symlinks on every clone and checkout — teammates install nothing and notice
nothing.

## Migrate an existing repository

```sh
config-init migrate --dry-run   # preview
config-init migrate             # or name entries: config-init migrate .claude .cursor
git add -A && git commit
pre-commit install
```

One idempotent command: moves the configs, creates the symlinks, updates `.gitignore`
(existing rules that reached inside a moved directory get matching `.config/` patterns),
and registers the hook below.

## Clones and new repos set themselves up

`migrate` writes this hook config; after `clone`, `checkout`, `merge`, or `rebase` it
restores any missing symlink:

```yaml
default_install_hook_types: [pre-commit, post-checkout, post-merge, post-rewrite]
repos:
  - repo: https://github.com/jmelahman/config-init
    rev: v0.2.0
    hooks:
      - id: config-init
```

Starting a new repository? Skip `migrate`: commit configs directly under `.config/`, add
the snippet above, done.

## CI configs migrate too

CI checkouts don't run hooks — but most tools take an explicit config path, so a file
like `.goreleaser.yaml` can live in `.config/` with no symlink at all:

```sh
config-init migrate .goreleaser.yaml
echo 'nolink .goreleaser.yaml' >> .config/config-init.conf
```

```yaml
- run: goreleaser release --clean --config .config/.goreleaser.yaml
```

This repository releases itself exactly this way.

## Guardrails

- `.git*` files are never migrated, and `.github`/`.pre-commit-config.yaml` require
  `--force` — a fresh clone must be able to bootstrap the hook.
- Caches, secrets, and CI-read configs (`.venv`, `.env`, `.golangci.yml`, ...) are
  skipped unless you name them.
- Conflicts are reported, never overwritten, and migrating a tracked *file* prints the
  `git rm --cached` command you'll want afterwards.

Tune the scan in `.config/config-init.conf` (committed) or `~/.config/config-init.conf`
(per user), one name or glob per line:

```
# never migrate these here
.idea*
# do migrate .env, despite the default skip
!.env
# keep in .config/ without a root symlink
nolink .goreleaser.yaml
```

## Install

The hook needs no install. For the CLI: `pip install config-init` — small static
binaries (~200KB, instant startup) for Linux and macOS, also on
[GitHub releases](https://github.com/jmelahman/config-init/releases). POSIX only.

---

*Why symlinks?* Hard links can't reference directories and git breaks them on checkout;
relative symlinks work everywhere paths resolve through the OS. — Contributions welcome:
see [CONTRIBUTING.md](CONTRIBUTING.md).
