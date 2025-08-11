//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/pool.h>

#include <benchmark/benchmark.h>

const int N = 1<<28;

static void pool_allocator_allocate(benchmark::State& state) {
    containers::PageGroupManager< 1ull<<35 > manager;
    containers::pool_allocator<uint64_t, decltype(manager) > allocator(manager);

    std::vector<uint64_t*> ptrs(state.range());

    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            ptrs[i] = allocator.allocate(1);
            (*ptrs[i]) = 1;
        }

        //state.PauseTiming();
        for (int i = 0; i < state.range(); ++i) {
            allocator.deallocate(ptrs[i], 1);
        }
        //state.ResumeTiming();
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}

static void allocator_allocate(benchmark::State& state) {
    std::allocator<uint64_t> allocator;

    std::vector<uint64_t*> ptrs(state.range());

    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            ptrs[i] = allocator.allocate(1);
            (*ptrs[i]) = 1;
        }
        for (int i = 0; i < state.range(); ++i) {
            allocator.deallocate(ptrs[i], sizeof(uint64_t));
        }
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}


BENCHMARK(pool_allocator_allocate)->Range(1, N);
BENCHMARK(allocator_allocate)->Range(1, N);

