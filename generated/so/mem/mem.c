#include "mem.h"

// -- Forward declarations --
static mem_Stats mem_atomicStats_get(void* self);

// -- Variables and constants --

// ErrOutOfMemory is returned when a memory allocation
// fails due to insufficient memory.
so_Error mem_ErrOutOfMemory = errors_New("out of memory");
so_Error mem_ErrNoAlloc = errors_New("mem: allocation not allowed");

// NoAlloc is an instance of an Allocator that returns an error
// on any allocation. Free is a no-op.
// It's meant for cases when allocations are strictly forbidden.
mem_Allocator mem_NoAlloc = (mem_Allocator){.self = &(mem_NoAllocator){}, .Alloc = mem_NoAllocator_Alloc, .Free = mem_NoAllocator_Free, .Realloc = mem_NoAllocator_Realloc};

// System is an instance of a memory allocator that uses
// the system's malloc, realloc, and free functions.
//
// System is thread-safe.
mem_Allocator mem_System = (mem_Allocator){.self = &(mem_SystemAllocator){}, .Alloc = mem_SystemAllocator_Alloc, .Free = mem_SystemAllocator_Free, .Realloc = mem_SystemAllocator_Realloc};

// -- allocator.go --

// -- arena.go --

// NewArena creates an arena allocator backed by the given buffer.
mem_Arena mem_NewArena(so_Slice buf) {
    return (mem_Arena){.buf = buf};
}

so_R_ptr_err mem_Arena_Alloc(void* self, so_int size, so_int align) {
    mem_Arena* a = self;
    if (size <= 0) {
        so_panic("mem: invalid allocation size");
    }
    if (align <= 0 || (align & (align - 1)) != 0) {
        so_panic("mem: invalid alignment");
    }
    so_int aligned = ((a->offset + align - 1) & ~(align - 1));
    if (aligned + size > so_len(a->buf)) {
        return (so_R_ptr_err){.val = NULL, .err = mem_ErrOutOfMemory};
    }
    so_clear(so_byte, so_slice(so_byte, a->buf, aligned, aligned + size));
    a->lastStart = aligned;
    a->offset = aligned + size;
    return (so_R_ptr_err){.val = &so_at(so_byte, a->buf, aligned), .err = (so_Error){0}};
}

so_R_ptr_err mem_Arena_Realloc(void* self, void* ptr, so_int oldSize, so_int newSize, so_int align) {
    mem_Arena* a = self;
    if (oldSize <= 0 || newSize <= 0) {
        so_panic("mem: invalid allocation size");
    }
    if (align <= 0 || (align & (align - 1)) != 0) {
        so_panic("mem: invalid alignment");
    }
    // Last allocation: resize in place.
    if (ptr == &so_at(so_byte, a->buf, a->lastStart) && a->lastStart + oldSize == a->offset) {
        so_int newEnd = a->lastStart + newSize;
        if (newEnd > so_len(a->buf)) {
            return (so_R_ptr_err){.val = NULL, .err = mem_ErrOutOfMemory};
        }
        if (newSize > oldSize) {
            so_clear(so_byte, so_slice(so_byte, a->buf, a->offset, newEnd));
        }
        a->offset = newEnd;
        return (so_R_ptr_err){.val = ptr, .err = (so_Error){0}};
    }
    // Not the last allocation, shrinking: return same pointer.
    if (newSize <= oldSize) {
        return (so_R_ptr_err){.val = ptr, .err = (so_Error){0}};
    }
    // Not the last allocation, growing: allocate new and copy.
    so_R_ptr_err _res1 = mem_Arena_Alloc(a, newSize, align);
    void* newPtr = _res1.val;
    so_Error err = _res1.err;
    if (err.self != NULL) {
        return (so_R_ptr_err){.val = NULL, .err = err};
    }
    memmove(newPtr, ptr, (uintptr_t)(oldSize));
    return (so_R_ptr_err){.val = newPtr, .err = (so_Error){0}};
}

void mem_Arena_Free(void* self, void* ptr, so_int size, so_int align) {
    mem_Arena* a = self;
    (void)align;
    // Last allocation: reclaim the space.
    if (ptr == &so_at(so_byte, a->buf, a->lastStart) && a->lastStart + size == a->offset) {
        a->offset = a->lastStart;
    }
}

// Reset reclaims all allocated memory, allowing the arena to be reused.
void mem_Arena_Reset(void* self) {
    mem_Arena* a = self;
    a->offset = 0;
    a->lastStart = 0;
}

// -- array.go --

// NewArray allocates storage for count values of vsize bytes each.
// Both vsize and count must be greater than 0.
// Call [Array.Free] exactly once when done.
mem_Array mem_NewArray(mem_Allocator alloc, so_int vsize, so_int count) {
    so_assert(vsize > 0, "mem.NewArray: vsize must be greater than 0");
    so_assert(count > 0, "mem.NewArray: count must be greater than 0");
    so_Slice vals = mem_AllocSlice(so_byte, (alloc), (count * vsize), (count * vsize));
    return (mem_Array){.alloc = alloc, .vals = vals, .vsize = vsize, .count = count};
}

// Load copies the value at index i into dst.
// dst must point to storage of at least vsize bytes.
void mem_Array_Load(void* self, so_int i, void* dst) {
    mem_Array* a = self;
    mem_Copy(dst, mem_Array_At(a, i), a->vsize);
}

// Store copies the value pointed to by v into slot i.
// v must point to storage of at least vsize bytes.
void mem_Array_Store(void* self, so_int i, void* v) {
    mem_Array* a = self;
    mem_Copy(mem_Array_At(a, i), v, a->vsize);
}

// At returns a pointer to the value at index i. The pointer stays
// valid until the slot is overwritten or [Array.Free] is called.
void* mem_Array_At(void* self, so_int i) {
    mem_Array* a = self;
    so_assert(i >= 0 && i < a->count, "mem.Array.At: index out of range");
    so_byte* vptr = unsafe_SliceData(a->vals);
    return c_PtrAdd(so_byte, (vptr), (i * a->vsize));
}

// Len returns the number of values.
so_int mem_Array_Len(void* self) {
    mem_Array* a = self;
    return a->count;
}

// Free releases the memory allocated for the values.
// After calling Free, the Array is unusable.
void mem_Array_Free(void* self) {
    mem_Array* a = self;
    mem_FreeSlice(so_byte, (a->alloc), (a->vals));
    a->vals = (so_Slice){0};
    a->count = 0;
}

// -- malloc.go --

// -- mem.go --

// FreeString frees a heap-allocated string.
// If the allocator is nil, uses the system allocator.
void mem_FreeString(mem_Allocator a, so_String s) {
    if (so_len(s) == 0) {
        return;
    }
    mem_Free(so_byte, (a), (unsafe_StringData(s)));
}

// -- noalloc.go --

so_R_ptr_err mem_NoAllocator_Alloc(void* self, so_int size, so_int align) {
    (void)self;
    (void)size;
    (void)align;
    return (so_R_ptr_err){.val = NULL, .err = mem_ErrNoAlloc};
}

so_R_ptr_err mem_NoAllocator_Realloc(void* self, void* ptr, so_int oldSize, so_int newSize, so_int align) {
    (void)self;
    (void)ptr;
    (void)oldSize;
    (void)newSize;
    (void)align;
    return (so_R_ptr_err){.val = NULL, .err = mem_ErrNoAlloc};
}

void mem_NoAllocator_Free(void* self, void* ptr, so_int size, so_int align) {
    (void)self;
    (void)ptr;
    (void)size;
    (void)align;
}

// -- system.go --

so_R_ptr_err mem_SystemAllocator_Alloc(void* self, so_int size, so_int align) {
    (void)self;
    if (size <= 0) {
        so_panic("mem: invalid allocation size");
    }
    if (align <= 0 || (align & (align - 1)) != 0) {
        so_panic("mem: invalid alignment");
    }
    void* ptr = calloc(1, (uintptr_t)(size));
    if (ptr == NULL) {
        return (so_R_ptr_err){.val = NULL, .err = mem_ErrOutOfMemory};
    }
    return (so_R_ptr_err){.val = ptr, .err = (so_Error){0}};
}

so_R_ptr_err mem_SystemAllocator_Realloc(void* self, void* ptr, so_int oldSize, so_int newSize, so_int align) {
    (void)self;
    if (oldSize <= 0 || newSize <= 0) {
        so_panic("mem: invalid allocation size");
    }
    if (align <= 0 || (align & (align - 1)) != 0) {
        so_panic("mem: invalid alignment");
    }
    void* newPtr = realloc(ptr, (uintptr_t)(newSize));
    if (newPtr == NULL) {
        return (so_R_ptr_err){.val = NULL, .err = mem_ErrOutOfMemory};
    }
    if (newSize > oldSize) {
        // Zero new memory beyond the old size.
        so_byte* p = c_PtrAdd(so_byte, ((so_byte*)newPtr), (oldSize));
        mem_Clear(p, newSize - oldSize);
    }
    return (so_R_ptr_err){.val = newPtr, .err = (so_Error){0}};
}

void mem_SystemAllocator_Free(void* self, void* ptr, so_int size, so_int align) {
    (void)self;
    (void)size;
    (void)align;
    free(ptr);
}

// -- tracker.go --

// get returns a snapshot of the current statistics.
static mem_Stats mem_atomicStats_get(void* self) {
    mem_atomicStats* s = self;
    return (mem_Stats){.Alloc = atomic_Uint64_Load(&s->alloc), .TotalAlloc = atomic_Uint64_Load(&s->totalAlloc), .Mallocs = atomic_Uint64_Load(&s->mallocs), .Frees = atomic_Uint64_Load(&s->frees)};
}

so_R_ptr_err mem_Tracker_Alloc(void* self, so_int size, so_int align) {
    mem_Tracker* t = self;
    so_R_ptr_err _res1 = t->Allocator.Alloc(t->Allocator.self, size, align);
    void* ptr = _res1.val;
    so_Error err = _res1.err;
    if (err.self != NULL) {
        return (so_R_ptr_err){.val = NULL, .err = err};
    }
    atomic_Uint64_Add(&t->stats.alloc, (uint64_t)(size));
    atomic_Uint64_Add(&t->stats.totalAlloc, (uint64_t)(size));
    atomic_Uint64_Add(&t->stats.mallocs, 1);
    return (so_R_ptr_err){.val = ptr, .err = (so_Error){0}};
}

so_R_ptr_err mem_Tracker_Realloc(void* self, void* ptr, so_int oldSize, so_int newSize, so_int align) {
    mem_Tracker* t = self;
    so_R_ptr_err _res1 = t->Allocator.Realloc(t->Allocator.self, ptr, oldSize, newSize, align);
    void* newPtr = _res1.val;
    so_Error err = _res1.err;
    if (err.self != NULL) {
        return (so_R_ptr_err){.val = NULL, .err = err};
    }
    if (newSize > oldSize) {
        atomic_Uint64_Add(&t->stats.alloc, (uint64_t)(newSize - oldSize));
        atomic_Uint64_Add(&t->stats.totalAlloc, (uint64_t)(newSize - oldSize));
    } else {
        atomic_Uint64_Sub(&t->stats.alloc, (uint64_t)(oldSize - newSize));
    }
    atomic_Uint64_Add(&t->stats.mallocs, 1);
    atomic_Uint64_Add(&t->stats.frees, 1);
    return (so_R_ptr_err){.val = newPtr, .err = (so_Error){0}};
}

void mem_Tracker_Free(void* self, void* ptr, so_int size, so_int align) {
    mem_Tracker* t = self;
    t->Allocator.Free(t->Allocator.self, ptr, size, align);
    atomic_Uint64_Sub(&t->stats.alloc, (uint64_t)(size));
    atomic_Uint64_Add(&t->stats.frees, 1);
}

// Stats returns a snapshot of the current memory statistics.
// Each counter is read atomically, but the overall snapshot is only eventually
// consistent. When accessed concurrently, the counters might not match up with
// each other.
mem_Stats mem_Tracker_Stats(void* self) {
    mem_Tracker* t = self;
    return mem_atomicStats_get(&t->stats);
}
