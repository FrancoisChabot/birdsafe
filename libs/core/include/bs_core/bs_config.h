/**
 * @file bs_config.h
 * @brief Platform configuration, architecture assertions, and compiler attributes.
 */

#ifndef BS_CORE_BS_CONFIG_H
#define BS_CORE_BS_CONFIG_H

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ========================================================================= */
/* Concurrency & Synchronization Primitives                                  */
/* ========================================================================= */

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
typedef SRWLOCK bs_mutex_t;
#define bs_mutex_init(m) InitializeSRWLock(m)
#define bs_mutex_lock(m) AcquireSRWLockExclusive(m)
#define bs_mutex_unlock(m) ReleaseSRWLockExclusive(m)
#define bs_mutex_destroy(m) ((void)(m))
#else
#include <pthread.h>
typedef pthread_mutex_t bs_mutex_t;
#define bs_mutex_init(m) pthread_mutex_init((m), NULL)
#define bs_mutex_lock(m) pthread_mutex_lock(m)
#define bs_mutex_unlock(m) pthread_mutex_unlock(m)
#define bs_mutex_destroy(m) pthread_mutex_destroy(m)
#endif

#if defined(__cplusplus)
#include <atomic>
typedef std::atomic<uint32_t> bs_atomic_uint32_t;
typedef std::atomic<uint64_t> bs_atomic_uint64_t;
typedef std::atomic<size_t>   bs_atomic_size_t;
typedef std::atomic<void *>   bs_atomic_ptr_t;

#define bs_atomic_init(ptr, val) (ptr)->store((val))
#define bs_atomic_load_acquire(ptr) (ptr)->load(std::memory_order_acquire)
#define bs_atomic_load_relaxed(ptr) (ptr)->load(std::memory_order_relaxed)
#define bs_atomic_store_release(ptr, val) (ptr)->store((val), std::memory_order_release)
#define bs_atomic_store_relaxed(ptr, val) (ptr)->store((val), std::memory_order_relaxed)
#define bs_atomic_fetch_add_relaxed(ptr, val) (ptr)->fetch_add((val), std::memory_order_relaxed)
#define bs_atomic_fetch_add_u64_relaxed(ptr, val) (ptr)->fetch_add((val), std::memory_order_relaxed)
#define bs_atomic_fetch_add_size_relaxed(ptr, val) (ptr)->fetch_add((val), std::memory_order_relaxed)
#define bs_atomic_fetch_sub_relaxed(ptr, val) (ptr)->fetch_sub((val), std::memory_order_relaxed)
#define bs_atomic_fetch_sub_size_relaxed(ptr, val) (ptr)->fetch_sub((val), std::memory_order_relaxed)
#define bs_atomic_fetch_sub_acq_rel(ptr, val) (ptr)->fetch_sub((val), std::memory_order_acq_rel)
#define bs_atomic_compare_exchange_weak_release_relaxed(ptr, expected_ptr, desired) \
    (ptr)->compare_exchange_weak(*(expected_ptr), (desired), std::memory_order_release, std::memory_order_relaxed)
#define bs_atomic_compare_exchange_weak_u64_relaxed(ptr, expected_ptr, desired) \
    (ptr)->compare_exchange_weak(*(expected_ptr), (desired), std::memory_order_relaxed, std::memory_order_relaxed)

#elif (defined(_MSC_VER) && !defined(__clang__)) || defined(__STDC_NO_ATOMICS__)
#include <intrin.h>

typedef volatile uint32_t bs_atomic_uint32_t;
typedef volatile uint64_t bs_atomic_uint64_t;
typedef volatile size_t   bs_atomic_size_t;
typedef void * volatile   bs_atomic_ptr_t;

#define bs_atomic_init(ptr, val) (*(ptr) = (val))
#define bs_atomic_load_acquire(ptr) (*(ptr))
#define bs_atomic_load_relaxed(ptr) (*(ptr))
#define bs_atomic_store_release(ptr, val) (*(ptr) = (val))
#define bs_atomic_store_relaxed(ptr, val) (*(ptr) = (val))
#define bs_atomic_fetch_add_relaxed(ptr, val) \
    ((uint32_t)_InterlockedExchangeAdd((volatile long *)(ptr), (long)(val)))
#define bs_atomic_fetch_sub_acq_rel(ptr, val) \
    ((uint32_t)_InterlockedExchangeAdd((volatile long *)(ptr), -(long)(val)))
#define bs_atomic_fetch_sub_relaxed(ptr, val) \
    ((uint32_t)_InterlockedExchangeAdd((volatile long *)(ptr), -(long)(val)))
#define bs_atomic_fetch_add_u64_relaxed(ptr, val) \
    ((uint64_t)_InterlockedExchangeAdd64((volatile __int64 *)(ptr), (__int64)(val)))
#define bs_atomic_fetch_add_size_relaxed(ptr, val) \
    ((size_t)_InterlockedExchangeAdd64((volatile __int64 *)(ptr), (__int64)(val)))
#define bs_atomic_fetch_sub_size_relaxed(ptr, val) \
    ((size_t)_InterlockedExchangeAdd64((volatile __int64 *)(ptr), -(__int64)(val)))

static inline bool bs_atomic_cas_ptr_weak(bs_atomic_ptr_t *ptr, void **expected, void *desired)
{
    void *old = *expected;
    void *prev = _InterlockedCompareExchangePointer((void * volatile *)ptr, desired, old);
    if (prev == old) return true;
    *expected = prev;
    return false;
}
#define bs_atomic_compare_exchange_weak_release_relaxed(ptr, expected_ptr, desired) \
    bs_atomic_cas_ptr_weak((ptr), (void **)(expected_ptr), (void *)(desired))

static inline bool bs_atomic_cas_u64_weak(bs_atomic_uint64_t *ptr, uint64_t *expected, uint64_t desired)
{
    uint64_t old = *expected;
    uint64_t prev = (uint64_t)_InterlockedCompareExchange64((volatile __int64 *)ptr, (__int64)desired, (__int64)old);
    if (prev == old) return true;
    *expected = prev;
    return false;
}
#define bs_atomic_compare_exchange_weak_u64_relaxed(ptr, expected_ptr, desired) \
    bs_atomic_cas_u64_weak((ptr), (expected_ptr), (desired))

#else
#include <stdatomic.h>

typedef _Atomic uint32_t bs_atomic_uint32_t;
typedef _Atomic uint64_t bs_atomic_uint64_t;
typedef _Atomic size_t   bs_atomic_size_t;
typedef _Atomic(void *)  bs_atomic_ptr_t;

#define bs_atomic_init(ptr, val) atomic_init((ptr), (val))
#define bs_atomic_load_acquire(ptr) atomic_load_explicit((ptr), memory_order_acquire)
#define bs_atomic_load_relaxed(ptr) atomic_load_explicit((ptr), memory_order_relaxed)
#define bs_atomic_store_release(ptr, val) atomic_store_explicit((ptr), (val), memory_order_release)
#define bs_atomic_store_relaxed(ptr, val) atomic_store_explicit((ptr), (val), memory_order_relaxed)
#define bs_atomic_fetch_add_relaxed(ptr, val) \
    atomic_fetch_add_explicit((ptr), (val), memory_order_relaxed)
#define bs_atomic_fetch_add_u64_relaxed(ptr, val) \
    atomic_fetch_add_explicit((ptr), (val), memory_order_relaxed)
#define bs_atomic_fetch_add_size_relaxed(ptr, val) \
    atomic_fetch_add_explicit((ptr), (val), memory_order_relaxed)
#define bs_atomic_fetch_sub_relaxed(ptr, val) \
    atomic_fetch_sub_explicit((ptr), (val), memory_order_relaxed)
#define bs_atomic_fetch_sub_size_relaxed(ptr, val) \
    atomic_fetch_sub_explicit((ptr), (val), memory_order_relaxed)
#define bs_atomic_fetch_sub_acq_rel(ptr, val) \
    atomic_fetch_sub_explicit((ptr), (val), memory_order_acq_rel)
#define bs_atomic_compare_exchange_weak_release_relaxed(ptr, expected_ptr, desired) \
    atomic_compare_exchange_weak_explicit((ptr), (expected_ptr), (desired), memory_order_release, memory_order_relaxed)
#define bs_atomic_compare_exchange_weak_u64_relaxed(ptr, expected_ptr, desired) \
    atomic_compare_exchange_weak_explicit((ptr), (expected_ptr), (desired), memory_order_relaxed, memory_order_relaxed)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Architecture Verification & Platform Types                                */
/* ========================================================================= */

#if defined(__SIZEOF_POINTER__)
#if __SIZEOF_POINTER__ != 8
#error "BirdSafe requires a 64-bit architecture where sizeof(void*) == 8"
#endif
#elif defined(UINTPTR_MAX)
#if UINTPTR_MAX != 0xFFFFFFFFFFFFFFFFULL
#error "BirdSafe requires a 64-bit architecture where UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFULL"
#endif
#else
#error "Cannot determine pointer size; 64-bit architecture required"
#endif

#define BS_WORD_BITS 64

#if defined(__SIZEOF_INT128__)
#define BS_HAS_INT128 1
__extension__ typedef unsigned __int128 bs_uint128_t;
__extension__ typedef signed __int128 bs_int128_t;
#endif

/* ========================================================================= */
/* Symbol Visibility & Compiler Attributes                                   */
/* ========================================================================= */

#if defined(BS_SHARED) || defined(BS_NUM_SHARED) || defined(BS_CORE_SHARED) || defined(BS_SETS_SHARED)
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(BS_BUILDING_DLL) || defined(BS_NUM_BUILDING_DLL) || defined(BS_CORE_BUILDING_DLL) || defined(BS_SETS_BUILDING_DLL)
#define BS_API __declspec(dllexport)
#else
#define BS_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define BS_API __attribute__((visibility("default")))
#else
#define BS_API
#endif
#else
#define BS_API
#endif

#if defined(__GNUC__) || defined(__clang__)
#define BS_INLINE static inline __attribute__((always_inline))
#define BS_UNUSED __attribute__((unused))
#define BS_NODISCARD __attribute__((warn_unused_result))
#elif defined(_MSC_VER)
#define BS_INLINE static __forceinline
#define BS_UNUSED
#define BS_NODISCARD _Check_return_
#else
#define BS_INLINE static inline
#define BS_UNUSED
#define BS_NODISCARD
#endif
#ifdef __cplusplus
}
#endif

#endif /* BS_CORE_BS_CONFIG_H */
