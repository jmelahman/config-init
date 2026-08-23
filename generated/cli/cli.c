#include "cli.h"

// -- Types --
typedef so_int linkResult;
typedef so_int moveResult;

// -- Forward declarations --
static so_int ensureGitignore(mem_Allocator a, so_Slice names, bool dryRun);
static bool refersInsideAny(so_String core, so_Slice names);
static so_int cmdInit(mem_Allocator a, bool dryRun);
static linkResult ensureLink(so_String name, bool dryRun);
static so_int cmdMigrate(mem_Allocator a, so_Slice explicit, bool dryRun);
static moveResult migrateEntry(so_String name, bool dryRun, bool explicit);
static so_int ensurePreCommitConfig(mem_Allocator a, bool dryRun);
static void writeRepoBlock(bytes_Buffer* buf, so_String indent);
static so_Error chdirRepoRoot(void);
static so_String cleanEntryArg(so_String arg);
static bool containsString(so_Slice list, so_String s);
static bool hasExactLine(so_Slice lines, so_String s);

// -- Variables and constants --

// configDir is where configuration entries live, relative to the repo root.
static const so_String configDir = so_str(".config");

// Package-level string constants can't be built by concatenation: the
// transpiler emits `+` as a runtime operation, which C static initializers
// don't allow. Keep them as single literals.
static const so_String usageText = so_str("config-init: declutter a repository root by storing configs in .config/\nand symlinking them back into the root.\n\nUsage:\n  config-init init                 Create root symlinks for every entry in .config/.\n                                   Safe to re-run; intended as a post-checkout hook.\n  config-init migrate [entry...]   Move root config entries into .config/, link them\n                                   back, gitignore the links, and register the\n                                   pre-commit hook. With no arguments, migrates all\n                                   root dotfiles except git-owned and cache entries.\n  config-init version              Print the version.\n  config-init help                 Print this help.\n\nFlags:\n  -n, --dry-run                    Print planned actions without changing anything.\n");

// arenaBuf backs every allocation the program makes; freed wholesale on exit.
static so_byte arenaBuf[1048576] = {0};
static const so_String gitignoreFile = so_str(".gitignore");
static const so_String gitignoreHeader = so_str("# Symlinks into .config/, recreated by `config-init init`.");
static const linkResult linkOK = 0;
static const linkResult linkCreated = 1;
static const linkResult linkRelinked = 2;
static const linkResult linkConflict = 3;
static const linkResult linkError = 4;

// protectedNames must stay at the repository root: git reads them directly
// from the worktree before any hook can run, GitHub reads .github/ from the
// tree, and pre-commit needs its config at the root to bootstrap the hook
// that recreates the symlinks.
static so_Slice protectedNames = (so_Slice){(so_String[9]){so_str(".git"), so_str(".gitignore"), so_str(".gitattributes"), so_str(".gitmodules"), so_str(".github"), so_str(".config"), so_str(".pre-commit-config.yaml"), so_str(".pre-commit-config.yml"), so_str(".pre-commit-hooks.yaml")}, 9, 9};

// skipByDefault holds machine state or secrets rather than shareable
// configuration, so the automatic scan leaves it alone. Naming one of these
// explicitly (`config-init migrate .env`) still migrates it.
static so_Slice skipByDefault = (so_Slice){(so_String[27]){so_str(".env"), so_str(".venv"), so_str(".direnv"), so_str(".DS_Store"), so_str(".cache"), so_str(".mypy_cache"), so_str(".pytest_cache"), so_str(".ruff_cache"), so_str(".tox"), so_str(".nox"), so_str(".coverage"), so_str(".hypothesis"), so_str(".terraform"), so_str(".gradle"), so_str(".next"), so_str(".nuxt"), so_str(".turbo"), so_str(".parcel-cache"), so_str(".angular"), so_str(".dart_tool"), so_str(".eggs"), so_str(".ipynb_checkpoints"), so_str(".sass-cache"), so_str(".history"), so_str(".vs"), so_str(".swiftpm"), so_str(".build")}, 27, 27};
static const moveResult moveMoved = 0;
static const moveResult moveSkipped = 1;
static const moveResult moveFailed = 2;
static const so_String hookRepoURL = so_str("https://github.com/jmelahman/config-init");
static const so_String hookRev = so_str("v0.1.0");
static const so_String preCommitYAML = so_str(".pre-commit-config.yaml");
static const so_String preCommitYML = so_str(".pre-commit-config.yml");
static const so_String hookTypesKey = so_str("default_install_hook_types");

// The symlinks must come back after any operation that rewrites the
// worktree, and plain `pre-commit install` only installs the pre-commit
// stage, hence the explicit hook types.
static const so_String hookTypesLine = so_str("default_install_hook_types: [pre-commit, post-checkout, post-merge, post-rewrite]");
static const so_String freshPreCommitConfig = so_str("default_install_hook_types: [pre-commit, post-checkout, post-merge, post-rewrite]\nrepos:\n  - repo: https://github.com/jmelahman/config-init\n    rev: v0.1.0\n    hooks:\n      - id: config-init\n");
static so_Error errNotRepo = errors_New("config-init: not inside a git repository (no .git found)");

// maxEntries bounds how many root entries a single run can manage.
static const int64_t maxEntries = 256;

// -- cli.go --

// Run executes the CLI and returns the process exit code.
so_int cli_Run(so_Slice args) {
    mem_Arena arena = mem_NewArena(so_array_slice(so_byte, arenaBuf, 0, 1048576, 1048576));
    if (so_len(args) < 2) {
        fmt_Print(so_cstr(usageText));
        return 2;
    }
    so_String cmd = so_at(so_String, args, 1);
    bool dryRun = false;
    so_String extras[256] = {0};
    so_int nExtras = 0;
    for (so_int _ = 0; _ < so_len(so_slice(so_String, args, 2, args.len)); _++) {
        so_String arg = so_at(so_String, so_slice(so_String, args, 2, args.len), _);
        if (so_string_eq(arg, so_str("--dry-run")) || so_string_eq(arg, so_str("-n"))) {
            dryRun = true;
            continue;
        }
        if (so_len(arg) > 0 && so_at(so_byte, arg, 0) == '-') {
            fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: unknown flag %s\n", so_cstr(arg));
            return 2;
        }
        if (nExtras >= maxEntries) {
            fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: too many arguments\n");
            return 2;
        }
        extras[nExtras] = cleanEntryArg(arg);
        nExtras++;
    }
    if (so_string_eq(cmd, so_str("help")) || so_string_eq(cmd, so_str("--help")) || so_string_eq(cmd, so_str("-h"))) {
        fmt_Print(so_cstr(usageText));
        return 0;
    } else if (so_string_eq(cmd, so_str("version")) || so_string_eq(cmd, so_str("--version")) || so_string_eq(cmd, so_str("-V"))) {
        fmt_Printf("config-init %s\n", so_cstr(cli_Version));
        return 0;
    } else if (so_string_eq(cmd, so_str("init"))) {
        {
            so_Error err = chdirRepoRoot();
            if (err.self != NULL) {
                fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "%s\n", so_cstr(err.Error(err.self)));
                return 1;
            }
        }
        return cmdInit((mem_Allocator){.self = &arena, .Alloc = mem_Arena_Alloc, .Free = mem_Arena_Free, .Realloc = mem_Arena_Realloc}, dryRun);
    } else if (so_string_eq(cmd, so_str("migrate"))) {
        {
            so_Error err = chdirRepoRoot();
            if (err.self != NULL) {
                fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "%s\n", so_cstr(err.Error(err.self)));
                return 1;
            }
        }
        return cmdMigrate((mem_Allocator){.self = &arena, .Alloc = mem_Arena_Alloc, .Free = mem_Arena_Free, .Realloc = mem_Arena_Realloc}, so_array_slice(so_String, extras, 0, nExtras, 256), dryRun);
    }
    fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: unknown command %s (try `config-init help`)\n", so_cstr(cmd));
    return 2;
}

// -- gitignore.go --

// ensureGitignore makes sure every root symlink is ignored via an anchored
// `/name` pattern. Anchored, because the tracked copy lives at .config/name;
// without a trailing slash, because a symlink never matches directory-only
// patterns like `name/`. Returns the number of added lines, or -1 on error.
static so_int ensureGitignore(mem_Allocator a, so_Slice names, bool dryRun) {
    so_String old = so_str("");
    so_R_slice_err _res1 = os_ReadFile(a, gitignoreFile);
    so_Slice data = _res1.val;
    so_Error err = _res1.err;
    if (err.self == NULL) {
        old = so_bytes_string(data);
    }
    so_Slice lines = strings_Split(a, old, so_str("\n"));
    so_String missing[256] = {0};
    so_int nMissing = 0;
    for (so_int _ = 0; _ < so_len(names); _++) {
        so_String name = so_at(so_String, names, _);
        so_String pattern = so_string_add(so_str("/"), name);
        if (hasExactLine(lines, pattern)) {
            continue;
        }
        if (nMissing < maxEntries) {
            missing[nMissing] = pattern;
            nMissing++;
        }
    }
    // Pre-existing patterns that reach inside a migrated entry (".yarn/cache")
    // contain a slash and are therefore root-anchored, so after the move they
    // no longer match the content at .config/. Add a companion pattern for
    // each so `git add -A` doesn't start tracking previously ignored files.
    for (so_int _ = 0; _ < so_len(lines); _++) {
        so_String ln = so_at(so_String, lines, _);
        so_String t = strings_TrimSpace(ln);
        if (so_string_eq(t, so_str("")) || strings_HasPrefix(t, so_str("#"))) {
            continue;
        }
        bool neg = strings_HasPrefix(t, so_str("!"));
        so_String core = strings_TrimPrefix(t, so_str("!"));
        core = strings_TrimPrefix(core, so_str("/"));
        if (!refersInsideAny(core, names)) {
            continue;
        }
        so_String companion = so_string_add(so_str("/.config/"), core);
        if (neg) {
            companion = so_string_add(so_str("!"), companion);
        }
        if (hasExactLine(lines, companion) || containsString(so_array_slice(so_String, missing, 0, nMissing, 256), companion)) {
            continue;
        }
        if (nMissing < maxEntries) {
            missing[nMissing] = companion;
            nMissing++;
        }
    }
    if (nMissing == 0) {
        return 0;
    }
    if (dryRun) {
        for (so_int i = 0; i < nMissing; i++) {
            fmt_Printf("config-init: [dry-run] would add %s to %s\n", so_cstr(missing[i]), so_cstr(gitignoreFile));
        }
        return nMissing;
    }
    bytes_Buffer buf = bytes_NewBuffer(a, (so_Slice){0});
    bytes_Buffer_WriteString(&buf, old);
    if (so_len(old) > 0 && !strings_HasSuffix(old, so_str("\n"))) {
        bytes_Buffer_WriteByte(&buf, '\n');
    }
    if (!hasExactLine(lines, gitignoreHeader)) {
        if (so_len(old) > 0) {
            bytes_Buffer_WriteByte(&buf, '\n');
        }
        bytes_Buffer_WriteString(&buf, gitignoreHeader);
        bytes_Buffer_WriteByte(&buf, '\n');
    }
    for (so_int i = 0; i < nMissing; i++) {
        bytes_Buffer_WriteString(&buf, missing[i]);
        bytes_Buffer_WriteByte(&buf, '\n');
    }
    {
        so_Error werr = os_WriteFile(gitignoreFile, bytes_Buffer_Bytes(&buf), 0644);
        if (werr.self != NULL) {
            fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: cannot write %s: %s\n", so_cstr(gitignoreFile), so_cstr(werr.Error(werr.self)));
            return -1;
        }
    }
    fmt_Printf("config-init: added %d entries to %s\n", nMissing, so_cstr(gitignoreFile));
    return nMissing;
}

// refersInsideAny reports whether the gitignore pattern core (leading "!"
// and "/" already stripped) points inside one of the managed entries, like
// ".yarn/cache" does for ".yarn".
static bool refersInsideAny(so_String core, so_Slice names) {
    for (so_int _ = 0; _ < so_len(names); _++) {
        so_String name = so_at(so_String, names, _);
        if (so_len(core) > so_len(name) && strings_HasPrefix(core, name) && so_at(so_byte, core, so_len(name)) == '/') {
            return true;
        }
    }
    return false;
}

// -- init.go --

// cmdInit symlinks every entry of .config/ into the repository root.
// A missing .config/ is a no-op so the hook is harmless in repos that
// don't use config-init yet.
static so_int cmdInit(mem_Allocator a, bool dryRun) {
    os_FileInfoResult _res1 = os_Lstat(configDir);
    os_FileInfo fi = _res1.val;
    so_Error err = _res1.err;
    if (err.self != NULL) {
        return 0;
    }
    if (!os_FileInfo_IsDir(&fi)) {
        fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: %s exists but is not a directory\n", so_cstr(configDir));
        return 1;
    }
    so_R_slice_err _res2 = os_ReadDir(a, configDir);
    so_Slice entries = _res2.val;
    so_Error rerr = _res2.err;
    if (rerr.self != NULL) {
        fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: cannot read %s: %s\n", so_cstr(configDir), so_cstr(rerr.Error(rerr.self)));
        return 1;
    }
    so_int fails = 0;
    for (so_int _ = 0; _ < so_len(entries); _++) {
        os_DirEntry e = so_at(os_DirEntry, entries, _);
        linkResult r = ensureLink(e.Name, dryRun);
        if (r == linkConflict || r == linkError) {
            fails++;
        }
    }
    if (fails > 0) {
        return 1;
    }
    return 0;
}

// ensureLink makes the root entry `name` a symlink to .config/name.
// A correct link is left alone; a stale or wrong link is replaced; a real
// file or directory is never touched.
static linkResult ensureLink(so_String name, bool dryRun) {
    so_String target = so_string_add(so_string_add(configDir, so_str("/")), name);
    linkResult result = linkCreated;
    os_FileInfoResult _res1 = os_Lstat(name);
    os_FileInfo fi = _res1.val;
    so_Error err = _res1.err;
    if (err.self == NULL) {
        if ((os_FileInfo_Mode(&fi) & os_ModeSymlink) == 0) {
            fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: %s already exists and is not a symlink; skipping (remove it or move it into %s/)\n", so_cstr(name), so_cstr(configDir));
            return linkConflict;
        }
        so_byte buf[4096] = {0};
        so_R_str_err _res2 = os_Readlink(so_array_slice(so_byte, buf, 0, 4096, 4096), name);
        so_String dest = _res2.val;
        so_Error rerr = _res2.err;
        if (rerr.self == NULL && so_string_eq(dest, target)) {
            return linkOK;
        }
        result = linkRelinked;
        if (!dryRun) {
            if (os_Remove(name).self != NULL) {
                fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: cannot remove stale link %s\n", so_cstr(name));
                return linkError;
            }
        }
    }
    if (dryRun) {
        fmt_Printf("config-init: [dry-run] would link %s -> %s\n", so_cstr(name), so_cstr(target));
        return result;
    }
    {
        so_Error serr = os_Symlink(target, name);
        if (serr.self != NULL) {
            fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: cannot link %s -> %s: %s\n", so_cstr(name), so_cstr(target), so_cstr(serr.Error(serr.self)));
            return linkError;
        }
    }
    fmt_Printf("config-init: linked %s -> %s\n", so_cstr(name), so_cstr(target));
    return result;
}

// -- migrate.go --

// cmdMigrate moves root config entries into .config/, links them back,
// gitignores the links, and registers the pre-commit hook. With explicit
// entries it migrates exactly those; otherwise it scans the root for
// dotfiles that aren't protected, skipped, or already symlinks.
static so_int cmdMigrate(mem_Allocator a, so_Slice explicit, bool dryRun) {
    os_FileInfoResult _res1 = os_Lstat(configDir);
    os_FileInfo fi = _res1.val;
    so_Error err = _res1.err;
    if (err.self != NULL) {
        if (dryRun) {
            fmt_Printf("config-init: [dry-run] would create %s/\n", so_cstr(configDir));
        } else {
            {
                so_Error merr = os_Mkdir(configDir, 0755);
                if (merr.self != NULL) {
                    fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: cannot create %s: %s\n", so_cstr(configDir), so_cstr(merr.Error(merr.self)));
                    return 1;
                }
            }
            fmt_Printf("config-init: created %s/\n", so_cstr(configDir));
        }
    } else if (!os_FileInfo_IsDir(&fi)) {
        fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: %s exists but is not a directory\n", so_cstr(configDir));
        return 1;
    }
    so_String migrated[256] = {0};
    so_int nMigrated = 0;
    so_int fails = 0;
    if (so_len(explicit) > 0) {
        for (so_int _ = 0; _ < so_len(explicit); _++) {
            so_String name = so_at(so_String, explicit, _);
            moveResult r = migrateEntry(name, dryRun, true);
            if (r == moveFailed) {
                fails++;
            } else if (r == moveMoved && nMigrated < maxEntries) {
                migrated[nMigrated] = name;
                nMigrated++;
            }
        }
    } else {
        so_R_slice_err _res2 = os_ReadDir(a, so_str("."));
        so_Slice entries = _res2.val;
        so_Error rerr = _res2.err;
        if (rerr.self != NULL) {
            fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: cannot read repository root: %s\n", so_cstr(rerr.Error(rerr.self)));
            return 1;
        }
        for (so_int _ = 0; _ < so_len(entries); _++) {
            os_DirEntry e = so_at(os_DirEntry, entries, _);
            if (!strings_HasPrefix(e.Name, so_str("."))) {
                continue;
            }
            if ((e.Type & os_ModeSymlink) != 0) {
                continue;
            }
            if (containsString(protectedNames, e.Name) || containsString(skipByDefault, e.Name)) {
                continue;
            }
            moveResult r = migrateEntry(e.Name, dryRun, false);
            if (r == moveFailed) {
                fails++;
            } else if (r == moveMoved && nMigrated < maxEntries) {
                migrated[nMigrated] = e.Name;
                nMigrated++;
            }
        }
    }
    // The ignore and link passes cover everything in .config/, not just this
    // run's moves, so a partially migrated repo self-heals. In dry-run mode
    // the moves didn't happen, so add them by hand.
    so_String names[256] = {0};
    so_int nNames = 0;
    so_R_slice_err _res3 = os_ReadDir(a, configDir);
    so_Slice entries = _res3.val;
    so_Error rerr = _res3.err;
    if (rerr.self == NULL) {
        for (so_int _ = 0; _ < so_len(entries); _++) {
            os_DirEntry e = so_at(os_DirEntry, entries, _);
            if (nNames < maxEntries) {
                names[nNames] = e.Name;
                nNames++;
            }
        }
    } else if (!dryRun) {
        fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: cannot read %s: %s\n", so_cstr(configDir), so_cstr(rerr.Error(rerr.self)));
        return 1;
    }
    if (dryRun) {
        for (so_int i = 0; i < nMigrated; i++) {
            if (!containsString(so_array_slice(so_String, names, 0, nNames, 256), migrated[i]) && nNames < maxEntries) {
                names[nNames] = migrated[i];
                nNames++;
            }
        }
    }
    if (nNames == 0 && nMigrated == 0) {
        fmt_Printf("config-init: nothing to migrate\n");
        return 0;
    }
    if (ensureGitignore(a, so_array_slice(so_String, names, 0, nNames, 256), dryRun) < 0) {
        fails++;
    }
    for (so_int i = 0; i < nNames; i++) {
        // In a dry run the moves above didn't happen, so entries planned for
        // migration still exist at the root; a real run links them after the
        // move, so don't report them as conflicts here.
        if (dryRun && containsString(so_array_slice(so_String, migrated, 0, nMigrated, 256), names[i])) {
            fmt_Printf("config-init: [dry-run] would link %s -> %s/%s\n", so_cstr(names[i]), so_cstr(configDir), so_cstr(names[i]));
            continue;
        }
        linkResult r = ensureLink(names[i], dryRun);
        if (r == linkConflict || r == linkError) {
            fails++;
        }
    }
    if (ensurePreCommitConfig(a, dryRun) < 0) {
        fails++;
    }
    if (fails > 0) {
        fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: completed with %d problems\n", fails);
        return 1;
    }
    if (dryRun) {
        fmt_Printf("config-init: dry run complete; re-run without --dry-run to apply\n");
        return 0;
    }
    fmt_Print("config-init: done. Suggested follow-up:\n", "  git add -A && git commit\n", "  pre-commit install  # installs post-checkout/post-merge hooks too\n");
    return 0;
}

// migrateEntry moves one root entry into .config/. The symlink back is
// created later by the shared link pass.
static moveResult migrateEntry(so_String name, bool dryRun, bool explicit) {
    if (so_string_eq(name, so_str(".")) || so_string_eq(name, so_str("..")) || strings_IndexByte(name, '/') >= 0) {
        fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: %s is not a root-level entry name\n", so_cstr(name));
        return moveFailed;
    }
    if (explicit && containsString(protectedNames, name)) {
        fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: %s must stay at the repository root; skipping\n", so_cstr(name));
        return moveFailed;
    }
    os_FileInfoResult _res1 = os_Lstat(name);
    os_FileInfo fi = _res1.val;
    so_Error err = _res1.err;
    if (err.self != NULL) {
        if (explicit) {
            fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: %s not found\n", so_cstr(name));
            return moveFailed;
        }
        return moveSkipped;
    }
    if ((os_FileInfo_Mode(&fi) & os_ModeSymlink) != 0) {
        return moveSkipped;
    }
    so_String dest = so_string_add(so_string_add(configDir, so_str("/")), name);
    {
        os_FileInfoResult _res2 = os_Lstat(dest);
        so_Error derr = _res2.err;
        if (derr.self == NULL) {
            fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: both %s and %s exist; resolve manually\n", so_cstr(name), so_cstr(dest));
            return moveFailed;
        }
    }
    if (dryRun) {
        fmt_Printf("config-init: [dry-run] would move %s -> %s\n", so_cstr(name), so_cstr(dest));
        return moveMoved;
    }
    {
        so_Error rerr = os_Rename(name, dest);
        if (rerr.self != NULL) {
            fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: cannot move %s -> %s: %s\n", so_cstr(name), so_cstr(dest), so_cstr(rerr.Error(rerr.self)));
            return moveFailed;
        }
    }
    fmt_Printf("config-init: moved %s -> %s\n", so_cstr(name), so_cstr(dest));
    return moveMoved;
}

// -- precommit.go --

// ensurePreCommitConfig registers the config-init hook in the repository's
// pre-commit config, creating the file if needed. Edits are plain text: the
// repo block is inserted before the first existing `- repo:` entry, reusing
// its indentation. Returns 1 if changed, 0 if already configured, -1 on error.
static so_int ensurePreCommitConfig(mem_Allocator a, bool dryRun) {
    so_String path = preCommitYAML;
    so_R_slice_err _res1 = os_ReadFile(a, path);
    so_Slice data = _res1.val;
    so_Error err = _res1.err;
    if (err.self != NULL) {
        so_R_slice_err _res2 = os_ReadFile(a, preCommitYML);
        so_Slice data2 = _res2.val;
        so_Error err2 = _res2.err;
        if (err2.self == NULL) {
            path = preCommitYML;
            data = data2;
            err = (so_Error){0};
        }
    }
    if (err.self != NULL) {
        if (dryRun) {
            fmt_Printf("config-init: [dry-run] would create %s with the config-init hook\n", so_cstr(preCommitYAML));
            return 1;
        }
        {
            so_Error werr = os_WriteFile(preCommitYAML, so_string_bytes(freshPreCommitConfig), 0644);
            if (werr.self != NULL) {
                fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: cannot write %s: %s\n", so_cstr(preCommitYAML), so_cstr(werr.Error(werr.self)));
                return -1;
            }
        }
        fmt_Printf("config-init: created %s\n", so_cstr(preCommitYAML));
        return 1;
    }
    so_String content = so_bytes_string(data);
    if (strings_Contains(content, hookRepoURL) || strings_Contains(content, so_str("id: config-init"))) {
        return 0;
    }
    if (dryRun) {
        fmt_Printf("config-init: [dry-run] would add the config-init hook to %s\n", so_cstr(path));
        return 1;
    }
    so_Slice lines = strings_Split(a, content, so_str("\n"));
    // Where to insert default_install_hook_types: the top of the document,
    // unless the key is already there.
    so_int insertTypesAt = 0;
    if (strings_Contains(content, hookTypesKey)) {
        insertTypesAt = -1;
    } else if (so_len(lines) > 0 && so_string_eq(strings_TrimSpace(so_at(so_String, lines, 0)), so_str("---"))) {
        insertTypesAt = 1;
    }
    // Where to insert the repo block: before the first `- repo:` entry,
    // reusing its indentation, or right after a bare `repos:` key.
    so_int insertRepoAt = -1;
    so_int reposIdx = -1;
    so_String indent = so_str("  ");
    for (so_int i = 0; i < so_len(lines); i++) {
        so_String ln = so_at(so_String, lines, i);
        so_String t = strings_TrimSpace(ln);
        if (reposIdx == -1 && strings_HasPrefix(t, so_str("repos:")) && !strings_HasPrefix(ln, so_str(" ")) && !strings_HasPrefix(ln, so_str("\t"))) {
            reposIdx = i;
            continue;
        }
        if (reposIdx != -1 && strings_HasPrefix(t, so_str("- repo:"))) {
            insertRepoAt = i;
            so_int dash = strings_IndexByte(ln, '-');
            if (dash > 0) {
                indent = so_string_slice(ln, 0, dash);
            }
            break;
        }
    }
    if (insertRepoAt == -1 && reposIdx != -1) {
        insertRepoAt = reposIdx + 1;
    }
    bytes_Buffer buf = bytes_NewBuffer(a, (so_Slice){0});
    for (so_int i = 0; i < so_len(lines); i++) {
        so_String ln = so_at(so_String, lines, i);
        if (i == insertTypesAt) {
            bytes_Buffer_WriteString(&buf, hookTypesLine);
            bytes_Buffer_WriteByte(&buf, '\n');
        }
        if (i == insertRepoAt) {
            writeRepoBlock(&buf, indent);
        }
        bytes_Buffer_WriteString(&buf, ln);
        if (i < so_len(lines) - 1) {
            bytes_Buffer_WriteByte(&buf, '\n');
        }
    }
    if (insertRepoAt == -1) {
        // No repos: key anywhere; append one at the end.
        if (so_len(content) > 0 && !strings_HasSuffix(content, so_str("\n"))) {
            bytes_Buffer_WriteByte(&buf, '\n');
        }
        bytes_Buffer_WriteString(&buf, so_str("repos:\n"));
        writeRepoBlock(&buf, so_str("  "));
    }
    {
        so_Error werr = os_WriteFile(path, bytes_Buffer_Bytes(&buf), 0644);
        if (werr.self != NULL) {
            fmt_Fprintf((io_Writer){.self = os_Stderr, .Write = os_File_Write}, "config-init: cannot write %s: %s\n", so_cstr(path), so_cstr(werr.Error(werr.self)));
            return -1;
        }
    }
    fmt_Printf("config-init: added the config-init hook to %s\n", so_cstr(path));
    return 1;
}

static void writeRepoBlock(bytes_Buffer* buf, so_String indent) {
    bytes_Buffer_WriteString(buf, indent);
    bytes_Buffer_WriteString(buf, so_string_add(so_string_add(so_str("- repo: "), hookRepoURL), so_str("\n")));
    bytes_Buffer_WriteString(buf, indent);
    bytes_Buffer_WriteString(buf, so_string_add(so_string_add(so_str("  rev: "), hookRev), so_str("\n")));
    bytes_Buffer_WriteString(buf, indent);
    bytes_Buffer_WriteString(buf, so_str("  hooks:\n"));
    bytes_Buffer_WriteString(buf, indent);
    bytes_Buffer_WriteString(buf, so_str("    - id: config-init\n"));
}

// -- repo.go --

// chdirRepoRoot walks up from the current directory until it finds a .git
// entry (directory, or file for worktrees/submodules) and makes that
// directory the working directory. Every other path in the program is
// relative to the repo root.
static so_Error chdirRepoRoot(void) {
    so_byte wdBuf[4096] = {0};
    for (so_int _i = 0; _i < 256; _i++) {
        {
            os_FileInfoResult _res1 = os_Lstat(so_str(".git"));
            so_Error err = _res1.err;
            if (err.self == NULL) {
                return (so_Error){0};
            }
        }
        so_R_str_err _res2 = os_Getwd(so_array_slice(so_byte, wdBuf, 0, 4096, 4096));
        so_String wd = _res2.val;
        so_Error err = _res2.err;
        if (err.self != NULL || so_string_eq(wd, so_str("/"))) {
            return errNotRepo;
        }
        {
            so_Error err2 = os_Chdir(so_str(".."));
            if (err2.self != NULL) {
                return errNotRepo;
            }
        }
    }
    return errNotRepo;
}

// -- util.go --

// cleanEntryArg normalizes a user-supplied entry path like "./.claude/" to ".claude".
static so_String cleanEntryArg(so_String arg) {
    so_String s = strings_TrimPrefix(arg, so_str("./"));
    s = strings_TrimSuffix(s, so_str("/"));
    return s;
}

static bool containsString(so_Slice list, so_String s) {
    for (so_int _ = 0; _ < so_len(list); _++) {
        so_String v = so_at(so_String, list, _);
        if (so_string_eq(v, s)) {
            return true;
        }
    }
    return false;
}

// hasExactLine reports whether any line equals s once trimmed of whitespace.
static bool hasExactLine(so_Slice lines, so_String s) {
    for (so_int _ = 0; _ < so_len(lines); _++) {
        so_String ln = so_at(so_String, lines, _);
        if (so_string_eq(strings_TrimSpace(ln), s)) {
            return true;
        }
    }
    return false;
}
