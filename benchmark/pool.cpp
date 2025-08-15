//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/pool.h>

#include <benchmark/benchmark.h>
#include <random>

const std::size_t N = 1<<26;

/* The state must be initialized to non-zero */
// https://en.wikipedia.org/wiki/Xorshift
uint64_t xorshift64(uint64_t& state) {
	uint64_t x = state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return state = x;
}

template<typename T> T get() { return T(); }

template<typename T> static void pool_allocator_allocate_seq(benchmark::State& state) {
    containers::PageGroupManagerStats stats {{0}};
    containers::LocalPageGroupManager< 1ull<<35 > manager(&stats);
    containers::pool_allocator<T, decltype(manager) > allocator(manager);

    std::vector<T*> ptrs(state.range());

    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            ptrs[i] = allocator.allocate(1);
            (*ptrs[i]) = get<T>();
        }

        for (int i = 0; i < state.range(); ++i) {
            allocator.deallocate(ptrs[i], 1);
        }
    }

    state.SetBytesProcessed(state.iterations() * state.range());
    // std::cerr << stats << std::endl;
}

template<typename T> static void pool_allocator_allocate_rnd(benchmark::State& state) {
    containers::PageGroupManagerStats stats {{0}};
    containers::LocalPageGroupManager< 1ull<<35 > manager(&stats);
    containers::pool_allocator<T, decltype(manager) > allocator(manager);

    std::vector<T*> ptrs(state.range());
    uint64_t tmp = 12345;
    uint64_t count = 0;
    for (int i = 0; i < state.range(); ++i) {
        auto rnd = xorshift64(tmp);
        if ((rnd ^ (rnd >> 33)) & 1) {
            ptrs[i] = allocator.allocate(1);
            (*ptrs[i]) = get<T>();
            ++count;
        }
    }

    __stats__(std::cerr << "allocation ratio " << (double)count / state.range() << std::endl;);

    auto rng = std::default_random_engine {};
    std::shuffle(std::begin(ptrs), std::end(ptrs), rng);

    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            if (ptrs[i]) {
                allocator.deallocate(ptrs[i], 1);
                ptrs[i] = 0;
            } else {
                ptrs[i] = allocator.allocate(1);
                (*ptrs[i]) = get<T>();
            }
        }
    }

    for (int i = 0; i < state.range(); ++i) {
        if (ptrs[i])
            allocator.deallocate(ptrs[i], 1);
    }

    state.SetBytesProcessed(state.iterations() * state.range());
    __stats__(std::cerr << stats << std::endl;);
}

template<typename T> static void allocator_allocate_seq(benchmark::State& state) {
    std::allocator<T> allocator;

    std::vector<T*> ptrs(state.range());

    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            ptrs[i] = allocator.allocate(1);
            (*ptrs[i]) = get<T>();
        }
    for (int i = 0; i < state.range(); ++i) {
            allocator.deallocate(ptrs[i], sizeof(uint64_t));
        }
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}

template<typename T> static void allocator_allocate_rnd(benchmark::State& state) {
    std::allocator<T> allocator;

    std::vector<T*> ptrs(state.range());
    uint64_t tmp = 12345;
    uint64_t count = 0;
    for (int i = 0; i < state.range(); ++i) {
        auto rnd = xorshift64(tmp);
        if ((rnd ^ (rnd >> 33)) & 1) {
            ptrs[i] = allocator.allocate(1);
            (*ptrs[i]) = get<T>();
            ++count;
        }
    }

    __stats__(std::cerr << "allocation ratio " << (double)count / state.range() << std::endl;);

    auto rng = std::default_random_engine {};
    std::shuffle(std::begin(ptrs), std::end(ptrs), rng);

    for (auto _ : state) {
        for (int i = 0; i < state.range(); ++i) {
            if (ptrs[i]) {
                allocator.deallocate(ptrs[i], 1);
                ptrs[i] = 0;
            } else {
                ptrs[i] = allocator.allocate(1);
                (*ptrs[i]) = get<T>();
            }
        }
    }

    for (int i = 0; i < state.range(); ++i) {
        if (ptrs[i])
            allocator.deallocate(ptrs[i], 1);
    }

    state.SetBytesProcessed(state.iterations() * state.range());
}

using T = std::array<uint64_t, 1>;

BENCHMARK_TEMPLATE(pool_allocator_allocate_seq, T)->Range(1, N);
BENCHMARK_TEMPLATE(pool_allocator_allocate_rnd, T)->Range(1, N);
//BENCHMARK(pool_allocator_allocate_global)->Range(1, N);
BENCHMARK_TEMPLATE(allocator_allocate_seq, T)->Range(1, N);
BENCHMARK_TEMPLATE(allocator_allocate_rnd, T)->Range(1, N);

