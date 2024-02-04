//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/eh/eh.h>
#include <unordered_set>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

static const size_t N = 12;

std::vector< size_t > get_data(size_t n) {
    std::mt19937 gen;
    std::uniform_int_distribution<size_t> dist;

    std::vector< size_t > data(n);
    for (size_t i = 0; i < n; ++i) {
        do {
            size_t x = dist(gen);
            if (x == 0) continue;
            data[i] = x;
            break;
        } while(true);
    }

    return data;
}

auto data = get_data(N);

static void hashtable_eh_insert(benchmark::State& state) {
    containers::hash_table<size_t> x;    
    for (auto _ : state) {
        for (auto value: data)
            x.insert(value);
    }

    state.SetBytesProcessed(state.iterations() * data.size());
}

static void hashtable_eh_get(benchmark::State& state) {
    containers::hash_table<int> x;
    for (auto& value : data)
        x.insert(value);

    for (auto _ : state) {
        for (auto value : data)
            benchmark::DoNotOptimize(x.get(value));
    }

    state.SetBytesProcessed(state.iterations() * data.size());
}

static void hashtable_unordered_insert(benchmark::State& state) {
    std::unordered_set<int> x;
    for (auto _ : state) {
        for (auto value : data)
            x.insert(value);
    }

    state.SetBytesProcessed(state.iterations() * data.size());
}

static void hashtable_unordered_get(benchmark::State& state) {
    std::unordered_set<int> x;
    for (auto& value : data)
        x.insert(value);

    for (auto _ : state) {
        for (auto value : data)
            benchmark::DoNotOptimize(x.find(value));
    }

    state.SetBytesProcessed(state.iterations() * data.size());
}

BENCHMARK(hashtable_eh_insert)->UseRealTime();
BENCHMARK(hashtable_eh_get)->UseRealTime();
BENCHMARK(hashtable_unordered_insert)->UseRealTime();
BENCHMARK(hashtable_unordered_get)->UseRealTime();

