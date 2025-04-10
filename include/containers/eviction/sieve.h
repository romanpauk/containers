//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/detail/linked_list.h>

namespace containers::eviction {
template< typename T > struct sieve {
    struct node {
            using value_type = T;
            value_type value;
            mutable const node* next = nullptr;
            mutable const node* prev = nullptr;
            mutable bool visited = false;
        };

        using iterator = typename detail::linked_list<node>::iterator;

        iterator evictable() const {
            auto n = hand_ ? hand_ : list_.tail();
            while(n) {
                if (!n->visited)
                    return n;
                n = n->prev;
            }

            return list_.tail();
        }

        iterator end() const { return list_.end(); }

        void erase(const node& n) {
            if (&n == hand_)
                hand_ = hand_->prev;
            list_.erase(n);
        }

        void emplace(const node& n, bool inserted) {
            if (inserted) {
                list_.push_front(n);
            } else {
                touch(n);
            }
        }

        void touch(const node& n) {
            n.visited = true;
        }

    private:
        detail::linked_list<node> list_;
        const node* hand_ = nullptr;
    };
}

