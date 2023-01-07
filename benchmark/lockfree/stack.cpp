//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/stack.h>
#include <containers/lockfree/counter.h>

#include <benchmark/benchmark.h>
#include <thread>
#include <mutex>
#include <stack>
#include <list>

static const auto max_threads = containers::thread::max_threads;
static const auto iterations = 1024;

namespace stl
{
    template< typename T > class stack
    {
    public:
        void push(T value)
        {
            auto guard = std::lock_guard(mutex_);
            stack_.push(value);
        }

        bool pop(T& value)
        {
            auto guard = std::lock_guard(mutex_);
            if (!stack_.empty())
            {
                value = std::move(stack_.top());
                stack_.pop();
                return true;
            }
            else
            {
                return false;
            }
        }

    private:
        std::mutex mutex_;
        std::stack< T > stack_;
    };
}

static void stl_stack(benchmark::State& state)
{
    static stl::stack< int > stack;
    int value;
    for (auto _ : state)
    {
        stack.push(1);
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations() * 2);
}

static void stl_stack_pop(benchmark::State& state)
{
    static stl::stack< int > stack;
    int value;
    for (auto _ : state)
    {
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void unbounded_stack(benchmark::State& state)
{
    static containers::unbounded_stack< int > stack;

    int value;
    for (auto _ : state)
    {
        stack.push(1);
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations() * 2);
}

static void unbounded_stack_pop(benchmark::State& state)
{
    static containers::unbounded_stack< int > stack;
    int value;
    for (auto _ : state)
    {
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

/*
// TODO: stack_eb uses two different allocators, so the trick with reinterpret_cast
// will not work. One option is to have single allocator for all pointers and distinguish
// them later. That means eash hazard_buffer would need to take type tag.

static void hazard_era_stack_eb(benchmark::State& state)
{
    static containers::stack_eb< int > stack;
    int value;
    for (auto _ : state)
    {
        stack.push(1);
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}
*/

static void bounded_stack(benchmark::State& state)
{
    static containers::bounded_stack< int, 1024 > stack;

    int value;
    for (auto _ : state)
    {
        stack.push(1);
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations() * 2);
}

static void bounded_stack_push(benchmark::State& state)
{
    for (auto _ : state)
    {
        containers::bounded_stack< int, 1024 > stack;
        for (size_t i = 0; i < stack.capacity(); ++i) {
            stack.push(1);
        }
    }
    state.SetBytesProcessed(state.iterations() * 1024);
}

static void bounded_stack_pop(benchmark::State& state)
{
    static containers::bounded_stack< int, 1024 > stack;

    int value;
    for (auto _ : state)
    {
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void unbounded_blocked_stack(benchmark::State& state)
{
    static containers::unbounded_blocked_stack< int > stack;

    int value;
    for (auto _ : state)
    {
        stack.push(1);
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations() * 2);
}

static void unbounded_blocked_stack_pop(benchmark::State& state)
{
    static containers::unbounded_blocked_stack< int > stack;

    int value;
    for (auto _ : state)
    {
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void elimination_stack(benchmark::State& state)
{
    static containers::elimination_stack< int, 8 > stack;
    int value{};
    for (auto _ : state)
    {
        stack.push(value, 64);
        stack.pop(value, 64);
    }

    state.SetItemsProcessed(state.iterations() * 2);
}

BENCHMARK(stl_stack)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(stl_stack_pop)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(unbounded_stack)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(unbounded_stack_pop)->ThreadRange(1, max_threads)->UseRealTime();
//BENCHMARK(hazard_era_stack_eb)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(bounded_stack)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(bounded_stack_push)->Iterations(iterations)->UseRealTime();
BENCHMARK(bounded_stack_pop)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK(unbounded_blocked_stack)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(unbounded_blocked_stack_pop)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK(elimination_stack)->ThreadRange(1, max_threads)->UseRealTime();

template < typename T > struct function_thread_local
{
    static T& instance() { static thread_local T value; return value; }
};

template < typename T > struct class_thread_local
{
    static T& instance() { return value; }
    static thread_local T value;
};

template< typename T > thread_local T class_thread_local< T >::value;

template < typename Class > static void thread_local_benchmark(benchmark::State& state)
{
    Class::instance() = 1;
    volatile int value = 0;
    for (auto _ : state)
    {
        value += Class::instance();
    }

    benchmark::DoNotOptimize(value);
    state.SetItemsProcessed(state.iterations());

}

//BENCHMARK_TEMPLATE(thread_local_benchmark, function_thread_local< int >)->ThreadRange(1, max_threads)->UseRealTime();
//BENCHMARK_TEMPLATE(thread_local_benchmark, class_thread_local< int >)->ThreadRange(1, max_threads)->UseRealTime();
