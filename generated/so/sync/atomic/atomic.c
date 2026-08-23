#include "atomic.h"

// -- atomic.go --

// -- bool.go --

// Load atomically loads and returns the value stored in x.
bool atomic_Bool_Load(void* self) {
    atomic_Bool* x = self;
    return so_atomic_load(bool, (&x->v));
}

// Store atomically stores val into x.
void atomic_Bool_Store(void* self, bool val) {
    atomic_Bool* x = self;
    so_atomic_store(bool, (&x->v), (val));
}

// Swap atomically stores new into x and returns the previous value.
bool atomic_Bool_Swap(void* self, bool new) {
    atomic_Bool* x = self;
    return so_atomic_swap(bool, (&x->v), (new));
}

// CompareAndSwap atomically sets x to new if it currently holds old,
// reporting whether the swap happened.
bool atomic_Bool_CompareAndSwap(void* self, bool old, bool new) {
    atomic_Bool* x = self;
    return so_atomic_cas(bool, (&x->v), (old), (new));
}

// -- extern.go --

// -- int32.go --

// Load atomically loads and returns the value stored in x.
int32_t atomic_Int32_Load(void* self) {
    atomic_Int32* x = self;
    return so_atomic_load(int32_t, (&x->v));
}

// Store atomically stores val into x.
void atomic_Int32_Store(void* self, int32_t val) {
    atomic_Int32* x = self;
    so_atomic_store(int32_t, (&x->v), (val));
}

// Add atomically adds delta to x and returns the new value.
int32_t atomic_Int32_Add(void* self, int32_t delta) {
    atomic_Int32* x = self;
    return so_atomic_add(int32_t, (&x->v), (delta));
}

// Swap atomically stores new into x and returns the previous value.
int32_t atomic_Int32_Swap(void* self, int32_t new) {
    atomic_Int32* x = self;
    return so_atomic_swap(int32_t, (&x->v), (new));
}

// CompareAndSwap atomically sets x to new if it currently holds old,
// reporting whether the swap happened.
bool atomic_Int32_CompareAndSwap(void* self, int32_t old, int32_t new) {
    atomic_Int32* x = self;
    return so_atomic_cas(int32_t, (&x->v), (old), (new));
}

// -- int64.go --

// Load atomically loads and returns the value stored in x.
int64_t atomic_Int64_Load(void* self) {
    atomic_Int64* x = self;
    return so_atomic_load(int64_t, (&x->v));
}

// Store atomically stores val into x.
void atomic_Int64_Store(void* self, int64_t val) {
    atomic_Int64* x = self;
    so_atomic_store(int64_t, (&x->v), (val));
}

// Add atomically adds delta to x and returns the new value.
int64_t atomic_Int64_Add(void* self, int64_t delta) {
    atomic_Int64* x = self;
    return so_atomic_add(int64_t, (&x->v), (delta));
}

// Swap atomically stores new into x and returns the previous value.
int64_t atomic_Int64_Swap(void* self, int64_t new) {
    atomic_Int64* x = self;
    return so_atomic_swap(int64_t, (&x->v), (new));
}

// CompareAndSwap atomically sets x to new if it currently holds old,
// reporting whether the swap happened.
bool atomic_Int64_CompareAndSwap(void* self, int64_t old, int64_t new) {
    atomic_Int64* x = self;
    return so_atomic_cas(int64_t, (&x->v), (old), (new));
}

// -- pointer.go --

// -- uint32.go --

// Load atomically loads and returns the value stored in x.
uint32_t atomic_Uint32_Load(void* self) {
    atomic_Uint32* x = self;
    return so_atomic_load(uint32_t, (&x->v));
}

// Store atomically stores val into x.
void atomic_Uint32_Store(void* self, uint32_t val) {
    atomic_Uint32* x = self;
    so_atomic_store(uint32_t, (&x->v), (val));
}

// Add atomically adds delta to x and returns the new value.
uint32_t atomic_Uint32_Add(void* self, uint32_t delta) {
    atomic_Uint32* x = self;
    return so_atomic_add(uint32_t, (&x->v), (delta));
}

// Sub atomically subtracts delta from x and returns the new value.
uint32_t atomic_Uint32_Sub(void* self, uint32_t delta) {
    atomic_Uint32* x = self;
    return so_atomic_add(uint32_t, (&x->v), (~(delta - 1)));
}

// Swap atomically stores new into x and returns the previous value.
uint32_t atomic_Uint32_Swap(void* self, uint32_t new) {
    atomic_Uint32* x = self;
    return so_atomic_swap(uint32_t, (&x->v), (new));
}

// CompareAndSwap atomically sets x to new if it currently holds old,
// reporting whether the swap happened.
bool atomic_Uint32_CompareAndSwap(void* self, uint32_t old, uint32_t new) {
    atomic_Uint32* x = self;
    return so_atomic_cas(uint32_t, (&x->v), (old), (new));
}

// -- uint64.go --

// Load atomically loads and returns the value stored in x.
uint64_t atomic_Uint64_Load(void* self) {
    atomic_Uint64* x = self;
    return so_atomic_load(uint64_t, (&x->v));
}

// Store atomically stores val into x.
void atomic_Uint64_Store(void* self, uint64_t val) {
    atomic_Uint64* x = self;
    so_atomic_store(uint64_t, (&x->v), (val));
}

// Add atomically adds delta to x and returns the new value.
uint64_t atomic_Uint64_Add(void* self, uint64_t delta) {
    atomic_Uint64* x = self;
    return so_atomic_add(uint64_t, (&x->v), (delta));
}

// Sub atomically subtracts delta from x and returns the new value.
uint64_t atomic_Uint64_Sub(void* self, uint64_t delta) {
    atomic_Uint64* x = self;
    return so_atomic_add(uint64_t, (&x->v), (~(delta - 1)));
}

// Swap atomically stores new into x and returns the previous value.
uint64_t atomic_Uint64_Swap(void* self, uint64_t new) {
    atomic_Uint64* x = self;
    return so_atomic_swap(uint64_t, (&x->v), (new));
}

// CompareAndSwap atomically sets x to new if it currently holds old,
// reporting whether the swap happened.
bool atomic_Uint64_CompareAndSwap(void* self, uint64_t old, uint64_t new) {
    atomic_Uint64* x = self;
    return so_atomic_cas(uint64_t, (&x->v), (old), (new));
}
