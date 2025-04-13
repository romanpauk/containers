//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>

namespace containers {
    template< typename T, std::size_t Size, std::size_t PageSize > struct vbr_page_allocator {
        std::atomic<uint64_t> version_ = 0x100;

        uint64_t get_version() { return version_.fetch_add(1, std::memory_order_relaxed); }

        void* allocate_page() {
            while(page* p = free_pages_.pop()) {
                // Expected state, in case of deallocated page, the CAS will fail as deallocated
                // pages are zeroed and state will not be queued.
                uint64_t state = (p->state() & ~0xFF) | page::queued;
                if (p->update_state_version(state, get_version() | page::active)) {
                  p->fixup();
                  return p;
              }
            }

            // get new page
        }

        void queue_page(page* p) {
            // TODO: we can queue a page that was deallocated
        }

        void deallocate_page(page* p) {
            // TODO: we can deallocate page that will still be queued later
        }
    };

    template< typename T, std::size_t N = 1<<30, std::size_t PageSize = 4096 > struct vbr_allocator {
        using version_type = uint64_t;

        version_type epoch_;

        static constexpr std::size_t PageElementCount = PageSize / (sizeof(uint8_t) + sizeof(T));
        static_assert(PageElementCount < 256);

        struct page {
            enum state {
                decommitted = 0,
                active = 1,
                full = 2,
                queued = 3,
            };

            // Local, for owning thread only
            uint8_t allocations_size_;
            uint8_t allocations_[PageElementCount];

            // Shared between threads
            alignas(64) std::atomic<uint8_t> deallocations_size_;
            atomic_bitmap<256> deallocations_;

            alignas(64) std::atomic<uint64_t> state_;
            alignas(64) T values_[PageElementCount];

            static constexpr uint8_t capacity() { return PageElementCount; }

            uint8_t allocations_size() const { return allocations_size_; }

            T* allocate() {
                assert(allocations_size_ > 0);
                return allocations_[--allocations_size_];
            }

            void deallocate(T* ptr) {
                assert(ptr >= values_ && ptr < values_ + PageElementCount);
                deallocations_.set(ptr - values_);
                deallocations_size_.fetch_add(1, std::memory_order_release);
            }

            bool handle_deallocations() {
                assert(allocations_size_ == 0);
                // TODO: go through the bitmap and refresh free list, atomically, eg. exchange the words
            }

            bool update_state(uint64_t& state, uint8_t value) {
                return state_.compare_exchange_strong(state, (state & ~0xFF) | value);
            }

            bool update_state_version(uint64_t& state, uint64_t value) {
                return state_.compare_exchange_strong(state, value);
            }

            void fixup() {
                // acquire
                // exchange deallocations
                // add them to allocation list
                // substract deallocations
            }
        };

        static_assert(sizeof(page) <= PageSize);

        vbr_page_allocator< T, N, PageSize > page_allocator_;

        // TODO: this needs to be initialized to point to some dummy page,
        // also needs a destruction when thread is gone
        static thread_local page* page_;

        T* allocate() {
            // unlikely
            if (page_->allocations_size() == 1) {
                T* ptr = page_->allocate();

                uint64_t state = page_->state();
                assert(state & page::active);
                if (page_->update_state(state, page::full)) {
                    state |= page::full;
                    if (page_->handle_deallocations()) {
                        if (page_->update_state(state, page::active))
                            return ptr;
                    }
                }

                page_ = page_allocator_.allocate_page();
                return ptr;
            }

            return page_->allocate();
        }

        void deallocate(T* ptr) {
            auto p = get_page(ptr);
            uint8_t state = p->state();
            p->deallocate(ptr);
            if (state & page::active)
                return;

            assert((state & page::full) || (state & page::queued));

            if (!(state & page::queued)) {
                if(p->update_state(state, page::queued)) {
                    page_allocator_.queue_page(p);
                }
            }

            if (p->capacity() == p->deallocated_size()) {
                if (p->update_state(state, page::deallocated)) {
                    page_allocator_.deallocate_page(p);
                }
            }
        }
    };
}
