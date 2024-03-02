//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/growable_array.h>

#include <gtest/gtest.h>

TEST(growable_array, basics_trivial) {
    containers::growable_array<size_t> array;
    for (size_t i = 0; i < 10000; ++i) {
        array.emplace_back(i);

        for (size_t j = 0; j < i; ++j) {
            ASSERT_EQ(array[j], j);
        }
    }
}

TEST(growable_array, basics) {
    containers::growable_array<std::string> array;
    for (size_t i = 0; i < 10000; ++i) {
        array.emplace_back(std::to_string(i));

        for (size_t j = 0; j < i; ++j) {
            ASSERT_EQ(array[j], std::to_string(j));
        }
    }
}
