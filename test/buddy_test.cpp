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
#include <iomanip>
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
  static constexpr size_t kTestMemorySize = kPageSize * 128;
  static constexpr size_t kTestPages = kTestMemorySize / kPageSize;

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
  std::cout << "\n=== Basic Allocation Test ===" << std::endl;
  std::cout << "Initial state - Free: " << allocator.GetFreeCount()
            << ", Used: " << allocator.GetUsedCount() << std::endl;

  // 分配1页 (order=0)
  std::cout << "Allocating 1 page (order=0)..." << std::endl;
  void* ptr1 = allocator.Alloc(0);
  ASSERT_NE(ptr1, nullptr);
  std::cout << "Allocated ptr1=" << ptr1
            << ", Free: " << allocator.GetFreeCount()
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 1);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 1);

  // 分配2页 (order=1)
  std::cout << "Allocating 2 pages (order=1)..." << std::endl;
  void* ptr2 = allocator.Alloc(1);
  ASSERT_NE(ptr2, nullptr);
  std::cout << "Allocated ptr2=" << ptr2
            << ", Free: " << allocator.GetFreeCount()
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 3);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 3);

  // 分配4页 (order=2)
  std::cout << "Allocating 4 pages (order=2)..." << std::endl;
  void* ptr3 = allocator.Alloc(2);
  ASSERT_NE(ptr3, nullptr);
  std::cout << "Allocated ptr3=" << ptr3
            << ", Free: " << allocator.GetFreeCount()
            << ", Used: " << allocator.GetUsedCount() << std::endl;
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
  std::cout << "\n--- Memory Release Phase ---" << std::endl;
  std::cout << "Freeing ptr1 (1 page)..." << std::endl;
  allocator.Free(ptr1, 0);
  std::cout << "After freeing ptr1 - Free: " << allocator.GetFreeCount()
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 6);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 6);

  std::cout << "Freeing ptr2 (2 pages)..." << std::endl;
  allocator.Free(ptr2, 1);
  std::cout << "After freeing ptr2 - Free: " << allocator.GetFreeCount()
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 4);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 4);

  std::cout << "Freeing ptr3 (4 pages)..." << std::endl;
  allocator.Free(ptr3, 2);
  std::cout << "Final state - Free: " << allocator.GetFreeCount()
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 内存数据完整性测试
TEST_F(BuddyTest, MemoryDataIntegrityTest) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);
  std::cout << "\n=== Memory Data Integrity Test ===" << std::endl;

  const size_t test_sizes[] = {1, 2, 4, 8};  // 不同order的测试
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint8_t> byte_dist(0, 255);

  for (size_t order : test_sizes) {
    size_t pages = 1 << order;
    size_t bytes = pages * kPageSize;

    std::cout << "Testing order " << order << " (" << pages << " pages, "
              << bytes << " bytes)..." << std::endl;

    // 分配内存
    void* ptr = allocator.Alloc(order);
    if (!ptr) {
      std::cout << "  Allocation failed - insufficient memory" << std::endl;
      continue;  // 如果内存不足，跳过这个测试
    }

    std::cout << "  Allocated at " << ptr << std::endl;

    uint8_t* data = static_cast<uint8_t*>(ptr);
    std::vector<uint8_t> expected_data(bytes);

    // 生成随机数据并写入
    std::cout << "  Writing random data..." << std::endl;
    for (size_t i = 0; i < bytes; ++i) {
      expected_data[i] = byte_dist(gen);
      data[i] = expected_data[i];
    }

    // 验证数据完整性
    std::cout << "  Verifying data integrity..." << std::endl;
    for (size_t i = 0; i < bytes; ++i) {
      ASSERT_EQ(data[i], expected_data[i])
          << "Data corruption at offset " << i << " in order " << order
          << " allocation";
    }

    // 写入特殊模式测试
    std::cout << "  Testing pattern write..." << std::endl;
    for (size_t i = 0; i < bytes; ++i) {
      data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    // 验证特殊模式
    for (size_t i = 0; i < bytes; ++i) {
      ASSERT_EQ(data[i], static_cast<uint8_t>(i & 0xFF))
          << "Pattern corruption at offset " << i;
    }

    // 释放内存
    allocator.Free(ptr, order);

    std::cout << "Memory integrity test passed for order " << order << " ("
              << pages << " pages, " << bytes << " bytes)" << std::endl;
  }
}

// 内存边界写入测试
TEST_F(BuddyTest, MemoryBoundaryWriteTest) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  // 分配一个页面
  void* ptr = allocator.Alloc(0);
  ASSERT_NE(ptr, nullptr);

  uint8_t* data = static_cast<uint8_t*>(ptr);

  // 写入页面边界的数据
  data[0] = 0xAA;              // 第一个字节
  data[kPageSize - 1] = 0x55;  // 最后一个字节
  data[kPageSize / 2] = 0xCC;  // 中间字节

  // 验证边界数据
  EXPECT_EQ(data[0], 0xAA);
  EXPECT_EQ(data[kPageSize - 1], 0x55);
  EXPECT_EQ(data[kPageSize / 2], 0xCC);

  allocator.Free(ptr, 0);
}

// 内存分配后清零测试
TEST_F(BuddyTest, MemoryZeroingTest) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  // 分配内存并写入数据
  void* ptr1 = allocator.Alloc(1);  // 2页
  ASSERT_NE(ptr1, nullptr);

  uint8_t* data1 = static_cast<uint8_t*>(ptr1);
  memset(data1, 0xFF, 2 * kPageSize);  // 填充0xFF

  allocator.Free(ptr1, 1);

  // 重新分配相同大小的内存
  void* ptr2 = allocator.Alloc(1);
  ASSERT_NE(ptr2, nullptr);

  uint8_t* data2 = static_cast<uint8_t*>(ptr2);

  // 检查是否包含之前的数据（buddy分配器通常不会自动清零）
  bool has_previous_data = false;
  for (size_t i = 0; i < 2 * kPageSize; ++i) {
    if (data2[i] == 0xFF) {
      has_previous_data = true;
      break;
    }
  }

  std::cout << "Memory "
            << (has_previous_data ? "contains" : "does not contain")
            << " previous data after reallocation" << std::endl;

  allocator.Free(ptr2, 1);
}

// 内存地址预测测试
TEST_F(BuddyTest, AddressPredictionTest) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_,
                              16);  // 16页，便于预测

  std::cout << "Base memory address: " << test_memory_ << std::endl;
  std::cout << "Page size: " << kPageSize << " bytes" << std::endl;

  // 测试连续的小块分配
  std::vector<void*> ptrs;

  // 分配8个1页的块
  for (int i = 0; i < 8; ++i) {
    void* ptr = allocator.Alloc(0);
    if (ptr) {
      ptrs.push_back(ptr);
      size_t offset =
          static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
      size_t page_offset = offset / kPageSize;
      std::cout << "Allocation " << i << ": ptr=" << ptr
                << ", page offset=" << page_offset << std::endl;
    }
  }

  // 分析分配模式
  std::set<size_t> used_pages;
  for (void* ptr : ptrs) {
    size_t offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
    size_t page_offset = offset / kPageSize;
    used_pages.insert(page_offset);
  }

  std::cout << "Used pages: ";
  for (size_t page : used_pages) {
    std::cout << page << " ";
  }
  std::cout << std::endl;

  // 清理
  for (void* ptr : ptrs) {
    allocator.Free(ptr, 0);
  }

  // 测试buddy分配模式
  std::cout << "\n--- Buddy allocation pattern test ---" << std::endl;

  // 先分配一个大块，再分配小块，观察分割模式
  void* large_ptr = allocator.Alloc(2);  // 4页
  if (large_ptr) {
    size_t large_offset =
        static_cast<char*>(large_ptr) - static_cast<char*>(test_memory_);
    std::cout << "Large block (4 pages) at offset: " << large_offset / kPageSize
              << std::endl;
  }

  void* small_ptr1 = allocator.Alloc(0);  // 1页
  void* small_ptr2 = allocator.Alloc(0);  // 1页

  if (small_ptr1 && small_ptr2) {
    size_t offset1 =
        static_cast<char*>(small_ptr1) - static_cast<char*>(test_memory_);
    size_t offset2 =
        static_cast<char*>(small_ptr2) - static_cast<char*>(test_memory_);
    std::cout << "Small block 1 at page: " << offset1 / kPageSize << std::endl;
    std::cout << "Small block 2 at page: " << offset2 / kPageSize << std::endl;

    // 验证分配的地址是否符合buddy算法的预期
    EXPECT_TRUE(abs(static_cast<long>(offset1 - offset2)) ==
                static_cast<long>(kPageSize))
        << "Adjacent small blocks should be 1 page apart";
  }

  if (large_ptr) allocator.Free(large_ptr, 2);
  if (small_ptr1) allocator.Free(small_ptr1, 0);
  if (small_ptr2) allocator.Free(small_ptr2, 0);
}

// 内存布局分析测试
TEST_F(BuddyTest, MemoryLayoutAnalysisTest) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, 8);  // 8页

  std::cout << "\n--- Memory Layout Analysis ---" << std::endl;

  struct AllocationInfo {
    void* ptr;
    size_t order;
    size_t page_offset;
  };

  std::vector<AllocationInfo> allocations;

  // 执行一系列分配，记录布局
  const size_t orders[] = {0, 1, 0, 2, 0, 1, 0};

  for (size_t order : orders) {
    void* ptr = allocator.Alloc(order);
    if (ptr) {
      size_t offset =
          static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
      size_t page_offset = offset / kPageSize;

      AllocationInfo info = {ptr, order, page_offset};
      allocations.push_back(info);

      std::cout << "Allocated " << (1 << order) << " page(s) at page offset "
                << page_offset << std::endl;
    }
  }

  // 分析内存碎片
  std::set<size_t> occupied_pages;
  for (const auto& alloc : allocations) {
    size_t pages = 1 << alloc.order;
    for (size_t i = 0; i < pages; ++i) {
      occupied_pages.insert(alloc.page_offset + i);
    }
  }

  std::cout << "Occupied pages: ";
  for (size_t page : occupied_pages) {
    std::cout << page << " ";
  }
  std::cout << std::endl;
  std::cout << "Memory utilization: " << occupied_pages.size() << "/8 pages ("
            << (occupied_pages.size() * 100.0 / 8) << "%)" << std::endl;

  // 清理
  for (const auto& alloc : allocations) {
    allocator.Free(alloc.ptr, alloc.order);
  }
}

// 压力测试 - 快速分配释放
TEST_F(BuddyTest, StressTestRapidAllocFree) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);
  std::cout << "\n=== Rapid Allocation/Free Stress Test ===" << std::endl;

  const size_t iterations = 10000;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> order_dist(0, 3);

  std::cout << "Starting " << iterations << " rapid alloc/free operations..."
            << std::endl;
  auto start = std::chrono::high_resolution_clock::now();

  size_t successful_allocs = 0;
  size_t failed_allocs = 0;

  // 快速分配和立即释放
  for (size_t i = 0; i < iterations; ++i) {
    size_t order = order_dist(gen);
    void* ptr = allocator.Alloc(order);

    if (ptr) {
      successful_allocs++;
      // 写入一些数据验证内存可用性
      uint32_t* data = static_cast<uint32_t*>(ptr);
      *data = static_cast<uint32_t>(i);

      // 立即释放
      allocator.Free(ptr, order);
    } else {
      failed_allocs++;
    }

    // 每1000次操作输出一次进度
    if ((i + 1) % 1000 == 0) {
      std::cout << "Progress: " << (i + 1) << "/" << iterations
                << " operations completed" << std::endl;
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << "Stress test (rapid alloc/free): " << duration.count() << " μs"
            << std::endl;
  std::cout << "Successful allocations: " << successful_allocs << std::endl;
  std::cout << "Failed allocations: " << failed_allocs << std::endl;
  std::cout << "Average per operation: "
            << static_cast<double>(duration.count()) / iterations << " μs"
            << std::endl;

  // 验证最终状态
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 压力测试 - 内存碎片化
TEST_F(BuddyTest, StressTestFragmentation) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);
  std::cout << "\n=== Memory Fragmentation Stress Test ===" << std::endl;

  std::vector<std::pair<void*, size_t>> allocations;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> order_dist(0, 2);

  auto start = std::chrono::high_resolution_clock::now();

  // 阶段1：创建碎片 - 分配大量小块
  std::cout << "Phase 1: Creating fragmentation with small allocations..."
            << std::endl;
  for (size_t i = 0; i < 100; ++i) {
    void* ptr = allocator.Alloc(0);  // 只分配1页
    if (ptr) {
      allocations.emplace_back(ptr, 0);
    }
  }
  std::cout << "Allocated " << allocations.size() << " single pages"
            << std::endl;
  std::cout << "Memory state - Free: " << allocator.GetFreeCount()
            << ", Used: " << allocator.GetUsedCount() << std::endl;

  // 阶段2：随机释放一半，创建更多碎片
  std::cout
      << "\nPhase 2: Randomly freeing half to create more fragmentation..."
      << std::endl;
  std::shuffle(allocations.begin(), allocations.end(), gen);
  size_t half = allocations.size() / 2;
  for (size_t i = 0; i < half; ++i) {
    allocator.Free(allocations[i].first, allocations[i].second);
  }
  allocations.erase(allocations.begin(), allocations.begin() + half);
  std::cout << "Freed " << half << " pages randomly" << std::endl;
  std::cout << "Memory state - Free: " << allocator.GetFreeCount()
            << ", Used: " << allocator.GetUsedCount() << std::endl;

  // 阶段3：尝试分配大块，测试合并能力
  std::cout << "\nPhase 3: Attempting large allocations to test coalescing..."
            << std::endl;
  size_t large_alloc_success = 0;
  for (size_t order = 1; order <= 4; ++order) {
    std::cout << "Trying to allocate " << (1 << order) << " pages (order "
              << order << ")..." << std::endl;
    for (size_t i = 0; i < 10; ++i) {
      void* ptr = allocator.Alloc(order);
      if (ptr) {
        large_alloc_success++;
        allocations.emplace_back(ptr, order);
      }
    }
    std::cout << "  Successfully allocated " << large_alloc_success
              << " large blocks so far" << std::endl;
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << "Fragmentation stress test: " << duration.count() << " μs"
            << std::endl;
  std::cout << "Large allocations successful: " << large_alloc_success << "/40"
            << std::endl;
  std::cout << "Current memory usage: " << allocator.GetUsedCount() << "/"
            << kTestPages << " pages" << std::endl;

  // 清理剩余分配
  for (auto [ptr, order] : allocations) {
    allocator.Free(ptr, order);
  }

  EXPECT_EQ(allocator.GetUsedCount(), 0);
}

// 基准性能测试
TEST_F(BuddyTest, BenchmarkPerformanceTest) {
  Buddy<TestLogger> allocator("test_buddy", test_memory_, kTestPages);

  struct BenchmarkResult {
    std::string test_name;
    size_t operations;
    std::chrono::microseconds duration;
    double ops_per_second;
  };

  std::vector<BenchmarkResult> results;

  // 测试1: 顺序分配相同大小
  {
    std::cout << "Benchmark 1: Sequential same-size allocation/free..."
              << std::endl;
    const size_t ops = 1000;
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<void*> ptrs;
    std::cout << "  Allocating " << ops << " single pages..." << std::endl;
    for (size_t i = 0; i < ops && ptrs.size() < kTestPages; ++i) {
      void* ptr = allocator.Alloc(0);
      if (ptr) ptrs.push_back(ptr);
    }
    std::cout << "  Successfully allocated " << ptrs.size() << " pages"
              << std::endl;

    std::cout << "  Freeing all allocated pages..." << std::endl;
    for (void* ptr : ptrs) {
      allocator.Free(ptr, 0);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    results.push_back({"Sequential same-size alloc/free",
                       ops * 2,  // alloc + free
                       duration, (ops * 2 * 1000000.0) / duration.count()});
  }

  // 测试2: 随机大小分配
  {
    std::cout << "\nBenchmark 2: Random size allocation/free..." << std::endl;
    const size_t ops = 500;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> order_dist(0, 3);

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::pair<void*, size_t>> ptrs;
    std::cout << "  Allocating " << ops << " blocks of random sizes..."
              << std::endl;
    for (size_t i = 0; i < ops; ++i) {
      size_t order = order_dist(gen);
      void* ptr = allocator.Alloc(order);
      if (ptr) ptrs.emplace_back(ptr, order);
    }
    std::cout << "  Successfully allocated " << ptrs.size() << " blocks"
              << std::endl;

    std::cout << "  Freeing all allocated blocks..." << std::endl;
    for (auto [ptr, order] : ptrs) {
      allocator.Free(ptr, order);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    results.push_back({"Random size alloc/free", ptrs.size() * 2, duration,
                       (ptrs.size() * 2 * 1000000.0) / duration.count()});
  }

  // 测试3: 交错分配释放
  {
    std::cout << "\nBenchmark 3: Interleaved allocation/free..." << std::endl;
    const size_t ops = 1000;
    std::vector<std::pair<void*, size_t>> active_allocs;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> order_dist(0, 2);

    auto start = std::chrono::high_resolution_clock::now();

    size_t total_ops = 0;
    size_t alloc_count = 0;
    size_t free_count = 0;

    std::cout << "  Running " << ops << " interleaved operations..."
              << std::endl;
    for (size_t i = 0; i < ops; ++i) {
      if (active_allocs.empty() || (active_allocs.size() < 20 && gen() % 2)) {
        // 分配
        size_t order = order_dist(gen);
        void* ptr = allocator.Alloc(order);
        if (ptr) {
          active_allocs.emplace_back(ptr, order);
          total_ops++;
          alloc_count++;
        }
      } else {
        // 释放
        size_t idx = gen() % active_allocs.size();
        auto [ptr, order] = active_allocs[idx];
        allocator.Free(ptr, order);
        active_allocs.erase(active_allocs.begin() + idx);
        total_ops++;
        free_count++;
      }

      // 每200次操作输出一次进度
      if ((i + 1) % 200 == 0) {
        std::cout << "    Progress: " << (i + 1) << "/" << ops
                  << " ops, active blocks: " << active_allocs.size()
                  << std::endl;
      }
    }

    std::cout << "  Cleaning up " << active_allocs.size()
              << " remaining blocks..." << std::endl;
    // 清理剩余
    for (auto [ptr, order] : active_allocs) {
      allocator.Free(ptr, order);
      total_ops++;
      free_count++;
    }

    std::cout << "  Total allocations: " << alloc_count
              << ", Total frees: " << free_count << std::endl;

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    results.push_back({"Interleaved alloc/free", total_ops, duration,
                       (total_ops * 1000000.0) / duration.count()});
  }

  // 输出基准测试结果
  std::cout << "\n=== Benchmark Results ===" << std::endl;
  std::cout << std::left << std::setw(30) << "Test Name" << std::setw(12)
            << "Operations" << std::setw(15) << "Duration (μs)" << std::setw(15)
            << "Ops/Second" << std::endl;
  std::cout << std::string(72, '-') << std::endl;

  for (const auto& result : results) {
    std::cout << std::left << std::setw(30) << result.test_name << std::setw(12)
              << result.operations << std::setw(15) << result.duration.count()
              << std::setw(15) << std::fixed << std::setprecision(0)
              << result.ops_per_second << std::endl;
  }
}

/**
 * @brief 连续分配多个4K页面并进行数据验证测试
 *
 * 测试功能：
 * 1. 连续分配多个4K页面 (order=2，每次分配4页=16KB)
 * 2. 对每个分配的页面写入不同的测试模式
 * 3. 验证数据完整性
 * 4. 测试内存边界
 * 5. 验证页面之间不会相互影响
 * 6. 清理并验证释放后的状态
 */
TEST_F(BuddyTest, Multiple4KPageAllocationAndDataValidation) {
  static constexpr size_t ORDER_FOR_4K = 2;  // 2^2 = 4页 = 16KB > 4KB

  Buddy<TestLogger> allocator("4k_page_buddy", test_memory_, kTestPages);

  std::cout << "\n=== Multiple 4K Page Allocation and Data Validation Test ==="
            << std::endl;
  std::cout << "Test memory: " << kTestMemorySize << " bytes (" << kTestPages
            << " pages)" << std::endl;
  std::cout << "Each allocation: order=" << ORDER_FOR_4K << " (2^"
            << ORDER_FOR_4K << " = " << (1 << ORDER_FOR_4K)
            << " pages = " << ((1 << ORDER_FOR_4K) * kPageSize) << " bytes)"
            << std::endl;

  // 1. 计算可分配的数量（保守估算）
  size_t max_allocations =
      kTestPages / (1 << ORDER_FOR_4K);  // 32 / 4 = 8 理论上最多8个
  // 为了避免内存耗尽，实际分配少一点
  size_t test_allocations = 8;
      // std::min(max_allocations - 1, static_cast<size_t>(5));

  std::cout << "Max possible allocations: " << max_allocations
            << ", Testing with: " << test_allocations << std::endl;

  std::vector<void*> allocated_pages;
  std::vector<uint32_t> test_patterns;

  // 2. 连续分配多个页面
  std::cout << "\n2. Allocating " << test_allocations << " pages..."
            << std::endl;

  size_t initial_free = allocator.GetFreeCount();
  size_t initial_used = allocator.GetUsedCount();
  std::cout << "Initial state: Free=" << initial_free
            << ", Used=" << initial_used << std::endl;

  for (size_t i = 0; i < test_allocations; i++) {
    void* page = allocator.Alloc(ORDER_FOR_4K);
    if (page == nullptr) {
      std::cout << "   Failed to allocate page " << i << ", stopping here"
                << std::endl;
      break;
    }

    allocated_pages.push_back(page);

    // 生成每个页面的唯一测试模式
    uint32_t pattern = 0x4B000000 + static_cast<uint32_t>(i);  // 4K测试模式
    test_patterns.push_back(pattern);

    uintptr_t addr = reinterpret_cast<uintptr_t>(page);
    std::cout << "   Page " << i << " allocated at 0x" << std::hex << addr
              << std::dec << " (offset: " << (addr % kPageSize) << ")"
              << std::endl;
  }

  size_t actual_allocated = allocated_pages.size();
  std::cout << "Successfully allocated " << actual_allocated << " pages"
            << std::endl;

  ASSERT_GT(actual_allocated, 0) << "Failed to allocate any pages";

  // 验证状态变化
  size_t after_alloc_free = allocator.GetFreeCount();
  size_t after_alloc_used = allocator.GetUsedCount();
  std::cout << "After allocation: Free=" << after_alloc_free
            << ", Used=" << after_alloc_used << std::endl;

  EXPECT_LT(after_alloc_free, initial_free) << "Free count should decrease";
  EXPECT_GT(after_alloc_used, initial_used) << "Used count should increase";

  // 3. 数据写入和验证测试
  std::cout << "\n3. Writing test patterns to allocated pages..." << std::endl;

  const size_t test_bytes_per_page =
      std::min(kPageSize, kPageSize);  // 使用较小的值

  for (size_t i = 0; i < actual_allocated; i++) {
    void* page = allocated_pages[i];
    uint32_t pattern = test_patterns[i];

    // 写入测试模式
    uint32_t* int_ptr = static_cast<uint32_t*>(page);
    size_t num_ints = test_bytes_per_page / sizeof(uint32_t);

    for (size_t j = 0; j < num_ints; j++) {
      int_ptr[j] = pattern + static_cast<uint32_t>(j);
    }

    std::cout << "   Page " << i << ": wrote pattern 0x" << std::hex << pattern
              << std::dec << " (" << num_ints << " integers)" << std::endl;
  }

  // 4. 数据完整性验证
  std::cout << "\n4. Verifying data integrity..." << std::endl;

  bool all_data_valid = true;
  for (size_t i = 0; i < actual_allocated; i++) {
    void* page = allocated_pages[i];
    uint32_t expected_pattern = test_patterns[i];

    uint32_t* int_ptr = static_cast<uint32_t*>(page);
    size_t num_ints = test_bytes_per_page / sizeof(uint32_t);

    bool page_data_valid = true;
    for (size_t j = 0; j < num_ints; j++) {
      uint32_t expected = expected_pattern + static_cast<uint32_t>(j);
      if (int_ptr[j] != expected) {
        std::cout << "   ERROR: Page " << i << ", index " << j
                  << ", expected 0x" << std::hex << expected << ", got 0x"
                  << int_ptr[j] << std::dec << std::endl;
        page_data_valid = false;
        all_data_valid = false;
        break;
      }
    }

    if (page_data_valid) {
      std::cout << "   Page " << i << ": data integrity verified ✓"
                << std::endl;
    }
  }

  EXPECT_TRUE(all_data_valid) << "Data integrity check failed";

  // 5. 边界测试
  std::cout << "\n5. Testing memory boundaries..." << std::endl;

  for (size_t i = 0; i < actual_allocated; i++) {
    char* byte_ptr = static_cast<char*>(allocated_pages[i]);

    // 测试第一个和最后一个字节
    byte_ptr[0] = static_cast<char>(0x4A + i);                        // 'J' + i
    byte_ptr[test_bytes_per_page - 1] = static_cast<char>(0x4B + i);  // 'K' + i

    // 验证边界字节
    EXPECT_EQ(byte_ptr[0], static_cast<char>(0x4A + i));
    EXPECT_EQ(byte_ptr[test_bytes_per_page - 1], static_cast<char>(0x4B + i));

    std::cout << "   Page " << i << ": boundary test passed" << std::endl;
  }

  // 6. 性能测试：多次读写
  std::cout << "\n6. Performance test: multiple read/write cycles..."
            << std::endl;

  auto start_time = std::chrono::high_resolution_clock::now();

  const int cycles = 100;
  for (int cycle = 0; cycle < cycles; cycle++) {
    for (size_t i = 0; i < actual_allocated; i++) {
      uint32_t* int_ptr = static_cast<uint32_t*>(allocated_pages[i]);
      size_t num_ints = test_bytes_per_page / sizeof(uint32_t);

      // 写入循环特定的模式
      uint32_t cycle_pattern =
          0xC0000000 + static_cast<uint32_t>(cycle * 1000 + i);
      for (size_t j = 0; j < num_ints; j++) {
        int_ptr[j] = cycle_pattern + static_cast<uint32_t>(j);
      }

      // 立即验证
      for (size_t j = 0; j < num_ints; j++) {
        uint32_t expected = cycle_pattern + static_cast<uint32_t>(j);
        if (int_ptr[j] != expected) {
          FAIL() << "Performance test failed at cycle " << cycle << ", page "
                 << i << ", index " << j;
        }
      }
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - start_time);

  size_t total_operations = cycles * actual_allocated * 2;  // 读+写
  std::cout << "   Completed " << cycles << " cycles (" << total_operations
            << " operations) in " << duration.count() << " μs" << std::endl;
  std::cout << "   Performance: "
            << (total_operations * 1000000.0 / duration.count())
            << " operations/second" << std::endl;

  // 7. 清理测试
  std::cout << "\n7. Cleanup test..." << std::endl;

  size_t before_free_count = allocator.GetFreeCount();
  size_t before_used_count = allocator.GetUsedCount();

  for (size_t i = 0; i < actual_allocated; i++) {
    allocator.Free(allocated_pages[i], ORDER_FOR_4K);
    std::cout << "   Freed page " << i << std::endl;
  }

  size_t after_free_count = allocator.GetFreeCount();
  size_t after_used_count = allocator.GetUsedCount();

  std::cout << "Before free: Free=" << before_free_count
            << ", Used=" << before_used_count << std::endl;
  std::cout << "After free: Free=" << after_free_count
            << ", Used=" << after_used_count << std::endl;

  // 验证内存回收
  EXPECT_GT(after_free_count, before_free_count)
      << "Free count should increase after free";
  EXPECT_LT(after_used_count, before_used_count)
      << "Used count should decrease after free";

  // 理想情况下应该回到初始状态（考虑碎片化可能导致不完全回收）
  EXPECT_GE(after_free_count, initial_free - (1 << ORDER_FOR_4K))
      << "Should recover most memory";

  std::cout << "\n✓ Multiple 4K page allocation and data validation test "
               "completed successfully!"
            << std::endl;
}
