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

#include "llvm\\ADT\\DenseSet.h"

static const size_t N = 1 << 26;

void* llvm::allocate_buffer(size_t size, size_t alignment) {
    return ::operator new(size, std::align_val_t(alignment));
}

void llvm::deallocate_buffer(void* ptr, size_t size, size_t alignment) {
    ::operator delete(ptr, size, std::align_val_t(alignment));
}

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

static void hashtable_eh_insert(benchmark::State& state) {
    containers::hash_table<size_t> x;
    auto data = get_data(state.range());
    for (auto _ : state) {
        for (auto value: data)
            x.insert(value);
    }

    state.SetBytesProcessed(state.iterations() * data.size());
}

static void hashtable_eh_get(benchmark::State& state) {
    containers::hash_table<int> x;
    auto data = get_data(state.range());
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
    auto data = get_data(state.range());
    for (auto _ : state) {
        for (auto value : data)
            x.insert(value);
    }

    state.SetBytesProcessed(state.iterations() * data.size());
}

static void hashtable_unordered_get(benchmark::State& state) {
    std::unordered_set<int> x;
    auto data = get_data(state.range());
    for (auto& value : data)
        x.insert(value);

    for (auto _ : state) {
        for (auto value : data)
            benchmark::DoNotOptimize(x.find(value));
    }

    state.SetBytesProcessed(state.iterations() * data.size());
}

static void hashtable_DenseSet_insert(benchmark::State& state)
{
    llvm::DenseSet<int> x;
    auto data = get_data(state.range());
    for (auto _ : state) {
        for (auto value : data)
            x.insert(value);
    }

    state.SetBytesProcessed(state.iterations() * data.size());
}

static void hashtable_DenseSet_get(benchmark::State& state)
{
    llvm::DenseSet<int> x;
    auto data = get_data(state.range());
    for (auto& value : data)
        x.insert(value);

    for (auto _ : state) {
        for (auto value : data)
            benchmark::DoNotOptimize(x.find(value));
    }

    state.SetBytesProcessed(state.iterations() * data.size());
}
BENCHMARK(hashtable_eh_insert)->UseRealTime()->Range(1, N)->RangeMultiplier(2);
BENCHMARK(hashtable_eh_get)->UseRealTime()->Range(1, N)->RangeMultiplier(2);
BENCHMARK(hashtable_unordered_insert)->UseRealTime()->Range(1, N)->RangeMultiplier(2);
BENCHMARK(hashtable_unordered_get)->UseRealTime()->Range(1, N)->RangeMultiplier(2);
BENCHMARK(hashtable_DenseSet_insert)->UseRealTime()->Range(1, N)->RangeMultiplier(2);
BENCHMARK(hashtable_DenseSet_get)->UseRealTime()->Range(1, N)->RangeMultiplier(2);;

