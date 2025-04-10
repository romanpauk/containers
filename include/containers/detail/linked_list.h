//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>

namespace containers {
    namespace detail {
        template< typename Node > struct linked_list {
            using node_type = Node;

            struct iterator {
                iterator(const node_type* n) : node_(n) {}

                bool operator == (const iterator& other) const { return node_ == other.node_; }
                bool operator != (const iterator& other) const { return node_ != other.node_; }

                iterator& operator++() {
                    assert(node_);
                    node_ = node_->next;
                    return *this;
                }

                iterator operator++(int) {
                    assert(node_);
                    const node_type* n = node_;
                    node_ = node_->next;
                    return n;
                }

                const node_type& node() {
                    assert(node_);
                    return *node_;
                }

            private:
                const node_type* node_;
            };

            iterator begin() const {
                assert(!head_ || !head_->prev);
                return head_;
            }

            iterator end() const {
                assert(!tail_ || !tail_->next);
                return nullptr;
            }

            void push_front(const node_type& n) {
                n.prev = nullptr;
                n.next = head_;
                if (head_) {
                    assert(!head_->prev);
                    assert(tail_ && !tail_->next);
                    head_->prev = &n;
                    head_ = &n;
                } else {
                    assert(!tail_);
                    head_ = tail_ = &n;
                }
            }

            void push_back(const node_type& n) {
                n.next = nullptr;
                n.prev = tail_;
                if (!tail_) {
                    assert(!head_);
                    tail_ = head_ = &n;
                } else {
                    assert(head_ && !head_->prev);
                    tail_->next = tail_ = &n;
                }
            }

            const node_type* erase(const node_type& n) {
                if (n.next) {
                    n.next->prev = n.prev;
                } else {
                    assert(tail_ == &n);
                    tail_ = n.prev;
                }

                if (n.prev) {
                    n.prev->next = n.next;
                } else {
                    assert(head_ == &n);
                    head_ = n.next;
                }

                return n.next;
            }

            const node_type* head() const {
                assert(!head_ || !head_->prev);
return head_;
            }

            const node_type* tail() const {
                assert(!tail_ || !tail_->next);
                return tail_;
            }

            void clear() { head_ = tail_ = nullptr; }

            bool empty() const { return head_ == nullptr; }

        private:
            const node_type* head_ = nullptr;
            const node_type* tail_ = nullptr;
        };
    }
}

