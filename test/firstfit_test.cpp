/**
 * Copyright The bmalloc Contributors
 * @file firstfit_test.cpp
 * @brief FirstFit分配器的Google Test测试用例
 */

#include "first_fit.hpp"

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
class FirstFitTest : public ::testing::Test {
 protected:
  static constexpr size_t kTestMemorySize = 64 * 1024;  // 64KB
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
TEST_F(FirstFitTest, BasicConstruction) {
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, kTestPages);

  std::cout << "\n=== FirstFit Basic Construction Test ===" << std::endl;
  std::cout << "Initial state - Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;

  // 验证初始状态
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
  EXPECT_EQ(allocator.GetUsedCount(), 0);
}

TEST_F(FirstFitTest, ConstructionWithZeroPages) {
  // 测试边界条件：0页
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, 0);
  
  std::cout << "\n=== Zero Pages Construction Test ===" << std::endl;
  std::cout << "State with 0 pages - Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;
}

// 基本分配和释放测试
TEST_F(FirstFitTest, BasicAllocation) {
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== FirstFit Basic Allocation Test ===" << std::endl;
  std::cout << "Initial state - Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;

  // 分配1页
  std::cout << "Allocating 1 page..." << std::endl;
  void* ptr1 = allocator.Alloc(1);
  ASSERT_NE(ptr1, nullptr);
  std::cout << "Allocated ptr1=" << ptr1 << ", Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 1);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 1);

  // 分配3页
  std::cout << "Allocating 3 pages..." << std::endl;
  void* ptr2 = allocator.Alloc(3);
  ASSERT_NE(ptr2, nullptr);
  std::cout << "Allocated ptr2=" << ptr2 << ", Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 4);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 4);

  // 分配2页
  std::cout << "Allocating 2 pages..." << std::endl;
  void* ptr3 = allocator.Alloc(2);
  ASSERT_NE(ptr3, nullptr);
  std::cout << "Allocated ptr3=" << ptr3 << ", Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 6);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 6);

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
  allocator.Free(ptr1, 1);
  std::cout << "After freeing ptr1 - Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 5);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 5);

  std::cout << "Freeing ptr2 (3 pages)..." << std::endl;
  allocator.Free(ptr2, 3);
  std::cout << "After freeing ptr2 - Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 2);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages - 2);

  std::cout << "Freeing ptr3 (2 pages)..." << std::endl;
  allocator.Free(ptr3, 2);
  std::cout << "Final state - Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// FirstFit算法特性测试
TEST_F(FirstFitTest, FirstFitAlgorithmTest) {
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== FirstFit Algorithm Behavior Test ===" << std::endl;

  // 分配多个不同大小的块
  void* ptr1 = allocator.Alloc(2);  // 占用页面 0-1
  void* ptr2 = allocator.Alloc(3);  // 占用页面 2-4
  void* ptr3 = allocator.Alloc(1);  // 占用页面 5
  
  ASSERT_NE(ptr1, nullptr);
  ASSERT_NE(ptr2, nullptr);
  ASSERT_NE(ptr3, nullptr);

  std::cout << "Allocated blocks: ptr1=" << ptr1 << " (2 pages), ptr2=" 
            << ptr2 << " (3 pages), ptr3=" << ptr3 << " (1 page)" << std::endl;

  // 释放中间的块，创建空洞
  std::cout << "Freeing middle block (ptr2, 3 pages)..." << std::endl;
  allocator.Free(ptr2, 3);
  
  std::cout << "Memory state after creating hole - Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;

  // FirstFit应该优先使用第一个找到的合适空洞
  std::cout << "Allocating 2 pages (should use the freed hole)..." << std::endl;
  void* ptr4 = allocator.Alloc(2);  // 应该使用ptr2释放的空间中的前2页
  ASSERT_NE(ptr4, nullptr);
  
  std::cout << "New allocation ptr4=" << ptr4 << std::endl;
  
  // ptr4应该在ptr2的位置（FirstFit特性）
  EXPECT_EQ(ptr4, ptr2) << "FirstFit should reuse the first available hole";

  // 清理
  allocator.Free(ptr1, 2);
  allocator.Free(ptr3, 1);
  allocator.Free(ptr4, 2);
}

// 内存碎片化和重组测试
TEST_F(FirstFitTest, FragmentationTest) {
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== Memory Fragmentation Test ===" << std::endl;

  std::vector<void*> ptrs;
  
  // 分配许多单页块，创建碎片
  std::cout << "Creating fragmentation with single-page allocations..." << std::endl;
  for (size_t i = 0; i < 8; ++i) {
    void* ptr = allocator.Alloc(1);
    if (ptr) {
      ptrs.push_back(ptr);
      std::cout << "  Allocated block " << i << " at " << ptr << std::endl;
    }
  }
  
  std::cout << "Allocated " << ptrs.size() << " single-page blocks" << std::endl;
  std::cout << "Memory state - Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;

  // 释放每隔一个块，创建碎片化
  std::cout << "\nCreating fragmentation by freeing every other block..." << std::endl;
  for (size_t i = 1; i < ptrs.size(); i += 2) {
    allocator.Free(ptrs[i], 1);
    ptrs[i] = nullptr;
    std::cout << "  Freed block " << i << std::endl;
  }
  
  std::cout << "Memory state after fragmentation - Free: " << allocator.GetFreeCount() 
            << ", Used: " << allocator.GetUsedCount() << std::endl;

  // 尝试分配大块（应该失败，因为没有连续空间）
  std::cout << "\nTrying to allocate 3 consecutive pages..." << std::endl;
  void* large_ptr = allocator.Alloc(3);
  if (large_ptr) {
    std::cout << "  Large allocation succeeded at " << large_ptr << std::endl;
    allocator.Free(large_ptr, 3);
  } else {
    std::cout << "  Large allocation failed due to fragmentation (expected)" << std::endl;
  }

  // 清理剩余的分配
  for (size_t i = 0; i < ptrs.size(); i += 2) {
    if (ptrs[i]) {
      allocator.Free(ptrs[i], 1);
    }
  }
}

// 边界条件测试
TEST_F(FirstFitTest, EdgeCaseTests) {
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== Edge Cases Test ===" << std::endl;

  // 测试分配0页
  std::cout << "Testing allocation of 0 pages..." << std::endl;
  void* ptr_zero = allocator.Alloc(0);
  EXPECT_EQ(ptr_zero, nullptr) << "Allocating 0 pages should fail";

  // 测试分配超过总页数
  std::cout << "Testing allocation of more pages than available..." << std::endl;
  void* ptr_too_many = allocator.Alloc(kTestPages + 1);
  EXPECT_EQ(ptr_too_many, nullptr) << "Over-allocation should fail";

  // 测试分配所有页面
  std::cout << "Testing allocation of all available pages..." << std::endl;
  void* ptr_all = allocator.Alloc(kTestPages);
  ASSERT_NE(ptr_all, nullptr) << "Should be able to allocate all pages";
  EXPECT_EQ(allocator.GetFreeCount(), 0);
  EXPECT_EQ(allocator.GetUsedCount(), kTestPages);

  // 在满载状态下再次分配应该失败
  std::cout << "Testing allocation when memory is full..." << std::endl;
  void* ptr_after_full = allocator.Alloc(1);
  EXPECT_EQ(ptr_after_full, nullptr) << "Allocation when full should fail";

  // 释放所有内存
  std::cout << "Freeing all memory..." << std::endl;
  allocator.Free(ptr_all, kTestPages);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
  EXPECT_EQ(allocator.GetUsedCount(), 0);
}

// 内存数据完整性测试
TEST_F(FirstFitTest, MemoryDataIntegrityTest) {
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== Memory Data Integrity Test ===" << std::endl;

  const size_t test_sizes[] = {1, 2, 4, 8};
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint8_t> byte_dist(0, 255);

  for (size_t page_count : test_sizes) {
    if (page_count > kTestPages) continue;
    
    size_t bytes = page_count * kPageSize;
    
    std::cout << "Testing " << page_count << " pages (" << bytes << " bytes)..." << std::endl;

    // 分配内存
    void* ptr = allocator.Alloc(page_count);
    if (!ptr) {
      std::cout << "  Allocation failed - insufficient memory" << std::endl;
      continue;
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
          << "Data corruption at offset " << i << " in " << page_count 
          << " page allocation";
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
    allocator.Free(ptr, page_count);

    std::cout << "  Memory integrity test passed for " << page_count 
              << " pages (" << bytes << " bytes)" << std::endl;
  }
}

// 压力测试 - 快速分配释放
TEST_F(FirstFitTest, StressTestRapidAllocFree) {
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== Rapid Allocation/Free Stress Test ===" << std::endl;

  const size_t iterations = 5000;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> size_dist(1, 3);

  std::cout << "Starting " << iterations << " rapid alloc/free operations..." << std::endl;
  auto start = std::chrono::high_resolution_clock::now();

  size_t successful_allocs = 0;
  size_t failed_allocs = 0;

  // 快速分配和立即释放
  for (size_t i = 0; i < iterations; ++i) {
    size_t page_count = size_dist(gen);
    void* ptr = allocator.Alloc(page_count);

    if (ptr) {
      successful_allocs++;
      // 写入一些数据验证内存可用性
      uint32_t* data = static_cast<uint32_t*>(ptr);
      *data = static_cast<uint32_t>(i);

      // 立即释放
      allocator.Free(ptr, page_count);
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
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << "Stress test (rapid alloc/free): " << duration.count() << " μs" << std::endl;
  std::cout << "Successful allocations: " << successful_allocs << std::endl;
  std::cout << "Failed allocations: " << failed_allocs << std::endl;
  std::cout << "Average per operation: " 
            << static_cast<double>(duration.count()) / iterations << " μs" << std::endl;

  // 验证最终状态
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 基准性能测试
TEST_F(FirstFitTest, BenchmarkPerformanceTest) {
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== FirstFit Performance Benchmark ===" << std::endl;

  struct BenchmarkResult {
    std::string test_name;
    size_t operations;
    std::chrono::microseconds duration;
    double ops_per_second;
  };

  std::vector<BenchmarkResult> results;

  // 测试1: 顺序分配相同大小
  {
    std::cout << "Benchmark 1: Sequential same-size allocation/free..." << std::endl;
    const size_t ops = 500;
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<void*> ptrs;
    std::cout << "  Allocating " << ops << " single pages..." << std::endl;
    for (size_t i = 0; i < ops && ptrs.size() < kTestPages; ++i) {
      void* ptr = allocator.Alloc(1);
      if (ptr) ptrs.push_back(ptr);
    }
    std::cout << "  Successfully allocated " << ptrs.size() << " pages" << std::endl;

    std::cout << "  Freeing all allocated pages..." << std::endl;
    for (void* ptr : ptrs) {
      allocator.Free(ptr, 1);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    results.push_back({"Sequential same-size alloc/free",
                       ops * 2,  // alloc + free
                       duration, (ops * 2 * 1000000.0) / duration.count()});
  }

  // 测试2: 随机大小分配
  {
    std::cout << "\nBenchmark 2: Random size allocation/free..." << std::endl;
    const size_t ops = 200;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> size_dist(1, 4);

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::pair<void*, size_t>> ptrs;
    std::cout << "  Allocating " << ops << " blocks of random sizes..." << std::endl;
    for (size_t i = 0; i < ops; ++i) {
      size_t page_count = size_dist(gen);
      void* ptr = allocator.Alloc(page_count);
      if (ptr) ptrs.emplace_back(ptr, page_count);
    }
    std::cout << "  Successfully allocated " << ptrs.size() << " blocks" << std::endl;

    std::cout << "  Freeing all allocated blocks..." << std::endl;
    for (auto [ptr, page_count] : ptrs) {
      allocator.Free(ptr, page_count);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    results.push_back({"Random size alloc/free", ptrs.size() * 2, duration,
                       (ptrs.size() * 2 * 1000000.0) / duration.count()});
  }

  // 测试3: 碎片化环境下的分配
  {
    std::cout << "\nBenchmark 3: Allocation in fragmented memory..." << std::endl;
    
    // 创建碎片化环境
    std::vector<void*> initial_ptrs;
    for (size_t i = 0; i < 8; ++i) {
      void* ptr = allocator.Alloc(1);
      if (ptr) initial_ptrs.push_back(ptr);
    }
    
    // 释放每隔一个块
    for (size_t i = 1; i < initial_ptrs.size(); i += 2) {
      allocator.Free(initial_ptrs[i], 1);
      initial_ptrs[i] = nullptr;
    }

    const size_t ops = 100;
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<void*> ptrs;
    for (size_t i = 0; i < ops; ++i) {
      void* ptr = allocator.Alloc(1);
      if (ptr) ptrs.push_back(ptr);
    }

    for (void* ptr : ptrs) {
      if (ptr) allocator.Free(ptr, 1);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    results.push_back({"Fragmented allocation/free", ptrs.size() * 2, duration,
                       (ptrs.size() * 2 * 1000000.0) / duration.count()});

    // 清理剩余分配
    for (size_t i = 0; i < initial_ptrs.size(); i += 2) {
      if (initial_ptrs[i]) {
        allocator.Free(initial_ptrs[i], 1);
      }
    }
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

// 地址连续性测试
TEST_F(FirstFitTest, AddressContinuityTest) {
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== Address Continuity Test ===" << std::endl;
  std::cout << "Base memory address: " << test_memory_ << std::endl;
  std::cout << "Page size: " << kPageSize << " bytes" << std::endl;

  // 连续分配多个单页块
  std::vector<void*> ptrs;
  for (size_t i = 0; i < 5; ++i) {
    void* ptr = allocator.Alloc(1);
    if (ptr) {
      ptrs.push_back(ptr);
      size_t offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
      size_t page_offset = offset / kPageSize;
      std::cout << "Allocation " << i << ": ptr=" << ptr 
                << ", page offset=" << page_offset << std::endl;
    }
  }

  // 验证地址连续性（FirstFit应该分配连续的地址）
  if (ptrs.size() >= 2) {
    for (size_t i = 1; i < ptrs.size(); ++i) {
      size_t prev_offset = static_cast<char*>(ptrs[i-1]) - static_cast<char*>(test_memory_);
      size_t curr_offset = static_cast<char*>(ptrs[i]) - static_cast<char*>(test_memory_);
      
      EXPECT_EQ(curr_offset - prev_offset, kPageSize) 
          << "Consecutive allocations should have consecutive addresses";
    }
  }

  // 清理
  for (void* ptr : ptrs) {
    allocator.Free(ptr, 1);
  }
}

// 错误处理测试
TEST_F(FirstFitTest, ErrorHandlingTest) {
  FirstFit<TestLogger> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== Error Handling Test ===" << std::endl;

  // 分配一些内存用于后续错误测试
  void* valid_ptr = allocator.Alloc(2);
  ASSERT_NE(valid_ptr, nullptr);

  // 测试释放无效地址
  std::cout << "Testing free with invalid address..." << std::endl;
  void* invalid_ptr = static_cast<char*>(test_memory_) + kTestMemorySize + 1000;
  allocator.Free(invalid_ptr, 1);  // 应该输出错误信息但不崩溃

  // 测试释放超出范围的页数
  std::cout << "Testing free with excessive page count..." << std::endl;
  allocator.Free(valid_ptr, kTestPages + 1);  // 应该输出错误信息

  // 正确释放
  std::cout << "Correctly freeing valid pointer..." << std::endl;
  allocator.Free(valid_ptr, 2);
  
  EXPECT_EQ(allocator.GetUsedCount(), 0);
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 多线程并发测试
TEST_F(FirstFitTest, MultiThreadConcurrentTest) {
  FirstFit<TestLogger, TestLock> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== Multi-Thread Concurrent Test ===" << std::endl;

  const size_t num_threads = 4;
  const size_t operations_per_thread = 500;
  std::vector<std::thread> threads;
  std::atomic<size_t> total_successful_allocs{0};
  std::atomic<size_t> total_failed_allocs{0};
  
  std::cout << "Starting " << num_threads << " threads, each performing " 
            << operations_per_thread << " operations..." << std::endl;

  auto worker = [&](size_t thread_id) {
    std::random_device rd;
    std::mt19937 gen(rd() + thread_id);  // 每个线程使用不同的种子
    std::uniform_int_distribution<> size_dist(1, 2);
    
    size_t local_success = 0;
    size_t local_failed = 0;
    
    for (size_t i = 0; i < operations_per_thread; ++i) {
      size_t page_count = size_dist(gen);
      void* ptr = allocator.Alloc(page_count);
      
      if (ptr) {
        local_success++;
        // 写入线程ID和操作序号，验证内存可用性
        uint64_t* data = static_cast<uint64_t*>(ptr);
        *data = (static_cast<uint64_t>(thread_id) << 32) | i;
        
        // 验证写入的数据
        EXPECT_EQ(*data, (static_cast<uint64_t>(thread_id) << 32) | i)
            << "Data corruption in thread " << thread_id << " operation " << i;
        
        // 立即释放
        allocator.Free(ptr, page_count);
      } else {
        local_failed++;
      }
      
      // 每100次操作让出CPU时间片
      if (i % 100 == 0) {
        std::this_thread::yield();
      }
    }
    
    total_successful_allocs += local_success;
    total_failed_allocs += local_failed;
    
    std::cout << "Thread " << thread_id << " completed: " 
              << local_success << " successful, " 
              << local_failed << " failed" << std::endl;
  };

  auto start = std::chrono::high_resolution_clock::now();

  // 启动所有线程
  for (size_t i = 0; i < num_threads; ++i) {
    threads.emplace_back(worker, i);
  }

  // 等待所有线程完成
  for (auto& thread : threads) {
    thread.join();
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << "\nMulti-thread test completed in " << duration.count() << " μs" << std::endl;
  std::cout << "Total successful allocations: " << total_successful_allocs << std::endl;
  std::cout << "Total failed allocations: " << total_failed_allocs << std::endl;
  std::cout << "Total operations: " << (total_successful_allocs + total_failed_allocs) << std::endl;
  std::cout << "Average per operation: " 
            << static_cast<double>(duration.count()) / (num_threads * operations_per_thread) 
            << " μs" << std::endl;

  // 验证最终状态 - 所有内存应该被释放
  EXPECT_EQ(allocator.GetUsedCount(), 0) << "Memory leak detected in multi-thread test";
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 多线程竞争测试
TEST_F(FirstFitTest, MultiThreadContentionTest) {
  FirstFit<TestLogger, TestLock> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== Multi-Thread Contention Test ===" << std::endl;

  const size_t num_threads = 8;
  const size_t allocations_per_thread = 50;
  std::vector<std::thread> threads;
  std::vector<std::vector<std::pair<void*, size_t>>> thread_allocations(num_threads);
  std::atomic<size_t> peak_memory_usage{0};
  
  std::cout << "Starting " << num_threads << " threads competing for limited memory..." << std::endl;

  auto competitive_worker = [&](size_t thread_id) {
    std::random_device rd;
    std::mt19937 gen(rd() + thread_id);
    std::uniform_int_distribution<> size_dist(1, 3);
    std::uniform_int_distribution<> action_dist(0, 1);  // 0=alloc, 1=free
    
    auto& my_allocations = thread_allocations[thread_id];
    
    for (size_t i = 0; i < allocations_per_thread * 2; ++i) {  // 更多操作以增加竞争
      if (my_allocations.empty() || action_dist(gen) == 0) {
        // 分配内存
        size_t page_count = size_dist(gen);
        void* ptr = allocator.Alloc(page_count);
        
        if (ptr) {
          my_allocations.emplace_back(ptr, page_count);
          
          // 写入数据模式
          uint32_t* data = static_cast<uint32_t*>(ptr);
          for (size_t j = 0; j < page_count * kPageSize / sizeof(uint32_t); ++j) {
            data[j] = static_cast<uint32_t>((thread_id << 24) | (i << 8) | j);
          }
          
          // 更新峰值内存使用量
          size_t current_usage = allocator.GetUsedCount();
          size_t expected = peak_memory_usage.load();
          while (current_usage > expected && 
                 !peak_memory_usage.compare_exchange_weak(expected, current_usage)) {
            // 继续尝试更新峰值
          }
        }
      } else {
        // 释放内存
        size_t idx = gen() % my_allocations.size();
        auto [ptr, page_count] = my_allocations[idx];
        
        // 验证数据完整性
        uint32_t* data = static_cast<uint32_t*>(ptr);
        bool data_valid = true;
        for (size_t j = 0; j < page_count * kPageSize / sizeof(uint32_t); ++j) {
          uint32_t expected_value = static_cast<uint32_t>((thread_id << 24) | j);
          if ((data[j] >> 8) != (expected_value >> 8)) {  // 忽略操作序号部分
            data_valid = false;
            break;
          }
        }
        
        EXPECT_TRUE(data_valid) << "Data corruption detected in thread " << thread_id;
        
        allocator.Free(ptr, page_count);
        my_allocations.erase(my_allocations.begin() + idx);
      }
      
      // 偶尔让出时间片以增加交错执行
      if (i % 10 == 0) {
        std::this_thread::yield();
      }
    }
    
    std::cout << "Thread " << thread_id << " finished with " 
              << my_allocations.size() << " outstanding allocations" << std::endl;
  };

  auto start = std::chrono::high_resolution_clock::now();

  // 启动所有竞争线程
  for (size_t i = 0; i < num_threads; ++i) {
    threads.emplace_back(competitive_worker, i);
  }

  // 等待所有线程完成
  for (auto& thread : threads) {
    thread.join();
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  std::cout << "\nContention test completed in " << duration.count() << " μs" << std::endl;
  std::cout << "Peak memory usage: " << peak_memory_usage.load() << "/" << kTestPages << " pages" << std::endl;

  // 清理所有剩余分配
  size_t total_cleanup = 0;
  for (size_t i = 0; i < num_threads; ++i) {
    for (auto [ptr, page_count] : thread_allocations[i]) {
      allocator.Free(ptr, page_count);
      total_cleanup++;
    }
  }
  
  std::cout << "Cleaned up " << total_cleanup << " outstanding allocations" << std::endl;

  // 验证最终状态
  EXPECT_EQ(allocator.GetUsedCount(), 0) << "Memory leak after contention test";
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
}

// 多线程死锁检测测试
TEST_F(FirstFitTest, MultiThreadDeadlockTest) {
  FirstFit<TestLogger, TestLock> allocator("test_firstfit", test_memory_, kTestPages);
  
  std::cout << "\n=== Multi-Thread Deadlock Detection Test ===" << std::endl;

  const size_t num_threads = 6;
  const size_t operations_per_thread = 200;
  std::vector<std::thread> threads;
  std::atomic<bool> test_completed{false};
  std::atomic<size_t> completed_threads{0};
  
  std::cout << "Starting deadlock detection test with " << num_threads << " threads..." << std::endl;

  auto deadlock_worker = [&](size_t thread_id) {
    std::random_device rd;
    std::mt19937 gen(rd() + thread_id);
    std::uniform_int_distribution<> size_dist(1, 2);
    std::uniform_int_distribution<> delay_dist(1, 5);  // 1-5 微秒延迟
    
    std::vector<std::pair<void*, size_t>> my_allocations;
    
    for (size_t i = 0; i < operations_per_thread && !test_completed; ++i) {
      // 随机延迟以增加交错执行的可能性
      std::this_thread::sleep_for(std::chrono::microseconds(delay_dist(gen)));
      
      if (my_allocations.size() < 3) {
        // 分配内存
        size_t page_count = size_dist(gen);
        void* ptr = allocator.Alloc(page_count);
        
        if (ptr) {
          my_allocations.emplace_back(ptr, page_count);
          
          // 写入测试数据
          uint32_t* data = static_cast<uint32_t*>(ptr);
          *data = static_cast<uint32_t>((thread_id << 16) | i);
        }
      } else {
        // 释放一些内存
        size_t to_free = std::min(my_allocations.size(), static_cast<size_t>(2));
        for (size_t j = 0; j < to_free; ++j) {
          auto [ptr, page_count] = my_allocations.back();
          
          // 验证数据
          uint32_t* data = static_cast<uint32_t*>(ptr);
          uint32_t expected = static_cast<uint32_t>((thread_id << 16));
          EXPECT_EQ((*data) >> 16, expected >> 16) 
              << "Data corruption in thread " << thread_id;
          
          allocator.Free(ptr, page_count);
          my_allocations.pop_back();
        }
      }
    }
    
    // 清理剩余分配
    for (auto [ptr, page_count] : my_allocations) {
      allocator.Free(ptr, page_count);
    }
    
    size_t finished = completed_threads.fetch_add(1) + 1;
    std::cout << "Thread " << thread_id << " completed (" 
              << finished << "/" << num_threads << ")" << std::endl;
  };

  auto start = std::chrono::high_resolution_clock::now();

  // 启动所有线程
  for (size_t i = 0; i < num_threads; ++i) {
    threads.emplace_back(deadlock_worker, i);
  }

  // 设置超时检测（10秒）
  auto timeout_thread = std::thread([&]() {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    if (!test_completed) {
      std::cout << "WARNING: Potential deadlock detected! Test taking too long." << std::endl;
      test_completed = true;
    }
  });

  // 等待所有工作线程完成
  for (auto& thread : threads) {
    thread.join();
  }

  test_completed = true;
  timeout_thread.join();

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "Deadlock test completed in " << duration.count() << " ms" << std::endl;
  std::cout << "All " << completed_threads.load() << " threads completed successfully" << std::endl;

  // 验证最终状态
  EXPECT_EQ(allocator.GetUsedCount(), 0) << "Memory leak after deadlock test";
  EXPECT_EQ(allocator.GetFreeCount(), kTestPages);
  EXPECT_LT(duration.count(), 8000) << "Test took too long, possible deadlock";
}

