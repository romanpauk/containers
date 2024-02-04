//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/eh/eh.h>

#include <gtest/gtest.h>

TEST(eh_test, basic_operations) {
    containers::hash_table<int> x;
    for(size_t i = 1; i < 128/2; ++i)
        x.insert(i);

    volatile int a = 0;
}

TEST(eh_test, collisions) {
    for (size_t i = 128/4; i < 1000; i += 128/4) {
        containers::hash_table<int> x;
        for (size_t j = 1; j <= i; ++j)
            x.insert(j);

        fprintf(stderr, "N: %lu, collisions %lu\n", i, x.collisions());
    }
}
