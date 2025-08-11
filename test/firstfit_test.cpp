/**
 * Copyright The bmalloc Contributors
 * @file firstfit_test.cpp
 * @brief FirstFit分配器的Google Test测试用例
 */

#include "first_fit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

namespace bmalloc {

/**
 * @brief 辅助函数：打印 FirstFit 分配器的当前状态
 * 由于 FirstFit 的成员现在是 protected，我们创建一个继承类来访问内部状态
 */
class FirstFitDebugHelper : public FirstFit {
 public:
  FirstFitDebugHelper(const char* name, void* start_addr, size_t page_count)
      : FirstFit(name, start_addr, page_count) {}

  void print() const {
    printf("\n==========================================\n");
    printf("FirstFit 分配器状态详情\n");
    printf("管理页数: %zu, 已使用: %zu, 空闲: %zu\n", 
           length_, used_count_, free_count_);

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
  bool IsPageAllocated(size_t page_index) const {
    return bitmap_[page_index];
  }

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
    firstfit_ = std::make_unique<FirstFitDebugHelper>("test_firstfit", test_memory_,
                                                      test_pages_);
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
    std::cout << "  总大小: " << (page_count * AllocatorBase::kPageSize) << " 字节" << std::endl;

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
      std::cout << "\n内存耗尽，共分配了 " << allocated_blocks.size() << " 页" << std::endl;
      break;
    }

    // 填充随机数据
    FillRandomData(ptr, 1, gen);
    auto data = SaveData(ptr, 1);
    allocated_blocks.emplace_back(ptr, std::move(data));

    if ((i + 1) <= 10 || (i + 1) % 50 == 0) {
      auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
      size_t page_index = offset / AllocatorBase::kPageSize;
      std::cout << "第" << (i + 1) << "个分配: " << ptr 
                << " (页" << page_index << ")" << std::endl;
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
  std::cout << "验证了 " << verified_count << " 个内存块的数据完整性" << std::endl;

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
 * @brief 测试FirstFit特有的功能：指定地址分配
 */
TEST_F(FirstFitTest, FixedAddressAllocation) {
  std::cout << "\n=== FixedAddressAllocation 测试开始 ===" << std::endl;
  
  // 计算特定页的地址
  size_t target_page = 10;
  void* target_addr = static_cast<char*>(test_memory_) + target_page * AllocatorBase::kPageSize;
  
  std::cout << "尝试在指定地址分配内存..." << std::endl;
  std::cout << "目标地址: " << target_addr << " (页" << target_page << ")" << std::endl;
  
  // 在指定地址分配2页
  bool success = firstfit_->Alloc(target_addr, 2);
  EXPECT_TRUE(success) << "指定地址分配应该成功";
  
  if (success) {
    std::cout << "✓ 指定地址分配成功" << std::endl;
    firstfit_->print();
    
    // 验证页面确实被标记为已分配
    EXPECT_TRUE(firstfit_->IsPageAllocated(target_page)) << "目标页应该被标记为已分配";
    EXPECT_TRUE(firstfit_->IsPageAllocated(target_page + 1)) << "目标页+1应该被标记为已分配";
    
    // 尝试在已分配的地址再次分配（应该失败）
    bool should_fail = firstfit_->Alloc(target_addr, 1);
    EXPECT_FALSE(should_fail) << "在已分配地址再次分配应该失败";
    
    // 释放内存
    firstfit_->Free(target_addr, 2);
    EXPECT_FALSE(firstfit_->IsPageAllocated(target_page)) << "释放后页面应该标记为空闲";
    EXPECT_FALSE(firstfit_->IsPageAllocated(target_page + 1)) << "释放后页面应该标记为空闲";
  }
  
  std::cout << "=== FixedAddressAllocation 测试结束 ===\n" << std::endl;
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
  void* invalid_addr2 = static_cast<char*>(test_memory_) - 4096;  // 管理范围之前的地址

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
        std::uniform_int_distribution<> index_dist(0, allocated_blocks.size() - 1);
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
  std::cout << "压力测试统计: 分配=" << alloc_count << ", 释放=" << free_count << std::endl;
}

}  // namespace bmalloc
