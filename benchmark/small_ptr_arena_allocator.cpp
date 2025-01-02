//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/small_ptr_arena_allocator.h>

#include <benchmark/benchmark.h>

const int N = 1<<24;

template< typename Allocator > static void small_ptr_arena_allocator_allocate(benchmark::State& state) {
    struct Class {
        uint8_t data[64];
    };

    static uint8_t buffer[1<<20];
    uintptr_t ptr = 0;
    for (auto _ : state) {
        containers::small_ptr_arena_allocator< Class > allocator;
        auto rm = allocator.resource_mark();

        for (size_t i = 0; i < (size_t)state.range(); ++i)
            ptr += (uintptr_t)(Class*)allocator.allocate(1);
    }

    benchmark::DoNotOptimize(ptr);
    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK_TEMPLATE(small_ptr_arena_allocator_allocate, std::allocator<char>)->Range(1, N)->UseRealTime();

