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
    template< typename Node > struct linked_list {
        using node_type = Node;
        using value_type = typename Node::value_type;

        struct iterator {
            iterator(const node_type* n) : node_(n) {}

            const value_type& operator*() { assert(node_); return node_->value; }
            const value_type* operator->() { assert(node_); return &node_->value; }

            bool operator == (const iterator& other) const { return node_ == other.node_; }
            bool operator != (const iterator& other) const { return node_ != other.node_; }

            iterator& operator++() { assert(node_); node_ = node_->next; return *this; }
            iterator operator++(int) { assert(node_); const node_type* n = node_; node_ = node_->next; return n; }

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
/*
        void push_front(const node& n) {
            if (head_) {
                assert(!head_->prev);
                assert(tail_);
                assert(!tail_->next);
                n.prev = nullptr;
                n.next = head_;
                head_->prev = &n;
                head_ = &n;
            } else {
                assert(!tail_);
                head_ = tail_ = &n;
                n.prev = n.next = nullptr;
            }
        }
*/
        void push_back(const node_type& n) {
            if (!tail_) {
                assert(!head_);
                tail_ = head_ = &n;
            } else {
                n.next = nullptr;
                n.prev = tail_;
                tail_->next = &n;
                tail_ = &n;
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
                head_ = n.next;;
            }

            return n.next;
        }

        const node_type& front() const {
            assert(head_);
            return *head_;
        }

        const node_type& back() const {
            assert(tail_);
            return *tail_;
        }

        void clear() { head_ = tail_ = nullptr; }

        bool empty() const { return head_ == nullptr; }

    private:
        const node_type* head_ = nullptr;
        const node_type* tail_ = nullptr;
    };

    template< typename T > struct lru_cache {
        struct node {
            using value_type = T;

            bool operator == (const node& n) const noexcept { return value.first == n.value.first; }
            bool operator != (const node& n) const noexcept { return value.first != n.value.first; }

            value_type value;
            mutable const node* next;
            mutable const node* prev;
        };

        using iterator = typename linked_list<node>::iterator;

        iterator evictable() const {
            return list_.begin();
        }

        iterator end() const { return list_.end(); }
        
        void erase(const node& n) { list_.erase(n); }

        void emplace(const node& n, bool inserted) {
            if (inserted) {
                list_.push_back(n);
            } else if (n != list_.back()) {
                list_.erase(n);
                list_.push_back(n);
            }
        }

        void find(const node&) {}

        void touch(const node& n) {
            list_.erase(n);
            list_.push_back(n);
        }

    private:
        linked_list<node> list_;
    };

    template< typename T > struct lru_segmented_cache {
        struct node {
            using value_type = T;

            bool operator == (const node& n) const noexcept { return value.first == n.value.first; }
            bool operator != (const node& n) const noexcept { return value.first != n.value.first; }

            value_type value;
            mutable linked_list<node>* segment;
            mutable const node* next;
            mutable const node* prev;
        };

        using iterator = typename linked_list<node>::iterator;

        iterator evictable() const {
            if (segments_[0].empty()) {
                return segments_[1].begin();
            }
            return segments_[0].begin();
        }

        iterator end() const { return typename linked_list<node>::iterator(nullptr); }
        
        void erase(const node& n) { 
            n.segment->erase(n);
        }

        void emplace(const node& n, bool inserted) {
            if (inserted) {
                n.segment = &segments_[0];
                segments_[0].push_back(n);
            } else {
                n.segment->erase(n);
                n.segment = &segments_[1];
                segments_[1].push_back(n);
            }
        }

        void find(const node&) {}

        void touch(const node& n) {
            n.segment->erase(n);
            n.segment = &segments_[1];
            segments_[1].push_back(n);
        }

    private:
        linked_list<node> segments_[2];
    };

    template<
        typename Key,
        typename Value,
        typename Hash = std::hash<Key>,
        typename KeyEqual = std::equal_to<Key>,
        typename Allocator = std::allocator< std::pair<const Key, Value > >,
        typename Cache = lru_segmented_cache< std::pair< const Key, Value > >
    > class lru_unordered_map {
        using value_type = std::pair< const Key, Value >;
        using node_type = typename Cache::node;

        struct hash: Hash {
            size_t operator()(const node_type& n) const noexcept { return static_cast<const Hash&>(*this)(n.value.first); }
        };

        using values_type = std::unordered_set< node_type, hash >;

        values_type values_;
        Cache cache_;

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
            template< typename KeyT, typename ValueT, typename HashT, typename KeyEqualT, typename AllocatorT, typename CacheT> friend class lru_unordered_map;
            typename values_type::iterator it_;
        };

        iterator begin() { return values_.begin(); }
        iterator end() { return values_.end(); }

        template<typename... Args> std::pair<iterator, bool> emplace(Args&&... args) {
            auto it = values_.emplace(typename Cache::node{{std::forward<Args>(args)...}});
            cache_.emplace(*it.first, it.second);
            return {it.first, it.second};
        }

        iterator find(const Key& key) {
            // TODO: this is solved by heterogenous hashing in C++20, what about C++17?
            auto it = values_.find({ {key, Value()} });
            if (it != values_.end())
                cache_.find(*it);
            return it;
        }

        Value& operator[](const Key& key) {
            return const_cast<Value&>(emplace(key, Value()).first->second);
        }

        size_t erase(const Key& key) {
            auto it = find(key);
            if (it != values_.end()) {
                cache_.erase(it->second);
                values_.erase(it);
                return 1;
            }
            return 0;
        }

        iterator erase(const iterator& it) {
            cache_.erase(*it.it_);
            return values_.erase(it.it_);
        }

        void clear() {
            values_.clear();
            cache_.clear();
        }

        size_t size() const { return values_.size(); }
        bool empty() const { return values_.empty(); }

        void touch(const iterator& it) {
            assert(it != end());
            cache_.touch(*it.it_);
        }

        void touch(const Key& key) {
            auto it = find(key);
            if (it != end()) touch(it);
        }

        const Cache& cache() { return cache_; }

        iterator evictable() {
            auto it = cache_.evictable();
            if (it != cache_.end())
                return values_.find(*it.node_);
            return end();
        }
    };
}
