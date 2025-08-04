//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/pool.h>

#include <benchmark/benchmark.h>

const int N = 1<<21;

static void pool_allocator_allocate(benchmark::State& state) {
    containers::page_manager<1<<12, 65536> page_manager;
    containers::pool_page_allocator<uint64_t, decltype(page_manager)> page_allocator(page_manager);
    containers::pool_allocator<uint64_t, decltype(page_allocator)> allocator(page_allocator);

    std::vector<uint64_t*> ptrs(state.range());

    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            ptrs[i] = allocator.allocate(1);
        }

        for (int i = 0; i < state.range(); ++i) {
            allocator.deallocate(ptrs[i]);
        }
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}

static void allocator_allocate(benchmark::State& state) {
    std::allocator<uint64_t> allocator;

    std::vector<uint64_t*> ptrs(state.range());

    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            ptrs[i] = allocator.allocate(1);
        }
        for (int i = 0; i < state.range(); ++i) {
            allocator.deallocate(ptrs[i], sizeof(uint64_t));
        }
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}


BENCHMARK(pool_allocator_allocate)->Range(1, N);
BENCHMARK(allocator_allocate)->Range(1, N);

