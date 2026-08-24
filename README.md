# config-init

Declutter a project's root directory by organizing and initializing configuration files.

Every tool wants a dotfile at the repository root: `.claude/`, `.cursor/`, `.yarn/`,
`.husky/`, `.vscode/`, ... config-init moves them into a single `.config/` directory and
symlinks them back, so every tool keeps working while the root stays readable:

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

Only `.config/` is tracked by git. The symlinks are recreated automatically by a
[pre-commit](https://pre-commit.com) hook (also works with
[prek](https://github.com/j178/prek)) — teammates clone, and everything is just there.

## Migrate an existing repository

```sh
config-init migrate --dry-run   # preview
config-init migrate             # do it
git add -A && git commit
pre-commit install              # if hooks weren't installed already
```

One command handles the whole transition, idempotently:

- moves root config entries into `.config/` and symlinks them back;
- updates `.gitignore` so the symlinks stay untracked — including companion patterns
  for existing rules that reach inside a moved directory (`.yarn/cache` keeps being
  ignored at `.config/.yarn/cache`);
- registers the pre-commit hook (below) so every future clone and checkout restores
  the symlinks by itself.

Prefer to move only specific things? `config-init migrate .claude .cursor`. Preview
any of it with `--dry-run`.

## New clones (and new repos) set themselves up

`migrate` writes this to `.pre-commit-config.yaml`:

```yaml
default_install_hook_types: [pre-commit, post-checkout, post-merge, post-rewrite]
repos:
  - repo: https://github.com/jmelahman/config-init
    rev: v0.2.0
    hooks:
      - id: config-init
```

After `clone`, `checkout`, `merge`, or `rebase`, the hook runs `config-init init`,
which walks `.config/` and (re)creates any missing root symlink. Nobody on the team
installs anything: pre-commit and prek build the hook themselves with their Go
toolchain support, with no network access needed during the build.

The same works for a **brand-new repository** — skip `migrate` entirely: commit your
configs directly under `.config/`, add the snippet above, and every checkout
materializes the symlinks on its own. (If you install binaries yourself, the
`config-init-system` hook id runs `config-init init` from `PATH` instead.)

`init` is safe to run any time: correct links are left alone, stale links are
repaired, and real files are never touched — conflicts are reported instead.

## CI reads configs too — use `--config` paths

CI checkouts don't run git hooks, so a config your CI service reads from the worktree
(`.goreleaser.yaml`, `.golangci.yml`, `.codecov.yml`, ...) would be a missing symlink
there. config-init's scan skips those by default, but you don't have to leave them at
the root: most tools accept an explicit config path, so the file can live in
`.config/` with **no symlink at all**:

```sh
config-init migrate .goreleaser.yaml    # move it (prints a git rm --cached hint)
echo 'nolink .goreleaser.yaml' >> .config/config-init.conf
```

```yaml
# in your workflow
- run: goreleaser release --clean --config .config/.goreleaser.yaml
```

`nolink` entries live in `.config/` without a root symlink; `init` even removes the
old symlink on everyone's checkout automatically. This repository does exactly this
with its own goreleaser config. Alternatively, keep the symlinks and add a
`config-init init` step to the workflow before the tool that needs them.

## What migrate won't touch

- **Never**: `.git`, `.gitignore`, `.gitattributes`, `.gitmodules` — git reads these
  before any hook can run; moving them would break the machinery that restores the
  symlinks.
- **Guarded** (requires naming explicitly plus `--force`): `.github`,
  `.pre-commit-config.yaml`/`.yml`, `.pre-commit-hooks.yaml` — these bootstrap a
  fresh clone before the hook can run. Only force-migrate them if your hooks come
  from somewhere else.
- **Skipped by the scan** (migratable by naming them): machine state and secrets
  (`.venv`, `.mypy_cache`, `.terraform`, `.env`, ...) and the CI-read configs above.

One git subtlety: for a previously *tracked file* (not directory), git keeps tracking
the root path as a symlink until you `git rm --cached <name>` — migrate prints the
exact commands whenever this applies.

## Configuration

`.config/config-init.conf` (per repository, committed) and
`${XDG_CONFIG_HOME:-~/.config}/config-init.conf` (per user) tune the automatic scan —
one root-level name or shell glob per line:

```
# don't migrate the IDE config in this repo
.vscode
.idea*
# do migrate .env here even though it's skipped by default
!.env
# lives in .config/ but needs no root symlink (tools use --config paths)
nolink .goreleaser.yaml
```

The user file loads first, then the repository file; the last matching rule wins, so a
repository can override a user-level ignore, and any rule overrides the built-in skip
list. `nolink` is honored from the repository file only.

## Install

The pre-commit hook needs no install at all. For the CLI itself:

```sh
pip install config-init
```

Wheels ship small static binaries (~200KB, instant startup) for Linux and macOS on
amd64/arm64; other POSIX platforms build from the sdist. Standalone binaries are on
[GitHub releases](https://github.com/jmelahman/config-init/releases). Windows is not
supported — the workflow depends on symlinks.

## Why symlinks (and not hard links)?

Hard links can't reference directories at all, and git breaks file hard links on
checkout. Relative symlinks work for files and directories alike, survive moving the
repository, and every tool that resolves paths through the OS follows them
transparently. They're gitignored rather than committed so platforms without symlink
support degrade gracefully instead of materializing text files containing a path.

---

Contributions welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for building from
source (config-init is written in [Solod](https://github.com/solod-dev/solod), a
subset of Go that translates to C) and the release process.
