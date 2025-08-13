//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/pool.h>

#include <benchmark/benchmark.h>

const int N = 1<<28;

/* The state must be initialized to non-zero */
// https://en.wikipedia.org/wiki/Xorshift
uint64_t xorshift64(uint64_t& state) {
	uint64_t x = state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return state = x;
}

static void pool_allocator_allocate_seq(benchmark::State& state) {
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

static void pool_allocator_allocate_rnd(benchmark::State& state) {
    containers::PageGroupManager< 1ull<<35 > manager;
    containers::pool_allocator<uint64_t, decltype(manager) > allocator(manager);

    std::vector<uint64_t*> ptrs(state.range());
    uint64_t tmp = 12345;

    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            // Select random position
            auto p = xorshift64(tmp) & (state.range() - 1);
            if (ptrs[p]) {
                allocator.deallocate(ptrs[p], 1);
                ptrs[p] = 0;
            } else {
                ptrs[p] = allocator.allocate(1);
                (*ptrs[p]) = 1;
            }
        }
    }

    for (int i = 0; i < state.range(); ++i) {
        if (ptrs[i])
            allocator.deallocate(ptrs[i], 1);
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}

static void allocator_allocate_seq(benchmark::State& state) {
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

static void allocator_allocate_rnd(benchmark::State& state) {
    std::allocator<uint64_t> allocator;

    std::vector<uint64_t*> ptrs(state.range());
    uint64_t tmp = 12345;

    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            // Select random position
            auto p = xorshift64(tmp) & (state.range() - 1);
            if (ptrs[p]) {
                allocator.deallocate(ptrs[p], 1);
                ptrs[p] = 0;
            } else {
                ptrs[p] = allocator.allocate(1);
                (*ptrs[p]) = 1;
            }
        }
    }

    for (int i = 0; i < state.range(); ++i) {
        if (ptrs[i])
            allocator.deallocate(ptrs[i], 1);
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}

BENCHMARK(pool_allocator_allocate_seq)->Range(1, N);
BENCHMARK(pool_allocator_allocate_rnd)->Range(1, N);
//BENCHMARK(pool_allocator_allocate_global)->Range(1, N);
BENCHMARK(allocator_allocate_seq)->Range(1, N);
BENCHMARK(allocator_allocate_rnd)->Range(1, N);

