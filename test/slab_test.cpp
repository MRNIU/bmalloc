/**
 * Copyright The bmalloc Contributors
 * @file slab_test.cpp
 * @brief Slab分配器的Google Test测试用例
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include "buddy.hpp"
#include "slab.hpp"

using namespace bmalloc;

// 日志函数类型
struct TestLogger {
  int operator()(const char* format, ...) const {
    va_list args;
    va_start(args, format);
    int result = vprintf(format, args);
    va_end(args);
    return result;
  }
};

// 测试用的锁实现
class TestLock : public LockBase {
 private:
  std::mutex mutex_;

 public:
  void Lock() override { mutex_.lock(); }

  void Unlock() override { mutex_.unlock(); }
};

// 测试夹具
class SlabTest : public ::testing::Test {
 protected:
  static constexpr size_t kTestMemorySize =
      128 * 1024;  // 128KB，支持更大的测试
  static constexpr size_t kTestPages = kTestMemorySize / kPageSize;  // 32页

  void SetUp() override {
    test_memory_ = aligned_alloc(kPageSize, kTestMemorySize);
    ASSERT_NE(test_memory_, nullptr) << "Failed to allocate test memory";
    memset(test_memory_, 0, kTestMemorySize);
  }

  void TearDown() override {
    if (test_memory_) {
      free(test_memory_);
      test_memory_ = nullptr;
    }
  }

  void* test_memory_ = nullptr;
};

/**
 * @brief Slab 分配器实例化示例
 *
 * 这个测试展示了如何正确实例化 Slab 分配器：
 * 1. PageAllocator: 使用 Buddy<TestLogger, TestLock> 作为页级分配器
 * 2. LogFunc: 使用 TestLogger 作为日志函数
 * 3. Lock: 使用 TestLock 作为锁机制
 */
TEST_F(SlabTest, InstantiationExample) {
  // 定义具体的分配器类型
  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger, TestLock>;

  // 实例化 Slab 分配器
  MySlab slab("test_slab", test_memory_, kTestPages);

  // 验证实例化成功
  EXPECT_EQ(slab.GetFreeCount(), kTestPages);
  EXPECT_EQ(slab.GetUsedCount(), 0);

  std::cout << "Slab allocator instantiated successfully!\n";
  std::cout << "  Name: test_slab\n";
  std::cout << "  Memory: " << test_memory_ << "\n";
  std::cout << "  Pages: " << kTestPages << "\n";
  std::cout << "  Free count: " << slab.GetFreeCount() << "\n";
  std::cout << "  Used count: " << slab.GetUsedCount() << "\n";
}

/**
 * @brief 不同模板参数组合的实例化示例
 */
TEST_F(SlabTest, DifferentTemplateParameterCombinations) {
  // 1. 使用默认模板参数的简化版本
  using SimpleBuddy = Buddy<>;  // 使用默认的 std::nullptr_t 和 LockBase
  using SimpleSlab = Slab<SimpleBuddy>;  // 使用默认的 LogFunc 和 Lock

  SimpleSlab simple_slab("simple_slab", test_memory_, kTestPages);
  EXPECT_EQ(simple_slab.GetFreeCount(), kTestPages);

  // 2. 只指定日志函数，使用默认锁
  using LoggedBuddy = Buddy<TestLogger>;
  using LoggedSlab = Slab<LoggedBuddy, TestLogger>;

  LoggedSlab logged_slab("logged_slab", test_memory_, kTestPages);
  EXPECT_EQ(logged_slab.GetFreeCount(), kTestPages);

  // 3. 完整指定所有模板参数
  using FullBuddy = Buddy<TestLogger>;
  using FullSlab = Slab<FullBuddy, TestLogger, TestLock>;

  FullSlab full_slab("full_slab", test_memory_, kTestPages);
  EXPECT_EQ(full_slab.GetFreeCount(), kTestPages);

  std::cout << "All template parameter combinations work correctly!\n";
}
