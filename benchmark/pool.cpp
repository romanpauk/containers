//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/pool.h>

#include <benchmark/benchmark.h>

static void pool_allocator_allocate(benchmark::State& state) {
    containers::page_allocator<1<<12, 100> page_allocator;
    containers::pool_allocator<uint64_t, decltype(page_allocator) > allocator(page_allocator);

    volatile uint64_t val = 0;
    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            volatile uint64_t* ptr = allocator.allocate(1);
            val += (uint64_t)ptr;
            allocator.deallocate((uint64_t*)ptr);
        }
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}

static void allocator_allocate(benchmark::State& state) {
    std::allocator<uint64_t> allocator;

    volatile uint64_t val = 0;
    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            volatile uint64_t* ptr = allocator.allocate(1);
            val += (uint64_t)ptr;
            allocator.deallocate((uint64_t*)ptr, sizeof(uint64_t));
        }
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}


BENCHMARK(pool_allocator_allocate)->Range(1, 1<<12);
BENCHMARK(allocator_allocate)->Range(1, 1<<12);

