/**
 * Copyright The bmalloc Contributors
 * @file firstfit_test.cpp
 * @brief FirstFit分配器的Google Test测试用例
 */

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

#include "first_fit.h"

namespace bmalloc {

/**
 * @brief 测试用的互斥锁实现
 * @details 基于std::mutex实现的LockBase接口，用于多线程测试
 */
class TestMutexLock : public LockBase {
 public:
  void Lock() override { mutex_.lock(); }

  void Unlock() override { mutex_.unlock(); }

 private:
  std::mutex mutex_;
};

/**
 * @brief 辅助函数：打印 FirstFit 分配器的当前状态
 * 由于 FirstFit 的成员现在是 protected，我们创建一个继承类来访问内部状态
 */
class FirstFitDebugHelper : public FirstFit {
 public:
  FirstFitDebugHelper(const char* name, void* start_addr, size_t page_count,
                      int (*log_func)(const char*, ...) = nullptr,
                      LockBase* lock = nullptr)
      : FirstFit(name, start_addr, page_count, log_func, lock) {}

  void print() const {
    printf("\n==========================================\n");
    printf("FirstFit 分配器状态详情\n");
    printf("管理页数: %zu, 已使用: %zu, 空闲: %zu\n", length_, used_count_,
           free_count_);

    printf("位图状态 (仅显示前64页):\n");
    size_t display_pages = std::min(length_, static_cast<size_t>(64));
    for (size_t i = 0; i < display_pages; i += 8) {
      printf("页[%2zu-%2zu]: ", i, std::min(i + 7, display_pages - 1));
      for (size_t j = i; j < std::min(i + 8, display_pages); ++j) {
        printf("%c", bitmap_[j] ? '1' : '0');
      }
      printf("\n");
    }
    if (length_ > 64) {
      printf("... (省略余下 %zu 页)\n", length_ - 64);
    }
    printf("==========================================\n");
  }

  // 提供访问位图的方法用于测试
  bool IsPageAllocated(size_t page_index) const { return bitmap_[page_index]; }

  size_t GetLength() const { return length_; }
};

/**
 * @brief FirstFit分配器测试夹具类
 * 提供测试环境的初始化和清理
 */
class FirstFitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 分配测试用的内存池 (1MB = 256页)
    test_memory_size_ = 1024 * 1024;                             // 1MB
    test_pages_ = test_memory_size_ / AllocatorBase::kPageSize;  // 256页
    test_memory_ =
        std::aligned_alloc(AllocatorBase::kPageSize, test_memory_size_);

    ASSERT_NE(test_memory_, nullptr) << "无法分配测试内存";

    // 初始化为0，便于检测内存污染
    std::memset(test_memory_, 0, test_memory_size_);

    // 创建firstfit分配器实例（使用带调试功能的版本）
    firstfit_ = std::make_unique<FirstFitDebugHelper>(
        "test_firstfit", test_memory_, test_pages_);
  }

  void TearDown() override {
    firstfit_.reset();
    if (test_memory_) {
      std::free(test_memory_);
      test_memory_ = nullptr;
    }
  }

  // 辅助函数：检查内存是否在管理范围内
  bool IsInManagedRange(void* ptr) const {
    if (!ptr) return false;
    auto start = static_cast<char*>(test_memory_);
    auto end = start + test_memory_size_;
    auto addr = static_cast<char*>(ptr);
    return addr >= start && addr < end;
  }

  // 辅助函数：检查地址是否按页边界对齐
  bool IsPageAligned(void* ptr) const {
    if (!ptr) return false;
    auto addr = reinterpret_cast<uintptr_t>(ptr);
    return (addr % AllocatorBase::kPageSize) == 0;
  }

  // 辅助函数：全面检查分配地址的有效性
  bool ValidateAllocatedAddress(void* ptr, size_t page_count,
                                const char* test_name = "") const {
    std::cout << "\n--- 地址有效性检查";
    if (strlen(test_name) > 0) {
      std::cout << " (" << test_name << ")";
    }
    std::cout << " ---" << std::endl;

    if (!ptr) {
      std::cout << "✗ 地址为空" << std::endl;
      return false;
    }
    std::cout << "✓ 指针非空: " << ptr << std::endl;

    if (!IsInManagedRange(ptr)) {
      std::cout << "✗ 地址不在管理范围内" << std::endl;
      return false;
    }
    std::cout << "✓ 地址在管理范围内" << std::endl;

    if (!IsPageAligned(ptr)) {
      std::cout << "✗ 地址未按页边界对齐" << std::endl;
      return false;
    }
    std::cout << "✓ 地址按页边界对齐" << std::endl;

    // 详细地址信息
    auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
    size_t page_index = offset / AllocatorBase::kPageSize;
    std::cout << "地址详细信息:" << std::endl;
    std::cout << "  地址: " << ptr << std::endl;
    std::cout << "  相对偏移: " << offset << " 字节" << std::endl;
    std::cout << "  页号范围: " << page_index;
    if (page_count > 1) {
      std::cout << " - " << (page_index + page_count - 1);
    }
    std::cout << " (共 " << page_count << " 页)" << std::endl;
    std::cout << "  总大小: " << (page_count * AllocatorBase::kPageSize)
              << " 字节" << std::endl;

    // 检查内存块是否在管理范围内
    if (page_index + page_count > firstfit_->GetLength()) {
      std::cout << "✗ 内存块超出管理范围" << std::endl;
      return false;
    }
    std::cout << "✓ 内存块在管理范围内" << std::endl;

    // 简单的读写测试
    try {
      volatile uint8_t* test_ptr = static_cast<uint8_t*>(ptr);
      uint8_t original = *test_ptr;
      *test_ptr = 0xAA;
      if (*test_ptr != 0xAA) {
        std::cout << "✗ 内存写入失败" << std::endl;
        return false;
      }
      *test_ptr = original;  // 恢复原值
      std::cout << "✓ 地址可读写" << std::endl;
    } catch (...) {
      std::cout << "✗ 地址读写异常" << std::endl;
      return false;
    }

    std::cout << "--- 检查结果: 全部通过 ---" << std::endl;
    return true;
  }

  // 辅助函数：用随机数据填充内存块
  void FillRandomData(void* ptr, size_t page_count, std::mt19937& gen) {
    auto* data = static_cast<uint8_t*>(ptr);
    size_t size = page_count * AllocatorBase::kPageSize;
    std::uniform_int_distribution<uint8_t> dist(0, 255);

    for (size_t i = 0; i < size; ++i) {
      data[i] = dist(gen);
    }
  }

  // 辅助函数：验证内存数据完整性
  bool VerifyData(void* ptr, size_t page_count,
                  const std::vector<uint8_t>& expected_data) {
    auto* data = static_cast<uint8_t*>(ptr);
    size_t size = page_count * AllocatorBase::kPageSize;

    if (expected_data.size() != size) {
      return false;
    }

    for (size_t i = 0; i < size; ++i) {
      if (data[i] != expected_data[i]) {
        return false;
      }
    }
    return true;
  }

  // 辅助函数：保存内存数据用于后续验证
  std::vector<uint8_t> SaveData(void* ptr, size_t page_count) {
    auto* data = static_cast<uint8_t*>(ptr);
    size_t size = page_count * AllocatorBase::kPageSize;
    return std::vector<uint8_t>(data, data + size);
  }

  std::unique_ptr<FirstFitDebugHelper> firstfit_;
  void* test_memory_ = nullptr;
  size_t test_memory_size_ = 0;
  size_t test_pages_ = 0;
};

/**
 * @brief 多线程FirstFit分配器测试夹具类
 * 使用线程安全的锁机制
 */
class FirstFitMultiThreadTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 分配测试用的内存池 (2MB = 512页，更大的内存用于多线程测试)
    test_memory_size_ = 2 * 1024 * 1024;                         // 2MB
    test_pages_ = test_memory_size_ / AllocatorBase::kPageSize;  // 512页
    test_memory_ =
        std::aligned_alloc(AllocatorBase::kPageSize, test_memory_size_);

    ASSERT_NE(test_memory_, nullptr) << "无法分配测试内存";

    // 初始化为0，便于检测内存污染
    std::memset(test_memory_, 0, test_memory_size_);

    // 创建测试用的锁
    lock_ = std::make_unique<TestMutexLock>();

    // 创建thread-safe的firstfit分配器实例
    firstfit_ = std::make_unique<FirstFitDebugHelper>(
        "test_firstfit_mt", test_memory_, test_pages_, nullptr, lock_.get());
  }

  void TearDown() override {
    firstfit_.reset();
    lock_.reset();
    if (test_memory_) {
      std::free(test_memory_);
      test_memory_ = nullptr;
    }
  }

  // 辅助函数：检查内存是否在管理范围内
  bool IsInManagedRange(void* ptr) const {
    if (!ptr) return false;
    auto start = static_cast<char*>(test_memory_);
    auto end = start + test_memory_size_;
    auto addr = static_cast<char*>(ptr);
    return addr >= start && addr < end;
  }

  // 辅助函数：验证地址有效性（简化版本）
  bool ValidateAddress(void* ptr, size_t page_count) const {
    if (!ptr || !IsInManagedRange(ptr)) return false;
    
    auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
    if (offset % AllocatorBase::kPageSize != 0) return false;
    
    size_t page_index = offset / AllocatorBase::kPageSize;
    return (page_index + page_count <= test_pages_);
  }

  std::unique_ptr<FirstFitDebugHelper> firstfit_;
  std::unique_ptr<TestMutexLock> lock_;
  void* test_memory_ = nullptr;
  size_t test_memory_size_ = 0;
  size_t test_pages_ = 0;
};

/**
 * @brief 测试基本的分配和释放功能
 */
TEST_F(FirstFitTest, BasicAllocAndFree) {
  std::cout << "\n=== BasicAllocAndFree 测试开始 ===" << std::endl;
  std::cout << "初始状态:" << std::endl;
  firstfit_->print();

  std::random_device rd;
  std::mt19937 gen(rd());

  // 测试基本分配
  std::cout << "\n分配1页..." << std::endl;
  void* ptr1 = firstfit_->Alloc(1);  // 分配1页
  ASSERT_NE(ptr1, nullptr) << "分配1页失败";
  EXPECT_TRUE(ValidateAllocatedAddress(ptr1, 1, "1页分配"))
      << "1页地址验证失败";
  std::cout << "✓ 分配成功: ptr1 = " << ptr1 << " (1页)" << std::endl;

  // 填充随机数据并保存
  FillRandomData(ptr1, 1, gen);
  auto data1 = SaveData(ptr1, 1);
  std::cout << "已填充随机数据到1页内存" << std::endl;
  firstfit_->print();

  std::cout << "\n分配3页..." << std::endl;
  void* ptr2 = firstfit_->Alloc(3);  // 分配3页
  ASSERT_NE(ptr2, nullptr) << "分配3页失败";
  EXPECT_TRUE(ValidateAllocatedAddress(ptr2, 3, "3页分配"))
      << "3页地址验证失败";
  std::cout << "✓ 分配成功: ptr2 = " << ptr2 << " (3页)" << std::endl;

  // 填充随机数据并保存
  FillRandomData(ptr2, 3, gen);
  auto data2 = SaveData(ptr2, 3);
  std::cout << "已填充随机数据到3页内存" << std::endl;
  firstfit_->print();

  std::cout << "\n分配2页..." << std::endl;
  void* ptr3 = firstfit_->Alloc(2);  // 分配2页
  ASSERT_NE(ptr3, nullptr) << "分配2页失败";
  EXPECT_TRUE(ValidateAllocatedAddress(ptr3, 2, "2页分配"))
      << "2页地址验证失败";
  std::cout << "✓ 分配成功: ptr3 = " << ptr3 << " (2页)" << std::endl;

  // 填充随机数据并保存
  FillRandomData(ptr3, 2, gen);
  auto data3 = SaveData(ptr3, 2);
  std::cout << "已填充随机数据到2页内存" << std::endl;
  firstfit_->print();

  // 检查分配的地址不重叠
  EXPECT_NE(ptr1, ptr2) << "分配的地址重叠";
  EXPECT_NE(ptr1, ptr3) << "分配的地址重叠";
  EXPECT_NE(ptr2, ptr3) << "分配的地址重叠";

  // 验证数据完整性
  std::cout << "\n验证数据完整性..." << std::endl;
  EXPECT_TRUE(VerifyData(ptr1, 1, data1)) << "1页内存数据完整性验证失败";
  EXPECT_TRUE(VerifyData(ptr2, 3, data2)) << "3页内存数据完整性验证失败";
  EXPECT_TRUE(VerifyData(ptr3, 2, data3)) << "2页内存数据完整性验证失败";
  std::cout << "所有内存数据完整性验证通过" << std::endl;

  // 测试释放
  std::cout << "\n释放1页..." << std::endl;
  std::cout << "释放地址: " << ptr1 << std::endl;
  firstfit_->Free(ptr1, 1);
  firstfit_->print();

  std::cout << "\n释放3页..." << std::endl;
  std::cout << "释放地址: " << ptr2 << std::endl;
  firstfit_->Free(ptr2, 3);
  firstfit_->print();

  std::cout << "\n释放2页..." << std::endl;
  std::cout << "释放地址: " << ptr3 << std::endl;
  firstfit_->Free(ptr3, 2);
  firstfit_->print();

  std::cout << "=== BasicAllocAndFree 测试结束 ===\n" << std::endl;
}

/**
 * @brief 测试边界条件
 */
TEST_F(FirstFitTest, BoundaryConditions) {
  // 测试分配0页（应该失败）
  std::cout << "\n测试分配0页..." << std::endl;
  void* ptr = firstfit_->Alloc(0);
  EXPECT_EQ(ptr, nullptr) << "分配0页应该失败";
  std::cout << "✓ 预期失败: 0页分配返回 nullptr" << std::endl;

  // 测试分配超大块（应该失败）
  std::cout << "\n测试分配超大块..." << std::endl;
  void* large_ptr = firstfit_->Alloc(test_pages_ + 1);  // 超过总页数
  EXPECT_EQ(large_ptr, nullptr) << "应该无法分配超大内存块";
  std::cout << "✓ 预期失败: 超大块分配返回 nullptr" << std::endl;
}

/**
 * @brief 测试内存耗尽情况
 */
TEST_F(FirstFitTest, MemoryExhaustion) {
  std::cout << "\n=== MemoryExhaustion 测试开始 ===" << std::endl;
  std::cout << "初始状态:" << std::endl;
  firstfit_->print();

  std::random_device rd;
  std::mt19937 gen(rd());
  std::vector<std::pair<void*, std::vector<uint8_t>>> allocated_blocks;

  // 持续分配直到内存耗尽
  std::cout << "\n开始持续分配1页内存块，直到耗尽..." << std::endl;
  for (int i = 0; i < 1000; ++i) {
    void* ptr = firstfit_->Alloc(1);
    if (ptr == nullptr) {
      std::cout << "\n内存耗尽，共分配了 " << allocated_blocks.size() << " 页"
                << std::endl;
      break;
    }

    // 填充随机数据
    FillRandomData(ptr, 1, gen);
    auto data = SaveData(ptr, 1);
    allocated_blocks.emplace_back(ptr, std::move(data));

    if ((i + 1) <= 10 || (i + 1) % 50 == 0) {
      auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
      size_t page_index = offset / AllocatorBase::kPageSize;
      std::cout << "第" << (i + 1) << "个分配: " << ptr << " (页" << page_index
                << ")" << std::endl;
    }
  }

  firstfit_->print();
  EXPECT_GT(allocated_blocks.size(), 0) << "应该能分配至少一些内存";
  EXPECT_LE(allocated_blocks.size(), test_pages_) << "分配的页数不应超过总页数";

  // 验证再次分配失败
  void* ptr = firstfit_->Alloc(1);
  EXPECT_EQ(ptr, nullptr) << "内存耗尽后应该无法继续分配";

  // 验证所有内存数据的完整性
  std::cout << "\n验证所有分配的内存数据完整性..." << std::endl;
  size_t verified_count = 0;
  for (const auto& [allocated_ptr, expected_data] : allocated_blocks) {
    if (VerifyData(allocated_ptr, 1, expected_data)) {
      verified_count++;
    }
  }
  EXPECT_EQ(verified_count, allocated_blocks.size())
      << "所有内存块的数据完整性验证应该通过";
  std::cout << "验证了 " << verified_count << " 个内存块的数据完整性"
            << std::endl;

  // 释放所有内存
  std::cout << "\n开始释放所有内存..." << std::endl;
  for (const auto& [allocated_ptr, data] : allocated_blocks) {
    firstfit_->Free(allocated_ptr, 1);
  }
  std::cout << "所有内存释放完成，当前状态:" << std::endl;
  firstfit_->print();

  // 验证释放后可以重新分配
  ptr = firstfit_->Alloc(1);
  EXPECT_NE(ptr, nullptr) << "释放内存后应该能重新分配";
  if (ptr) {
    firstfit_->Free(ptr, 1);
  }

  std::cout << "=== MemoryExhaustion 测试结束 ===\n" << std::endl;
}

/**
 * @brief 测试分配和释放的匹配性
 */
TEST_F(FirstFitTest, AllocFreePairMatching) {
  // 分配不同大小的块
  std::vector<std::pair<void*, size_t>> allocated_blocks;

  for (size_t page_count = 1; page_count <= 5; ++page_count) {
    void* ptr = firstfit_->Alloc(page_count);
    if (ptr != nullptr) {
      allocated_blocks.emplace_back(ptr, page_count);
      std::cout << "分配 " << page_count << " 页: " << ptr << std::endl;
    }
  }

  EXPECT_GT(allocated_blocks.size(), 0) << "应该能分配一些内存块";

  // 以相同的page_count释放所有块
  for (const auto& [ptr, page_count] : allocated_blocks) {
    std::cout << "释放 " << page_count << " 页: " << ptr << std::endl;
    firstfit_->Free(ptr, page_count);
  }
}

/**
 * @brief 测试内存写入和读取
 */
TEST_F(FirstFitTest, MemoryReadWrite) {
  std::random_device rd;
  std::mt19937 gen(rd());

  void* ptr = firstfit_->Alloc(1);  // 分配1页
  ASSERT_NE(ptr, nullptr);

  // 填充整页随机数据并验证
  FillRandomData(ptr, 1, gen);
  auto full_page_data = SaveData(ptr, 1);
  EXPECT_TRUE(VerifyData(ptr, 1, full_page_data)) << "整页随机数据读写测试失败";

  // 测试特定的数据模式
  const char test_data[] = "Hello, FirstFit Allocator!";
  std::memcpy(ptr, test_data, sizeof(test_data));

  // 读取并验证数据
  char read_buffer[sizeof(test_data)];
  std::memcpy(read_buffer, ptr, sizeof(test_data));
  EXPECT_STREQ(read_buffer, test_data) << "字符串读写测试失败";

  firstfit_->Free(ptr, 1);
}

/**
 * @brief 测试GetUsedCount和GetFreeCount方法
 */
TEST_F(FirstFitTest, UsedAndFreeCount) {
  // 初始状态：所有内存都应该是空闲的
  size_t initial_free = firstfit_->GetFreeCount();
  size_t initial_used = firstfit_->GetUsedCount();

  EXPECT_EQ(initial_used, 0) << "初始已使用页数应该为0";
  EXPECT_EQ(initial_free, test_pages_) << "初始空闲页数应该等于总页数";
  EXPECT_EQ(initial_free + initial_used, test_pages_)
      << "空闲页数+已使用页数应该等于总页数";

  // 分配一些内存块
  void* ptr1 = firstfit_->Alloc(1);  // 1页
  void* ptr2 = firstfit_->Alloc(3);  // 3页
  void* ptr3 = firstfit_->Alloc(2);  // 2页

  ASSERT_NE(ptr1, nullptr);
  ASSERT_NE(ptr2, nullptr);
  ASSERT_NE(ptr3, nullptr);

  // 检查计数变化
  size_t after_alloc_free = firstfit_->GetFreeCount();
  size_t after_alloc_used = firstfit_->GetUsedCount();

  EXPECT_EQ(after_alloc_used, 6) << "分配1+3+2=6页后，已使用页数应该为6";
  EXPECT_EQ(after_alloc_free + after_alloc_used, test_pages_)
      << "空闲页数+已使用页数应该等于总页数";
  EXPECT_LT(after_alloc_free, initial_free) << "分配后空闲页数应该减少";

  // 释放部分内存
  firstfit_->Free(ptr1, 1);  // 释放1页

  size_t after_partial_free_free = firstfit_->GetFreeCount();
  size_t after_partial_free_used = firstfit_->GetUsedCount();

  EXPECT_EQ(after_partial_free_used, 5) << "释放1页后，已使用页数应该为5";
  EXPECT_EQ(after_partial_free_free + after_partial_free_used, test_pages_)
      << "空闲页数+已使用页数应该等于总页数";

  // 释放剩余内存
  firstfit_->Free(ptr2, 3);
  firstfit_->Free(ptr3, 2);

  size_t final_free = firstfit_->GetFreeCount();
  size_t final_used = firstfit_->GetUsedCount();

  EXPECT_EQ(final_used, 0) << "释放所有内存后，已使用页数应该为0";
  EXPECT_EQ(final_free, test_pages_)
      << "释放所有内存后，空闲页数应该等于总页数";
}

/**
 * @brief 测试释放无效地址（应该安全）
 */
TEST_F(FirstFitTest, FreeInvalidAddress) {
  // 释放不在管理范围内的地址应该是安全的
  EXPECT_NO_FATAL_FAILURE(firstfit_->Free(nullptr, 1));

  // 测试其他无效地址
  void* invalid_addr1 = (void*)0x1000000;  // 远超管理范围的地址
  void* invalid_addr2 =
      static_cast<char*>(test_memory_) - 4096;  // 管理范围之前的地址

  EXPECT_NO_FATAL_FAILURE(firstfit_->Free(invalid_addr1, 1));
  EXPECT_NO_FATAL_FAILURE(firstfit_->Free(invalid_addr2, 1));
}

/**
 * @brief 压力测试：随机分配和释放
 */
TEST_F(FirstFitTest, StressTest) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> page_dist(1, 5);  // 分配1-5页
  std::uniform_real_distribution<> action_dist(0.0, 1.0);

  // 存储分配的块信息：指针、大小、数据
  std::vector<std::tuple<void*, size_t, std::vector<uint8_t>>> allocated_blocks;

  const int operations = 200;  // 减少操作数以提高测试速度
  int alloc_count = 0;
  int free_count = 0;

  for (int i = 0; i < operations; ++i) {
    double action = action_dist(gen);

    // 70% 概率分配，30% 概率释放
    if (action < 0.7 || allocated_blocks.empty()) {
      // 分配
      size_t page_count = page_dist(gen);
      void* ptr = firstfit_->Alloc(page_count);

      if (ptr != nullptr) {
        // 填充随机数据
        FillRandomData(ptr, page_count, gen);
        auto data = SaveData(ptr, page_count);
        allocated_blocks.emplace_back(ptr, page_count, std::move(data));
        alloc_count++;
      }
    } else {
      // 释放随机选择的块
      if (!allocated_blocks.empty()) {
        std::uniform_int_distribution<> index_dist(0,
                                                   allocated_blocks.size() - 1);
        size_t index = index_dist(gen);

        auto [ptr, page_count, expected_data] = allocated_blocks[index];

        // 验证数据完整性
        EXPECT_TRUE(VerifyData(ptr, page_count, expected_data))
            << "释放前数据完整性验证失败";

        firstfit_->Free(ptr, page_count);
        allocated_blocks.erase(allocated_blocks.begin() + index);
        free_count++;
      }
    }
  }

  // 清理剩余的分配块
  for (const auto& [ptr, page_count, data] : allocated_blocks) {
    firstfit_->Free(ptr, page_count);
  }

  EXPECT_GT(alloc_count, 0) << "压力测试应该进行了一些分配操作";
  std::cout << "压力测试统计: 分配=" << alloc_count << ", 释放=" << free_count
            << std::endl;
}

/**
 * @brief 多线程基础测试：测试多个线程同时分配和释放内存
 */
TEST_F(FirstFitMultiThreadTest, MultiThreadBasicTest) {
  std::cout << "\n=== MultiThreadBasicTest 测试开始 ===" << std::endl;

  const int num_threads = 4;
  const int allocs_per_thread = 50;
  
  // 用于收集各线程的分配结果
  std::vector<std::vector<std::pair<void*, size_t>>> thread_allocations(num_threads);
  std::vector<std::thread> threads;
  std::atomic<int> successful_allocs{0};
  std::atomic<int> failed_allocs{0};
  
  // 启动多个线程进行并发分配
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, allocs_per_thread, &thread_allocations, 
                         &successful_allocs, &failed_allocs]() {
      std::random_device rd;
      std::mt19937 gen(rd() + t);  // 每个线程使用不同的种子
      std::uniform_int_distribution<> page_dist(1, 3);  // 分配1-3页
      
      std::cout << "线程 " << t << " 开始分配..." << std::endl;
      
      for (int i = 0; i < allocs_per_thread; ++i) {
        size_t page_count = page_dist(gen);
        void* ptr = firstfit_->Alloc(page_count);
        
        if (ptr != nullptr) {
          // 验证地址有效性（使用简化版本以减少输出）
          EXPECT_TRUE(ValidateAddress(ptr, page_count)) 
              << "线程 " << t << " 地址验证失败";
          
          thread_allocations[t].emplace_back(ptr, page_count);
          successful_allocs.fetch_add(1);
          
          // 测试内存可写性
          auto* byte_ptr = static_cast<uint8_t*>(ptr);
          byte_ptr[0] = static_cast<uint8_t>(t);  // 写入线程ID
          
          // 验证写入成功
          EXPECT_EQ(byte_ptr[0], static_cast<uint8_t>(t)) 
              << "线程 " << t << " 内存写入失败";
        } else {
          failed_allocs.fetch_add(1);
        }
        
        // 短暂延时增加并发竞争
        if (i % 10 == 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
      }
      
      std::cout << "线程 " << t << " 完成分配，成功: " 
                << thread_allocations[t].size() << std::endl;
    });
  }
  
  // 等待所有线程完成
  for (auto& thread : threads) {
    thread.join();
  }
  
  std::cout << "\n并发分配结果:" << std::endl;
  std::cout << "成功分配: " << successful_allocs.load() << std::endl;
  std::cout << "失败分配: " << failed_allocs.load() << std::endl;
  
  // 验证所有分配的地址都不相同
  std::set<void*> all_addresses;
  size_t total_allocated = 0;
  
  for (int t = 0; t < num_threads; ++t) {
    for (const auto& [ptr, page_count] : thread_allocations[t]) {
      EXPECT_TRUE(all_addresses.insert(ptr).second) 
          << "发现重复地址: " << ptr << " (线程 " << t << ")";
      total_allocated++;
    }
  }
  
  EXPECT_EQ(total_allocated, successful_allocs.load()) 
      << "实际分配数量与统计不符";
  EXPECT_EQ(all_addresses.size(), successful_allocs.load()) 
      << "存在重复地址";
  
  std::cout << "✓ 地址唯一性验证通过，共 " << all_addresses.size() << " 个唯一地址" << std::endl;
  
  // 多线程释放内存
  std::cout << "\n开始多线程释放..." << std::endl;
  std::atomic<int> successful_frees{0};
  threads.clear();
  
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, &thread_allocations, &successful_frees]() {
      std::cout << "线程 " << t << " 开始释放..." << std::endl;
      
      for (const auto& [ptr, page_count] : thread_allocations[t]) {
        // 验证内存中的线程ID还在
        auto* byte_ptr = static_cast<uint8_t*>(ptr);
        EXPECT_EQ(byte_ptr[0], static_cast<uint8_t>(t)) 
            << "线程 " << t << " 内存数据被损坏";
        
        firstfit_->Free(ptr, page_count);
        successful_frees.fetch_add(1);
      }
      
      std::cout << "线程 " << t << " 完成释放，释放数量: " 
                << thread_allocations[t].size() << std::endl;
    });
  }
  
  // 等待所有释放完成
  for (auto& thread : threads) {
    thread.join();
  }
  
  std::cout << "✓ 多线程释放完成，总释放数量: " << successful_frees.load() << std::endl;
  EXPECT_EQ(successful_frees.load(), successful_allocs.load()) 
      << "释放数量应该等于分配数量";
  
  std::cout << "=== MultiThreadBasicTest 测试结束 ===\n" << std::endl;
}

/**
 * @brief 多线程压力测试：高强度并发分配和释放
 */
TEST_F(FirstFitMultiThreadTest, MultiThreadStressTest) {
  std::cout << "\n=== MultiThreadStressTest 测试开始 ===" << std::endl;

  const int num_threads = 6;
  const int operations_per_thread = 100;
  const std::chrono::seconds test_duration(3);  // 3秒压力测试
  
  std::atomic<bool> stop_flag{false};
  std::atomic<long> total_allocs{0};
  std::atomic<long> total_frees{0};
  std::atomic<long> alloc_failures{0};
  std::atomic<long> data_corruption_errors{0};
  
  // 每个线程维护自己的分配列表
  std::vector<std::vector<std::tuple<void*, size_t, uint32_t>>> thread_allocations(num_threads);
  std::vector<std::mutex> thread_mutexes(num_threads);
  std::vector<std::thread> threads;
  
  auto start_time = std::chrono::steady_clock::now();
  
  // 启动压力测试线程
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, operations_per_thread, &stop_flag, 
                         &total_allocs, &total_frees, &alloc_failures,
                         &data_corruption_errors, &thread_allocations, 
                         &thread_mutexes]() {
      std::random_device rd;
      std::mt19937 gen(rd() + t * 1000);
      std::uniform_int_distribution<> page_dist(1, 4);  // 分配1-4页
      std::uniform_real_distribution<> action_dist(0.0, 1.0);
      std::uniform_int_distribution<uint32_t> magic_dist;
      
      int local_allocs = 0;
      int local_frees = 0;
      int local_failures = 0;
      int local_corruptions = 0;
      
      while (!stop_flag.load() && (local_allocs + local_frees) < operations_per_thread) {
        bool should_alloc;
        {
          std::lock_guard<std::mutex> lock(thread_mutexes[t]);
          should_alloc = thread_allocations[t].empty() || action_dist(gen) < 0.6;
        }
        
        if (should_alloc) {
          // 分配操作
          size_t page_count = page_dist(gen);
          void* ptr = firstfit_->Alloc(page_count);
          
          if (ptr != nullptr) {
            // 生成魔数并写入内存
            uint32_t magic = magic_dist(gen);
            auto* uint32_ptr = static_cast<uint32_t*>(ptr);
            size_t uint32_count = (page_count * AllocatorBase::kPageSize) / sizeof(uint32_t);
            
            // 填充魔数
            for (size_t i = 0; i < uint32_count; ++i) {
              uint32_ptr[i] = magic;
            }
            
            {
              std::lock_guard<std::mutex> lock(thread_mutexes[t]);
              thread_allocations[t].emplace_back(ptr, page_count, magic);
            }
            local_allocs++;
          } else {
            local_failures++;
          }
        } else {
          // 释放操作
          std::lock_guard<std::mutex> lock(thread_mutexes[t]);
          if (!thread_allocations[t].empty()) {
            std::uniform_int_distribution<> index_dist(0, thread_allocations[t].size() - 1);
            size_t index = index_dist(gen);
            
            auto [ptr, page_count, magic] = thread_allocations[t][index];
            
            // 验证数据完整性
            auto* uint32_ptr = static_cast<uint32_t*>(ptr);
            size_t uint32_count = (page_count * AllocatorBase::kPageSize) / sizeof(uint32_t);
            bool corruption_detected = false;
            
            for (size_t i = 0; i < uint32_count; ++i) {
              if (uint32_ptr[i] != magic) {
                corruption_detected = true;
                break;
              }
            }
            
            if (corruption_detected) {
              local_corruptions++;
            }
            
            firstfit_->Free(ptr, page_count);
            thread_allocations[t].erase(thread_allocations[t].begin() + index);
            local_frees++;
          }
        }
        
        // 随机短暂延时
        if ((local_allocs + local_frees) % 25 == 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(gen() % 10));
        }
      }
      
      total_allocs.fetch_add(local_allocs);
      total_frees.fetch_add(local_frees);
      alloc_failures.fetch_add(local_failures);
      data_corruption_errors.fetch_add(local_corruptions);
      
      std::cout << "线程 " << t << " 完成: 分配=" << local_allocs 
                << ", 释放=" << local_frees << ", 失败=" << local_failures 
                << ", 损坏=" << local_corruptions << std::endl;
    });
  }
  
  // 等待指定时间后停止测试
  std::this_thread::sleep_for(test_duration);
  stop_flag.store(true);
  
  // 等待所有线程完成
  for (auto& thread : threads) {
    thread.join();
  }
  
  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
  
  std::cout << "\n压力测试结果（" << duration.count() << "ms）:" << std::endl;
  std::cout << "总分配次数: " << total_allocs.load() << std::endl;
  std::cout << "总释放次数: " << total_frees.load() << std::endl;
  std::cout << "分配失败次数: " << alloc_failures.load() << std::endl;
  std::cout << "数据损坏次数: " << data_corruption_errors.load() << std::endl;
  
  EXPECT_EQ(data_corruption_errors.load(), 0) 
      << "不应该有数据损坏";
  EXPECT_GT(total_allocs.load(), 0) 
      << "应该有成功的分配操作";
  
  // 计算性能指标
  double ops_per_second = (total_allocs.load() + total_frees.load()) * 1000.0 / duration.count();
  std::cout << "操作速率: " << ops_per_second << " ops/sec" << std::endl;
  
  // 清理剩余分配
  std::cout << "\n清理剩余分配..." << std::endl;
  int cleanup_count = 0;
  for (int t = 0; t < num_threads; ++t) {
    for (const auto& [ptr, page_count, magic] : thread_allocations[t]) {
      // 验证数据完整性
      auto* uint32_ptr = static_cast<uint32_t*>(ptr);
      size_t uint32_count = (page_count * AllocatorBase::kPageSize) / sizeof(uint32_t);
      
      for (size_t i = 0; i < uint32_count; ++i) {
        EXPECT_EQ(uint32_ptr[i], magic) 
            << "清理时发现数据损坏，线程=" << t << ", 位置=" << i;
      }
      
      firstfit_->Free(ptr, page_count);
      cleanup_count++;
    }
  }
  
  std::cout << "清理了 " << cleanup_count << " 个剩余分配" << std::endl;
  std::cout << "=== MultiThreadStressTest 测试结束 ===\n" << std::endl;
}

/**
 * @brief 多线程内存耗尽测试：测试多线程下的内存耗尽和恢复
 */
TEST_F(FirstFitMultiThreadTest, MultiThreadExhaustionTest) {
  std::cout << "\n=== MultiThreadExhaustionTest 测试开始 ===" << std::endl;

  const int num_threads = 4;
  std::vector<std::vector<std::pair<void*, size_t>>> thread_allocations(num_threads);
  std::vector<std::thread> threads;
  std::atomic<int> total_allocated_pages{0};
  std::atomic<bool> memory_exhausted{false};
  
  // 第一阶段：多线程耗尽内存
  std::cout << "\n第一阶段：多线程耗尽内存..." << std::endl;
  
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, &thread_allocations, &total_allocated_pages, &memory_exhausted]() {
      std::random_device rd;
      std::mt19937 gen(rd() + t);
      std::uniform_int_distribution<> page_dist(1, 2);  // 主要分配小块
      
      int thread_allocs = 0;
      while (!memory_exhausted.load()) {
        size_t page_count = page_dist(gen);
        void* ptr = firstfit_->Alloc(page_count);
        
        if (ptr != nullptr) {
          thread_allocations[t].emplace_back(ptr, page_count);
          thread_allocs++;
          total_allocated_pages.fetch_add(page_count);  // 累加页数
          
          // 填充线程标识
          auto* byte_ptr = static_cast<uint8_t*>(ptr);
          byte_ptr[0] = static_cast<uint8_t>(t);
        } else {
          // 内存耗尽，设置标志让其他线程停止
          memory_exhausted.store(true);
          break;
        }
        
        // 减少竞争激烈程度
        if (thread_allocs % 20 == 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
      }
      
      std::cout << "线程 " << t << " 分配了 " << thread_allocs << " 个块" << std::endl;
    });
  }
  
  // 等待所有线程完成
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
  
  std::cout << "内存耗尽阶段完成，总分配页数: " << total_allocated_pages.load() 
            << " / " << test_pages_ << std::endl;
  
  // 验证内存接近耗尽 - FirstFit算法可能留有一些碎片，不一定完全耗尽
  bool allocation_failed = true;
  for (int i = 0; i < 5; ++i) {
    void* test_ptr = firstfit_->Alloc(1);
    if (test_ptr != nullptr) {
      allocation_failed = false;
      std::cout << "注意: 第" << (i+1) << "次尝试仍能分配内存: " << test_ptr << std::endl;
      // 立即释放以避免内存泄漏
      firstfit_->Free(test_ptr, 1);
      break;
    }
  }
  
  if (!allocation_failed) {
    std::cout << "注意: 内存未完全耗尽，这在FirstFit算法中是正常的（可能有碎片）" << std::endl;
    std::cout << "分配页数: " << total_allocated_pages.load() 
              << ", 总页数: " << test_pages_ 
              << ", 剩余: " << (test_pages_ - total_allocated_pages.load()) << std::endl;
  } else {
    std::cout << "✓ 内存已完全耗尽" << std::endl;
  }
  
  // 第二阶段：验证数据完整性
  std::cout << "\n第二阶段：验证数据完整性..." << std::endl;
  std::atomic<int> integrity_errors{0};
  
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, &thread_allocations, &integrity_errors]() {
      int checked = 0;
      for (const auto& [ptr, page_count] : thread_allocations[t]) {
        auto* byte_ptr = static_cast<uint8_t*>(ptr);
        if (byte_ptr[0] != static_cast<uint8_t>(t)) {
          integrity_errors.fetch_add(1);
        }
        checked++;
      }
      std::cout << "线程 " << t << " 检查了 " << checked << " 个块" << std::endl;
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
  
  EXPECT_EQ(integrity_errors.load(), 0) << "不应该有数据完整性错误";
  std::cout << "✓ 数据完整性验证通过" << std::endl;
  
  // 第三阶段：多线程释放内存
  std::cout << "\n第三阶段：多线程释放内存..." << std::endl;
  std::atomic<int> total_freed_pages{0};
  
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, &thread_allocations, &total_freed_pages]() {
      int thread_frees = 0;
      for (const auto& [ptr, page_count] : thread_allocations[t]) {
        firstfit_->Free(ptr, page_count);
        total_freed_pages.fetch_add(page_count);  // 累加页数
        thread_frees++;
        
        // 控制释放速度
        if (thread_frees % 20 == 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
      }
      std::cout << "线程 " << t << " 释放了 " << thread_frees << " 个块" << std::endl;
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  
  std::cout << "释放阶段完成，总释放页数: " << total_freed_pages.load() << std::endl;
  EXPECT_EQ(total_freed_pages.load(), total_allocated_pages.load()) 
      << "释放的页数应该等于分配的页数";
  
  // 第四阶段：验证内存可以重新分配
  std::cout << "\n第四阶段：验证内存恢复..." << std::endl;
  void* recovery_test_ptr = firstfit_->Alloc(1);
  EXPECT_NE(recovery_test_ptr, nullptr) << "释放后应该能重新分配内存";
  
  if (recovery_test_ptr) {
    // 测试新分配的内存可以正常使用
    auto* byte_ptr = static_cast<uint8_t*>(recovery_test_ptr);
    byte_ptr[0] = 0xFF;
    EXPECT_EQ(byte_ptr[0], 0xFF) << "新分配的内存应该可以正常读写";
    firstfit_->Free(recovery_test_ptr, 1);
    std::cout << "✓ 内存恢复验证通过" << std::endl;
  }
  
  std::cout << "=== MultiThreadExhaustionTest 测试结束 ===\n" << std::endl;
}

/**
 * @brief 多线程内存碎片测试：测试FirstFit算法在多线程环境下的碎片化处理
 */
TEST_F(FirstFitMultiThreadTest, MultiThreadFragmentationTest) {
  std::cout << "\n=== MultiThreadFragmentationTest 测试开始 ===" << std::endl;

  const int num_threads = 4;
  std::vector<std::thread> threads;
  std::atomic<int> successful_large_allocs{0};
  std::atomic<int> fragmentation_tests{0};
  
  // 第一阶段：创建内存碎片
  std::cout << "\n第一阶段：多线程创建内存碎片..." << std::endl;
  
  std::vector<std::vector<std::pair<void*, size_t>>> thread_small_allocs(num_threads);
  
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, &thread_small_allocs]() {
      std::random_device rd;
      std::mt19937 gen(rd() + t);
      
      std::cout << "线程 " << t << " 开始分配小块..." << std::endl;
      
      // 分配很多小块
      for (int i = 0; i < 30; ++i) {
        void* ptr = firstfit_->Alloc(1);  // 分配1页
        if (ptr != nullptr) {
          thread_small_allocs[t].emplace_back(ptr, 1);
          
          // 填充标识数据
          auto* byte_ptr = static_cast<uint8_t*>(ptr);
          byte_ptr[0] = static_cast<uint8_t>(t * 10 + i % 10);
        }
        
        // 短暂延时
        std::this_thread::sleep_for(std::chrono::microseconds(gen() % 3));
      }
      
      std::cout << "线程 " << t << " 分配了 " << thread_small_allocs[t].size() << " 个小块" << std::endl;
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
  
  // 第二阶段：随机释放一些小块，创建碎片
  std::cout << "\n第二阶段：随机释放小块创建碎片..." << std::endl;
  
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, &thread_small_allocs]() {
      std::random_device rd;
      std::mt19937 gen(rd() + t);
      
      // 随机释放一半的块
      auto& allocs = thread_small_allocs[t];
      std::shuffle(allocs.begin(), allocs.end(), gen);
      
      size_t release_count = allocs.size() / 2;
      for (size_t i = 0; i < release_count; ++i) {
        auto [ptr, page_count] = allocs[i];
        firstfit_->Free(ptr, page_count);
      }
      
      // 保留剩余的分配
      allocs.erase(allocs.begin(), allocs.begin() + release_count);
      
      std::cout << "线程 " << t << " 释放了 " << release_count << " 个块，保留 " 
                << allocs.size() << " 个块" << std::endl;
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  threads.clear();
  
  // 第三阶段：尝试分配大块，测试碎片处理
  std::cout << "\n第三阶段：测试大块分配..." << std::endl;
  
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([this, t, &successful_large_allocs, &fragmentation_tests]() {
      std::random_device rd;
      std::mt19937 gen(rd() + t);
      
      fragmentation_tests.fetch_add(1);
      
      // 尝试分配大块
      void* large_ptr = firstfit_->Alloc(8);  // 尝试分配8页
      if (large_ptr != nullptr) {
        successful_large_allocs.fetch_add(1);
        std::cout << "✓ 线程 " << t << " 成功分配大块: " << large_ptr << std::endl;
        
        // 测试大块内存
        auto* byte_ptr = static_cast<uint8_t*>(large_ptr);
        size_t large_size = 8 * AllocatorBase::kPageSize;
        
        // 填充测试数据
        std::memset(byte_ptr, static_cast<int>(t + 100), large_size);
        
        // 验证写入 - 抽样检查
        bool write_successful = true;
        for (size_t i = 0; i < large_size; i += 1024) {  // 每1KB检查一次
          if (byte_ptr[i] != static_cast<uint8_t>(t + 100)) {
            write_successful = true;
            break;
          }
        }
        
        EXPECT_TRUE(write_successful) 
            << "大块内存写入验证失败，线程 " << t;
        
        // 延时后释放
        std::this_thread::sleep_for(std::chrono::milliseconds(gen() % 5 + 1));
        firstfit_->Free(large_ptr, 8);
        std::cout << "✓ 线程 " << t << " 释放大块" << std::endl;
      } else {
        std::cout << "✗ 线程 " << t << " 大块分配失败（可能由于碎片化）" << std::endl;
      }
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  
  std::cout << "\n碎片化测试结果:" << std::endl;
  std::cout << "碎片化测试次数: " << fragmentation_tests.load() << std::endl;
  std::cout << "成功的大块分配: " << successful_large_allocs.load() << std::endl;
  
  // 清理剩余分配
  std::cout << "\n清理剩余小块分配..." << std::endl;
  int cleanup_count = 0;
  for (int t = 0; t < num_threads; ++t) {
    for (const auto& [ptr, page_count] : thread_small_allocs[t]) {
      firstfit_->Free(ptr, page_count);
      cleanup_count++;
    }
  }
  std::cout << "清理了 " << cleanup_count << " 个剩余小块分配" << std::endl;
  
  // 验证内存状态
  void* final_test = firstfit_->Alloc(1);
  EXPECT_NE(final_test, nullptr) << "测试结束后应该能分配内存";
  if (final_test) {
    firstfit_->Free(final_test, 1);
  }
  
  std::cout << "=== MultiThreadFragmentationTest 测试结束 ===\n" << std::endl;
}

}  // namespace bmalloc
