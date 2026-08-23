#pragma once
#include "so/builtin/builtin.h"

// -- Embeds --

#include "so/builtin/builtin.h"

// This package maps Go-style atomics onto the compiler's __atomic builtins,
// which operate on ordinary (non-_Atomic) objects. Every operation uses
// sequentially consistent ordering (__ATOMIC_SEQ_CST), matching Go's
// sync/atomic. The T macro argument is the C type of the value.

// so_atomic_load atomically loads the value at p.
#define so_atomic_load(T, p) \
    (__atomic_load_n((p), __ATOMIC_SEQ_CST))

// so_atomic_store atomically stores v at p.
#define so_atomic_store(T, p, v) \
    (__atomic_store_n((p), (v), __ATOMIC_SEQ_CST))

// so_atomic_add atomically adds delta to *p and returns the new value.
#define so_atomic_add(T, p, delta) \
    (__atomic_add_fetch((p), (delta), __ATOMIC_SEQ_CST))

// so_atomic_swap atomically stores v at p and returns the previous value.
#define so_atomic_swap(T, p, v) \
    (__atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST))

// so_atomic_cas atomically sets *p to new if it equals old,
// reporting whether the swap happened.
#define so_atomic_cas(T, p, old, new) ({                             \
    T _old = (old);                                                  \
    __atomic_compare_exchange_n((p), &_old, (new), false,            \
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
})

// atomic_Pointer is the backing store for atomic.Pointer[T]: a single machine
// pointer. T is erased to void* in C; the wrappers cast back at each access.
typedef struct {
    void* v;
} atomic_Pointer;

#define atomic_Pointer_Load(T, p) \
    ((T*)__atomic_load_n(&(p)->v, __ATOMIC_SEQ_CST))

#define atomic_Pointer_Store(T, p, val) \
    (__atomic_store_n(&(p)->v, (void*)(val), __ATOMIC_SEQ_CST))

#define atomic_Pointer_Swap(T, p, val) \
    ((T*)__atomic_exchange_n(&(p)->v, (void*)(val), __ATOMIC_SEQ_CST))

#define atomic_Pointer_CompareAndSwap(T, p, old, new) ({             \
    void* _old = (void*)(old);                                       \
    __atomic_compare_exchange_n(&(p)->v, &_old, (void*)(new), false, \
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
})

// -- Types --

typedef struct atomic_Bool atomic_Bool;
typedef struct atomic_Int32 atomic_Int32;
typedef struct atomic_Int64 atomic_Int64;
typedef struct atomic_Uint32 atomic_Uint32;
typedef struct atomic_Uint64 atomic_Uint64;

// Bool is an atomic boolean value. The zero value is false.
// Bool must not be copied after first use.
typedef struct atomic_Bool {
    bool v;
} atomic_Bool;

// Int32 is an atomic int32. The zero value is zero.
// Int32 must not be copied after first use.
typedef struct atomic_Int32 {
    int32_t v;
} atomic_Int32;

// Int64 is an atomic int64. The zero value is zero.
// Int64 must not be copied after first use.
typedef struct atomic_Int64 {
    int64_t v;
} atomic_Int64;

// Uint32 is an atomic uint32. The zero value is zero.
// Uint32 must not be copied after first use.
typedef struct atomic_Uint32 {
    uint32_t v;
} atomic_Uint32;

// Uint64 is an atomic uint64. The zero value is zero.
// Uint64 must not be copied after first use.
typedef struct atomic_Uint64 {
    uint64_t v;
} atomic_Uint64;

// -- Functions and methods --

// Load atomically loads and returns the value stored in x.
bool atomic_Bool_Load(void* self);

// Store atomically stores val into x.
void atomic_Bool_Store(void* self, bool val);

// Swap atomically stores new into x and returns the previous value.
bool atomic_Bool_Swap(void* self, bool new);

// CompareAndSwap atomically sets x to new if it currently holds old,
// reporting whether the swap happened.
bool atomic_Bool_CompareAndSwap(void* self, bool old, bool new);

// Load atomically loads and returns the value stored in x.
int32_t atomic_Int32_Load(void* self);

// Store atomically stores val into x.
void atomic_Int32_Store(void* self, int32_t val);

// Add atomically adds delta to x and returns the new value.
int32_t atomic_Int32_Add(void* self, int32_t delta);

// Swap atomically stores new into x and returns the previous value.
int32_t atomic_Int32_Swap(void* self, int32_t new);

// CompareAndSwap atomically sets x to new if it currently holds old,
// reporting whether the swap happened.
bool atomic_Int32_CompareAndSwap(void* self, int32_t old, int32_t new);

// Load atomically loads and returns the value stored in x.
int64_t atomic_Int64_Load(void* self);

// Store atomically stores val into x.
void atomic_Int64_Store(void* self, int64_t val);

// Add atomically adds delta to x and returns the new value.
int64_t atomic_Int64_Add(void* self, int64_t delta);

// Swap atomically stores new into x and returns the previous value.
int64_t atomic_Int64_Swap(void* self, int64_t new);

// CompareAndSwap atomically sets x to new if it currently holds old,
// reporting whether the swap happened.
bool atomic_Int64_CompareAndSwap(void* self, int64_t old, int64_t new);

// Load atomically loads and returns the value stored in x.
uint32_t atomic_Uint32_Load(void* self);

// Store atomically stores val into x.
void atomic_Uint32_Store(void* self, uint32_t val);

// Add atomically adds delta to x and returns the new value.
uint32_t atomic_Uint32_Add(void* self, uint32_t delta);

// Sub atomically subtracts delta from x and returns the new value.
uint32_t atomic_Uint32_Sub(void* self, uint32_t delta);

// Swap atomically stores new into x and returns the previous value.
uint32_t atomic_Uint32_Swap(void* self, uint32_t new);

// CompareAndSwap atomically sets x to new if it currently holds old,
// reporting whether the swap happened.
bool atomic_Uint32_CompareAndSwap(void* self, uint32_t old, uint32_t new);

// Load atomically loads and returns the value stored in x.
uint64_t atomic_Uint64_Load(void* self);

// Store atomically stores val into x.
void atomic_Uint64_Store(void* self, uint64_t val);

// Add atomically adds delta to x and returns the new value.
uint64_t atomic_Uint64_Add(void* self, uint64_t delta);

// Sub atomically subtracts delta from x and returns the new value.
uint64_t atomic_Uint64_Sub(void* self, uint64_t delta);

// Swap atomically stores new into x and returns the previous value.
uint64_t atomic_Uint64_Swap(void* self, uint64_t new);

// CompareAndSwap atomically sets x to new if it currently holds old,
// reporting whether the swap happened.
bool atomic_Uint64_CompareAndSwap(void* self, uint64_t old, uint64_t new);
