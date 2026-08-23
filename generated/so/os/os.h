#pragma once
#include "so/builtin/builtin.h"
#include <dirent.h>
#include <fcntl.h>
#include "so/c/c.h"
#include "so/errors/errors.h"
#include "so/io/io.h"
#include "so/mem/mem.h"
#include "so/slices/slices.h"
#include "so/strings/strings.h"
#include "so/time/time.h"

// -- Embeds --

#include "so/builtin/builtin.h"

#ifndef so_build_hosted
#error "os: hosted environment required"
#endif

// -- Types --

typedef struct os_File os_File;
typedef struct os_DirEntry os_DirEntry;
typedef struct os_FileInfo os_FileInfo;

// File represents an open file descriptor.
typedef struct os_File {
    FILE* fd;
    so_String name;
    bool closed;
} os_File;

// FileMode represents a file's mode and permission bits.
// The bits have the same definition on all systems, so that
// information about files can be moved from one system
// to another portably.
typedef uint32_t os_FileMode;

// A DirEntry is an entry read from a directory (using the [ReadDir] function).
typedef struct os_DirEntry {
    so_String Name;
    bool IsDir;
    os_FileMode Type;
} os_DirEntry;

// A FileInfo describes a file and is returned by [Stat] and [Lstat].
typedef struct os_FileInfo {
    so_String name;
    int64_t size;
    os_FileMode mode;
    time_Time modTime;
    uint64_t dev;
    uint64_t ino;
} os_FileInfo;

// -- Result types --

typedef struct os_FileResult {
    os_File val;
    so_Error err;
} os_FileResult;

typedef struct os_FileInfoResult {
    os_FileInfo val;
    so_Error err;
} os_FileInfoResult;

// -- Variables and constants --
extern os_File* os_Stdin;
extern os_File* os_Stdout;
extern os_File* os_Stderr;

// The defined file mode bits are the most significant bits of the FileMode.
static const os_FileMode os_ModeDir = ((os_FileMode)1 << (32 - 1 - 0));
static const os_FileMode os_ModeSymlink = ((os_FileMode)1 << (32 - 1 - 1));
static const os_FileMode os_ModeNamedPipe = ((os_FileMode)1 << (32 - 1 - 2));
static const os_FileMode os_ModeSocket = ((os_FileMode)1 << (32 - 1 - 3));
static const os_FileMode os_ModeDevice = ((os_FileMode)1 << (32 - 1 - 4));
static const os_FileMode os_ModeCharDevice = ((os_FileMode)1 << (32 - 1 - 5));
static const os_FileMode os_ModeSetuid = ((os_FileMode)1 << (32 - 1 - 6));
static const os_FileMode os_ModeSetgid = ((os_FileMode)1 << (32 - 1 - 7));
static const os_FileMode os_ModeSticky = ((os_FileMode)1 << (32 - 1 - 8));
static const os_FileMode os_ModeIrregular = ((os_FileMode)1 << (32 - 1 - 9));

// ModePerm is the Unix permission bits.
static const os_FileMode os_ModePerm = 0777;

// MaxPathLen is the maximum length of a path.
static const int64_t os_MaxPathLen = 4096;

// MaxNameLen is the maximum length of a filename.
static const int64_t os_MaxNameLen = 256;

// IO-related errors that can be returned by functions in this package.
extern so_Error os_ErrClosed;
extern so_Error os_ErrExist;
extern so_Error os_ErrIsDir;
extern so_Error os_ErrNotDir;
extern so_Error os_ErrNotExist;
extern so_Error os_ErrPermission;

// ErrIO is a generic I/O error that is returned when the error
// does not match any of the other, more specific errors.
extern so_Error os_ErrIO;

// -- Functions and methods --

// ReadDir reads the named directory, returning
// all its directory entries sorted by filename.
// If an error occurs reading the directory, returns the entries it was
// able to read before the error, along with the error.
//
// If the allocator is nil, uses the system allocator.
// The returned slice and entry names are allocated; the caller owns them.
// Use [FreeDirEntry] to free the result.
so_R_slice_err os_ReadDir(mem_Allocator a, so_String name);

// FreeDirEntry frees a slice of DirEntry previously returned by [ReadDir].
// It frees each entry's Name string and the slice itself.
//
// If the allocator is nil, uses the system allocator.
void os_FreeDirEntry(mem_Allocator a, so_Slice entries);

// Getenv retrieves the value of the environment variable named by the key.
// It returns the value, which will be empty if the variable is not present.
// The returned string is a view into the static environment buffer;
// the caller must not modify or free it.
so_String os_Getenv(so_String key);

// LookupEnv retrieves the value of the environment variable named
// by the key. If the variable is present in the environment the
// value (which may be empty) is returned and the boolean is true.
// Otherwise the returned value will be empty and the boolean will
// be false.
//
// The returned string is a view into the static environment buffer;
// the caller must not modify or free it.
so_R_str_bool os_LookupEnv(so_String key);

// Setenv sets the value of the environment variable named by the key.
// It returns an error, if any.
so_Error os_Setenv(so_String key, so_String value);

// Unsetenv unsets a single environment variable.
so_Error os_Unsetenv(so_String key);

// Create creates or truncates the named file. If the file already exists,
// it is truncated. If the file does not exist, it is created with mode 0o666
// (before umask). If successful, methods on the returned File can
// be used for I/O; the associated file descriptor has mode O_RDWR.
// The directory containing the file must already exist.
os_FileResult os_Create(so_String name);

// Open opens the named file for reading. If successful, methods on
// the returned file can be used for reading; the associated file
// descriptor has mode O_RDONLY.
os_FileResult os_Open(so_String name);

// OpenFile is the generalized open call; most users will use Open
// or Create instead. It opens the named file with specified flag
// ([O_RDONLY] etc.). If the file does not exist, and the [O_CREATE] flag
// is passed, it is created with mode perm (before umask);
// the containing directory must exist. If successful,
// methods on the returned File can be used for I/O.
os_FileResult os_OpenFile(so_String name, so_int flag, os_FileMode perm);

// Name returns the name of the file as presented to Open/Create.
so_String os_File_Name(void* self);

// Read reads up to len(b) bytes from the file and stores them in b.
// It returns the number of bytes read and any error encountered.
// At end of file, Read returns 0, io.EOF.
so_R_int_err os_File_Read(void* self, so_Slice b);

// Write writes len(b) bytes from b to the file.
// It returns the number of bytes written and an error, if any.
// Write returns a non-nil error when n != len(b).
so_R_int_err os_File_Write(void* self, so_Slice b);

// Seek sets the offset for the next Read or Write on file to offset,
// interpreted according to whence: [io.SeekStart] means relative to
// the start of the file, [io.SeekCurrent] means relative to the current
// offset, and [io.SeekEnd] means relative to the end.
so_R_i64_err os_File_Seek(void* self, int64_t offset, so_int whence);

// ReadAt reads len(b) bytes from the file starting at byte offset off.
// It returns the number of bytes read and the error, if any.
// ReadAt always returns a non-nil error when n < len(b).
so_R_int_err os_File_ReadAt(void* self, so_Slice b, int64_t off);

// WriteAt writes len(b) bytes to the file starting at byte offset off.
// It returns the number of bytes written and an error, if any.
so_R_int_err os_File_WriteAt(void* self, so_Slice b, int64_t off);

// WriteString is like Write, but writes the contents of string s
// rather than a slice of bytes.
so_R_int_err os_File_WriteString(void* self, so_String s);

// Close closes the file, rendering it unusable for I/O.
// Close will return an error if it has already been called.
so_Error os_File_Close(void* self);

// ReadFile reads the named file and returns the contents.
// A successful call returns err == nil, not err == EOF.
// Because ReadFile reads the whole file, it does not treat
// an EOF from Read as an error to be reported.
//
// If the allocator is nil, uses the system allocator.
// The returned slice is allocated; the caller owns it.
so_R_slice_err os_ReadFile(mem_Allocator a, so_String name);

// WriteFile writes data to the named file, creating it if necessary.
// If the file does not exist, WriteFile creates it with permissions perm (before umask);
// otherwise WriteFile truncates it before writing, without changing permissions.
//
// Since WriteFile requires multiple system calls to complete, a failure mid-operation
// can leave the file in a partially written state.
so_Error os_WriteFile(so_String name, so_Slice data, os_FileMode perm);

// IsDir reports whether m describes a directory.
bool os_FileMode_IsDir(os_FileMode m);

// IsRegular reports whether m describes a regular file.
bool os_FileMode_IsRegular(os_FileMode m);

// Perm returns the Unix permission bits in m.
os_FileMode os_FileMode_Perm(os_FileMode m);

// Chdir changes the current working directory to the named directory.
so_Error os_Chdir(so_String dir);

// Chmod changes the mode of the named file to mode.
// If the file is a symbolic link, it changes the mode of the link's target.
so_Error os_Chmod(so_String name, os_FileMode mode);

// Chown changes the numeric uid and gid of the named file.
// If the file is a symbolic link, it changes the uid and gid of the link's target.
// A uid or gid of -1 means to not change that value.
so_Error os_Chown(so_String name, so_int uid, so_int gid);

// Chtimes changes the access and modification times of the named
// file, similar to the Unix utime() or utimes() functions.
// A zero [time.Time] value will leave the corresponding file time unchanged.
so_Error os_Chtimes(so_String name, time_Time atime, time_Time mtime);

// Lchown changes the numeric uid and gid of the named file.
// If the file is a symbolic link, it changes the uid and gid of the link itself.
so_Error os_Lchown(so_String name, so_int uid, so_int gid);

// Link creates newname as a hard link to the oldname file.
so_Error os_Link(so_String oldname, so_String newname);

// Mkdir creates a new directory with the specified name and permission
// bits (before umask).
so_Error os_Mkdir(so_String name, os_FileMode perm);

// Readlink returns the destination of the named symbolic link.
// If the link destination is relative, Readlink returns the relative path
// without resolving it to an absolute one.
//
// Writes the result into buf. Panics if buf is empty.
// The returned string is a view into buf.
so_R_str_err os_Readlink(so_Slice buf, so_String name);

// Remove removes the named file or (empty) directory.
so_Error os_Remove(so_String name);

// Rename renames (moves) oldpath to newpath. If newpath already exists
// and is not a directory, Rename replaces it. If newpath already exists
// and is a directory, Rename returns an error. OS-specific restrictions
// may apply when oldpath and newpath are in different directories.
// Even within the same directory, on non-Unix platforms Rename
// is not an atomic operation.
so_Error os_Rename(so_String oldpath, so_String newpath);

// SameFile reports whether fi1 and fi2 describe the same file.
// For example, on Unix this means that the device and inode fields
// of the two underlying structures are identical.
bool os_SameFile(os_FileInfo fi1, os_FileInfo fi2);

// Symlink creates newname as a symbolic link to oldname.
so_Error os_Symlink(so_String oldname, so_String newname);

// Truncate changes the size of the named file.
// If the file is a symbolic link, it changes the size of the link's target.
so_Error os_Truncate(so_String name, int64_t size);

// Getegid returns the numeric effective group id of the caller.
so_int os_Getegid(void);

// Geteuid returns the numeric effective user id of the caller.
so_int os_Geteuid(void);

// Getgid returns the numeric group id of the caller.
so_int os_Getgid(void);

// Getpid returns the process id of the caller.
so_int os_Getpid(void);

// Getppid returns the process id of the caller's parent.
so_int os_Getppid(void);

// Getuid returns the numeric user id of the caller.
so_int os_Getuid(void);

// Getwd returns an absolute path name corresponding to the
// current directory.
//
// Writes the result into buf. Panics if buf is empty.
// The returned string is a view into buf.
so_R_str_err os_Getwd(so_Slice buf);

// Hostname returns the host name reported by the kernel.
//
// Writes the result into buf. Panics if buf is empty.
// The returned string is a view into buf.
so_R_str_err os_Hostname(so_Slice buf);

// Exit causes the current program to exit with the given status code.
// Conventionally, code zero indicates success, non-zero an error.
void os_Exit(so_int code);

// Name returns the base name of the file.
so_String os_FileInfo_Name(void* self);

// Size returns the length in bytes for regular files; system-dependent for others.
int64_t os_FileInfo_Size(void* self);

// Mode returns the file mode bits.
os_FileMode os_FileInfo_Mode(void* self);

// ModTime returns the modification time.
time_Time os_FileInfo_ModTime(void* self);

// IsDir reports whether the file is a directory.
bool os_FileInfo_IsDir(void* self);

// Stat returns a [FileInfo] describing the named file.
os_FileInfoResult os_Stat(so_String name);

// Lstat returns a [FileInfo] describing the named file.
// If the file is a symbolic link, the returned FileInfo
// describes the symbolic link. Lstat makes no attempt to follow the link.
os_FileInfoResult os_Lstat(so_String name);

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
os_FileResult os_CreateTemp(so_Slice buf, so_String dir, so_String pattern);

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
so_R_str_err os_MkdirTemp(so_Slice buf, so_String dir, so_String pattern);

// TempDir returns the default directory to use for temporary files.
// On Unix systems, it returns $TMPDIR if non-empty, else /tmp.
// The directory is neither guaranteed to exist nor have accessible permissions.
so_String os_TempDir(void);
