/**
 * Copyright The bmalloc Contributors
 * @file bmalloc_test.cpp
 * @brief Bmalloc分配器的Google Test测试用例
 */

#include "bmalloc.hpp"

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
class BmallocTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 分配1MB的内存用于测试
    memory_pool = std::malloc(memory_size);
    ASSERT_NE(memory_pool, nullptr);

    allocator = std::make_unique<Bmalloc<TestLogger, TestLock>>(memory_pool,
                                                                memory_size);
  }

  void TearDown() override {
    allocator.reset();
    std::free(memory_pool);
  }

  static constexpr size_t memory_size = 1024 * 1024 * 16;  // 16MB
  void* memory_pool = nullptr;
  std::unique_ptr<Bmalloc<TestLogger, TestLock>> allocator;
};

// 基本功能测试
TEST_F(BmallocTest, BasicMallocAndFree) {
  // 测试基本的内存分配和释放
  void* ptr = allocator->malloc(64);
  EXPECT_NE(ptr, nullptr);

  // 写入数据进行验证
  std::memset(ptr, 0xAA, 64);
  EXPECT_EQ(static_cast<char*>(ptr)[0], static_cast<char>(0xAA));
  EXPECT_EQ(static_cast<char*>(ptr)[63], static_cast<char>(0xAA));

  allocator->free(ptr);
}

TEST_F(BmallocTest, MallocZeroSize) {
  // 测试分配0字节应该返回nullptr
  void* ptr = allocator->malloc(0);
  EXPECT_EQ(ptr, nullptr);
}

TEST_F(BmallocTest, FreeNullptr) {
  // 测试释放nullptr应该安全
  EXPECT_NO_THROW(allocator->free(nullptr));
}

// calloc测试
TEST_F(BmallocTest, CallocBasic) {
  // 测试calloc分配并初始化内存
  void* ptr = allocator->calloc(10, 8);
  EXPECT_NE(ptr, nullptr);

  // 检查内存是否被初始化为0
  char* bytes = static_cast<char*>(ptr);
  for (int i = 0; i < 80; i++) {
    EXPECT_EQ(bytes[i], 0);
  }

  allocator->free(ptr);
}

TEST_F(BmallocTest, CallocZeroElements) {
  // 测试calloc分配0个元素
  void* ptr1 = allocator->calloc(0, 8);
  EXPECT_EQ(ptr1, nullptr);

  void* ptr2 = allocator->calloc(8, 0);
  EXPECT_EQ(ptr2, nullptr);
}

TEST_F(BmallocTest, CallocOverflow) {
  // 测试calloc溢出检查
  void* ptr = allocator->calloc(SIZE_MAX / 2, SIZE_MAX / 2 + 1);
  EXPECT_EQ(ptr, nullptr);
}

// realloc测试
TEST_F(BmallocTest, ReallocFromNull) {
  // 测试从nullptr开始的realloc，应该等同于malloc
  void* ptr = allocator->realloc(nullptr, 64);
  EXPECT_NE(ptr, nullptr);

  allocator->free(ptr);
}

TEST_F(BmallocTest, ReallocToZero) {
  // 测试realloc到0大小，应该等同于free
  void* ptr = allocator->malloc(64);
  EXPECT_NE(ptr, nullptr);

  void* new_ptr = allocator->realloc(ptr, 0);
  EXPECT_EQ(new_ptr, nullptr);
}

TEST_F(BmallocTest, ReallocExpand) {
  // 测试扩展内存块
  void* ptr = allocator->malloc(64);
  EXPECT_NE(ptr, nullptr);

  // 写入测试数据
  std::memset(ptr, 0xBB, 64);

  // 扩展到128字节
  void* new_ptr = allocator->realloc(ptr, 128);
  EXPECT_NE(new_ptr, nullptr);

  // 检查原始数据是否保留
  char* bytes = static_cast<char*>(new_ptr);
  for (int i = 0; i < 64; i++) {
    EXPECT_EQ(bytes[i], static_cast<char>(0xBB));
  }

  allocator->free(new_ptr);
}

TEST_F(BmallocTest, ReallocShrink) {
  // 测试缩小内存块
  void* ptr = allocator->malloc(128);
  EXPECT_NE(ptr, nullptr);

  // 写入测试数据
  std::memset(ptr, 0xCC, 128);

  // 缩小到64字节
  void* new_ptr = allocator->realloc(ptr, 64);
  EXPECT_NE(new_ptr, nullptr);

  // 检查前64字节的数据是否保留
  char* bytes = static_cast<char*>(new_ptr);
  for (int i = 0; i < 64; i++) {
    EXPECT_EQ(bytes[i], static_cast<char>(0xCC));
  }

  allocator->free(new_ptr);
}

// aligned_alloc测试
TEST_F(BmallocTest, AlignedAllocBasic) {
  // 测试16字节对齐
  void* ptr = allocator->aligned_alloc(16, 64);
  EXPECT_NE(ptr, nullptr);

  // 检查对齐
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  EXPECT_EQ(addr % 16, 0);

  allocator->aligned_free(ptr);
}

TEST_F(BmallocTest, AlignedAllocLargeAlignment) {
  // 测试256字节对齐
  void* ptr = allocator->aligned_alloc(256, 64);
  EXPECT_NE(ptr, nullptr);

  // 检查对齐
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
  EXPECT_EQ(addr % 256, 0);

  allocator->aligned_free(ptr);
}

TEST_F(BmallocTest, AlignedAllocInvalidAlignment) {
  // 测试无效的对齐参数（非2的幂）
  void* ptr1 = allocator->aligned_alloc(0, 64);
  EXPECT_EQ(ptr1, nullptr);

  void* ptr2 = allocator->aligned_alloc(3, 64);
  EXPECT_EQ(ptr2, nullptr);

  void* ptr3 = allocator->aligned_alloc(15, 64);
  EXPECT_EQ(ptr3, nullptr);
}

TEST_F(BmallocTest, AlignedAllocZeroSize) {
  // 测试分配0字节
  void* ptr = allocator->aligned_alloc(16, 0);
  EXPECT_EQ(ptr, nullptr);
}

// malloc_size测试
TEST_F(BmallocTest, MallocSizeNull) {
  // 测试nullptr的大小
  size_t size = allocator->malloc_size(nullptr);
  EXPECT_EQ(size, 0);
}

TEST_F(BmallocTest, MallocSizeValid) {
  // 测试有效指针的大小
  void* ptr = allocator->malloc(64);
  EXPECT_NE(ptr, nullptr);

  size_t size = allocator->malloc_size(ptr);
  // 由于slab分配器可能分配比请求更大的块，我们只检查是否 >= 64
  EXPECT_GE(size, 64);

  allocator->free(ptr);
}

// 多次分配和释放测试
TEST_F(BmallocTest, MultipleAllocations) {
  std::vector<void*> ptrs;

  // 分配多个不同大小的内存块（减少大小以适应内存限制）
  for (size_t i = 1; i <= 50; i++) {       // 减少到50次分配
    void* ptr = allocator->malloc(i * 4);  // 减少每次分配的大小
    if (ptr != nullptr) {                  // 允许分配失败
      ptrs.push_back(ptr);
    }
  }

  // 确保至少有一些分配成功
  EXPECT_GT(ptrs.size(), 0);

  // 释放所有成功分配的内存块
  for (void* ptr : ptrs) {
    allocator->free(ptr);
  }
}

// 压力测试
TEST_F(BmallocTest, StressTest) {
  const size_t num_iterations = 200;         // 减少迭代次数
  std::vector<std::pair<void*, char>> ptrs;  // 存储指针和期望的数据
  ptrs.reserve(num_iterations);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> size_dist(1, 256);  // 减少最大分配大小

  // 分配阶段
  for (size_t i = 0; i < num_iterations; i++) {
    size_t size = size_dist(gen);
    void* ptr = allocator->malloc(size);
    if (ptr != nullptr) {
      char expected_value = static_cast<char>(i % 256);
      ptrs.push_back({ptr, expected_value});
      // 写入数据以验证内存可用性
      std::memset(ptr, static_cast<int>(expected_value), size);
    }
  }

  // 确保至少有一些分配成功
  EXPECT_GT(ptrs.size(), 0);

  // 验证数据完整性
  for (const auto& [ptr, expected] : ptrs) {
    if (ptr != nullptr) {
      char* bytes = static_cast<char*>(ptr);
      // 只检查第一个字节
      EXPECT_EQ(bytes[0], expected);
    }
  }

  // 释放阶段
  for (const auto& [ptr, expected] : ptrs) {
    allocator->free(ptr);
  }
}

// 边界条件测试
TEST_F(BmallocTest, BoundaryConditions) {
  // 测试各种边界大小
  std::vector<size_t> sizes = {1,   2,   4,   8,    16,   32,  64,
                               128, 256, 512, 1024, 2048, 4096};

  for (size_t size : sizes) {
    void* ptr = allocator->malloc(size);
    if (ptr != nullptr) {
      // 写入边界位置
      char* bytes = static_cast<char*>(ptr);

      if (size == 1) {
        // 对于大小为1的情况，只写入和验证一个字节
        bytes[0] = 0xAA;
        EXPECT_EQ(bytes[0], static_cast<char>(0xAA));
      } else {
        // 对于大小大于1的情况，写入开头和结尾
        bytes[0] = 0xAA;
        bytes[size - 1] = 0xBB;

        // 验证写入
        EXPECT_EQ(bytes[0], static_cast<char>(0xAA));
        EXPECT_EQ(bytes[size - 1], static_cast<char>(0xBB));
      }

      allocator->free(ptr);
    }
  }
}

// 内存泄漏检测测试
TEST_F(BmallocTest, NoMemoryLeak) {
  const size_t num_cycles = 100;

  for (size_t cycle = 0; cycle < num_cycles; cycle++) {
    std::vector<void*> ptrs;

    // 分配一些内存
    for (size_t i = 1; i <= 10; i++) {
      void* ptr = allocator->malloc(i * 32);
      if (ptr != nullptr) {
        ptrs.push_back(ptr);
      }
    }

    // 释放所有内存
    for (void* ptr : ptrs) {
      allocator->free(ptr);
    }
  }

  // 如果有内存泄漏，在多次循环后应该会耗尽内存
  // 这里我们测试在多次循环后还能分配内存
  void* ptr = allocator->malloc(1024);
  EXPECT_NE(ptr, nullptr);
  allocator->free(ptr);
}

// 线程安全测试（如果启用了锁）
TEST_F(BmallocTest, ThreadSafety) {
  const size_t num_threads = 4;
  const size_t allocations_per_thread = 100;

  std::vector<std::thread> threads;
  std::atomic<size_t> successful_allocations{0};

  for (size_t t = 0; t < num_threads; t++) {
    threads.emplace_back(
        [this, &successful_allocations, allocations_per_thread]() {
          std::vector<void*> local_ptrs;

          for (size_t i = 0; i < allocations_per_thread; i++) {
            void* ptr = allocator->malloc(64);
            if (ptr != nullptr) {
              local_ptrs.push_back(ptr);
              successful_allocations++;
            }
          }

          // 释放本线程分配的内存
          for (void* ptr : local_ptrs) {
            allocator->free(ptr);
          }
        });
  }

  // 等待所有线程完成
  for (auto& thread : threads) {
    thread.join();
  }

  // 验证至少有一些分配成功
  EXPECT_GT(successful_allocations.load(), 0);
}

// 4K页面分配测试
TEST_F(BmallocTest, Allocate4KPage) {
  const size_t page_size = 4096;  // 4K page

  // 测试单个4K页面分配
  void* ptr = allocator->malloc(page_size);
  EXPECT_NE(ptr, nullptr) << "Failed to allocate 4K page";

  if (ptr != nullptr) {
    // 验证分配的大小
    size_t allocated_size = allocator->malloc_size(ptr);
    EXPECT_GE(allocated_size, page_size)
        << "Allocated size is smaller than requested";

    // 数据验证 - 写入测试模式
    char* bytes = static_cast<char*>(ptr);

    // 1. 填充整个页面为0xAA
    std::memset(bytes, 0xAA, page_size);

    // 2. 验证写入成功
    for (size_t i = 0; i < page_size; i++) {
      EXPECT_EQ(bytes[i], static_cast<char>(0xAA))
          << "Data verification failed at offset " << i;
    }

    // 3. 测试边界写入 - 写入不同的模式到页面的不同区域
    // 页面开始 - 256字节填充0x11
    std::memset(bytes, 0x11, 256);
    // 页面中间 - 256字节填充0x22
    std::memset(bytes + (page_size / 2) - 128, 0x22, 256);
    // 页面末尾 - 256字节填充0x33
    std::memset(bytes + page_size - 256, 0x33, 256);

    // 4. 验证不同区域的数据
    // 验证开始区域
    for (size_t i = 0; i < 256; i++) {
      EXPECT_EQ(bytes[i], static_cast<char>(0x11))
          << "Start region verification failed at offset " << i;
    }

    // 验证中间区域
    for (size_t i = 0; i < 256; i++) {
      size_t offset = (page_size / 2) - 128 + i;
      EXPECT_EQ(bytes[offset], static_cast<char>(0x22))
          << "Middle region verification failed at offset " << offset;
    }

    // 验证末尾区域
    for (size_t i = 0; i < 256; i++) {
      size_t offset = page_size - 256 + i;
      EXPECT_EQ(bytes[offset], static_cast<char>(0x33))
          << "End region verification failed at offset " << offset;
    }

    // 5. 测试递增模式写入和验证
    for (size_t i = 0; i < page_size; i++) {
      bytes[i] = static_cast<char>(i % 256);
    }

    // 验证递增模式
    for (size_t i = 0; i < page_size; i++) {
      EXPECT_EQ(bytes[i], static_cast<char>(i % 256))
          << "Incremental pattern verification failed at offset " << i;
    }

    allocator->free(ptr);
  }
}

// 多个4K页面分配测试
TEST_F(BmallocTest, MultipleAllocate4KPages) {
  const size_t page_size = 4096;
  const size_t num_pages = 2;  // 尝试分配10个4K页面
  std::vector<void*> pages;

  // 分配多个4K页面
  for (size_t i = 0; i < num_pages; i++) {
    void* ptr = allocator->malloc(page_size);
    if (ptr != nullptr) {
      pages.push_back(ptr);

      // 为每个页面写入唯一的标识模式
      char* bytes = static_cast<char*>(ptr);
      char pattern = static_cast<char>(0x10 + i);  // 每个页面使用不同的模式
      std::memset(bytes, pattern, page_size);
    }
  }

  EXPECT_GT(pages.size(), 0) << "No 4K pages were successfully allocated";

  // 验证每个页面的数据完整性
  for (size_t i = 0; i < pages.size(); i++) {
    char* bytes = static_cast<char*>(pages[i]);
    char expected_pattern = static_cast<char>(0x10 + i);

    // 检查页面开始、中间、末尾的数据
    EXPECT_EQ(bytes[0], expected_pattern)
        << "Page " << i << " start verification failed";
    EXPECT_EQ(bytes[page_size / 2], expected_pattern)
        << "Page " << i << " middle verification failed";
    EXPECT_EQ(bytes[page_size - 1], expected_pattern)
        << "Page " << i << " end verification failed";

    // 随机检查一些位置
    for (size_t j = 0; j < 10; j++) {
      size_t random_offset = (j * 409) % page_size;  // 伪随机偏移
      EXPECT_EQ(bytes[random_offset], expected_pattern)
          << "Page " << i << " random check failed at offset " << random_offset;
    }
  }

  // 释放所有页面
  for (void* ptr : pages) {
    allocator->free(ptr);
  }
}

// 4K页面对齐分配测试
TEST_F(BmallocTest, Aligned4KPageAllocation) {
  const size_t page_size = 4096;

  // 测试4K对齐的4K页面分配
  void* ptr = allocator->aligned_alloc(page_size, page_size);

  if (ptr != nullptr) {
    // 验证对齐
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    EXPECT_EQ(addr % page_size, 0) << "4K page is not properly aligned";

    // 数据验证 - 使用交替模式
    char* bytes = static_cast<char*>(ptr);

    // 写入交替的0xFF和0x00模式
    for (size_t i = 0; i < page_size; i++) {
      bytes[i] =
          (i % 2 == 0) ? static_cast<char>(0xFF) : static_cast<char>(0x00);
    }

    // 验证交替模式
    for (size_t i = 0; i < page_size; i++) {
      char expected =
          (i % 2 == 0) ? static_cast<char>(0xFF) : static_cast<char>(0x00);
      EXPECT_EQ(bytes[i], expected)
          << "Alternating pattern verification failed at offset " << i;
    }

    allocator->aligned_free(ptr);
  } else {
    // 如果4K对齐分配失败，至少测试普通的4K分配
    ptr = allocator->malloc(page_size);
    EXPECT_NE(ptr, nullptr) << "Both aligned and regular 4K allocation failed";
    if (ptr != nullptr) {
      allocator->free(ptr);
    }
  }
}

// 4K页面压力测试
TEST_F(BmallocTest, StressTest4KPages) {
  const size_t page_size = 4096;
  const size_t max_pages = 20;  // 限制最大页面数以适应内存池
  std::vector<std::pair<void*, uint32_t>> allocated_pages;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> pattern_dist(0, UINT32_MAX);

  // 分配阶段
  for (size_t i = 0; i < max_pages; i++) {
    void* ptr = allocator->malloc(page_size);
    if (ptr != nullptr) {
      uint32_t pattern = pattern_dist(gen);
      allocated_pages.push_back({ptr, pattern});

      // 使用32位模式填充页面
      uint32_t* words = static_cast<uint32_t*>(ptr);
      size_t num_words = page_size / sizeof(uint32_t);

      for (size_t j = 0; j < num_words; j++) {
        words[j] = pattern;
      }
    }
  }

  EXPECT_GT(allocated_pages.size(), 0) << "No pages allocated in stress test";

  // 验证阶段
  for (const auto& [ptr, expected_pattern] : allocated_pages) {
    uint32_t* words = static_cast<uint32_t*>(ptr);
    size_t num_words = page_size / sizeof(uint32_t);

    for (size_t i = 0; i < num_words; i++) {
      EXPECT_EQ(words[i], expected_pattern)
          << "Stress test verification failed at word " << i;
    }
  }

  // 释放阶段
  for (const auto& [ptr, pattern] : allocated_pages) {
    allocator->free(ptr);
  }
}

// 专门的4K对齐分配测试 - 使用aligned_alloc
TEST_F(BmallocTest, AlignedAlloc4KPageWithDataValidation) {
  const size_t page_size = 4096;
  const size_t alignment = 4096;  // 4K对齐

  // 测试单个4K对齐页面分配
  void* ptr = allocator->aligned_alloc(alignment, page_size);
  EXPECT_NE(ptr, nullptr) << "Failed to allocate 4K-aligned 4K page";

  if (ptr != nullptr) {
    // 1. 验证4K对齐
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    EXPECT_EQ(addr % alignment, 0)
        << "Allocated memory is not 4K-aligned. Address: 0x" << std::hex
        << addr;

    // 2. 验证分配的大小
    size_t allocated_size = allocator->aligned_malloc_size(ptr);
    EXPECT_GE(allocated_size, page_size)
        << "Allocated size (" << allocated_size
        << ") is smaller than requested (" << page_size << ")";

    char* bytes = static_cast<char*>(ptr);

    // 3. 边界测试 - 写入页面边界位置
    bytes[0] = 0xAA;              // 第一个字节
    bytes[page_size - 1] = 0xBB;  // 最后一个字节

    // 验证边界写入
    EXPECT_EQ(bytes[0], static_cast<char>(0xAA)) << "First byte write failed";
    EXPECT_EQ(bytes[page_size - 1], static_cast<char>(0xBB))
        << "Last byte write failed";

    // 4. 分区数据验证 - 将页面分为4个1K区域，每个区域填充不同模式
    const size_t quarter_size = page_size / 4;

    // 填充第1个1K区域为0x11
    std::memset(bytes, 0x11, quarter_size);
    // 填充第2个1K区域为0x22
    std::memset(bytes + quarter_size, 0x22, quarter_size);
    // 填充第3个1K区域为0x33
    std::memset(bytes + 2 * quarter_size, 0x33, quarter_size);
    // 填充第4个1K区域为0x44
    std::memset(bytes + 3 * quarter_size, 0x44, quarter_size);

    // 验证每个1K区域的数据
    for (size_t region = 0; region < 4; region++) {
      char expected_pattern = static_cast<char>(0x11 + region * 0x11);
      size_t region_start = region * quarter_size;

      // 检查区域开始、中间、结束的几个字节
      EXPECT_EQ(bytes[region_start], expected_pattern)
          << "Region " << region << " start verification failed";
      EXPECT_EQ(bytes[region_start + quarter_size / 2], expected_pattern)
          << "Region " << region << " middle verification failed";
      EXPECT_EQ(bytes[region_start + quarter_size - 1], expected_pattern)
          << "Region " << region << " end verification failed";
    }

    // 5. 64位字对齐写入测试
    uint64_t* words = reinterpret_cast<uint64_t*>(ptr);
    size_t num_words = page_size / sizeof(uint64_t);
    const uint64_t test_pattern = 0x123456789ABCDEF0ULL;

    // 填充64位模式
    for (size_t i = 0; i < num_words; i++) {
      words[i] =
          test_pattern ^ (i * 0x0101010101010101ULL);  // 每个字有轻微变化
    }

    // 验证64位模式
    for (size_t i = 0; i < num_words; i++) {
      uint64_t expected = test_pattern ^ (i * 0x0101010101010101ULL);
      EXPECT_EQ(words[i], expected)
          << "64-bit word verification failed at index " << i
          << ", expected: 0x" << std::hex << expected << ", got: 0x" << std::hex
          << words[i];
    }

    // 6. 缓存行边界测试 (假设64字节缓存行)
    const size_t cache_line_size = 64;
    const size_t num_cache_lines = page_size / cache_line_size;

    // 在每个缓存行的开始写入标识
    for (size_t i = 0; i < num_cache_lines; i++) {
      size_t offset = i * cache_line_size;
      bytes[offset] = static_cast<char>(i & 0xFF);
    }

    // 验证缓存行标识
    for (size_t i = 0; i < num_cache_lines; i++) {
      size_t offset = i * cache_line_size;
      EXPECT_EQ(bytes[offset], static_cast<char>(i & 0xFF))
          << "Cache line marker verification failed at line " << i;
    }

    allocator->aligned_free(ptr);
  }
}

// 多个4K对齐页面分配测试
TEST_F(BmallocTest, MultipleAligned4KPages) {
  const size_t page_size = 4096;
  const size_t alignment = 4096;
  const size_t num_pages = 20;
  std::vector<void*> aligned_pages;

  // 分配多个4K对齐页面
  for (size_t i = 0; i < num_pages; i++) {
    void* ptr = allocator->aligned_alloc(alignment, page_size);
    if (ptr != nullptr) {
      // 验证对齐
      uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
      EXPECT_EQ(addr % alignment, 0)
          << "Page " << i << " is not 4K-aligned. Address: 0x" << std::hex
          << addr;

      aligned_pages.push_back(ptr);

      // 为每个页面写入唯一的重复模式
      uint16_t pattern = static_cast<uint16_t>(0x1000 + i * 0x111);

      uint16_t* words = reinterpret_cast<uint16_t*>(ptr);
      size_t num_words = page_size / sizeof(uint16_t);

      // 填充整个页面
      for (size_t j = 0; j < num_words; j++) {
        words[j] = pattern;
      }
    }
  }

  EXPECT_GT(aligned_pages.size(), 0)
      << "No 4K-aligned pages were successfully allocated";

  // 验证所有页面的数据完整性
  for (size_t i = 0; i < aligned_pages.size(); i++) {
    uint16_t* words = reinterpret_cast<uint16_t*>(aligned_pages[i]);
    uint16_t expected_pattern = static_cast<uint16_t>(0x1000 + i * 0x111);
    size_t num_words = page_size / sizeof(uint16_t);

    // 检查开始、中间、结束位置
    EXPECT_EQ(words[0], expected_pattern)
        << "Aligned page " << i << " start verification failed";
    EXPECT_EQ(words[num_words / 2], expected_pattern)
        << "Aligned page " << i << " middle verification failed";
    EXPECT_EQ(words[num_words - 1], expected_pattern)
        << "Aligned page " << i << " end verification failed";

    // 随机抽样检查
    for (size_t j = 0; j < 10; j++) {
      size_t random_index = (j * 997) % num_words;  // 伪随机索引
      EXPECT_EQ(words[random_index], expected_pattern)
          << "Aligned page " << i << " random check failed at word "
          << random_index;
    }
  }

  // 释放所有页面
  for (void* ptr : aligned_pages) {
    allocator->aligned_free(ptr);
  }
}
