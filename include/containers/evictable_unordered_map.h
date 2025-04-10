//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <cassert>
#include <memory>
#include <unordered_set>

#include <containers/detail/linked_list.h>
#include <containers/eviction/lru.h>

namespace containers {
    namespace detail {
    #if __cpp_lib_generic_unordered_lookup != 201811L
        template< typename Node, typename Key, bool IsTriviallyDestructible = std::is_trivially_destructible_v<Key> > struct hashable_node;

        template< typename Node, typename Key> struct hashable_node<Node, Key, true> {
            hashable_node(const Key& key) {
                new (const_cast<Key*>(&node().value.first)) Key(key);
            }

            Node& node() { return *reinterpret_cast<Node*>(&storage_); }
        private:
            std::aligned_storage_t< sizeof(Node) > storage_;
        };

        template< typename Node, typename Key > struct hashable_node<Node, Key, false>
            : hashable_node<Node, Key, true>
        {
            hashable_node(const Key& key): hashable_node<Node, Key, true>(key) {}
            ~hashable_node() { const_cast<Key*>(&this->node().value.first)->~Key(); }
        };
    #endif
    };

    template<
        typename Key,
        typename Value,
        typename Hash = std::hash<Key>,
        typename KeyEqual = std::equal_to<Key>,
        typename Allocator = std::allocator< std::pair<const Key, Value > >,
        typename Eviction = eviction::lru< std::pair< const Key, Value > >
    > class evictable_unordered_map {
    public:
        using eviction_type = Eviction;
        using node_type = typename Eviction::node;
        using value_type = std::pair< const Key, Value >;
        using allocator_type = Allocator;

    private:
        struct hash: Hash {
            size_t operator()(const node_type& n) const noexcept { return static_cast<const Hash&>(*this)(n.value.first); }
        #if __cpp_lib_generic_unordered_lookup == 201811L
            using is_transparent = void;
            size_t operator()(const Key& key) const noexcept { return static_cast<const Hash&>(*this)(key); }
        #endif
        };

        struct key_equal : KeyEqual {
            size_t operator()(const node_type& lhs, const node_type& rhs) const noexcept { return static_cast<const KeyEqual&>(*this)(lhs.value.first, rhs.value.first); }
        #if __cpp_lib_generic_unordered_lookup == 201811L
            using is_transparent = void;
            size_t operator()(const Key& lhs, const node_type& rhs) const noexcept { return static_cast<const KeyEqual&>(*this)(lhs, rhs.value.second); }
        #endif
        };

        using values_type = std::unordered_set< node_type, hash, key_equal,
            typename std::allocator_traits< Allocator >::template rebind_alloc< node_type > >;

        eviction_type eviction_;
        values_type values_;

    public:
        evictable_unordered_map() = default;
        evictable_unordered_map(Allocator allocator): values_(allocator) {}

        struct iterator {
            iterator(typename values_type::iterator it): it_(it) {}

            const std::pair<const Key, Value>& operator*() { return it_->value; }
            const std::pair<const Key, Value>* operator->() { return &it_->value; }

            bool operator == (const iterator& other) const { return it_ == other.it_; }
            bool operator != (const iterator& other) const { return it_ != other.it_; }

            iterator& operator++() { return ++it_; }
            iterator operator++(int) { typename values_type::iterator it = it_; ++it_; return it; }

        private:
            template< typename KeyT, typename ValueT, typename HashT, typename KeyEqualT, typename AllocatorT, typename CacheT>
            friend class evictable_unordered_map;

            const node_type& node() const { return *it_; }

            typename values_type::iterator it_;
        };

        iterator begin() { return values_.begin(); }
        iterator end() { return values_.end(); }

        template<typename... Args> std::pair<iterator, bool> emplace(Args&&... args) {
            auto it = values_.emplace(node_type{{std::forward<Args>(args)...}});
            eviction_.emplace(*it.first, it.second);
            return {it.first, it.second};
        }


        iterator find(const Key& key) {
            auto it = lookup(key);
            if (it != end()) {
                eviction_.touch(it.node());
            }
            return it;
        }

        Value& operator[](const Key& key) {
            auto it = lookup(key);
            if (it != end()) {
                eviction_.touch(it.node());
                return const_cast<Value&>(it->second); // TODO: const_cast
            }
            return const_cast<Value&>(emplace(key, Value()).first->second);
        }

        size_t erase(const Key& key) {
            auto it = lookup(key);
            if (it != end()) {
                eviction_.erase(it.node());
                values_.erase(it);
                return 1;
            }
            return 0;
        }

        iterator erase(const iterator& it) {
            assert(it != end());
            eviction_.erase(it.node());
            return values_.erase(it.it_);
        }

        void clear() {
            eviction_.clear();
            values_.clear();
        }

        size_t size() const { return values_.size(); }
        bool empty() const { return values_.empty(); }

        void touch(const iterator& it) {
            assert(it != end());
            eviction_.touch(it.node());
        }

        void touch(const Key& key) {
            auto it = lookup(key);
            if (it != end())
                eviction_.touch(it.node());
        }

        const eviction_type& eviction() { return eviction_; }

        iterator evictable() {
            auto it = eviction_.evictable();
            if (it != eviction_.end())
                return values_.find(it.node());
            return end();
        }

    private:
        iterator lookup(const Key& key) {
        #if __cpp_lib_generic_unordered_lookup == 201811L
            auto it = values_.find(key);
        #else
            // This still needs to copy the key, but at least not the value.
            detail::hashable_node<node_type, Key> key_node(key);
            auto it = values_.find(key_node.node());
        #endif
            return it;
        }
    };
}
