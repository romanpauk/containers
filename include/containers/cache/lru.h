//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <cassert>
#include <optional>
#include <unordered_set>

namespace containers {
    template< typename Key, typename Value > class lru_unordered_map {
        struct node {
            bool operator == (const node& n) const noexcept { return value.first == n.value.first; }

            std::pair<const Key, Value> value;
            mutable const node* next;
            mutable const node* prev;
        };

        struct hash {
            size_t operator()(const node& n) const noexcept { return std::hash<Key>()(n.value.first); }
        };

        struct list {
            const node* head_ = nullptr;
            const node* tail_ = nullptr;

            const node* front() const {
                assert(!head_ || !head_->prev);
                return head_;
            }

            void push_front(const node* n) {
                assert(n);
                if (head_) {
                    assert(!head_->prev);
                    assert(tail_);
                    assert(!tail_->next);
                    n->prev = nullptr;
                    n->next = head_;
                    head_->prev = n;
                    head_ = n;
                } else {
                    assert(!tail_);
                    head_ = tail_ = n;
                    n->prev = n->next = nullptr;
                }
            }

            void erase(const node* n) {
                assert(n);
                if (n->next) {
                    n->next->prev = n->prev;
                } else {
                    assert(tail_ == n);
                    tail_ = n->prev;
                }

                if (n->prev) {
                    n->prev->next = n->next;
                } else {
                    assert(head_ == n);
                    assert(!tail_);
                    head_ = nullptr;
                }
            }

            const node* back() const {
                assert(!tail_ || !tail_->next);
                return tail_;
            }

            const node* pop_back() {
                const node* n = tail_;
                if (tail_) {
                    tail_ = tail_->prev;
                    if (tail_) {
                        tail_->next = nullptr;
                    } else {
                        assert(!tail_);
                        head_ = nullptr;
                    }
                }
                return n;
            }

            void clear() { head_ = tail_ = nullptr; }
        };

        using values_type = std::unordered_set< node, hash >;

        values_type values_;
        list list_;

    public:
        struct iterator {
            iterator(typename values_type::iterator it): it_(it) {}

            const std::pair<const Key, Value>& operator*() { return it_->value; }
            const std::pair<const Key, Value>* operator->() { return &it_->value; }

            bool operator == (const iterator& other) const { return it_ == other.it_; }
            bool operator != (const iterator& other) const { return it_ != other.it_; }

            iterator& operator++() { return ++it_; }
            iterator operator++(int) { typename values_type::iterator it = it_; ++it_; return it; }

        private:
            template< typename Key, typename Value > friend class lru_unordered_map;
            typename values_type::iterator it_;
        };

        // TODO: what iterator operations should change evictability?
        // Intuitivelly find yes, iteration no... but it would be better all or none.
        iterator begin() { return values_.begin(); }
        iterator end() { return values_.end(); }

        template<typename... Args> std::pair<iterator, bool> emplace(Args&&... args) {
            auto it = values_.emplace(node{{std::forward<Args>(args)...}});
            const node* n = &*it.first;
            if (it.second) {
                list_.push_front(n);
            } else if (n != list_.front()) {
                list_.erase(n);
                list_.push_front(n);
            }
            return {it.first, it.second};
        }

        iterator find(const Key& key) {
            return find_impl<true>(key);
        }

        Value& operator[](const Key& key) {
            return const_cast<Value&>(emplace(key, Value()).first->second);
        }

        void erase(const Key& key) {
            auto it = find_impl<false>(key);
            if (it != values_.end()) {
                list_.erase(&it->second);
                values_.erase(it);
            }
        }

        void erase(iterator it) {
            list_.erase(&*it.it_);
            values_.erase(it.it_);
        }

        void clear() {
            values_.clear();
            list_.clear();
        }

        size_t size() const { return values_.size(); }
        bool empty() const { return values_.empty(); }

        iterator evictable() {
            auto* n = list_.back();
            return n ? values_.find(*n) : end();
        }

    private:
        template< bool Update > iterator find_impl(const Key& key) {
            // TODO: this is solved by heterogenous hashing in C++20, what about C++17?
            auto it = values_.find({ {key, Value()} });
            if constexpr (Update) {
                if (it != values_.end()) {
                    auto* n = &*it;
                    if (list_.front() != n) {
                        list_.erase(n);
                        list_.push_front(n);
                    }
                }
            }
            return it;
        }
    };
}
