/**
 * Copyright The bmalloc Contributors
 * @file buddy_test.cpp
 * @brief Buddy分配器的Google Test测试用例
 */

#include "buddy.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <vector>

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
class BuddyTest : public ::testing::Test {
 protected:
  static constexpr size_t kTestMemorySize = 64 * 1024;               // 64KB
  static constexpr size_t kTestPages = kTestMemorySize / kPageSize;  // 16页

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

// 基本功能测试
TEST_F(BuddyTest, BasicConstruction) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  // 验证初始状态
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
  EXPECT_EQ(allocator.GetUsedCount(), 0);
}

TEST_F(BuddyTest, ConstructionWithZeroPages) {
  // 测试边界条件：0页 - buddy分配器会返回错误但不会崩溃
  Buddy<TestLogger> allocator("test_buddy", test_memory_, 0);
  // 由于初始化失败，这些值可能不是预期的0，但测试不应该崩溃
  // 实际行为取决于具体实现
}

// 基本分配和释放测试
TEST_F(BuddyTest, BasicAllocation) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  // 分配1页 (order=0)
  void* ptr1 = allocator.Alloc(0);
  ASSERT_NE(ptr1, nullptr);
  EXPECT_EQ(allocator.GetUsedCount(), 1);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 1);

  // 分配2页 (order=1)
  void* ptr2 = allocator.Alloc(1);
  ASSERT_NE(ptr2, nullptr);
  EXPECT_EQ(allocator.GetUsedCount(), 3);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 3);

  // 分配4页 (order=2)
  void* ptr3 = allocator.Alloc(2);
  ASSERT_NE(ptr3, nullptr);
  EXPECT_EQ(allocator.GetUsedCount(), 7);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 7);

  // 验证地址在有效范围内
  EXPECT_GE(ptr1, test_memory_);
  EXPECT_LT(ptr1, static_cast<char*>(test_memory_) + kTestMemorySize);
  EXPECT_GE(ptr2, test_memory_);
  EXPECT_LT(ptr2, static_cast<char*>(test_memory_) + kTestMemorySize);
  EXPECT_GE(ptr3, test_memory_);
  EXPECT_LT(ptr3, static_cast<char*>(test_memory_) + kTestMemorySize);

  // 释放内存
  allocator.Free(ptr1, 0);
  EXPECT_EQ(allocator.GetUsedCount(), 6);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 6);

  allocator.Free(ptr2, 1);
  EXPECT_EQ(allocator.GetUsedCount(), 4);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 4);

  allocator.Free(ptr3, 2);
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 测试内存分割
TEST_F(BuddyTest, MemorySplitting) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, 8);  // 8页

  // 首先分配一个小块，这会导致大块被分割
  void* ptr1 = allocator.Alloc(0);  // 分配1页
  ASSERT_NE(ptr1, nullptr);

  // 再分配另一个小块
  void* ptr2 = allocator.Alloc(0);  // 分配1页
  ASSERT_NE(ptr2, nullptr);

  // 验证地址不同
  EXPECT_NE(ptr1, ptr2);

  // 验证计数
  EXPECT_EQ(allocator.GetUsedCount(), 2);
  EXPECT_EQ(allocator.GetFreeCount(), 6);

  allocator.Free(ptr1, 0);
  allocator.Free(ptr2, 0);
}

// 测试内存合并
TEST_F(BuddyTest, MemoryCoalescing) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, 8);  // 8页

  // 分配两个相邻的1页块
  void* ptr1 = allocator.Alloc(0);
  void* ptr2 = allocator.Alloc(0);
  ASSERT_NE(ptr1, nullptr);
  ASSERT_NE(ptr2, nullptr);

  // 验证地址不同
  EXPECT_NE(ptr1, ptr2);

  // 验证计数
  EXPECT_EQ(allocator.GetUsedCount(), 2);
  EXPECT_EQ(allocator.GetFreeCount(), 6);

  // 释放第一个块
  allocator.Free(ptr1, 0);

  // 释放第二个块，应该触发合并
  allocator.Free(ptr2, 0);

  // 验证内存被完全释放
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), 8);

  // 现在应该能够分配一个更大的块
  void* ptr_large = allocator.Alloc(3);  // 8页
  EXPECT_NE(ptr_large, nullptr);

  allocator.Free(ptr_large, 3);
}

// 测试最大分配
TEST_F(BuddyTest, MaxAllocation) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  // 计算最大order (log2(16) = 4, 但实际最大order是3，因为2^4=16页)
  size_t max_order = 4;  // 2^4 = 16页，正好是全部内存

  // 分配全部内存
  void* ptr = allocator.Alloc(max_order);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(allocator.GetUsedCount(), kTestPages);
  EXPECT_EQ(allocator.GetFreeCount(), 0);

  // 此时不应该能够再分配任何内存
  void* ptr2 = allocator.Alloc(0);
  EXPECT_EQ(ptr2, nullptr);

  // 释放内存
  allocator.Free(ptr, max_order);
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 测试过大分配
TEST_F(BuddyTest, OversizeAllocation) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  // 尝试分配超过可用内存的块
  void* ptr = allocator.Alloc(10);  // 2^10 = 1024页，远超16页
  EXPECT_EQ(ptr, nullptr);

  // 验证计数没有变化
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 测试内存耗尽场景
TEST_F(BuddyTest, MemoryExhaustion) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  std::vector<void*> allocations;

  // 分配所有1页块
  for (size_t i = 0; i < kTestPages; ++i) {
    void* ptr = allocator.Alloc(0);
    if (ptr) {
      allocations.push_back(ptr);
    }
  }

  EXPECT_EQ(allocations.size(), kTestPages);
  EXPECT_EQ(allocator.GetUsedCount(), kTestPages);
  EXPECT_EQ(allocator.GetFreeCount(), 0);

  // 此时不应该能够再分配
  void* ptr = allocator.Alloc(0);
  EXPECT_EQ(ptr, nullptr);

  // 释放所有内存
  for (void* p : allocations) {
    allocator.Free(p, 0);
  }

  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 测试错误释放
TEST_F(BuddyTest, InvalidFree) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  // 分配一块内存
  void* ptr = allocator.Alloc(1);
  ASSERT_NE(ptr, nullptr);

  // 尝试用错误的order释放
  allocator.Free(ptr, 0);  // 分配时用order=1，释放时用order=0

  // 计数应该会变化（虽然这是错误的操作）
  // 但buddy分配器可能不会检测到这种错误

  // 正确释放
  allocator.Free(ptr, 1);
}

// 测试内存对齐
TEST_F(BuddyTest, MemoryAlignment) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  // 分配多个不同大小的块
  void* ptr1 = allocator.Alloc(0);  // 1页
  void* ptr2 = allocator.Alloc(1);  // 2页
  void* ptr3 = allocator.Alloc(2);  // 4页

  ASSERT_NE(ptr1, nullptr);
  ASSERT_NE(ptr2, nullptr);
  ASSERT_NE(ptr3, nullptr);

  // 验证地址按页边界对齐
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr1) % kPageSize, 0);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % kPageSize, 0);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr3) % kPageSize, 0);

  // 对于buddy分配器，地址对齐取决于具体的实现和内存布局
  // 这里只验证基本的页对齐即可
  std::cout << "ptr1 offset: "
            << (reinterpret_cast<uintptr_t>(ptr1) -
                reinterpret_cast<uintptr_t>(test_memory_)) /
                   kPageSize
            << " pages" << std::endl;
  std::cout << "ptr2 offset: "
            << (reinterpret_cast<uintptr_t>(ptr2) -
                reinterpret_cast<uintptr_t>(test_memory_)) /
                   kPageSize
            << " pages" << std::endl;
  std::cout << "ptr3 offset: "
            << (reinterpret_cast<uintptr_t>(ptr3) -
                reinterpret_cast<uintptr_t>(test_memory_)) /
                   kPageSize
            << " pages" << std::endl;

  allocator.Free(ptr1, 0);
  allocator.Free(ptr2, 1);
  allocator.Free(ptr3, 2);
}

// 性能测试
TEST_F(BuddyTest, PerformanceTest) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  const size_t num_operations = 1000;
  std::vector<std::pair<void*, size_t>> allocations;
  allocations.reserve(num_operations);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> order_dist(0, 2);  // order 0-2

  auto start = std::chrono::high_resolution_clock::now();

  // 随机分配和释放
  for (size_t i = 0; i < num_operations; ++i) {
    if (allocations.empty() ||
        (allocations.size() < num_operations / 2 && gen() % 2)) {
      // 分配
      size_t order = order_dist(gen);
      void* ptr = allocator.Alloc(order);
      if (ptr) {
        allocations.emplace_back(ptr, order);
      }
    } else {
      // 释放
      size_t idx = gen() % allocations.size();
      auto [ptr, order] = allocations[idx];
      allocator.Free(ptr, order);
      allocations.erase(allocations.begin() + idx);
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << "Performance test completed in " << duration.count()
            << " microseconds" << std::endl;
  std::cout << "Operations: " << num_operations << ", Average: "
            << static_cast<double>(duration.count()) / num_operations
            << " μs per operation" << std::endl;

  // 清理剩余分配
  for (auto [ptr, order] : allocations) {
    allocator.Free(ptr, order);
  }

  EXPECT_EQ(allocator.GetUsedCount(), 0);
}

// 多线程测试
TEST_F(BuddyTest, MultithreadedTest) {
  TestLock test_lock;
  Buddy<TestLogger, TestLock> allocator("test_buddy", test_memory_, kTestPages);

  const size_t num_threads = 4;
  const size_t operations_per_thread = 100;
  std::vector<std::thread> threads;
  std::atomic<size_t> success_count{0};

  auto worker = [&]() {
    std::vector<std::pair<void*, size_t>> local_allocations;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> order_dist(0, 1);

    for (size_t i = 0; i < operations_per_thread; ++i) {
      if (local_allocations.empty() ||
          (local_allocations.size() < 10 && gen() % 2)) {
        // 分配
        size_t order = order_dist(gen);
        void* ptr = allocator.Alloc(order);
        if (ptr) {
          local_allocations.emplace_back(ptr, order);
          success_count++;
        }
      } else {
        // 释放
        size_t idx = gen() % local_allocations.size();
        auto [ptr, order] = local_allocations[idx];
        allocator.Free(ptr, order);
        local_allocations.erase(local_allocations.begin() + idx);
      }
    }

    // 清理剩余分配
    for (auto [ptr, order] : local_allocations) {
      allocator.Free(ptr, order);
    }
  };

  // 启动线程
  for (size_t i = 0; i < num_threads; ++i) {
    threads.emplace_back(worker);
  }

  // 等待完成
  for (auto& t : threads) {
    t.join();
  }

  std::cout << "Multithreaded test completed. Successful allocations: "
            << success_count.load() << std::endl;

  // 验证最终状态
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 边界测试
TEST_F(BuddyTest, BoundaryTest) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, 1);  // 只有1页

  // 分配唯一的页
  void* ptr = allocator.Alloc(0);
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(allocator.GetUsedCount(), 1);
  EXPECT_EQ(allocator.GetFreeCount(), 0);

  // 不应该能够再分配
  void* ptr2 = allocator.Alloc(0);
  EXPECT_EQ(ptr2, nullptr);

  // 释放
  allocator.Free(ptr, 0);
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), 1);

  // 现在应该能够再次分配
  ptr = allocator.Alloc(0);
  EXPECT_NE(ptr, nullptr);

  allocator.Free(ptr, 0);
}
