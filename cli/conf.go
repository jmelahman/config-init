package cli

import (
	"solod.dev/so/fmt"
	"solod.dev/so/mem"
	"solod.dev/so/os"
	"solod.dev/so/path"
	"solod.dev/so/strings"
)

// confFileName is the ignore configuration read from .config/ in the
// repository and from ${XDG_CONFIG_HOME:-~/.config}/ for the user. It is
// never symlinked into the repository root.
//
// Format, one rule per line: a root-level entry name or shell glob
// (".env", ".cla*") that `config-init migrate` should leave alone, or
// "!name" to un-ignore an entry (overriding earlier rules and the built-in
// skip list). "#" starts a comment. The user file loads first, then the
// repository file; the last matching rule wins.
const confFileName = "config-init.conf"

type ignoreRule struct {
	pattern string
	negate  bool
}

// The CLI is single-threaded and rules must outlive the loading frame, so
// package-level storage is the simplest safe home for them. Patterns are
// views into arena-backed file contents.
var ignoreRules [maxEntries]ignoreRule
var nIgnoreRules int

// loadIgnoreRules reads the user-level and repository-level config files.
// Must be called with the repository root as the working directory.
func loadIgnoreRules(a mem.Allocator) {
	nIgnoreRules = 0
	base := os.Getenv("XDG_CONFIG_HOME")
	if base == "" {
		home := os.Getenv("HOME")
		if home != "" {
			base = home + "/.config"
		}
	}
	if base != "" {
		loadConfFile(a, base+"/"+confFileName)
	}
	loadConfFile(a, configDir+"/"+confFileName)
}

func loadConfFile(a mem.Allocator, fname string) {
	data, err := os.ReadFile(a, fname)
	if err != nil {
		return
	}
	lines := strings.Split(a, string(data), "\n")
	for _, ln := range lines {
		t := strings.TrimSpace(ln)
		if t == "" || strings.HasPrefix(t, "#") {
			continue
		}
		neg := false
		if strings.HasPrefix(t, "!") {
			neg = true
			t = strings.TrimSpace(t[1:])
		}
		if t == "" {
			continue
		}
		if strings.IndexByte(t, '/') >= 0 {
			fmt.Fprintf(os.Stderr, "config-init: %s: ignoring %s (rules match root-level names, not paths)\n", fname, t)
			continue
		}
		if _, merr := path.Match(t, "x"); merr != nil {
			fmt.Fprintf(os.Stderr, "config-init: %s: ignoring malformed pattern %s\n", fname, t)
			continue
		}
		if nIgnoreRules < maxEntries {
			ignoreRules[nIgnoreRules] = ignoreRule{pattern: t, negate: neg}
			nIgnoreRules++
		}
	}
}

// isIgnored reports whether the automatic scan should skip a root entry.
// Config rules take precedence over the built-in skip list; among rules,
// the last match wins.
func isIgnored(name string) bool {
	matched := false
	ignored := false
	for i := range nIgnoreRules {
		ok, merr := path.Match(ignoreRules[i].pattern, name)
		if merr == nil && ok {
			matched = true
			ignored = !ignoreRules[i].negate
		}
	}
	if matched {
		return ignored
	}
	return containsString(skipByDefault, name)
}
