//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/hazard_era_allocator.h>
#include <containers/lockfree/detail/aligned.h>
#include <containers/lockfree/atomic16.h>

#include <atomic>
#include <memory>
#include <iostream>

namespace containers
{
    // https://en.wikipedia.org/wiki/Treiber_stack
    template< typename T, typename Allocator = hazard_era_allocator< T >, typename Backoff = exp_backoff<> > class unbounded_stack
    {
        struct stack_node
        {
            T value;
            stack_node* next;
        };

        using allocator_type = typename Allocator::template rebind< stack_node >::other;
        allocator_type& allocator_;

        alignas(64) std::atomic< stack_node* > head_;

    public:
        unbounded_stack(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast< allocator_type* >(&allocator))
        {}

        ~unbounded_stack()
        {
            clear();
        }

        template< typename Ty > void push(Ty&& value)
        {
            auto head = allocator_.allocate(std::forward< Ty >(value), head_.load(std::memory_order_relaxed));
            Backoff backoff;
            while (!head_.compare_exchange_weak(head->next, head))
                backoff();
        }

        bool pop(T& value)
        {
            Backoff backoff;
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                if (!head)
                {
                    return false;
                }
                
                if (head_.compare_exchange_weak(head, head->next))
                {
                    value = std::move(head->value);
                    allocator_.retire(head);
                    return true;
                }
                else
                    backoff();
            }
        }

    private:
        void clear()
        {
            auto head = head_.load();
            while (head)
            {
                auto next = head->next;
                allocator_.deallocate_unsafe(head);
                head = next;
            }
        }
    };

    // Non-blocking Array-based Algorithms for Stack and Queues - https://link.springer.com/chapter/10.1007/978-3-540-92295-7_10
    template< typename T, size_t Size, typename Backoff, uint32_t Mark = 0 > struct bounded_stack_base
    {
        struct node
        {
            alignas(8) T value;
            alignas(8) uint32_t index;
            uint32_t counter;
        };

        static_assert(sizeof(node) == 16);
        static_assert(Size > 1);

        alignas(64) atomic16< node > top_{};
        alignas(64) std::array< atomic16< node >, Size + 1 > array_{};
    
        using value_type = T;

        bool push(T value)
        {
            Backoff backoff;
            while (true)
            {
                auto top = top_.load(std::memory_order_relaxed);
                if (Mark && top.index == Mark)
                    return false;
                if (top.index == array_.size() - 1)
                    return false;

                // See comment below, if the stack is full, we do not need to finish the top,
                // as only operation that can be done is pop and that will finish it.
                finish(top);

                auto aboveTopCounter = array_[top.index + 1].load(std::memory_order_relaxed).counter;
                if (top_.compare_exchange_strong(top, node{ value, top.index + 1, aboveTopCounter + 1 }))
                    return true;
                backoff();
            }
        }

        bool pop(T& value)
        {
            Backoff backoff;
            while (true)
            {
                auto top = top_.load(std::memory_order_relaxed);
                if (Mark && top.index == Mark)
                    return false;
                if (top.index == 0)
                    return false;

                // The article has finish() before if(top.index == 0), yet that worsens
                // pop() scalability in empty stack. As pop on empty stack has no effect,
                // and push() still helps with finish, it is safe.
                finish(top);

                auto belowTop = array_[top.index - 1].load(std::memory_order_relaxed);
                if (top_.compare_exchange_strong(top, node{ belowTop.value , top.index - 1, belowTop.counter + 1 }))
                    return true;
                backoff();
            }
        }

        static constexpr size_t capacity() { return Size; }

    private:
        void finish(node& n)
        {
            assert(!Mark || n.index != Mark);
            auto top = array_[n.index].load(std::memory_order_relaxed);
            node expected = { top.value, n.index, n.counter - 1 };
            array_[n.index].compare_exchange_strong(expected, { n.value, n.index, n.counter });
        }
    };

    template< typename T, size_t Size > struct elimination_stack
    {
        struct operation
        {
            enum
            {
                none = 0,
                push,
                pop,
            };

            alignas(8) T value;
            uint32_t type;
            uint32_t index;
        };

        alignas(64) std::array< aligned< atomic16< operation > >, Size > eliminations_{};

        struct thread_data
        {
            int hit;
            int spin;
            int width;
        };

        std::array< aligned< thread_data >, thread::max_threads > data_{};
        
        const int threshold = 256;

        bool push(T& value, size_t spin)
        {
            operation op { value, operation::push, 0 };
            return eliminate(op, spin);
        }

        bool pop(T& value, size_t spin)
        {
            operation op { T{}, operation::pop, 0 };
            if (eliminate(op, spin))
            {
                value = std::move(op.value);
                return true;
            }

            return false;
        }

        bool eliminate(operation& op, size_t spin)
        {
            auto width = std::max(1, data_[thread::instance().id()].width);
            auto index = thread::instance().id() & (width - 1);
            index += Size / 2 - width / 2;
            //auto index = thread::instance().id() & (Size - 1);

            auto eli = eliminations_[index].load(std::memory_order_relaxed);
            if (eli.type == operation::none)
            {
                if(!spin)
                    return false;
                
                if (eliminations_[index].compare_exchange_strong(eli, op))
                {
                    auto wait = spin; //spin_[thread::instance().id()];
                    while(wait--) _mm_pause();

                    if (!eliminations_[index].compare_exchange_strong(op, operation{ T{}, operation::none, 0 }))
                    {
                        clear(index);
                        if(spin) update(true, spin);
                        return true;
                    }

                    if(spin) update(false, spin);
                }
            }
            else if (eli.type != op.type)
            {
                bool result = eliminate(op, eli, index);
                if(spin) update(result, spin);
                return result;
            }

            return false;
        }

        bool eliminate(operation& op, operation& eli, uint32_t index)
        {
            switch (op.type)
            {
            case operation::pop:
                if (eliminations_[index].compare_exchange_strong(eli, operation{ T{}, operation::none, 0 }))
                {
                    op.value = std::move(eli.value);
                    return true;
                }
                break;
            case operation::push:
                if (eliminations_[index].compare_exchange_strong(eli, operation{ op.value, operation::none, 0 }))
                    return true;
                break;
            default:
                assert(false);
            }

            return false;
        }

        void clear(uint32_t index)
        {
            eliminations_[index].store({ T{}, operation::none, 0 }, std::memory_order_relaxed);
        }

        void update(bool result, size_t spin)
        {
            auto& data = data_[thread::instance().id()];
            if(result)
            {
                if(data.hit++ > threshold)
                {
                    data.spin = std::max(1, data.spin / 2);
                    data.width = std::max(1, data.width / 2);
                    data.hit = 0;
                }
            }
            else
            {
                if (data.hit-- < -threshold)
                {
                    data.spin = std::max(1, (data.spin * 2) & 1023);
                    data.width = std::max((int)1, (int)((data.width * 2) & (Size - 1)));
                    data.hit = 0;
                }
            }
        }
    };

    template< typename T, size_t Size, typename Backoff, uint32_t Mark = 0 > struct bounded_stack_base_eb
    {
        struct node
        {
            alignas(8) T value;
            uint32_t index;
            uint32_t counter;
        };

        static_assert(sizeof(node) == 16);
        static_assert(Size > 1);

        alignas(64) atomic16< node > top_{};
        alignas(64) std::array< atomic16< node >, Size + 1 > array_{};

        using value_type = T;

        elimination_stack< T, thread::max_threads / 2 > elimination_stack_;

        bool push(T value)
        {
            Backoff backoff;
            while (true)
            {
                auto top = top_.load(std::memory_order_relaxed);
                if (Mark && top.index == Mark)
                    return false;
                if (top.index == array_.size() - 1)
                    return false;
                
                // See comment below, if the stack is full, we do not need to finish the top,
                // as only operation that can be done is pop and that will finish it.
                finish(top);

                auto aboveTopCounter = array_[top.index + 1].load(std::memory_order_relaxed).counter;
                if (top_.compare_exchange_strong(top, node{ value, top.index + 1, aboveTopCounter + 1 }))
                    return true;

                // TODO: something is wrong, the elimination helps only for large contention - 256 512 threads
                // and does not react to spin.
                if (elimination_stack_.push(value, 0))
                   return true;

                backoff();
            }
        }
       
        bool pop(T& value)
        {
            Backoff backoff;
            while (true)
            {
                auto top = top_.load(std::memory_order_relaxed);
                if (Mark && top.index == Mark)
                    return false;
                if (top.index == 0)
                    return false;
                
                // The article has finish() before if(top.index == 0), yet that worsens
                // pop() scalability in empty stack. As pop on empty stack has no effect,
                // and push() still helps with finish, it is safe.
                finish(top);

                auto belowTop = array_[top.index - 1].load(std::memory_order_relaxed);
                if (top_.compare_exchange_strong(top, node{ belowTop.value , top.index - 1, belowTop.counter + 1 }))
                    return true;

                if(elimination_stack_.pop(value, 0))
                    return true;

                backoff();
            }
        }

        constexpr size_t capacity() const { return Size; }        

    private:
        void finish(node& n)
        {
            assert(Mark && n.index != Mark);
            auto top = array_[n.index].load(std::memory_order_relaxed);
            node expected = { top.value, n.index, n.counter - 1 };
            array_[n.index].compare_exchange_strong(expected, { n.value, n.index, n.counter });
        }
    };

    template< typename T, size_t Size, typename Backoff = exp_backoff<> > class bounded_stack
        : private bounded_stack_base< T, Size, Backoff >
    {
    public:
        using value_type = typename bounded_stack_base< T, Size, Backoff >::value_type;
        using bounded_stack_base< T, Size, Backoff >::push;
        using bounded_stack_base< T, Size, Backoff >::pop;
        using bounded_stack_base< T, Size, Backoff >::capacity;
    };

    //
    // This algorithm I don't remember reading anywhere, it is pretty simple,
    // but not proven and amateurish attempt, so be warned.
    //
    // With N=128, we can run hazard_era_reclamation with every allocation/deallocation
    // without performance impact.
    //
    template<
        typename T,
        typename Allocator = hazard_era_allocator< T >,
        typename Backoff = exp_backoff<>,
        typename InnerStack = bounded_stack_base< T, 128, Backoff, -1 >
    > class unbounded_blocked_stack
    {
        struct node
        {
            node* next{};
            InnerStack stack;
        };

        using allocator_type = typename Allocator::template rebind< node >::other;
        allocator_type& allocator_;
        
        alignas(64) std::atomic< node* > head_{};

    public:
        unbounded_blocked_stack(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {
            head_ = allocator_.allocate(nullptr);
        }

        ~unbounded_blocked_stack()
        {
            clear();
        }

        template< typename Ty > void push(Ty&& value)
        {
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                auto top = head->stack.top_.load(std::memory_order_relaxed);
                if (head->stack.push(std::forward< Ty >(value)))
                    return;

                if (top.index == -1 && head_.compare_exchange_weak(head, head->next))
                {
                    allocator_.retire(head);
                }
                else
                {
                    head = allocator_.allocate(nullptr);
                    if (!head_.compare_exchange_strong(head->next, head))
                        allocator_.deallocate_unsafe(head);
                }
            }
        }

        bool pop(T& value)
        {
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                if (!head)
                    return false;
                
                auto top = head->stack.top_.load(std::memory_order_relaxed);
                if (head->stack.pop(value))
                    return true;

                if(!head->next)
                    return false;

                if(top.index == -1 || head->stack.top_.compare_exchange_strong(top, {T{}, (uint32_t)-1, top.counter + 1}))
                {
                    if (head_.compare_exchange_weak(head, head->next))
                        allocator_.retire(head);
                }
            }
        }

    private:
        void clear()
        {
            auto head = head_.load();
            while (head)
            {
                auto next = head->next;
                allocator_.deallocate_unsafe(head);
                head = next;
            }
        }
    };

    /*
    // A Scalable Lock-free Stack Algorithm - https://people.csail.mit.edu/shanir/publications/Lock_Free.pdf
    template< typename T, typename Allocator = hazard_era_allocator< T >, typename Backoff = exp_backoff<> > class stack_eb
    {
        struct stack_node
        {
            T value;
            stack_node* next;
        };

        using allocator_type = typename Allocator::template rebind< stack_node >::other;
        allocator_type allocator_;

        struct operation
        {
            size_t thread_id;
            bool push;
            stack_node* node;
        };

        hazard_era_allocator< operation > op_allocator_;
        
        static constexpr int32_t collision_freq = 4;
        static_assert(is_power_of_2< collision_freq >::value);

        alignas(64) std::array< aligned< std::atomic< operation* > >, thread::max_threads > location_;
        alignas(64) std::array< aligned< std::atomic< size_t > >, thread::max_threads / 4 > collision_;
        alignas(64) std::array< aligned< std::atomic< int32_t > >, thread::max_threads > collision_count_;
        alignas(64) std::array< aligned< std::atomic< uint64_t > >, thread::max_threads > collision_area_;
        alignas(64) std::array< aligned< std::atomic< uint32_t > >, thread::max_threads > spin_;
        alignas(64) std::atomic< stack_node* > head_;

    public:
        stack_eb(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {}

        ~stack_eb()
        {
            clear();
        }

        template< typename Ty > void push(Ty&& value)
        {
            auto guard = allocator_.guard();
            auto head = allocator_.allocate(std::forward< Ty >(value), head_.load(std::memory_order_relaxed));
            while (true)
            {
                if(head_.compare_exchange_weak(head->next, head))
                {
                   return;
                }
                else
                {
                    auto op = op_allocator_.allocate(thread_id(), true, head);
                    op_allocator_.protect(op);
                    if (collide(op))
                        return;
                    op_allocator_.retire(op);
                }
            }
        }

        bool pop(T& value)
        {
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                if (!head)
                    return false;
                
                if (head_.compare_exchange_weak(head, head->next))
                {
                    value = std::move(head->value);
                    allocator_.retire(head);
                    return true;
                }
                else
                {
                    operation op{ thread_id(), false, nullptr };
                    if (collide(&op))
                    {
                        value = std::move(op.node->value);
                        allocator_.deallocate_unsafe(op.node);
                        return true;
                    }
                }
            }
        }

    private:
        size_t thread_id() { return allocator_.thread_id(); }

        void backoff_update(bool success)
        {
            if (success)
            {
                if(collision_count_[thread_id()].fetch_add(1, std::memory_order_relaxed) == collision_freq)
                {
                    collision_count_[thread_id()].store(0, std::memory_order_relaxed);

                    auto spin = spin_[thread_id()].load(std::memory_order_relaxed);
                    spin_[thread_id()].store(spin * 2, std::memory_order_relaxed);
                }
            }
            else
            {
                if (collision_count_[thread_id()].fetch_sub(1, std::memory_order_relaxed) == -collision_freq)
                {
                    collision_count_[thread_id()].store(0, std::memory_order_relaxed);

                    auto spin = std::max(uint32_t(2), spin_[thread_id()].load(std::memory_order_relaxed));
                    spin_[thread_id()].store(spin / 2, std::memory_order_relaxed);
                }
            }
        }

        bool collide(operation* op)
        {
            Backoff backoff;
            assert(location_[op->thread_id].load() == 0);
            location_[op->thread_id].store(op);
            auto pos = op->thread_id & (collision_.size() - 1);
            auto other_id = collision_[pos].load(std::memory_order_relaxed);
            while(!collision_[pos].compare_exchange_weak(other_id, op->thread_id))
                backoff();
            if (other_id)
            {
                auto other_op = op_allocator_.protect(location_[other_id]);
                if (other_op && other_op->thread_id == other_id && other_op->push != op->push)
                {
                    auto p = op;
                    if (location_[op->thread_id].compare_exchange_strong(p, nullptr))
                    {
                        bool result = collide(op, other_op);
                        backoff_update(result);
                        assert(location_[op->thread_id].load() == 0);
                        return result;
                    }
                    else
                    {
                        if (!op->push)
                        {
                            assert(p);
                            assert(p->push);
                            assert(p->node);
                            op->node = p->node;
                            location_[op->thread_id].store(nullptr);
                            op_allocator_.retire(p);
                        }

                        backoff_update(true);
                        assert(location_[op->thread_id].load() == 0);
                        return true;
                    }
                }
            }

            auto spin = 1024; //spin_[op->thread_id].load(std::memory_order_relaxed);
            while(spin--) _mm_pause();

            auto p = op;
            if (!location_[op->thread_id].compare_exchange_strong(p, nullptr))
            {
                if (!op->push)
                {
                    assert(p);
                    assert(p->push);
                    assert(p->node);
                    op->node = p->node;
                    location_[op->thread_id].store(nullptr);
                    op_allocator_.retire(p);
                }

                backoff_update(true);
                assert(location_[op->thread_id].load() == 0);
                return true;
            }
            else
            {
                backoff_update(false);
                assert(location_[op->thread_id].load() == 0);
                return false;
            }
        }

        bool collide(operation* op, operation* other_op)
        {
            assert(op);
            assert(other_op);
            assert(op != other_op);
            assert(op->thread_id != other_op->thread_id);
            assert(op->push != other_op->push);

            assert(location_[op->thread_id].load() == 0);

            if (op->push)
            {
                return location_[other_op->thread_id].compare_exchange_strong(other_op, op);
            }
            else
            {
                if (location_[other_op->thread_id].compare_exchange_strong(other_op, nullptr))
                {
                    assert(other_op->node);
                    op->node = other_op->node;
                    op_allocator_.retire(other_op);
                    return true;
                }
                else
                {
                    return false;
                }
            }
        }

        void clear()
        {
            auto head = head_.load();
            while (head)
            {
                auto next = head->next;
                allocator_.deallocate_unsafe(head);
                head = next;
            }
        }
    };
    */
}
