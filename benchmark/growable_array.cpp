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

#define N 1ull << 20

template< typename Container > static void container_push_back(benchmark::State& state) {
    for (auto _ : state) {
        Container container;
        for (size_t i = 0; i < state.range(); ++i)
            container.push_back(i);
    }
    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK_TEMPLATE(container_push_back, std::vector<int>)->Range(1, N);
BENCHMARK_TEMPLATE(container_push_back, std::deque<int>)->Range(1, N);
//BENCHMARK_TEMPLATE(container_push_back, containers::growable_array<int>)->Range(1, N);
BENCHMARK_TEMPLATE(container_push_back, containers::growable_array2<int>)->Range(1, N);
//BENCHMARK_TEMPLATE(container_push_back, containers::mmapped_array<int>)->Range(1, N);
