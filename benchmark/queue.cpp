//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/vbr.h>
#include <containers/queue.hpp>

#include <benchmark/benchmark.h>

#include <queue>
#include <mutex>

static const int N = 1 << 20;

template< typename T > class stl_queue {
public:
    using value_type = T;

    bool push(T value) {
        auto guard = std::lock_guard(mutex_);
        queue_.push(value);
        return true;
    }

    bool pop(T& value) {
        auto guard = std::lock_guard(mutex_);
        if (!queue_.empty()) {
            value = std::move(queue_.front());
            queue_.pop();
            return true;
        } else {
            return false;
        }
    }

    bool empty() const {
        auto guard = std::lock_guard(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::queue< T > queue_;
};

template< typename Container > static void queue_push_pop(benchmark::State& state) {
    Container queue;

    size_t ops = 0;
    int in = 1; int out;
    for (auto _ : state) {
        queue.push(in);
        ops += queue.pop(out);
    }

    state.SetBytesProcessed(ops);
}

template< typename Atomic > static void cas(benchmark::State& state) {
    Atomic value;
    typename Atomic::value_type expected = {};

    for (auto _ : state) {
        value.compare_exchange_strong(expected, {});
    }

    state.SetBytesProcessed(state.iterations());
}

struct alignas(16) CAS16 {
    uint64_t a, b;
};

BENCHMARK_TEMPLATE(cas, std::atomic<uint64_t>)->Range(1, N);
BENCHMARK_TEMPLATE(cas, std::atomic<CAS16>)->Range(1, N);


BENCHMARK_TEMPLATE(queue_push_pop, containers::queue<int>)->Range(1, N);
BENCHMARK_TEMPLATE(queue_push_pop, stl_queue<int>)->Range(1, N);


