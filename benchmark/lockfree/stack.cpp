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

template< typename T > class stl_stack
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

template< typename Stack > static void stack_push_pop(benchmark::State& state)
{
    static Stack stack;
    int value = 0;
    for (auto _ : state)
    {
        stack.push(1);
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations() * 2);
}

template< typename Stack > static void stack_push(benchmark::State& state)
{
    for (auto _ : state)
    {
        Stack stack;
        for (size_t i = 0; i < stack.capacity(); ++i) {
            stack.push(1);
        }
    }
    state.SetBytesProcessed(state.iterations() * Stack::capacity());
}

template< typename Stack > static void stack_pop(benchmark::State& state)
{
    static Stack stack;

    int value;
    for (auto _ : state)
    {
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void elimination_stack(benchmark::State& state)
{
    // Size 8 and spin 32 are great numbers. Why?

    static containers::elimination_stack< int, containers::thread::max_threads / 2 > stack;
    int elims = 0;
    int value = 0;
    for (auto _ : state)
    {
        if(value++ & 1)
            elims += stack.push(value, 32);
        else
            elims += stack.pop(value, 32);
    }

    state.SetItemsProcessed(elims);
}

BENCHMARK_TEMPLATE(stack_push_pop,stl_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_pop,stl_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(stack_push_pop,containers::unbounded_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_pop,containers::unbounded_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(stack_push_pop,containers::bounded_stack<int,1024>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_push,containers::bounded_stack<int,1024>)->Iterations(iterations)->UseRealTime();
BENCHMARK_TEMPLATE(stack_pop,containers::bounded_stack<int,1024>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(stack_push_pop,containers::unbounded_blocked_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_pop,containers::unbounded_blocked_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();

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
