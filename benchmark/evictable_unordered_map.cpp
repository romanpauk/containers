//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/evictable_unordered_map.h>
#include <containers/eviction/lru.h>
#include <containers/eviction/sieve.h>

#include <containers/allocators/arena_allocator.h>

#include <benchmark/benchmark.h>

#include <unordered_map>

#define N 1ull << 20

static uint8_t buffer[1<<16];

template< typename Container > static void container_emplace(benchmark::State& state) {
    for (auto _ : state) {
        containers::arena arena(buffer, 1<<20);
        containers::arena_allocator<void> allocator(arena);

        Container container(allocator);
        for (size_t i = 0; i < (size_t)state.range(); ++i) {
            container.emplace(i, i);
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Container > static void container_find(benchmark::State& state) {
    containers::arena arena(buffer, 1<<20);
    containers::arena_allocator<void> allocator(arena);
    Container container(allocator);
    for (size_t i = 0; i < (size_t)state.range(); ++i) {
        container.emplace(i, i);
    }

    bool result = true;
    for (auto _ : state) {
        for (size_t i = 0; i < (size_t)state.range(); ++i) {
            result &= container.find(i) != container.end();
        }
    }

    state.SetItemsProcessed(result ? state.iterations() * state.range() : 0);
}

template< typename Container > static void container_operator_array(benchmark::State& state) {
    containers::arena arena(buffer, 1<<20);
    containers::arena_allocator<void> allocator(arena);
    Container container(allocator);
    for (size_t i = 0; i < (size_t)state.range(); ++i) {
        container.emplace(i, i);
    }

    bool result = true;
    for (auto _ : state) {
        for (size_t i = 0; i < (size_t)state.range(); ++i) {
            result &= container[i];
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range());
}

using unordered_map = std::unordered_map< size_t, size_t, std::hash<size_t>, std::equal_to<size_t>, containers::arena_allocator< std::pair< const size_t, size_t > > >;

using evictable_unordered_map_lru = containers::evictable_unordered_map< size_t, size_t, std::hash<size_t>, std::equal_to<size_t>, containers::arena_allocator< std::pair< const size_t, size_t > >, containers::eviction::lru< std::pair< const size_t, size_t > > >;

using evictable_unordered_map_sieve = containers::evictable_unordered_map< size_t, size_t, std::hash<size_t>, std::equal_to<size_t>, containers::arena_allocator< std::pair< const size_t, size_t > >, containers::eviction::sieve< std::pair< const size_t, size_t > > >;

BENCHMARK_TEMPLATE(container_emplace, unordered_map)->Range(1, N);
BENCHMARK_TEMPLATE(container_emplace, evictable_unordered_map_lru)->Range(1, N);
BENCHMARK_TEMPLATE(container_emplace, evictable_unordered_map_sieve)->Range(1, N);

BENCHMARK_TEMPLATE(container_find, unordered_map)->Range(1, N);
BENCHMARK_TEMPLATE(container_find, evictable_unordered_map_lru)->Range(1, N);
BENCHMARK_TEMPLATE(container_find, evictable_unordered_map_sieve)->Range(1, N);


BENCHMARK_TEMPLATE(container_operator_array, unordered_map)->Range(1, N);
BENCHMARK_TEMPLATE(container_operator_array, evictable_unordered_map_lru)->Range(1, N);
BENCHMARK_TEMPLATE(container_operator_array, evictable_unordered_map_sieve)->Range(1, N);

