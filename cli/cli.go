// Package cli implements the config-init command-line interface.
//
// config-init keeps a repository's root directory free of tool configuration
// clutter: config files and directories (.claude/, .cursor/, .yarn/, ...) are
// stored under .config/ and symlinked back into the root. The symlinks are
// gitignored and recreated on checkout by the `config-init init` pre-commit
// hook, so only .config/ is tracked.
package cli

import (
	"solod.dev/so/fmt"
	"solod.dev/so/mem"
	"solod.dev/so/os"
)

// Version is the config-init release version.
const Version = "0.1.0"

// configDir is where configuration entries live, relative to the repo root.
const configDir = ".config"

// Package-level string constants can't be built by concatenation: the
// transpiler emits `+` as a runtime operation, which C static initializers
// don't allow. Keep them as single literals.
const usageText = `config-init: declutter a repository root by storing configs in .config/
and symlinking them back into the root.

Usage:
  config-init init                 Create root symlinks for every entry in .config/.
                                   Safe to re-run; intended as a post-checkout hook.
  config-init migrate [entry...]   Move root config entries into .config/, link them
                                   back, gitignore the links, and register the
                                   pre-commit hook. With no arguments, migrates all
                                   root dotfiles except git-owned and cache entries.
  config-init version              Print the version.
  config-init help                 Print this help.

Flags:
  -n, --dry-run                    Print planned actions without changing anything.
`

// arenaBuf backs every allocation the program makes; freed wholesale on exit.
var arenaBuf [1 << 20]byte

// Run executes the CLI and returns the process exit code.
func Run(args []string) int {
	arena := mem.NewArena(arenaBuf[:])
	if len(args) < 2 {
		fmt.Print(usageText)
		return 2
	}
	cmd := args[1]
	dryRun := false
	var extras [maxEntries]string
	nExtras := 0
	for _, arg := range args[2:] {
		if arg == "--dry-run" || arg == "-n" {
			dryRun = true
			continue
		}
		if len(arg) > 0 && arg[0] == '-' {
			fmt.Fprintf(os.Stderr, "config-init: unknown flag %s\n", arg)
			return 2
		}
		if nExtras >= maxEntries {
			fmt.Fprintf(os.Stderr, "config-init: too many arguments\n")
			return 2
		}
		extras[nExtras] = cleanEntryArg(arg)
		nExtras++
	}

	switch cmd {
	case "help", "--help", "-h":
		fmt.Print(usageText)
		return 0
	case "version", "--version", "-V":
		fmt.Printf("config-init %s\n", Version)
		return 0
	case "init":
		if err := chdirRepoRoot(); err != nil {
			fmt.Fprintf(os.Stderr, "%s\n", err.Error())
			return 1
		}
		return cmdInit(&arena, dryRun)
	case "migrate":
		if err := chdirRepoRoot(); err != nil {
			fmt.Fprintf(os.Stderr, "%s\n", err.Error())
			return 1
		}
		return cmdMigrate(&arena, extras[:nExtras], dryRun)
	}
	fmt.Fprintf(os.Stderr, "config-init: unknown command %s (try `config-init help`)\n", cmd)
	return 2
}
