//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/growable_array.h>

#include <benchmark/benchmark.h>
#include <deque>
#include <vector>
#include <thread>

#define N 1ull << 22

std::mutex mutex;

template< typename Container > static void container_push_back_locked(benchmark::State& state) {
    for (auto _ : state) {
        Container container;
        for (size_t i = 0; i < state.range(); ++i) {
            std::lock_guard lock(mutex);
            container.emplace_back(i);
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Container > static void container_push_back(benchmark::State& state) {
    for (auto _ : state) {
        Container container;
        for (size_t i = 0; i < state.range(); ++i)
            container.emplace_back(i);
    }
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Container > static void container_indexed_access(benchmark::State& state) {
    Container container;
    container.push_back(0);
    size_t result = 0;
    for (auto _ : state) {
        for (size_t i = 0; i < state.range(); ++i)
            result += container[0];
    }
    benchmark::DoNotOptimize(result);
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Container > static void container_indexed_access_local(benchmark::State& state) {
    Container container;
    container.push_back(0);
    size_t result = 0;
    static thread_local typename Container::reader_state reader;
    for (auto _ : state) {
        for (size_t i = 0; i < state.range(); ++i)
            result += container.read(reader, 0);
    }
    benchmark::DoNotOptimize(result);
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Container > static void container_indexed_access_locked(benchmark::State& state) {
    Container container;
    container.push_back(0);
    size_t result = 0;
    for (auto _ : state) {
        for (size_t i = 0; i < state.range(); ++i) {
            std::lock_guard lock(mutex);
            result += container[0];
        }
    }
    benchmark::DoNotOptimize(result);
    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK_TEMPLATE(container_push_back_locked, std::vector<size_t>)->Range(1, N);
BENCHMARK_TEMPLATE(container_push_back_locked, std::deque<size_t>)->Range(1, N);
BENCHMARK_TEMPLATE(container_push_back, containers::growable_array<size_t>)->Range(1, N);
BENCHMARK_TEMPLATE(container_indexed_access, containers::growable_array<size_t>)->Range(1, N);
BENCHMARK_TEMPLATE(container_indexed_access_local, containers::growable_array<size_t>)->Range(1, N);
BENCHMARK_TEMPLATE(container_indexed_access_locked, std::deque<size_t>)->Range(1, N);
//BENCHMARK_TEMPLATE(container_push_back, containers::mmapped_array<int>)->Range(1, N);
