//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/growable_array.h>

#include <benchmark/benchmark.h>
#include <thread>

static void growable_array(benchmark::State& state)
{
    containers::growable_array<int> array;
    for (auto _ : state) {
        for (size_t i = 0; i < 10000; ++i) {
            array.push_back(1);
        }
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(growable_array);
