//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/allocators/arena_allocator.h>
#include <containers/allocators/page_allocator.h>

#include <benchmark/benchmark.h>

#include <set>
#include <unordered_set>

static const int N = 1 << 24;

uint8_t buffer[1<<16];

template< typename Allocator > static void arena_allocator_allocate(benchmark::State& state) {
    struct Class {
        uint8_t data[64];
    };

    uintptr_t ptr = 0;

    containers::arena< Allocator > arena(buffer, 1<<16);
    containers::arena_allocator< Class, decltype(arena) > allocator(arena);
    for (auto _ : state) {
        auto rm = allocator.resource_mark();
        for (size_t i = 0; i < (size_t)state.range(); ++i)
            ptr += (uintptr_t)allocator.allocate(1);
    }

    benchmark::DoNotOptimize(ptr);
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Allocator > static void arena_allocator_allocate_set(benchmark::State& state) {
    containers::arena< Allocator > arena(buffer, 1<<16);
    containers::arena_allocator< uint64_t, decltype(arena) > allocator(arena);

    for (auto _ : state) {
        std::set<uint64_t, std::less<uint64_t>, decltype(allocator) > set(allocator);
        for (int i = 0; i < state.range(); ++i) {
            set.insert(i);
        }
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}

template< typename Allocator > static void arena_allocator_allocate_unordered_set(benchmark::State& state) {
    containers::arena< Allocator > arena(buffer, 1<<16);
    containers::arena_allocator< uint64_t, decltype(arena) > allocator(arena);

    for (auto _ : state) {
        std::unordered_set<uint64_t, std::hash<uint64_t>, std::equal_to<uint64_t>, decltype(allocator) > set(allocator);
        for (int i = 0; i < state.range(); ++i) {
            set.insert(i);
        }
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}

template< typename Allocator > static void allocator_allocate_unordered_set(benchmark::State& state) {
    Allocator allocator;

    for (auto _ : state) {
        std::unordered_set<uint64_t, std::hash<uint64_t>, std::equal_to<uint64_t>, decltype(allocator) > set(allocator);
        for (int i = 0; i < state.range(); ++i) {
            set.insert(i);
        }
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}

template< typename Allocator > static void arena_allocator_allocate_nobuffer(benchmark::State& state) {
    struct Class {
        uint8_t data[64];
    };

    uintptr_t ptr = 0;

    containers::arena< Allocator > arena(1<<16);
    containers::arena_allocator< Class, decltype(arena) > allocator(arena);
    for (auto _ : state) {
        auto rm = allocator.resource_mark();
        for (size_t i = 0; i < (size_t)state.range(); ++i)
            ptr += (uintptr_t)allocator.allocate(1);
    }

    benchmark::DoNotOptimize(ptr);
    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK_TEMPLATE(arena_allocator_allocate, std::allocator<char>)->Range(1, N)->UseRealTime();
//BENCHMARK_TEMPLATE(arena_allocator_allocate_nobuffer, std::allocator<char>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate, containers::page_allocator<char>)->Range(1, N)->UseRealTime();
//BENCHMARK_TEMPLATE(arena_allocator_allocate_nobuffer, containers::page_allocator<char>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate_set, std::allocator<char>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate_unordered_set, std::allocator<char>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(allocator_allocate_unordered_set, std::allocator<uint64_t>)->Range(1, N)->UseRealTime();

