//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/detail/linked_list.h>

namespace containers::eviction {
    template< typename T > struct lru {
        struct node {
            using value_type = T;
            value_type value;
            mutable const node* next = nullptr;
            mutable const node* prev = nullptr;
        };

        using iterator = typename detail::linked_list<node>::iterator;

        iterator evictable() const {
            return list_.tail();
        }

        iterator end() const { return list_.end(); }

        void erase(const node& n) { list_.erase(n); }

        void emplace(const node& n, bool inserted) {
            if (inserted) {
                list_.push_front(n);
            } else if (&n != list_.head()) {
                list_.erase(n);
                list_.push_front(n);
            }
        }

        void touch(const node& n) {
            list_.erase(n);
            list_.push_front(n);
        }

    private:
        detail::linked_list<node> list_;
    };

    template< typename T > struct lru_segmented {
        struct node {
            using value_type = T;
            value_type value;
            mutable detail::linked_list<node>* segment = nullptr;
            mutable const node* next = nullptr;
            mutable const node* prev = nullptr;
        };

        using iterator = typename detail::linked_list<node>::iterator;

        iterator evictable() const {
            return segments_[0].empty() ? segments_[1].tail() : segments_[0].tail();
        }

        iterator end() const { return typename detail::linked_list<node>::iterator(nullptr); }

        void erase(const node& n) {
            n.segment->erase(n);
        }

        void emplace(const node& n, bool inserted) {
            if (inserted) {
                n.segment = &segments_[0];
                segments_[0].push_front(n);
            } else {
                n.segment->erase(n);
                n.segment = &segments_[1];
                segments_[1].push_front(n);
            }
        }

        void touch(const node& n) {
            n.segment->erase(n);
            n.segment = &segments_[1];
            segments_[1].push_front(n);
        }

    private:
        detail::linked_list<node> segments_[2];
    };
}

