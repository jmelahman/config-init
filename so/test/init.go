package main

import (
	"solod.dev/so/os"
	"solod.dev/so/strings"
	"solod.dev/so/testing"
)

func TestInitNoConfigDir(t *testing.T) {
	if !enterTempRepo() {
		t.Fatal("cannot set up temp repo")
		return
	}
	if runCLI("init") != 0 {
		t.Error("init should be a no-op without .config/")
	}
}

func TestInitCreatesAndRepairsLinks(t *testing.T) {
	if !enterTempRepo() {
		t.Fatal("cannot set up temp repo")
		return
	}
	os.Mkdir(".config", 0o755)
	os.Mkdir(".config/.claude", 0o755)
	os.WriteFile(".config/.myrc", []byte("x=1\n"), 0o644)
	// A stale link that points at the wrong place.
	os.Symlink("somewhere/else", ".myrc")

	if runCLI("init") != 0 {
		t.Fatal("init failed")
		return
	}
	if !isLinkTo(".claude", ".config/.claude") {
		t.Error(".claude link not created")
	}
	if !isLinkTo(".myrc", ".config/.myrc") {
		t.Error(".myrc stale link not repaired")
	}

	// Re-running changes nothing and stays quiet about it.
	if runCLI("init") != 0 {
		t.Error("second init failed")
	}
}

func TestInitConflict(t *testing.T) {
	if !enterTempRepo() {
		t.Fatal("cannot set up temp repo")
		return
	}
	os.Mkdir(".config", 0o755)
	os.Mkdir(".config/.claude", 0o755)
	os.Mkdir(".claude", 0o755)
	os.WriteFile(".claude/precious.txt", []byte("keep me\n"), 0o644)

	if runCLI("init") == 0 {
		t.Error("init should fail on a real-directory conflict")
	}
	if readFile(".claude/precious.txt") != "keep me\n" {
		t.Error("conflicting directory contents must never be touched")
	}
}

func TestNotARepo(t *testing.T) {
	// A temp dir without .git anywhere up the tree (/tmp has none).
	dir, err := os.MkdirTemp(tmpBuf[:], "", "config-init-norepo-")
	if err != nil {
		t.Fatal("cannot create temp dir")
		return
	}
	if os.Chdir(dir) != nil {
		t.Fatal("cannot chdir")
		return
	}
	if runCLI("init") == 0 {
		t.Error("init outside a repository should fail")
	}
}

func TestPreCommitInsertExisting(t *testing.T) {
	if !enterTempRepo() {
		t.Fatal("cannot set up temp repo")
		return
	}
	existing := "repos:\n" +
		"  - repo: https://github.com/psf/black\n" +
		"    rev: 24.1.0\n" +
		"    hooks:\n" +
		"      - id: black\n"
	os.WriteFile(".pre-commit-config.yaml", []byte(existing), 0o644)
	os.Mkdir(".claude", 0o755)

	if runCLI("migrate") != 0 {
		t.Fatal("migrate failed")
		return
	}
	pc := readFile(".pre-commit-config.yaml")
	if !strings.Contains(pc, "id: black") {
		t.Error("existing hook lost")
	}
	if !strings.Contains(pc, "id: config-init") {
		t.Error("config-init hook not added")
	}
	if !strings.HasPrefix(pc, "default_install_hook_types:") {
		t.Error("default_install_hook_types not inserted at the top")
	}
	// Our block must appear inside the repos list, before the existing entry.
	ours := strings.Index(pc, "jmelahman/config-init")
	theirs := strings.Index(pc, "psf/black")
	if ours == -1 || theirs == -1 || ours > theirs {
		t.Error("config-init repo block not inserted before existing repos")
	}
}
