/**
 * Copyright The bmalloc Contributors
 * @file slab_test.cpp
 * @brief Slab分配器的Google Test测试用例
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

#include "slab.hpp"
