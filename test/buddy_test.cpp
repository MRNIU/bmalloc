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
  static constexpr size_t kTestMemorySize = kPageSize * kPageSize;
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

// 基本分配和释放测试
TEST_F(BuddyTest, BasicAllocationAndFree) {
  Buddy<TestLogger, TestLock> allocator("TestBuddy", test_memory_,
                                        kTestMemorySize);

  // 分配一小块内存
  void* ptr1 = allocator.Alloc(64);
  ASSERT_NE(ptr1, nullptr);
  EXPECT_GE(ptr1, test_memory_);
  EXPECT_LT(ptr1, static_cast<char*>(test_memory_) + kTestMemorySize);

  // 写入数据验证内存可用，并验证数据完整性
  unsigned char* data = static_cast<unsigned char*>(ptr1);
  for (size_t i = 0; i < 64; ++i) {
    data[i] = static_cast<unsigned char>(i % 256);
  }

  // 验证写入的数据
  for (size_t i = 0; i < 64; ++i) {
    EXPECT_EQ(data[i], static_cast<unsigned char>(i % 256))
        << "Data corruption at index " << i;
  }

  // 释放内存
  allocator.Free(ptr1);

  // 再次分配相同大小应该成功
  void* ptr2 = allocator.Alloc(64);
  ASSERT_NE(ptr2, nullptr);

  // 验证新分配的内存可以正常写入和读取
  unsigned char* data2 = static_cast<unsigned char*>(ptr2);
  for (size_t i = 0; i < 64; ++i) {
    data2[i] = static_cast<unsigned char>((i + 1) % 256);
  }

  // 验证数据完整性
  for (size_t i = 0; i < 64; ++i) {
    EXPECT_EQ(data2[i], static_cast<unsigned char>((i + 1) % 256))
        << "Data corruption in second allocation at index " << i;
  }

  allocator.Free(ptr2);
}

// 不同大小分配测试
TEST_F(BuddyTest, DifferentSizeAllocations) {
  Buddy<TestLogger, TestLock> allocator("TestBuddy", test_memory_,
                                        kTestMemorySize);

  std::vector<void*> ptrs;
  std::vector<size_t> sizes = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
  std::vector<std::vector<unsigned char>> expected_data;

  // 分配不同大小的内存块
  for (size_t i = 0; i < sizes.size(); ++i) {
    size_t size = sizes[i];
    void* ptr = allocator.Alloc(size);
    ASSERT_NE(ptr, nullptr) << "Failed to allocate " << size << " bytes";
    ptrs.push_back(ptr);

    // 准备测试数据并写入
    std::vector<unsigned char> test_data(size);
    unsigned char* mem_ptr = static_cast<unsigned char*>(ptr);
    for (size_t j = 0; j < size; ++j) {
      test_data[j] = static_cast<unsigned char>((i * 37 + j) % 256);
      mem_ptr[j] = test_data[j];
    }
    expected_data.push_back(test_data);
  }

  // 验证所有内存块的数据完整性
  for (size_t i = 0; i < ptrs.size(); ++i) {
    unsigned char* mem_ptr = static_cast<unsigned char*>(ptrs[i]);
    for (size_t j = 0; j < sizes[i]; ++j) {
      EXPECT_EQ(mem_ptr[j], expected_data[i][j])
          << "Data corruption in block " << i << " at offset " << j
          << " (size=" << sizes[i] << ")";
    }
  }

  // 释放所有内存
  for (void* ptr : ptrs) {
    allocator.Free(ptr);
  }
}

// 内存对齐测试
TEST_F(BuddyTest, MemoryAlignment) {
  Buddy<TestLogger, TestLock> allocator("TestBuddy", test_memory_,
                                        kTestMemorySize);

  // 分配一些内存块并检查对齐
  std::vector<void*> ptrs;
  for (int i = 0; i < 10; ++i) {
    size_t alloc_size = 64 + i * 8;
    void* ptr = allocator.Alloc(alloc_size);
    ASSERT_NE(ptr, nullptr);
    ptrs.push_back(ptr);

    // 检查地址是否正确对齐（通常是8或16字节对齐）
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    EXPECT_EQ(addr % 8, 0) << "Address " << ptr << " is not 8-byte aligned";

    // 写入并验证数据，确保对齐的内存能够正确访问
    unsigned char* data = static_cast<unsigned char*>(ptr);
    for (size_t j = 0; j < alloc_size; ++j) {
      data[j] = static_cast<unsigned char>((i + j) % 256);
    }

    // 立即验证数据完整性
    for (size_t j = 0; j < alloc_size; ++j) {
      EXPECT_EQ(data[j], static_cast<unsigned char>((i + j) % 256))
          << "Data corruption in alignment test block " << i << " at offset "
          << j;
    }
  }

  // 清理内存
  for (void* ptr : ptrs) {
    allocator.Free(ptr);
  }
}

// 大块内存分配测试
TEST_F(BuddyTest, LargeAllocation) {
  Buddy<TestLogger, TestLock> allocator("TestBuddy", test_memory_,
                                        kTestMemorySize);

  // 尝试分配一个大块内存（但小于总大小）
  size_t large_size = kTestMemorySize / 4;
  void* ptr = allocator.Alloc(large_size);
  ASSERT_NE(ptr, nullptr);

  // 写入特定模式的数据进行验证
  unsigned char* data = static_cast<unsigned char*>(ptr);

  // 使用复杂的数据模式：前1/4写入递增，中间1/2写入递减，最后1/4写入异或
  size_t quarter = large_size / 4;

  // 第一部分：递增模式
  for (size_t i = 0; i < quarter; ++i) {
    data[i] = static_cast<unsigned char>(i % 256);
  }

  // 第二、三部分：递减模式
  for (size_t i = quarter; i < 3 * quarter; ++i) {
    data[i] = static_cast<unsigned char>((255 - i) % 256);
  }

  // 第四部分：异或模式
  for (size_t i = 3 * quarter; i < large_size; ++i) {
    data[i] = static_cast<unsigned char>((i ^ 0xAA) % 256);
  }

  // 验证数据完整性
  for (size_t i = 0; i < quarter; ++i) {
    EXPECT_EQ(data[i], static_cast<unsigned char>(i % 256))
        << "Data corruption in large allocation (increment pattern) at " << i;
  }

  for (size_t i = quarter; i < 3 * quarter; ++i) {
    EXPECT_EQ(data[i], static_cast<unsigned char>((255 - i) % 256))
        << "Data corruption in large allocation (decrement pattern) at " << i;
  }

  for (size_t i = 3 * quarter; i < large_size; ++i) {
    EXPECT_EQ(data[i], static_cast<unsigned char>((i ^ 0xAA) % 256))
        << "Data corruption in large allocation (XOR pattern) at " << i;
  }

  allocator.Free(ptr);
}

// 分配失败测试
TEST_F(BuddyTest, AllocationFailure) {
  Buddy<TestLogger, TestLock> allocator("TestBuddy", test_memory_,
                                        kTestMemorySize);

  // 尝试分配超过总大小的内存，应该失败
  void* ptr = allocator.Alloc(kTestMemorySize * 2);
  EXPECT_EQ(ptr, nullptr);

  // 尝试分配零大小
  ptr = allocator.Alloc(0);
  // 根据实现，这可能返回nullptr或有效指针
  if (ptr != nullptr) {
    allocator.Free(ptr);
  }
}

// 碎片化测试
TEST_F(BuddyTest, FragmentationTest) {
  Buddy<TestLogger, TestLock> allocator("TestBuddy", test_memory_,
                                        kTestMemorySize);

  std::vector<void*> ptrs;
  std::vector<std::vector<unsigned char>> expected_data;

  // 分配多个小块
  for (int i = 0; i < 100; ++i) {
    void* ptr = allocator.Alloc(32);
    if (ptr != nullptr) {
      ptrs.push_back(ptr);

      // 为每个块写入唯一的数据模式
      std::vector<unsigned char> block_data(32);
      unsigned char* data = static_cast<unsigned char*>(ptr);
      for (size_t j = 0; j < 32; ++j) {
        block_data[j] = static_cast<unsigned char>((i * 7 + j) % 256);
        data[j] = block_data[j];
      }
      expected_data.push_back(block_data);
    }
  }

  EXPECT_FALSE(ptrs.empty());

  // 验证所有分配块的数据完整性
  for (size_t i = 0; i < ptrs.size(); ++i) {
    if (ptrs[i] != nullptr) {
      unsigned char* data = static_cast<unsigned char*>(ptrs[i]);
      for (size_t j = 0; j < 32; ++j) {
        EXPECT_EQ(data[j], expected_data[i][j])
            << "Data corruption before fragmentation in block " << i
            << " at offset " << j;
      }
    }
  }

  // 释放每隔一个块，造成碎片
  for (size_t i = 0; i < ptrs.size(); i += 2) {
    allocator.Free(ptrs[i]);
    ptrs[i] = nullptr;
  }

  // 验证剩余块的数据完整性
  for (size_t i = 1; i < ptrs.size(); i += 2) {
    if (ptrs[i] != nullptr) {
      unsigned char* data = static_cast<unsigned char*>(ptrs[i]);
      for (size_t j = 0; j < 32; ++j) {
        EXPECT_EQ(data[j], expected_data[i][j])
            << "Data corruption after fragmentation in block " << i
            << " at offset " << j;
      }
    }
  }

  // 尝试重新分配
  for (size_t i = 0; i < ptrs.size(); i += 2) {
    if (ptrs[i] == nullptr) {
      void* new_ptr = allocator.Alloc(32);
      if (new_ptr != nullptr) {
        ptrs[i] = new_ptr;
        // 写入新的数据模式并验证
        unsigned char* data = static_cast<unsigned char*>(new_ptr);
        for (size_t j = 0; j < 32; ++j) {
          unsigned char expected =
              static_cast<unsigned char>((i * 11 + j) % 256);
          data[j] = expected;
        }
        // 立即验证写入
        for (size_t j = 0; j < 32; ++j) {
          unsigned char expected =
              static_cast<unsigned char>((i * 11 + j) % 256);
          EXPECT_EQ(data[j], expected)
              << "Data corruption in reallocated block " << i << " at offset "
              << j;
        }
      }
    }
  }

  // 清理所有内存
  for (void* ptr : ptrs) {
    if (ptr != nullptr) {
      allocator.Free(ptr);
    }
  }
}

// 重复分配释放测试
TEST_F(BuddyTest, RepeatedAllocFree) {
  Buddy<TestLogger, TestLock> allocator("TestBuddy", test_memory_,
                                        kTestMemorySize);

  const int iterations = 1000;
  const size_t alloc_size = 128;

  for (int i = 0; i < iterations; ++i) {
    void* ptr = allocator.Alloc(alloc_size);
    ASSERT_NE(ptr, nullptr) << "Allocation failed at iteration " << i;

    // 写入特定模式的数据
    unsigned char* data = static_cast<unsigned char*>(ptr);
    unsigned char pattern = static_cast<unsigned char>(i % 256);
    for (size_t j = 0; j < alloc_size; ++j) {
      data[j] = static_cast<unsigned char>((pattern + j) % 256);
    }

    // 验证数据完整性
    for (size_t j = 0; j < alloc_size; ++j) {
      EXPECT_EQ(data[j], static_cast<unsigned char>((pattern + j) % 256))
          << "Data corruption in iteration " << i << " at offset " << j;
    }

    // 立即释放
    allocator.Free(ptr);
    allocator.Free(ptr);
  }
}

// 边界情况测试
TEST_F(BuddyTest, BoundaryConditions) {
  Buddy<TestLogger, TestLock> allocator("TestBuddy", test_memory_,
                                        kTestMemorySize);

  // 测试1字节分配
  void* ptr1 = allocator.Alloc(1);
  if (ptr1 != nullptr) {
    unsigned char* data1 = static_cast<unsigned char*>(ptr1);
    *data1 = 0xAA;
    EXPECT_EQ(*data1, 0xAA) << "Single byte allocation data corruption";
    allocator.Free(ptr1);
  }

  // 测试最大可能分配（接近总大小）
  size_t max_size = kTestMemorySize / 2;  // 保守估计
  void* ptr2 = allocator.Alloc(max_size);
  if (ptr2 != nullptr) {
    unsigned char* data2 = static_cast<unsigned char*>(ptr2);

    // 写入开头、中间和结尾的测试模式
    data2[0] = 0xBB;
    data2[max_size / 2] = 0xCC;
    data2[max_size - 1] = 0xDD;

    // 验证数据完整性
    EXPECT_EQ(data2[0], 0xBB) << "Large allocation start data corruption";
    EXPECT_EQ(data2[max_size / 2], 0xCC)
        << "Large allocation middle data corruption";
    EXPECT_EQ(data2[max_size - 1], 0xDD)
        << "Large allocation end data corruption";

    // 写入更复杂的模式到整个块
    for (size_t i = 0; i < max_size; ++i) {
      data2[i] = static_cast<unsigned char>((i ^ 0x55) % 256);
    }

    // 验证整个块的数据完整性
    for (size_t i = 0; i < max_size; ++i) {
      EXPECT_EQ(data2[i], static_cast<unsigned char>((i ^ 0x55) % 256))
          << "Large allocation full pattern data corruption at offset " << i;
    }

    allocator.Free(ptr2);
  }
}

// 无锁版本测试
TEST_F(BuddyTest, NoLockVersion) {
  Buddy<TestLogger, LockBase> allocator("NoLockBuddy", test_memory_,
                                        kTestMemorySize);

  void* ptr = allocator.Alloc(256);
  ASSERT_NE(ptr, nullptr);

  // 写入并验证数据
  unsigned char* data = static_cast<unsigned char*>(ptr);
  for (size_t i = 0; i < 256; ++i) {
    data[i] = static_cast<unsigned char>((i + 0x77) % 256);
  }

  // 验证数据完整性
  for (size_t i = 0; i < 256; ++i) {
    EXPECT_EQ(data[i], static_cast<unsigned char>((i + 0x77) % 256))
        << "Data corruption in no-lock version at offset " << i;
  }

  allocator.Free(ptr);
}

// 无日志版本测试
TEST_F(BuddyTest, NoLogVersion) {
  Buddy<std::nullptr_t, TestLock> allocator("NoLogBuddy", test_memory_,
                                            kTestMemorySize);

  void* ptr = allocator.Alloc(512);
  ASSERT_NE(ptr, nullptr);

  // 写入并验证数据
  unsigned char* data = static_cast<unsigned char*>(ptr);
  for (size_t i = 0; i < 512; ++i) {
    data[i] = static_cast<unsigned char>((i * 3 + 0x88) % 256);
  }

  // 验证数据完整性
  for (size_t i = 0; i < 512; ++i) {
    EXPECT_EQ(data[i], static_cast<unsigned char>((i * 3 + 0x88) % 256))
        << "Data corruption in no-log version at offset " << i;
  }

  allocator.Free(ptr);
}

// 数据完整性压力测试
TEST_F(BuddyTest, DataIntegrityStressTest) {
  Buddy<TestLogger, TestLock> allocator("StressTest", test_memory_,
                                        kTestMemorySize);

  const int num_blocks = 50;
  const size_t block_size = 256;

  struct BlockInfo {
    void* ptr;
    std::vector<unsigned char> expected_data;
  };

  std::vector<BlockInfo> blocks;

  // 分配多个块并写入唯一数据
  for (int i = 0; i < num_blocks; ++i) {
    void* ptr = allocator.Alloc(block_size);
    if (ptr != nullptr) {
      BlockInfo block;
      block.ptr = ptr;
      block.expected_data.resize(block_size);

      unsigned char* data = static_cast<unsigned char*>(ptr);

      // 为每个块生成唯一的数据模式
      for (size_t j = 0; j < block_size; ++j) {
        unsigned char value =
            static_cast<unsigned char>((i * 17 + j * 31) % 256);
        block.expected_data[j] = value;
        data[j] = value;
      }

      blocks.push_back(block);
    }
  }

  EXPECT_FALSE(blocks.empty())
      << "Failed to allocate any blocks for stress test";

  // 多次验证所有块的数据完整性
  for (int round = 0; round < 10; ++round) {
    for (size_t i = 0; i < blocks.size(); ++i) {
      unsigned char* data = static_cast<unsigned char*>(blocks[i].ptr);
      for (size_t j = 0; j < block_size; ++j) {
        EXPECT_EQ(data[j], blocks[i].expected_data[j])
            << "Data corruption in stress test round " << round << ", block "
            << i << ", offset " << j;
      }
    }
  }

  // 清理所有块
  for (const auto& block : blocks) {
    allocator.Free(block.ptr);
  }
}
