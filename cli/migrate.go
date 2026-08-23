package cli

import (
	"solod.dev/so/fmt"
	"solod.dev/so/mem"
	"solod.dev/so/os"
	"solod.dev/so/strings"
)

// protectedNames must stay at the repository root: git reads them directly
// from the worktree before any hook can run, GitHub reads .github/ from the
// tree, and pre-commit needs its config at the root to bootstrap the hook
// that recreates the symlinks.
var protectedNames = []string{
	".git",
	".gitignore",
	".gitattributes",
	".gitmodules",
	".github",
	".config",
	".pre-commit-config.yaml",
	".pre-commit-config.yml",
	".pre-commit-hooks.yaml",
}

// skipByDefault holds machine state or secrets rather than shareable
// configuration, so the automatic scan leaves it alone. Naming one of these
// explicitly (`config-init migrate .env`) still migrates it.
var skipByDefault = []string{
	".env",
	".venv",
	".direnv",
	".DS_Store",
	".cache",
	".mypy_cache",
	".pytest_cache",
	".ruff_cache",
	".tox",
	".nox",
	".coverage",
	".hypothesis",
	".terraform",
	".gradle",
	".next",
	".nuxt",
	".turbo",
	".parcel-cache",
	".angular",
	".dart_tool",
	".eggs",
	".ipynb_checkpoints",
	".sass-cache",
	".history",
	".vs",
	".swiftpm",
	".build",
}

type moveResult int

const (
	moveMoved moveResult = iota
	moveSkipped
	moveFailed
)

// cmdMigrate moves root config entries into .config/, links them back,
// gitignores the links, and registers the pre-commit hook. With explicit
// entries it migrates exactly those; otherwise it scans the root for
// dotfiles that aren't protected, skipped, or already symlinks.
func cmdMigrate(a mem.Allocator, explicit []string, dryRun bool) int {
	fi, err := os.Lstat(configDir)
	if err != nil {
		if dryRun {
			fmt.Printf("config-init: [dry-run] would create %s/\n", configDir)
		} else {
			if merr := os.Mkdir(configDir, 0o755); merr != nil {
				fmt.Fprintf(os.Stderr, "config-init: cannot create %s: %s\n", configDir, merr.Error())
				return 1
			}
			fmt.Printf("config-init: created %s/\n", configDir)
		}
	} else if !fi.IsDir() {
		fmt.Fprintf(os.Stderr, "config-init: %s exists but is not a directory\n", configDir)
		return 1
	}

	var migrated [maxEntries]string
	nMigrated := 0
	fails := 0

	if len(explicit) > 0 {
		for _, name := range explicit {
			r := migrateEntry(name, dryRun, true)
			if r == moveFailed {
				fails++
			} else if r == moveMoved && nMigrated < maxEntries {
				migrated[nMigrated] = name
				nMigrated++
			}
		}
	} else {
		entries, rerr := os.ReadDir(a, ".")
		if rerr != nil {
			fmt.Fprintf(os.Stderr, "config-init: cannot read repository root: %s\n", rerr.Error())
			return 1
		}
		for _, e := range entries {
			if !strings.HasPrefix(e.Name, ".") {
				continue
			}
			if e.Type&os.ModeSymlink != 0 {
				continue
			}
			if containsString(protectedNames, e.Name) || containsString(skipByDefault, e.Name) {
				continue
			}
			r := migrateEntry(e.Name, dryRun, false)
			if r == moveFailed {
				fails++
			} else if r == moveMoved && nMigrated < maxEntries {
				migrated[nMigrated] = e.Name
				nMigrated++
			}
		}
	}

	// The ignore and link passes cover everything in .config/, not just this
	// run's moves, so a partially migrated repo self-heals. In dry-run mode
	// the moves didn't happen, so add them by hand.
	var names [maxEntries]string
	nNames := 0
	entries, rerr := os.ReadDir(a, configDir)
	if rerr == nil {
		for _, e := range entries {
			if nNames < maxEntries {
				names[nNames] = e.Name
				nNames++
			}
		}
	} else if !dryRun {
		fmt.Fprintf(os.Stderr, "config-init: cannot read %s: %s\n", configDir, rerr.Error())
		return 1
	}
	if dryRun {
		for i := range nMigrated {
			if !containsString(names[:nNames], migrated[i]) && nNames < maxEntries {
				names[nNames] = migrated[i]
				nNames++
			}
		}
	}

	if nNames == 0 && nMigrated == 0 {
		fmt.Printf("config-init: nothing to migrate\n")
		return 0
	}

	if ensureGitignore(a, names[:nNames], dryRun) < 0 {
		fails++
	}
	for i := range nNames {
		// In a dry run the moves above didn't happen, so entries planned for
		// migration still exist at the root; a real run links them after the
		// move, so don't report them as conflicts here.
		if dryRun && containsString(migrated[:nMigrated], names[i]) {
			fmt.Printf("config-init: [dry-run] would link %s -> %s/%s\n", names[i], configDir, names[i])
			continue
		}
		r := ensureLink(names[i], dryRun)
		if r == linkConflict || r == linkError {
			fails++
		}
	}
	if ensurePreCommitConfig(a, dryRun) < 0 {
		fails++
	}

	if fails > 0 {
		fmt.Fprintf(os.Stderr, "config-init: completed with %d problems\n", fails)
		return 1
	}
	if dryRun {
		fmt.Printf("config-init: dry run complete; re-run without --dry-run to apply\n")
		return 0
	}
	fmt.Print(
		"config-init: done. Suggested follow-up:\n",
		"  git add -A && git commit\n",
		"  pre-commit install  # installs post-checkout/post-merge hooks too\n")
	return 0
}

// migrateEntry moves one root entry into .config/. The symlink back is
// created later by the shared link pass.
func migrateEntry(name string, dryRun bool, explicit bool) moveResult {
	if name == "." || name == ".." || strings.IndexByte(name, '/') >= 0 {
		fmt.Fprintf(os.Stderr, "config-init: %s is not a root-level entry name\n", name)
		return moveFailed
	}
	if explicit && containsString(protectedNames, name) {
		fmt.Fprintf(os.Stderr, "config-init: %s must stay at the repository root; skipping\n", name)
		return moveFailed
	}
	fi, err := os.Lstat(name)
	if err != nil {
		if explicit {
			fmt.Fprintf(os.Stderr, "config-init: %s not found\n", name)
			return moveFailed
		}
		return moveSkipped
	}
	if fi.Mode()&os.ModeSymlink != 0 {
		return moveSkipped
	}
	dest := configDir + "/" + name
	if _, derr := os.Lstat(dest); derr == nil {
		fmt.Fprintf(os.Stderr, "config-init: both %s and %s exist; resolve manually\n", name, dest)
		return moveFailed
	}
	if dryRun {
		fmt.Printf("config-init: [dry-run] would move %s -> %s\n", name, dest)
		return moveMoved
	}
	if rerr := os.Rename(name, dest); rerr != nil {
		fmt.Fprintf(os.Stderr, "config-init: cannot move %s -> %s: %s\n", name, dest, rerr.Error())
		return moveFailed
	}
	fmt.Printf("config-init: moved %s -> %s\n", name, dest)
	return moveMoved
}
