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

/**
 * @brief 测试 kmem_cache_create 函数
 */
TEST_F(SlabTest, KmemCacheCreateTest) {
  // 使用简单的配置
  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger>;

  MySlab slab("test_slab", test_memory_, kTestPages);

  // 测试构造函数和析构函数
  auto ctor = [](void* ptr) {
    // 简单的构造函数，将内存初始化为0
    memset(ptr, 0, sizeof(int));
  };

  auto dtor = [](void* ptr) {
    // 简单的析构函数
    (void)ptr;  // 避免未使用参数警告
  };

  // 1. 测试正常创建 cache
  auto cache1 = slab.kmem_cache_create("test_cache_1", sizeof(int), ctor, dtor);
  ASSERT_NE(cache1, nullptr);
  EXPECT_STREQ(cache1->name, "test_cache_1");
  EXPECT_EQ(cache1->objectSize, sizeof(int));
  EXPECT_EQ(cache1->ctor, ctor);
  EXPECT_EQ(cache1->dtor, dtor);
  EXPECT_EQ(cache1->num_active, 0);
  EXPECT_GT(cache1->objectsInSlab, 0);
  EXPECT_EQ(cache1->error_code, 0);

  // 2. 测试创建不同大小的 cache
  auto cache2 =
      slab.kmem_cache_create("test_cache_2", sizeof(double), nullptr, nullptr);
  ASSERT_NE(cache2, nullptr);
  EXPECT_STREQ(cache2->name, "test_cache_2");
  EXPECT_EQ(cache2->objectSize, sizeof(double));
  EXPECT_EQ(cache2->ctor, nullptr);
  EXPECT_EQ(cache2->dtor, nullptr);

  // 3. 测试创建大对象的 cache（需要更高的 order）
  auto cache3 = slab.kmem_cache_create("large_cache", 8192, nullptr, nullptr);
  ASSERT_NE(cache3, nullptr);
  EXPECT_STREQ(cache3->name, "large_cache");
  EXPECT_EQ(cache3->objectSize, 8192);
  EXPECT_GT(cache3->order, 0);  // 大对象需要更高的 order

  // 4. 测试重复创建相同的 cache（应该返回已存在的）
  auto cache1_duplicate =
      slab.kmem_cache_create("test_cache_1", sizeof(int), ctor, dtor);
  EXPECT_EQ(cache1_duplicate, cache1);  // 应该返回相同的指针

  // 5. 测试错误情况：空名称
  auto invalid_cache1 =
      slab.kmem_cache_create("", sizeof(int), nullptr, nullptr);
  EXPECT_EQ(invalid_cache1, nullptr);
  // 由于 cache_cache 是 protected，我们通过返回值判断错误

  // 6. 测试错误情况：nullptr 名称
  auto invalid_cache2 =
      slab.kmem_cache_create(nullptr, sizeof(int), nullptr, nullptr);
  EXPECT_EQ(invalid_cache2, nullptr);

  // 7. 测试错误情况：非法大小
  auto invalid_cache3 =
      slab.kmem_cache_create("invalid_size", 0, nullptr, nullptr);
  EXPECT_EQ(invalid_cache3, nullptr);

  // 8. 测试错误情况：尝试创建与 cache_cache 同名的 cache
  auto invalid_cache4 =
      slab.kmem_cache_create("kmem_cache", sizeof(int), nullptr, nullptr);
  EXPECT_EQ(invalid_cache4, nullptr);

  std::cout << "kmem_cache_create tests completed successfully!\n";
  std::cout << "Created caches:\n";
  std::cout << "  - " << cache1->name << " (size: " << cache1->objectSize
            << ", objects per slab: " << cache1->objectsInSlab << ")\n";
  std::cout << "  - " << cache2->name << " (size: " << cache2->objectSize
            << ", objects per slab: " << cache2->objectsInSlab << ")\n";
  std::cout << "  - " << cache3->name << " (size: " << cache3->objectSize
            << ", order: " << cache3->order << ")\n";
}
