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

#include <containers/atomic_bitset.h>

namespace containers {
    template< typename Page, std::size_t PageCount, std::size_t PageSize > struct vbr_page_allocator {
        std::atomic<uint64_t> version_ = 0x100;

        uint64_t get_version() { return version_.fetch_add(1, std::memory_order_relaxed); }

        void* allocate_page() {
        #if 0
            while(page* p = queued_pages_.pop()) {
                // Construct expected state as in case of deallocated page,
                // page will be zeroed, so we can't trust it unless we
                // successfully CAS into it for the first time.
                uint64_t state = (p->state() & ~0xFF) | page::queued;
                if (p->update_state_version(state, get_version() | page::active)) {
                    // Page state was queued (= not deallocated)
                  p->fixup();
                  return p;
              }
            }

            // TODO: Go through deallocated pages
            //
            // TODO: Commit a new page
        #endif
            return nullptr;
        }

        void queue_page(Page* p) {
            // TODO: we can queue a page that was deallocated. Should not matter
            // as it is handled in allocate_page().
        }

        void deallocate_page(Page* p) {
            // TODO: we can deallocate page that could still be queued later.
            // Should not matter as it is handled in allocate_page().
            //
            // How to do the deallocation?
            // Write to deallocated queue and deallocate from the back.
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
            atomic_bitset<256> deallocations_;

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

            uint64_t state() const { return state_.load(std::memory_order_relaxed); }

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

        vbr_page_allocator< page, N / PageElementCount, PageSize > page_allocator_;

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
            //
            // TODO: we can avoid this load, if each deallocate() call returns if this was
            // the first deallocation or not. If it was first, we will try to do the load
            // and eventually move the state from full to queued.
            // The same logic could apply to full deallocation. If the word is fully deallocated,
            // we can check deallocated size and eventually deallocate...
            //
            uint64_t state = p->state();
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
