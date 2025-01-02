//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/thread_arena_allocator.h>

#include <deque>
#include <set>
#include <unordered_set>
#include <vector>

#include <benchmark/benchmark.h>

const int N = 1 << 24;

template< typename T > struct ResourceMark {};

template< typename T > struct ResourceMark< containers::thread_arena_allocator<T> > {
    typename containers::thread_arena_allocator<T>::resource_mark_type rm_;
    ResourceMark(): rm_(containers::thread_arena_allocator<T>::resource_mark()) {}
};

template< typename Allocator > static void thread_arena_allocator_allocate(benchmark::State& state) {
    struct Class {
        uint8_t data[64];
    };

    static uint8_t buffer[1<<16];
    uintptr_t ptr = 0;
    Allocator allocator;
    for (auto _ : state) {
        auto rm = allocator.resource_mark();
        for (size_t i = 0; i < (size_t)state.range(); ++i)
            ptr += (uintptr_t)allocator.allocate(1);
    }

    benchmark::DoNotOptimize(ptr);
    state.SetItemsProcessed(state.iterations() * state.range());
}


template< typename Allocator > static void vector_append(benchmark::State& state) {
    for (auto _ : state) {
        ResourceMark< Allocator > rm;
        std::vector< int, Allocator > c;
        for (int i = 0; i < state.range(); ++i)
            c.emplace_back(i);
    }

    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Allocator > static void deque_append(benchmark::State& state) {
    for (auto _ : state) {
        ResourceMark< Allocator > rm;
        std::deque< int, Allocator > c;
        for (int i = 0; i < state.range(); ++i)
            c.emplace_back(i);
    }

    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Allocator > static void set_append(benchmark::State& state) {
    for (auto _ : state) {
        ResourceMark< Allocator > rm;
        std::set< int, std::less<int>, Allocator > c;
        for (int i = 0; i < state.range(); ++i)
            c.emplace(i);
    }

    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Allocator > static void unordered_set_append(benchmark::State& state) {
    for (auto _ : state) {
        ResourceMark< Allocator > rm;
        std::unordered_set< int, std::hash<int>, std::equal_to<int>, Allocator > c;
        for (int i = 0; i < state.range(); ++i)
            c.emplace(i);
    }

    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK_TEMPLATE(thread_arena_allocator_allocate, containers::thread_arena_allocator<int>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(thread_arena_allocator_allocate, containers::thread_arena_allocator<int, containers::thread_mmap_arena_factory >)->Range(1, N)->UseRealTime();

BENCHMARK_TEMPLATE(vector_append, std::allocator<int>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(vector_append, containers::thread_arena_allocator<int>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(deque_append, std::allocator<int>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(deque_append, containers::thread_arena_allocator<int>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(set_append, std::allocator<int>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(set_append, containers::thread_arena_allocator<int>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(unordered_set_append, std::allocator<int>)->Range(1, N)->UseRealTime();
BENCHMARK_TEMPLATE(unordered_set_append, containers::thread_arena_allocator<int>)->Range(1, N)->UseRealTime();

