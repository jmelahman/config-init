#include "os.h"
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

// -- Embeds --

//go:build ignore
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include "so/builtin/builtin.h"

// Stat result - flat struct filled by C helpers.
typedef struct {
    int64_t size;
    uint32_t mode;
    int64_t modSec;
    int64_t modNsec;
    uint64_t dev;
    uint64_t ino;
    bool ok;
} os_statResult;

// os_lstat fills result from lstat().
static os_statResult os_lstat(const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0) return (os_statResult){.ok = false};
    return (os_statResult){
        .size = st.st_size,
        .mode = st.st_mode,
        .modSec = st.st_mtime,
        .modNsec = 0,  // fields differ on macos and linux, set to 0 for now
        .dev = st.st_dev,
        .ino = st.st_ino,
        .ok = true,
    };
}

// os_stat fills result from stat().
static os_statResult os_stat(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return (os_statResult){.ok = false};
    return (os_statResult){
        .size = st.st_size,
        .mode = st.st_mode,
        .modSec = st.st_mtime,
        .modNsec = 0,  // fields differ on macos and linux, set to 0 for now
        .dev = st.st_dev,
        .ino = st.st_ino,
        .ok = true,
    };
}

// os_utimens sets access and modification times using utimensat.
// A tv_nsec of UTIME_OMIT leaves the corresponding time unchanged.
static int os_utimens(const char* path, int64_t asec, int64_t ansec, int64_t msec, int64_t mnsec) {
    struct timespec times[2] = {
        {.tv_sec = asec, .tv_nsec = ansec},
        {.tv_sec = msec, .tv_nsec = mnsec},
    };
    return utimensat(AT_FDCWD, path, times, 0);
}

// readdir result - one entry at a time.
typedef struct {
    int32_t nameLen;
    uint8_t dtype;
    bool ok;
} os_readdirResult;

// os_readdir_next reads the next directory entry.
// Copies d_name into buf. Returns {nameLen, dtype, ok}.
static os_readdirResult os_readdir_next(DIR* dir, char* buf, size_t bufsize) {
    errno = 0;
    struct dirent* ent = readdir(dir);
    if (ent == NULL) return (os_readdirResult){.ok = false};
    size_t n = strlen(ent->d_name);
    if (n >= bufsize) n = bufsize - 1;
    memcpy(buf, ent->d_name, n);
    buf[n] = '\0';
    return (os_readdirResult){.nameLen = (int32_t)n, .dtype = ent->d_type, .ok = true};
}

// -- Types --

typedef struct dtypeModeResult dtypeModeResult;

// dtypeModeResult holds the result of dtypeToMode.
typedef struct dtypeModeResult {
    bool isDir;
    os_FileMode mode;
} dtypeModeResult;

// -- Forward declarations --
static so_R_slice_err readDir(mem_Allocator a, so_String name);
static so_int compareEntry(void* a, void* b);
static dtypeModeResult dtypeToMode(uint8_t dtype);
static so_String fdopenMode(so_int flag);
static mode_t makePosixMode(os_FileMode fmode);
static os_FileMode mode_t_toFileMode(mode_t m);
static so_Error mapError(void);
static so_String baseName(so_String path);
static so_Slice buildTempTemplate(so_Slice buf, so_String dir, so_String pattern);

// -- Variables and constants --

// Standard input, output, and error streams.
static os_File stdin_ = {0};
os_File* os_Stdin = NULL;
static os_File stdout_ = {0};
os_File* os_Stdout = NULL;
static os_File stderr_ = {0};
os_File* os_Stderr = NULL;

// IO-related errors that can be returned by functions in this package.
so_Error os_ErrClosed = errors_New("os: file already closed");
so_Error os_ErrExist = errors_New("os: file already exists");
so_Error os_ErrIsDir = errors_New("os: is a directory");
so_Error os_ErrNotDir = errors_New("os: not a directory");
so_Error os_ErrNotExist = errors_New("os: no such file or directory");
so_Error os_ErrPermission = errors_New("os: permission denied");

// ErrIO is a generic I/O error that is returned when the error
// does not match any of the other, more specific errors.
so_Error os_ErrIO = errors_New("os: i/o error");

// -- dir.go --

// ReadDir reads the named directory, returning
// all its directory entries sorted by filename.
// If an error occurs reading the directory, returns the entries it was
// able to read before the error, along with the error.
//
// If the allocator is nil, uses the system allocator.
// The returned slice and entry names are allocated; the caller owns them.
// Use [FreeDirEntry] to free the result.
so_R_slice_err os_ReadDir(mem_Allocator a, so_String name) {
    so_R_slice_err _res1 = readDir(a, name);
    so_Slice entries = _res1.val;
    so_Error err = _res1.err;
    slices_SortFunc(os_DirEntry, (entries), (compareEntry));
    return (so_R_slice_err){.val = entries, .err = err};
}

// FreeDirEntry frees a slice of DirEntry previously returned by [ReadDir].
// It frees each entry's Name string and the slice itself.
//
// If the allocator is nil, uses the system allocator.
void os_FreeDirEntry(mem_Allocator a, so_Slice entries) {
    for (so_int i = 0; i < so_len(entries); i++) {
        mem_FreeString(a, so_at(os_DirEntry, entries, i).Name);
    }
    slices_Free(os_DirEntry, (a), (entries));
}

// readDir reads the named directory and returns
// all its directory entries without sorting.
static so_R_slice_err readDir(mem_Allocator a, so_String name) {
    DIR* dir = opendir(so_cstr(name));
    if (dir == NULL) {
        return (so_R_slice_err){.val = (so_Slice){0}, .err = mapError()};
    }
    so_Slice entries = slices_MakeCap(os_DirEntry, (a), (0), (16));
    so_byte nameBuf[256] = {0};
    char* cname = (char*)(&nameBuf[0]);
    for (;;) {
        os_readdirResult r = os_readdir_next(dir, cname, os_MaxNameLen);
        if (!r.ok) {
            break;
        }
        so_String entryName = unsafe_String((so_byte*)(cname), (so_int)(r.nameLen));
        // Skip "." and "..".
        if (so_string_eq(entryName, so_str(".")) || so_string_eq(entryName, so_str(".."))) {
            continue;
        }
        dtypeModeResult dm = dtypeToMode(r.dtype);
        bool isDir = dm.isDir;
        os_FileMode mode = dm.mode;
        // DT_UNKNOWN: fall back to Lstat.
        if (r.dtype == DT_UNKNOWN) {
            os_FileInfoResult _res1 = os_Lstat(so_string_add(so_string_add(name, so_str("/")), entryName));
            os_FileInfo fi = _res1.val;
            so_Error err = _res1.err;
            if (err.self == NULL) {
                isDir = os_FileInfo_IsDir(&fi);
                mode = (os_FileInfo_Mode(&fi) & ((((((os_ModeDir | os_ModeSymlink) | os_ModeNamedPipe) | os_ModeSocket) | os_ModeDevice) | os_ModeCharDevice) | os_ModeIrregular));
            }
        }
        so_String clonedName = strings_Clone(a, entryName);
        entries = slices_Append(os_DirEntry, (a), (entries), ((os_DirEntry){.Name = clonedName, .IsDir = isDir, .Type = mode}));
    }
    // Check for read error (errno set by readdir).
    so_int readErr = errno;
    closedir(dir);
    if (readErr != 0) {
        // restore errno from readdir
        errno = readErr;
        return (so_R_slice_err){.val = entries, .err = mapError()};
    }
    return (so_R_slice_err){.val = entries, .err = (so_Error){0}};
}

// compareEntry compares two DirEntry values by their name for sorting.
static so_int compareEntry(void* a, void* b) {
    os_DirEntry* e1 = c_PtrAs(os_DirEntry, (a));
    os_DirEntry* e2 = c_PtrAs(os_DirEntry, (b));
    return strings_Compare(e1->Name, e2->Name);
}

// dtypeToMode converts a dirent d_type value to isDir and FileMode type bits.
static dtypeModeResult dtypeToMode(uint8_t dtype) {
    if (dtype == (DT_DIR)) {
        return (dtypeModeResult){.isDir = true, .mode = os_ModeDir};
    } else if (dtype == (DT_LNK)) {
        return (dtypeModeResult){.mode = os_ModeSymlink};
    } else if (dtype == (DT_FIFO)) {
        return (dtypeModeResult){.mode = os_ModeNamedPipe};
    } else if (dtype == (DT_SOCK)) {
        return (dtypeModeResult){.mode = os_ModeSocket};
    } else if (dtype == (DT_BLK)) {
        return (dtypeModeResult){.mode = os_ModeDevice};
    } else if (dtype == (DT_CHR)) {
        return (dtypeModeResult){.mode = os_ModeCharDevice};
    } else {
        return (dtypeModeResult){};
    }
}

// -- env.go --

// Getenv retrieves the value of the environment variable named by the key.
// It returns the value, which will be empty if the variable is not present.
// The returned string is a view into the static environment buffer;
// the caller must not modify or free it.
so_String os_Getenv(so_String key) {
    char* ptr = getenv(so_cstr(key));
    if (ptr == NULL) {
        return so_str("");
    }
    return c_String(char, (ptr));
}

// LookupEnv retrieves the value of the environment variable named
// by the key. If the variable is present in the environment the
// value (which may be empty) is returned and the boolean is true.
// Otherwise the returned value will be empty and the boolean will
// be false.
//
// The returned string is a view into the static environment buffer;
// the caller must not modify or free it.
so_R_str_bool os_LookupEnv(so_String key) {
    char* ptr = getenv(so_cstr(key));
    if (ptr == NULL) {
        return (so_R_str_bool){.val = so_str(""), .val2 = false};
    }
    return (so_R_str_bool){.val = c_String(char, (ptr)), .val2 = true};
}

// Setenv sets the value of the environment variable named by the key.
// It returns an error, if any.
so_Error os_Setenv(so_String key, so_String value) {
    if (setenv(so_cstr(key), so_cstr(value), 1) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// Unsetenv unsets a single environment variable.
so_Error os_Unsetenv(so_String key) {
    if (unsetenv(so_cstr(key)) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// -- extern.go --

// -- file.go --

// Create creates or truncates the named file. If the file already exists,
// it is truncated. If the file does not exist, it is created with mode 0o666
// (before umask). If successful, methods on the returned File can
// be used for I/O; the associated file descriptor has mode O_RDWR.
// The directory containing the file must already exist.
os_FileResult os_Create(so_String name) {
    FILE* fd = fopen(so_cstr(name), "w+b");
    if (fd == NULL) {
        return (os_FileResult){.val = (os_File){}, .err = mapError()};
    }
    return (os_FileResult){.val = (os_File){.fd = fd, .name = name}, .err = (so_Error){0}};
}

// Open opens the named file for reading. If successful, methods on
// the returned file can be used for reading; the associated file
// descriptor has mode O_RDONLY.
os_FileResult os_Open(so_String name) {
    FILE* fd = fopen(so_cstr(name), "rb");
    if (fd == NULL) {
        return (os_FileResult){.val = (os_File){}, .err = mapError()};
    }
    return (os_FileResult){.val = (os_File){.fd = fd, .name = name}, .err = (so_Error){0}};
}

// OpenFile is the generalized open call; most users will use Open
// or Create instead. It opens the named file with specified flag
// ([O_RDONLY] etc.). If the file does not exist, and the [O_CREATE] flag
// is passed, it is created with mode perm (before umask);
// the containing directory must exist. If successful,
// methods on the returned File can be used for I/O.
os_FileResult os_OpenFile(so_String name, so_int flag, os_FileMode perm) {
    mode_t pmode = makePosixMode(perm);
    int fd = open(so_cstr(name), (int)(flag), (int)(pmode));
    if (fd < 0) {
        return (os_FileResult){.val = (os_File){}, .err = mapError()};
    }
    so_String mode = fdopenMode(flag);
    FILE* fp = fdopen(fd, so_cstr(mode));
    if (fp == NULL) {
        close(fd);
        return (os_FileResult){.val = (os_File){}, .err = mapError()};
    }
    return (os_FileResult){.val = (os_File){.fd = fp, .name = name}, .err = (so_Error){0}};
}

// Name returns the name of the file as presented to Open/Create.
so_String os_File_Name(void* self) {
    os_File* f = self;
    return f->name;
}

// Read reads up to len(b) bytes from the file and stores them in b.
// It returns the number of bytes read and any error encountered.
// At end of file, Read returns 0, io.EOF.
so_R_int_err os_File_Read(void* self, so_Slice b) {
    os_File* f = self;
    if (so_len(b) == 0) {
        return (so_R_int_err){.val = 0, .err = (so_Error){0}};
    }
    so_int n = (so_int)(fread(unsafe_SliceData(b), 1, (uintptr_t)(so_len(b)), f->fd));
    if (n < so_len(b)) {
        if (ferror(f->fd)) {
            return (so_R_int_err){.val = n, .err = mapError()};
        }
        if (n == 0) {
            return (so_R_int_err){.val = 0, .err = io_EOF};
        }
    }
    return (so_R_int_err){.val = n, .err = (so_Error){0}};
}

// Write writes len(b) bytes from b to the file.
// It returns the number of bytes written and an error, if any.
// Write returns a non-nil error when n != len(b).
so_R_int_err os_File_Write(void* self, so_Slice b) {
    os_File* f = self;
    if (so_len(b) == 0) {
        return (so_R_int_err){.val = 0, .err = (so_Error){0}};
    }
    so_int n = (so_int)(fwrite(unsafe_SliceData(b), 1, (uintptr_t)(so_len(b)), f->fd));
    if (n < so_len(b)) {
        return (so_R_int_err){.val = n, .err = mapError()};
    }
    return (so_R_int_err){.val = n, .err = (so_Error){0}};
}

// Seek sets the offset for the next Read or Write on file to offset,
// interpreted according to whence: [io.SeekStart] means relative to
// the start of the file, [io.SeekCurrent] means relative to the current
// offset, and [io.SeekEnd] means relative to the end.
so_R_i64_err os_File_Seek(void* self, int64_t offset, so_int whence) {
    os_File* f = self;
    if (fseeko(f->fd, offset, (int)(whence)) != 0) {
        return (so_R_i64_err){.val = 0, .err = mapError()};
    }
    int64_t pos = ftello(f->fd);
    if (pos < 0) {
        return (so_R_i64_err){.val = 0, .err = mapError()};
    }
    return (so_R_i64_err){.val = pos, .err = (so_Error){0}};
}

// ReadAt reads len(b) bytes from the file starting at byte offset off.
// It returns the number of bytes read and the error, if any.
// ReadAt always returns a non-nil error when n < len(b).
so_R_int_err os_File_ReadAt(void* self, so_Slice b, int64_t off) {
    os_File* f = self;
    if (off < 0) {
        return (so_R_int_err){.val = 0, .err = io_ErrOffset};
    }
    int64_t cur = ftello(f->fd);
    if (cur < 0) {
        return (so_R_int_err){.val = 0, .err = mapError()};
    }
    if (fseeko(f->fd, off, io_SeekStart) != 0) {
        return (so_R_int_err){.val = 0, .err = mapError()};
    }
    so_R_int_err _res1 = os_File_Read(f, b);
    so_int n = _res1.val;
    so_Error err = _res1.err;
    if (fseeko(f->fd, cur, io_SeekStart) != 0 && err.self == NULL) {
        return (so_R_int_err){.val = n, .err = mapError()};
    }
    if (n < so_len(b) && err.self == NULL) {
        err = io_EOF;
    }
    return (so_R_int_err){.val = n, .err = err};
}

// WriteAt writes len(b) bytes to the file starting at byte offset off.
// It returns the number of bytes written and an error, if any.
so_R_int_err os_File_WriteAt(void* self, so_Slice b, int64_t off) {
    os_File* f = self;
    if (off < 0) {
        return (so_R_int_err){.val = 0, .err = io_ErrOffset};
    }
    int64_t cur = ftello(f->fd);
    if (cur < 0) {
        return (so_R_int_err){.val = 0, .err = mapError()};
    }
    if (fseeko(f->fd, off, io_SeekStart) != 0) {
        return (so_R_int_err){.val = 0, .err = mapError()};
    }
    so_R_int_err _res1 = os_File_Write(f, b);
    so_int n = _res1.val;
    so_Error err = _res1.err;
    if (fseeko(f->fd, cur, io_SeekStart) != 0 && err.self == NULL) {
        return (so_R_int_err){.val = n, .err = mapError()};
    }
    return (so_R_int_err){.val = n, .err = err};
}

// WriteString is like Write, but writes the contents of string s
// rather than a slice of bytes.
so_R_int_err os_File_WriteString(void* self, so_String s) {
    os_File* f = self;
    return os_File_Write(f, so_string_bytes(s));
}

// Close closes the file, rendering it unusable for I/O.
// Close will return an error if it has already been called.
so_Error os_File_Close(void* self) {
    os_File* f = self;
    if (f->closed) {
        return os_ErrClosed;
    }
    if (fclose(f->fd) != 0) {
        return mapError();
    }
    f->closed = true;
    return (so_Error){0};
}

// ReadFile reads the named file and returns the contents.
// A successful call returns err == nil, not err == EOF.
// Because ReadFile reads the whole file, it does not treat
// an EOF from Read as an error to be reported.
//
// If the allocator is nil, uses the system allocator.
// The returned slice is allocated; the caller owns it.
so_R_slice_err os_ReadFile(mem_Allocator a, so_String name) {
    os_FileResult _res1 = os_Open(name);
    os_File f = _res1.val;
    so_Error err = _res1.err;
    if (err.self != NULL) {
        return (so_R_slice_err){.val = (so_Slice){0}, .err = err};
    }
    so_R_slice_err _res2 = io_ReadAll(a, (io_Reader){.self = &f, .Read = os_File_Read});
    so_Slice b = _res2.val;
    err = _res2.err;
    os_File_Close(&f);
    return (so_R_slice_err){.val = b, .err = err};
}

// WriteFile writes data to the named file, creating it if necessary.
// If the file does not exist, WriteFile creates it with permissions perm (before umask);
// otherwise WriteFile truncates it before writing, without changing permissions.
//
// Since WriteFile requires multiple system calls to complete, a failure mid-operation
// can leave the file in a partially written state.
so_Error os_WriteFile(so_String name, so_Slice data, os_FileMode perm) {
    os_FileResult _res1 = os_OpenFile(name, ((O_WRONLY | O_CREAT) | O_TRUNC), perm);
    os_File f = _res1.val;
    so_Error err = _res1.err;
    if (err.self != NULL) {
        return err;
    }
    so_R_int_err _res2 = os_File_Write(&f, data);
    err = _res2.err;
    so_Error closeErr = os_File_Close(&f);
    if (err.self != NULL) {
        return err;
    }
    return closeErr;
}

// fdopenMode returns the fdopen mode string for the given open flags.
static so_String fdopenMode(so_int flag) {
    if ((flag & ((O_RDONLY | O_WRONLY) | O_RDWR)) == (O_WRONLY)) {
        if ((flag & O_APPEND) != 0) {
            return so_str("ab");
        }
        return so_str("wb");
    } else if ((flag & ((O_RDONLY | O_WRONLY) | O_RDWR)) == (O_RDWR)) {
        if ((flag & O_APPEND) != 0) {
            return so_str("a+b");
        }
        if ((flag & O_TRUNC) != 0) {
            return so_str("w+b");
        }
        return so_str("r+b");
    } else {
        return so_str("rb");
    }
}

// -- fmode.go --

// IsDir reports whether m describes a directory.
bool os_FileMode_IsDir(os_FileMode m) {
    return (m & os_ModeDir) != 0;
}

// IsRegular reports whether m describes a regular file.
bool os_FileMode_IsRegular(os_FileMode m) {
    return (m & ((((((os_ModeDir | os_ModeSymlink) | os_ModeNamedPipe) | os_ModeSocket) | os_ModeDevice) | os_ModeCharDevice) | os_ModeIrregular)) == 0;
}

// Perm returns the Unix permission bits in m.
os_FileMode os_FileMode_Perm(os_FileMode m) {
    return (m & os_ModePerm);
}

// makePosixMode converts Go FileMode bits to POSIX mode_t bits.
static mode_t makePosixMode(os_FileMode fmode) {
    mode_t pmode = (mode_t)(fmode & 0777);
    if ((fmode & os_ModeSetuid) != 0) {
        pmode |= S_ISUID;
    }
    if ((fmode & os_ModeSetgid) != 0) {
        pmode |= S_ISGID;
    }
    if ((fmode & os_ModeSticky) != 0) {
        pmode |= S_ISVTX;
    }
    return pmode;
}

// toFileMode converts POSIX mode_t bits to Go FileMode bits.
// Go FileMode layout: high bits are type/special, low 9 bits are permissions.
static os_FileMode mode_t_toFileMode(mode_t m) {
    // permission bits pass through
    os_FileMode fmode = (os_FileMode)(m & 0777);
    if ((m & S_IFMT) == (S_IFDIR)) {
        fmode |= os_ModeDir;
    } else if ((m & S_IFMT) == (S_IFLNK)) {
        fmode |= os_ModeSymlink;
    } else if ((m & S_IFMT) == (S_IFIFO)) {
        fmode |= os_ModeNamedPipe;
    } else if ((m & S_IFMT) == (S_IFSOCK)) {
        fmode |= os_ModeSocket;
    } else if ((m & S_IFMT) == (S_IFBLK)) {
        fmode |= os_ModeDevice;
    } else if ((m & S_IFMT) == (S_IFCHR)) {
        fmode |= os_ModeCharDevice;
    } else if ((m & S_IFMT) == (S_IFREG)) {
    }
    if ((m & S_ISUID) != 0) {
        fmode |= os_ModeSetuid;
    }
    if ((m & S_ISGID) != 0) {
        fmode |= os_ModeSetgid;
    }
    if ((m & S_ISVTX) != 0) {
        fmode |= os_ModeSticky;
    }
    return fmode;
}

// -- os.go --

// Chdir changes the current working directory to the named directory.
so_Error os_Chdir(so_String dir) {
    if (chdir(so_cstr(dir)) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// Chmod changes the mode of the named file to mode.
// If the file is a symbolic link, it changes the mode of the link's target.
so_Error os_Chmod(so_String name, os_FileMode mode) {
    mode_t pmode = makePosixMode(mode);
    if (chmod(so_cstr(name), pmode) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// Chown changes the numeric uid and gid of the named file.
// If the file is a symbolic link, it changes the uid and gid of the link's target.
// A uid or gid of -1 means to not change that value.
so_Error os_Chown(so_String name, so_int uid, so_int gid) {
    if (chown(so_cstr(name), (uid_t)(uid), (gid_t)(gid)) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// Chtimes changes the access and modification times of the named
// file, similar to the Unix utime() or utimes() functions.
// A zero [time.Time] value will leave the corresponding file time unchanged.
so_Error os_Chtimes(so_String name, time_Time atime, time_Time mtime) {
    int64_t asec = 0, ansec = 0, msec = 0, mnsec = 0;
    if (time_Time_IsZero(atime)) {
        ansec = UTIME_OMIT;
    } else {
        asec = time_Time_Unix(atime);
        ansec = (int64_t)(time_Time_Nanosecond(atime));
    }
    if (time_Time_IsZero(mtime)) {
        mnsec = UTIME_OMIT;
    } else {
        msec = time_Time_Unix(mtime);
        mnsec = (int64_t)(time_Time_Nanosecond(mtime));
    }
    if (os_utimens(so_cstr(name), asec, ansec, msec, mnsec) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// Lchown changes the numeric uid and gid of the named file.
// If the file is a symbolic link, it changes the uid and gid of the link itself.
so_Error os_Lchown(so_String name, so_int uid, so_int gid) {
    if (lchown(so_cstr(name), (uid_t)(uid), (gid_t)(gid)) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// Link creates newname as a hard link to the oldname file.
so_Error os_Link(so_String oldname, so_String newname) {
    if (link(so_cstr(oldname), so_cstr(newname)) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// Mkdir creates a new directory with the specified name and permission
// bits (before umask).
so_Error os_Mkdir(so_String name, os_FileMode perm) {
    mode_t pmode = makePosixMode(perm);
    if (mkdir(so_cstr(name), pmode) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// Readlink returns the destination of the named symbolic link.
// If the link destination is relative, Readlink returns the relative path
// without resolving it to an absolute one.
//
// Writes the result into buf. Panics if buf is empty.
// The returned string is a view into buf.
so_R_str_err os_Readlink(so_Slice buf, so_String name) {
    so_byte* bufPtr = unsafe_SliceData(buf);
    if (bufPtr == NULL) {
        so_panic("os: empty buffer");
    }
    so_int n = readlink(so_cstr(name), (char*)(bufPtr), (uintptr_t)(so_len(buf)));
    if (n < 0) {
        return (so_R_str_err){.val = so_str(""), .err = mapError()};
    }
    return (so_R_str_err){.val = unsafe_String(bufPtr, n), .err = (so_Error){0}};
}

// Remove removes the named file or (empty) directory.
so_Error os_Remove(so_String name) {
    if (remove(so_cstr(name)) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// Rename renames (moves) oldpath to newpath. If newpath already exists
// and is not a directory, Rename replaces it. If newpath already exists
// and is a directory, Rename returns an error. OS-specific restrictions
// may apply when oldpath and newpath are in different directories.
// Even within the same directory, on non-Unix platforms Rename
// is not an atomic operation.
so_Error os_Rename(so_String oldpath, so_String newpath) {
    if (rename(so_cstr(oldpath), so_cstr(newpath)) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// SameFile reports whether fi1 and fi2 describe the same file.
// For example, on Unix this means that the device and inode fields
// of the two underlying structures are identical.
bool os_SameFile(os_FileInfo fi1, os_FileInfo fi2) {
    return fi1.dev == fi2.dev && fi1.ino == fi2.ino;
}

// Symlink creates newname as a symbolic link to oldname.
so_Error os_Symlink(so_String oldname, so_String newname) {
    if (symlink(so_cstr(oldname), so_cstr(newname)) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// Truncate changes the size of the named file.
// If the file is a symbolic link, it changes the size of the link's target.
so_Error os_Truncate(so_String name, int64_t size) {
    if (truncate(so_cstr(name), size) != 0) {
        return mapError();
    }
    return (so_Error){0};
}

// mapError maps errno to a sentinel error.
static so_Error mapError(void) {
    if (errno == EACCES) {
        return os_ErrPermission;
    }
    if (errno == EEXIST) {
        return os_ErrExist;
    }
    if (errno == EISDIR) {
        return os_ErrIsDir;
    }
    if (errno == ENOENT) {
        return os_ErrNotExist;
    }
    if (errno == ENOTDIR) {
        return os_ErrNotDir;
    }
    if (errno == EPERM) {
        return os_ErrPermission;
    }
    return os_ErrIO;
}

// -- posix.go --

// -- proc.go --

// Getegid returns the numeric effective group id of the caller.
so_int os_Getegid(void) {
    gid_t gid = getegid();
    return (so_int)(gid);
}

// Geteuid returns the numeric effective user id of the caller.
so_int os_Geteuid(void) {
    uid_t uid = geteuid();
    return (so_int)(uid);
}

// Getgid returns the numeric group id of the caller.
so_int os_Getgid(void) {
    gid_t gid = getgid();
    return (so_int)(gid);
}

// Getpid returns the process id of the caller.
so_int os_Getpid(void) {
    pid_t pid = getpid();
    return (so_int)(pid);
}

// Getppid returns the process id of the caller's parent.
so_int os_Getppid(void) {
    pid_t ppid = getppid();
    return (so_int)(ppid);
}

// Getuid returns the numeric user id of the caller.
so_int os_Getuid(void) {
    uid_t uid = getuid();
    return (so_int)(uid);
}

// Getwd returns an absolute path name corresponding to the
// current directory.
//
// Writes the result into buf. Panics if buf is empty.
// The returned string is a view into buf.
so_R_str_err os_Getwd(so_Slice buf) {
    so_byte* bufPtr = unsafe_SliceData(buf);
    if (bufPtr == NULL) {
        so_panic("os: empty buffer");
    }
    char* cwd = getcwd((char*)(bufPtr), (uintptr_t)(so_len(buf)));
    if (cwd == NULL) {
        return (so_R_str_err){.val = so_str(""), .err = mapError()};
    }
    return (so_R_str_err){.val = c_String(char, (cwd)), .err = (so_Error){0}};
}

// Hostname returns the host name reported by the kernel.
//
// Writes the result into buf. Panics if buf is empty.
// The returned string is a view into buf.
so_R_str_err os_Hostname(so_Slice buf) {
    char* name = (char*)(unsafe_SliceData(buf));
    if (name == NULL) {
        so_panic("os: empty buffer");
    }
    if (gethostname(name, (uintptr_t)(so_len(buf))) != 0) {
        return (so_R_str_err){.val = so_str(""), .err = mapError()};
    }
    return (so_R_str_err){.val = c_String(char, (name)), .err = (so_Error){0}};
}

// Exit causes the current program to exit with the given status code.
// Conventionally, code zero indicates success, non-zero an error.
void os_Exit(so_int code) {
    exit((int)(code));
}

// -- stat.go --

// Name returns the base name of the file.
so_String os_FileInfo_Name(void* self) {
    os_FileInfo* fi = self;
    return fi->name;
}

// Size returns the length in bytes for regular files; system-dependent for others.
int64_t os_FileInfo_Size(void* self) {
    os_FileInfo* fi = self;
    return fi->size;
}

// Mode returns the file mode bits.
os_FileMode os_FileInfo_Mode(void* self) {
    os_FileInfo* fi = self;
    return fi->mode;
}

// ModTime returns the modification time.
time_Time os_FileInfo_ModTime(void* self) {
    os_FileInfo* fi = self;
    return fi->modTime;
}

// IsDir reports whether the file is a directory.
bool os_FileInfo_IsDir(void* self) {
    os_FileInfo* fi = self;
    return os_FileMode_IsDir(fi->mode);
}

// baseName returns the last element of the path.
static so_String baseName(so_String path) {
    so_int i = so_len(path) - 1;
    // Strip trailing slashes.
    for (; i > 0 && so_at(so_byte, path, i) == '/';) {
        i--;
    }
    so_int end = i + 1;
    // Find the last slash.
    for (; i >= 0 && so_at(so_byte, path, i) != '/';) {
        i--;
    }
    if (end == 0) {
        return so_str(".");
    }
    return so_string_slice(path, i + 1, end);
}

// Stat returns a [FileInfo] describing the named file.
os_FileInfoResult os_Stat(so_String name) {
    os_statResult r = os_stat(so_cstr(name));
    if (!r.ok) {
        return (os_FileInfoResult){.val = (os_FileInfo){}, .err = mapError()};
    }
    os_FileMode fmode = mode_t_toFileMode((mode_t)(r.mode));
    return (os_FileInfoResult){.val = (os_FileInfo){.name = baseName(name), .size = r.size, .mode = fmode, .modTime = time_Unix(r.modSec, r.modNsec), .dev = r.dev, .ino = r.ino}, .err = (so_Error){0}};
}

// Lstat returns a [FileInfo] describing the named file.
// If the file is a symbolic link, the returned FileInfo
// describes the symbolic link. Lstat makes no attempt to follow the link.
os_FileInfoResult os_Lstat(so_String name) {
    os_statResult r = os_lstat(so_cstr(name));
    if (!r.ok) {
        return (os_FileInfoResult){.val = (os_FileInfo){}, .err = mapError()};
    }
    os_FileMode fmode = mode_t_toFileMode((mode_t)(r.mode));
    return (os_FileInfoResult){.val = (os_FileInfo){.name = baseName(name), .size = r.size, .mode = fmode, .modTime = time_Unix(r.modSec, r.modNsec), .dev = r.dev, .ino = r.ino}, .err = (so_Error){0}};
}

// -- temp.go --

// CreateTemp creates a new temporary file in the directory dir,
// opens the file for reading and writing, and returns the resulting file.
// The filename is generated by taking pattern and adding a random string to the end.
// The file is created with mode 0o600 (before umask).
//
// If dir is the empty string, CreateTemp uses the default directory
// for temporary files, as returned by [TempDir].
// The caller can use the file's Name method to find the pathname of the file.
// It is the caller's responsibility to remove the file when it is no longer needed.
//
// Writes the path of the created file into buf. Panics if buf is empty.
// The name in the returned File is a view into buf.
os_FileResult os_CreateTemp(so_Slice buf, so_String dir, so_String pattern) {
    so_Slice tmpl = buildTempTemplate(so_slice(so_byte, buf, 0, 0), dir, pattern);
    char* tmplPtr = (char*)(unsafe_SliceData(tmpl));
    if (tmplPtr == NULL) {
        so_panic("os: empty buffer");
    }
    int fd = mkstemp(tmplPtr);
    if (fd < 0) {
        return (os_FileResult){.val = (os_File){}, .err = mapError()};
    }
    FILE* fp = fdopen(fd, "w+b");
    if (fp == NULL) {
        close(fd);
        return (os_FileResult){.val = (os_File){}, .err = mapError()};
    }
    so_String name = so_bytes_string(so_slice(so_byte, tmpl, 0, so_len(tmpl) - 1));
    return (os_FileResult){.val = (os_File){.fd = fp, .name = name}, .err = (so_Error){0}};
}

// MkdirTemp creates a new temporary directory in the directory dir
// and returns the pathname of the new directory.
// The new directory's name is generated by adding a random string to the end of pattern.
// The directory is created with mode 0o700 (before umask).
//
// If dir is the empty string, MkdirTemp uses the default directory
// for temporary files, as returned by TempDir.
// It is the caller's responsibility to remove the directory when it is no longer needed.
//
// Writes the path of the created directory into buf. Panics if buf is empty.
// The returned string is a view into buf.
so_R_str_err os_MkdirTemp(so_Slice buf, so_String dir, so_String pattern) {
    so_Slice tmpl = buildTempTemplate(so_slice(so_byte, buf, 0, 0), dir, pattern);
    char* tmplPtr = (char*)(unsafe_SliceData(tmpl));
    if (tmplPtr == NULL) {
        so_panic("os: empty buffer");
    }
    char* result = mkdtemp(tmplPtr);
    if (result == NULL) {
        return (so_R_str_err){.val = so_str(""), .err = mapError()};
    }
    return (so_R_str_err){.val = so_bytes_string(so_slice(so_byte, tmpl, 0, so_len(tmpl) - 1)), .err = (so_Error){0}};
}

// buildTempTemplate builds a null-terminated template string for mkstemp/mkdtemp.
// The template ends with "XXXXXX" as required by these functions.
static so_Slice buildTempTemplate(so_Slice buf, so_String dir, so_String pattern) {
    if (so_string_eq(dir, so_str(""))) {
        dir = os_TempDir();
    }
    buf = so_extend(so_byte, buf, so_string_bytes(dir));
    if (so_len(dir) > 0 && so_at(so_byte, dir, so_len(dir) - 1) != '/') {
        buf = so_append(so_byte, buf, '/');
    }
    buf = so_extend(so_byte, buf, so_string_bytes(pattern));
    buf = so_extend(so_byte, buf, so_string_bytes(so_str("XXXXXX")));
    // null terminator
    buf = so_append(so_byte, buf, 0);
    return buf;
}

// TempDir returns the default directory to use for temporary files.
// On Unix systems, it returns $TMPDIR if non-empty, else /tmp.
// The directory is neither guaranteed to exist nor have accessible permissions.
so_String os_TempDir(void) {
    so_String dir = os_Getenv(so_str("TMPDIR"));
    if (so_string_ne(dir, so_str(""))) {
        return dir;
    }
    return so_str("/tmp");
}

static void __attribute__((constructor)) os_init() {
    stdin_ = (os_File){.fd = stdin, .name = so_str("/dev/stdin")};
    os_Stdin = &stdin_;
    stdout_ = (os_File){.fd = stdout, .name = so_str("/dev/stdout")};
    os_Stdout = &stdout_;
    stderr_ = (os_File){.fd = stderr, .name = so_str("/dev/stderr")};
    os_Stderr = &stderr_;
}
