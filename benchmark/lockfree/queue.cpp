//
// This file is part of containers project <https://github.com/romanpauk/smart_ptr>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/bounded_queue.h>
#include <containers/lockfree/bounded_queue_bbq.h>
#include <containers/lockfree/unbounded_queue.h>

#include <benchmark/benchmark.h>
#include <thread>
#include <queue>
#include <mutex>
#include <random>

#include <concurrentqueue.h>

static const auto max_threads = std::thread::hardware_concurrency();

template< typename T > class stl_queue
{
public:
    using value_type = T;

    void push(T value)
    {
        auto guard = std::lock_guard(mutex_);
        queue_.push(value);
    }

    bool pop(T& value)
    {
        auto guard = std::lock_guard(mutex_);
        if (!queue_.empty())
        {
            value = std::move(queue_.front());
            queue_.pop();
            return true;
        }
        else
        {
            return false;
        }
    }

    bool empty() const
    {
        auto guard = std::lock_guard(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::queue< T > queue_;
};

template< typename T > class concurrent_queue
{
public:
    using value_type = T;

    bool push(T value)
    {
        return queue_.enqueue(value);
    }

    bool pop(T& value)
    {
        return queue_.try_dequeue(value);
    }

    bool empty() const
    {
        return queue_.empty();
    }

private:
    moodycamel::ConcurrentQueue< T > queue_;
};

template< typename Queue > static void queue_push_pop(benchmark::State& state)
{
    static Queue queue;
    typename Queue::value_type value{}, result{};
    for (auto _ : state)
    {
        queue.push(value);
        queue.pop(result);
    }

    state.SetBytesProcessed(state.iterations() * 2);
}

template< typename Queue > static void queue_push_pop_rand(benchmark::State& state)
{
    std::minstd_rand r;

    static Queue queue;
    typename Queue::value_type value{}, result{};
    for (auto _ : state)
    {
        if(r() & 1)
            queue.push(value);
        else
            queue.pop(result);
    }

    state.SetBytesProcessed(state.iterations() * 2);
}

template< typename Queue > static void queue_pop(benchmark::State& state)
{
    static Queue queue;
    typename Queue::value_type value;
    for (auto _ : state)
    {        
        queue.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

template< typename Queue > static void queue_empty(benchmark::State& state)
{
    static Queue queue;
    typename Queue::value_type value;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(queue.empty());
    }

    state.SetBytesProcessed(state.iterations());
}

namespace containers::detail
{
    // In-place construction is cheaper than construction and move
    //template< typename T > struct is_trivial< std::shared_ptr< T > >: std::true_type {};
    //template<> struct is_trivial< std::string > : std::true_type {};
}

BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::bounded_queue_bbq<int, 1024 * 64>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::unbounded_blocked_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, concurrent_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::bounded_queue_bbq<std::string, 1024 * 64>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::unbounded_blocked_queue<std::string>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, concurrent_queue<std::string>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::bounded_queue_bbq<std::shared_ptr< int >, 1024 * 64>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::unbounded_blocked_queue<std::shared_ptr< int >>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, concurrent_queue<std::shared_ptr< int >>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::bounded_queue_bbq<std::vector< int >, 1024 * 64>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::unbounded_blocked_queue<std::vector< int >>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, concurrent_queue<std::vector< int >>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop, containers::bounded_queue_bbq<int, 1024 * 64>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop, containers::unbounded_blocked_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop, concurrent_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop, containers::bounded_queue_bbq<std::string, 1024 * 64>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop, containers::unbounded_blocked_queue<std::string>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop, concurrent_queue<std::string>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop, containers::bounded_queue_bbq<std::shared_ptr<int>, 1024 * 64>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop, containers::unbounded_blocked_queue<std::shared_ptr<int>>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop, concurrent_queue<std::shared_ptr<int>>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop, containers::bounded_queue_bbq<std::vector<int>, 1024 * 64>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop, containers::unbounded_blocked_queue<std::vector<int>>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop, concurrent_queue<std::vector<int>>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop, stl_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, stl_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_pop, stl_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_empty, stl_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop, containers::unbounded_blocked_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::unbounded_blocked_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_pop, containers::unbounded_blocked_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_empty, containers::unbounded_blocked_queue<int>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop, containers::bounded_queue<int, 1 << 16>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::bounded_queue<int, 1 << 16>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_pop, containers::bounded_queue<int, 1 << 16>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_empty, containers::bounded_queue<int, 1 << 16>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(queue_push_pop, containers::bounded_queue_bbq<int, 1 << 16>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_push_pop_rand, containers::bounded_queue_bbq<int, 1 << 16>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_pop, containers::bounded_queue_bbq<int, 1 << 16>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(queue_empty, containers::bounded_queue_bbq<int, 1 << 16>)->ThreadRange(1, max_threads)->UseRealTime();

