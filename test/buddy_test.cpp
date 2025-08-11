/**
 * Copyright The bmalloc Contributors
 * @file buddy_test.cpp
 * @brief Buddy分配器的Google Test测试用例
 */

#include "buddy.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

namespace bmalloc {

/**
 * @brief 辅助函数：打印 Buddy 分配器的当前状态
 * 由于 Buddy 的成员现在是 protected，我们创建一个继承类来访问内部状态
 */
class BuddyDebugHelper : public Buddy {
 public:
  BuddyDebugHelper(const char* name, void* start_addr, size_t total_pages)
      : Buddy(name, start_addr, total_pages) {}

  void print() const {
    printf("\n==========================================\n");
    printf("Buddy 分配器状态详情\n");

    printf("当前空闲块链表状态:\n");
    for (size_t i = 0; i < length_; i++) {
      auto size = static_cast<size_t>(1 << i);
      printf("entry[%zu](管理%zu页块) -> ", i, size);
      FreeBlockNode* curr = free_block_lists_[i];

      bool has_blocks = false;
      while (curr != nullptr) {
        auto first = static_cast<size_t>(
            (static_cast<const char*>(static_cast<void*>(curr)) -
             static_cast<const char*>(start_addr_)) /
            kPageSize);
        printf("块[页%zu~%zu] -> ", first, first + size - 1);
        curr = curr->next;
        has_blocks = true;
      }
      printf("NULL");
      if (!has_blocks) {
        printf("  (此大小无空闲块)");
      }
      printf("\n");
    }
    printf("==========================================\n");
  }
};

/**
 * @brief Buddy分配器测试夹具类
 * 提供测试环境的初始化和清理
 */
class BuddyTest : public ::testing::Test {
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

    // 创建buddy分配器实例（使用带调试功能的版本）
    buddy_ = std::make_unique<BuddyDebugHelper>("test_buddy", test_memory_,
                                                test_pages_);
  }

  void TearDown() override {
    buddy_.reset();
    if (test_memory_) {
      std::free(test_memory_);
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

  // 辅助函数：检查地址是否按指定order对齐
  bool IsAligned(void* ptr, size_t order) const {
    if (!ptr) return false;
    auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
    size_t pages = 1 << order;
    return (offset / AllocatorBase::kPageSize) % pages == 0;
  }

  // 辅助函数：全面检查分配地址的有效性
  bool ValidateAllocatedAddress(void* ptr, size_t order,
                                const char* test_name = "") const {
    std::cout << "\n--- 地址有效性检查";
    if (strlen(test_name) > 0) {
      std::cout << " (" << test_name << ")";
    }
    std::cout << " ---" << std::endl;

    // 检查1: 非空指针
    EXPECT_NE(ptr, nullptr) << "指针不应该为空";
    if (ptr == nullptr) {
      std::cout << "✗ 检查失败: 指针为空" << std::endl;
      return false;
    }
    std::cout << "✓ 指针非空: " << ptr << std::endl;

    // 检查2: 在管理范围内
    EXPECT_TRUE(IsInManagedRange(ptr)) << "地址应该在管理范围内";
    if (!IsInManagedRange(ptr)) {
      std::cout << "✗ 检查失败: 地址不在管理范围内" << std::endl;
      std::cout << "  地址: " << ptr << std::endl;
      std::cout << "  管理范围: " << test_memory_ << " - "
                << (static_cast<char*>(test_memory_) + test_memory_size_)
                << std::endl;
      return false;
    }
    std::cout << "✓ 地址在管理范围内" << std::endl;

    // 检查3: 按页对齐
    auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
    EXPECT_EQ(offset % AllocatorBase::kPageSize, 0) << "地址应该按页边界对齐";
    if (offset % AllocatorBase::kPageSize != 0) {
      std::cout << "✗ 检查失败: 地址未按页边界对齐" << std::endl;
      std::cout << "  偏移: " << offset
                << " 字节, 页大小: " << AllocatorBase::kPageSize << std::endl;
      return false;
    }
    std::cout << "✓ 地址按页边界对齐" << std::endl;

    // 检查4: 按order对齐
    EXPECT_TRUE(IsAligned(ptr, order)) << "地址应该按order对齐";
    if (!IsAligned(ptr, order)) {
      std::cout << "✗ 检查失败: 地址未按order对齐" << std::endl;
      size_t pages = 1 << order;
      size_t page_num = offset / AllocatorBase::kPageSize;
      std::cout << "  页号: " << page_num << ", 需要按 " << pages << " 页对齐"
                << std::endl;
      return false;
    }
    std::cout << "✓ 地址按order对齐" << std::endl;

    // 检查5: 计算并显示详细信息
    size_t page_num = offset / AllocatorBase::kPageSize;
    size_t pages = 1 << order;
    size_t end_page = page_num + pages - 1;

    std::cout << "地址详细信息:" << std::endl;
    std::cout << "  地址: " << ptr << std::endl;
    std::cout << "  相对偏移: " << offset << " 字节" << std::endl;
    std::cout << "  页号范围: " << page_num;
    if (pages > 1) {
      std::cout << " - " << end_page;
    }
    std::cout << " (共 " << pages << " 页)" << std::endl;
    std::cout << "  总大小: " << (pages * AllocatorBase::kPageSize) << " 字节"
              << std::endl;

    // 检查6: 边界检查
    char* end_addr =
        static_cast<char*>(ptr) + (pages * AllocatorBase::kPageSize) - 1;
    char* memory_end = static_cast<char*>(test_memory_) + test_memory_size_ - 1;
    EXPECT_LE(end_addr, memory_end) << "分配的内存块不应该超出管理范围";
    if (end_addr > memory_end) {
      std::cout << "✗ 检查失败: 分配的内存块超出管理范围" << std::endl;
      std::cout << "  块结束地址: " << static_cast<void*>(end_addr)
                << std::endl;
      std::cout << "  管理结束地址: " << static_cast<void*>(memory_end)
                << std::endl;
      return false;
    }
    std::cout << "✓ 内存块在管理范围内" << std::endl;

    // 检查7: 地址可写性测试
    try {
      volatile char* test_ptr = static_cast<char*>(ptr);
      char original = *test_ptr;  // 读取原始值
      *test_ptr = 0xAA;           // 写入测试值
      EXPECT_EQ(*test_ptr, 0xAA) << "地址应该可写";
      if (*test_ptr != 0xAA) {
        std::cout << "✗ 检查失败: 地址不可写" << std::endl;
        return false;
      } else {
        *test_ptr = original;  // 恢复原始值
        std::cout << "✓ 地址可读写" << std::endl;
      }
    } catch (...) {
      ADD_FAILURE() << "访问地址时发生异常";
      std::cout << "✗ 检查失败: 访问地址时发生异常" << std::endl;
      return false;
    }

    // 检查8: 页号范围有效性
    EXPECT_LT(page_num, test_pages_) << "起始页号应该在有效范围内";
    EXPECT_LT(end_page, test_pages_) << "结束页号应该在有效范围内";

    // 检查9: order合理性
    EXPECT_GE(order, 0) << "order应该大于等于0";
    EXPECT_LT(order, 32) << "order应该小于32（避免溢出）";

    // 检查10: 页对齐验证
    size_t page_alignment = 1 << order;
    EXPECT_EQ(page_num % page_alignment, 0)
        << "页号应该按 " << page_alignment << " 页对齐";

    std::cout << "--- 检查结果: 全部通过 ---" << std::endl;
    return true;
  }

  // 辅助函数：比较两个地址的相对位置
  void CompareAddresses(void* ptr1, void* ptr2, const char* name1 = "ptr1",
                        const char* name2 = "ptr2") const {
    EXPECT_NE(ptr1, nullptr) << name1 << " 不应该为空";
    EXPECT_NE(ptr2, nullptr) << name2 << " 不应该为空";

    if (!ptr1 || !ptr2) return;

    auto addr1 = static_cast<char*>(ptr1);
    auto addr2 = static_cast<char*>(ptr2);
    auto offset1 = addr1 - static_cast<char*>(test_memory_);
    auto offset2 = addr2 - static_cast<char*>(test_memory_);

    // 验证两个地址都在管理范围内
    EXPECT_TRUE(IsInManagedRange(ptr1)) << name1 << " 应该在管理范围内";
    EXPECT_TRUE(IsInManagedRange(ptr2)) << name2 << " 应该在管理范围内";

    // 验证地址不相同（不重叠）
    EXPECT_NE(ptr1, ptr2) << "两个地址不应该相同";

    std::cout << "\n地址比较:" << std::endl;
    std::cout << "  " << name1 << ": " << ptr1 << " (偏移 " << offset1 << ")"
              << std::endl;
    std::cout << "  " << name2 << ": " << ptr2 << " (偏移 " << offset2 << ")"
              << std::endl;

    if (addr1 == addr2) {
      std::cout << "  关系: 地址相同 (重叠!)" << std::endl;
      ADD_FAILURE() << "地址重叠: " << name1 << " 和 " << name2;
    } else if (addr1 < addr2) {
      size_t distance = addr2 - addr1;
      std::cout << "  关系: " << name1 << " 在前, 间距 " << distance << " 字节"
                << std::endl;
      EXPECT_GT(distance, 0) << "地址间距应该大于0";
    } else {
      size_t distance = addr1 - addr2;
      std::cout << "  关系: " << name2 << " 在前, 间距 " << distance << " 字节"
                << std::endl;
      EXPECT_GT(distance, 0) << "地址间距应该大于0";
    }
  }

  // 辅助函数：验证两个内存块不重叠
  void ValidateNoOverlap(void* ptr1, size_t order1, void* ptr2, size_t order2,
                         const char* name1 = "block1",
                         const char* name2 = "block2") const {
    EXPECT_NE(ptr1, nullptr) << name1 << " 不应该为空";
    EXPECT_NE(ptr2, nullptr) << name2 << " 不应该为空";

    if (!ptr1 || !ptr2) return;

    auto addr1 = static_cast<char*>(ptr1);
    auto addr2 = static_cast<char*>(ptr2);
    size_t size1 = (1 << order1) * AllocatorBase::kPageSize;
    size_t size2 = (1 << order2) * AllocatorBase::kPageSize;

    // 检查地址不相同
    EXPECT_NE(ptr1, ptr2) << name1 << " 和 " << name2 << " 的地址不应该相同";

    // 检查内存块不重叠
    bool no_overlap = (addr1 + size1 <= addr2) || (addr2 + size2 <= addr1);
    EXPECT_TRUE(no_overlap) << name1 << " 和 " << name2 << " 的内存块不应该重叠"
                            << "\n  " << name1 << ": " << ptr1 << " - "
                            << static_cast<void*>(addr1 + size1 - 1)
                            << " (大小: " << size1 << " 字节)"
                            << "\n  " << name2 << ": " << ptr2 << " - "
                            << static_cast<void*>(addr2 + size2 - 1)
                            << " (大小: " << size2 << " 字节)";

    if (no_overlap) {
      size_t distance =
          (addr1 < addr2) ? (addr2 - addr1 - size1) : (addr1 - addr2 - size2);
      EXPECT_GE(distance, 0) << "内存块之间应该有非负的间距";
    }
  }

  // 辅助函数：用随机数据填充内存块
  void FillRandomData(void* ptr, size_t order, std::mt19937& gen) {
    if (!ptr) return;

    size_t pages = 1 << order;
    size_t total_size = pages * AllocatorBase::kPageSize;
    auto* data = static_cast<uint8_t*>(ptr);

    std::uniform_int_distribution<uint8_t> byte_dist(0, 255);
    for (size_t i = 0; i < total_size; ++i) {
      data[i] = byte_dist(gen);
    }
  }

  // 辅助函数：验证内存数据完整性
  bool VerifyData(void* ptr, size_t order,
                  const std::vector<uint8_t>& expected_data) {
    if (!ptr) return false;

    size_t pages = 1 << order;
    size_t total_size = pages * AllocatorBase::kPageSize;
    auto* data = static_cast<uint8_t*>(ptr);

    if (expected_data.size() != total_size) return false;

    for (size_t i = 0; i < total_size; ++i) {
      if (data[i] != expected_data[i]) {
        return false;
      }
    }
    return true;
  }

  // 辅助函数：保存内存数据用于后续验证
  std::vector<uint8_t> SaveData(void* ptr, size_t order) {
    std::vector<uint8_t> data;
    if (!ptr) return data;

    size_t pages = 1 << order;
    size_t total_size = pages * AllocatorBase::kPageSize;
    auto* mem_data = static_cast<uint8_t*>(ptr);

    data.resize(total_size);
    std::memcpy(data.data(), mem_data, total_size);
    return data;
  }

  std::unique_ptr<BuddyDebugHelper> buddy_;
  void* test_memory_ = nullptr;
  size_t test_memory_size_ = 0;
  size_t test_pages_ = 0;
};

/**
 * @brief 测试基本的分配和释放功能
 */
TEST_F(BuddyTest, BasicAllocAndFree) {
  std::cout << "\n=== BasicAllocAndFree 测试开始 ===" << std::endl;
  std::cout << "初始状态:" << std::endl;
  buddy_->print();

  std::random_device rd;
  std::mt19937 gen(rd());

  // 测试基本分配
  std::cout << "\n分配1页 (order=0)..." << std::endl;
  void* ptr1 = buddy_->Alloc(0);  // 分配1页 (2^0 = 1页)
  ASSERT_NE(ptr1, nullptr) << "分配1页失败";
  EXPECT_TRUE(ValidateAllocatedAddress(ptr1, 0, "1页分配"))
      << "1页地址验证失败";
  std::cout << "✓ 分配成功: ptr1 = " << ptr1 << " (1页)" << std::endl;

  // 填充随机数据并保存
  FillRandomData(ptr1, 0, gen);
  auto data1 = SaveData(ptr1, 0);
  std::cout << "已填充随机数据到1页内存" << std::endl;
  buddy_->print();

  std::cout << "\n分配2页 (order=1)..." << std::endl;
  void* ptr2 = buddy_->Alloc(1);  // 分配2页 (2^1 = 2页)
  ASSERT_NE(ptr2, nullptr) << "分配2页失败";
  EXPECT_TRUE(ValidateAllocatedAddress(ptr2, 1, "2页分配"))
      << "2页地址验证失败";
  std::cout << "✓ 分配成功: ptr2 = " << ptr2 << " (2页)" << std::endl;

  // 填充随机数据并保存
  FillRandomData(ptr2, 1, gen);
  auto data2 = SaveData(ptr2, 1);
  std::cout << "已填充随机数据到2页内存" << std::endl;
  buddy_->print();

  std::cout << "\n分配4页 (order=2)..." << std::endl;
  void* ptr3 = buddy_->Alloc(2);  // 分配4页 (2^2 = 4页)
  ASSERT_NE(ptr3, nullptr) << "分配4页失败";
  EXPECT_TRUE(ValidateAllocatedAddress(ptr3, 2, "4页分配"))
      << "4页地址验证失败";
  std::cout << "✓ 分配成功: ptr3 = " << ptr3 << " (4页)" << std::endl;

  // 填充随机数据并保存
  FillRandomData(ptr3, 2, gen);
  auto data3 = SaveData(ptr3, 2);
  std::cout << "已填充随机数据到4页内存" << std::endl;
  buddy_->print();

  // 检查分配的地址不重叠
  EXPECT_NE(ptr1, ptr2) << "分配的地址重叠";
  EXPECT_NE(ptr1, ptr3) << "分配的地址重叠";
  EXPECT_NE(ptr2, ptr3) << "分配的地址重叠";

  // 详细的地址比较
  CompareAddresses(ptr1, ptr2, "ptr1(1页)", "ptr2(2页)");
  CompareAddresses(ptr1, ptr3, "ptr1(1页)", "ptr3(4页)");
  CompareAddresses(ptr2, ptr3, "ptr2(2页)", "ptr3(4页)");

  // 打印地址信息用于调试
  std::cout << "\n分配的地址信息:" << std::endl;
  std::cout << "  ptr1 (1页): " << ptr1 << std::endl;
  std::cout << "  ptr2 (2页): " << ptr2 << std::endl;
  std::cout << "  ptr3 (4页): " << ptr3 << std::endl;

  // 计算相对偏移
  auto offset1 = static_cast<char*>(ptr1) - static_cast<char*>(test_memory_);
  auto offset2 = static_cast<char*>(ptr2) - static_cast<char*>(test_memory_);
  auto offset3 = static_cast<char*>(ptr3) - static_cast<char*>(test_memory_);
  std::cout << "  相对偏移:" << std::endl;
  std::cout << "    ptr1: " << offset1 << " 字节 (页"
            << offset1 / AllocatorBase::kPageSize << ")" << std::endl;
  std::cout << "    ptr2: " << offset2 << " 字节 (页"
            << offset2 / AllocatorBase::kPageSize << ")" << std::endl;
  std::cout << "    ptr3: " << offset3 << " 字节 (页"
            << offset3 / AllocatorBase::kPageSize << ")" << std::endl;

  // 验证数据完整性
  std::cout << "\n验证数据完整性..." << std::endl;
  EXPECT_TRUE(VerifyData(ptr1, 0, data1)) << "1页内存数据完整性验证失败";
  EXPECT_TRUE(VerifyData(ptr2, 1, data2)) << "2页内存数据完整性验证失败";
  EXPECT_TRUE(VerifyData(ptr3, 2, data3)) << "4页内存数据完整性验证失败";
  std::cout << "所有内存数据完整性验证通过" << std::endl;

  // 测试释放
  std::cout << "\n释放1页..." << std::endl;
  std::cout << "释放地址: " << ptr1 << std::endl;
  buddy_->Free(ptr1, 0);
  buddy_->print();

  std::cout << "\n释放2页..." << std::endl;
  std::cout << "释放地址: " << ptr2 << std::endl;
  buddy_->Free(ptr2, 1);
  buddy_->print();

  std::cout << "\n释放4页..." << std::endl;
  std::cout << "释放地址: " << ptr3 << std::endl;
  buddy_->Free(ptr3, 2);
  buddy_->print();

  std::cout << "=== BasicAllocAndFree 测试结束 ===\n" << std::endl;
}

/**
 * @brief 测试边界条件
 */
TEST_F(BuddyTest, BoundaryConditions) {
  // 测试分配0页（最小单位）
  std::cout << "\n测试分配最小单位 (order=0)..." << std::endl;
  void* ptr = buddy_->Alloc(0);
  ASSERT_NE(ptr, nullptr) << "分配最小单位失败";
  EXPECT_TRUE(ValidateAllocatedAddress(ptr, 0, "最小单位分配"))
      << "最小单位地址验证失败";
  std::cout << "✓ 分配成功: " << ptr << " (1页)" << std::endl;
  buddy_->Free(ptr, 0);
  std::cout << "✓ 释放成功: " << ptr << std::endl;

  // 测试分配超大块（应该失败）
  std::cout << "\n测试分配超大块 (order=20)..." << std::endl;
  void* large_ptr = buddy_->Alloc(20);  // 2^20 = 1M页，远超测试内存
  EXPECT_EQ(large_ptr, nullptr) << "应该无法分配超大内存块";
  if (large_ptr == nullptr) {
    std::cout << "✓ 预期失败: 超大块分配返回 nullptr" << std::endl;
  } else {
    std::cout << "✗ 意外成功: " << large_ptr << std::endl;
  }

  // 测试无效order
  std::cout << "\n测试无效order (order=100)..." << std::endl;
  void* invalid_ptr = buddy_->Alloc(100);
  EXPECT_EQ(invalid_ptr, nullptr) << "应该无法分配无效order的内存";
  if (invalid_ptr == nullptr) {
    std::cout << "✓ 预期失败: 无效order分配返回 nullptr" << std::endl;
  } else {
    std::cout << "✗ 意外成功: " << invalid_ptr << std::endl;
  }
}

/**
 * @brief 测试内存耗尽情况
 */
TEST_F(BuddyTest, MemoryExhaustion) {
  std::cout << "\n=== MemoryExhaustion 测试开始 ===" << std::endl;
  std::cout << "初始状态:" << std::endl;
  buddy_->print();

  std::random_device rd;
  std::mt19937 gen(rd());
  std::vector<std::pair<void*, std::vector<uint8_t>>> allocated_blocks;

  // 持续分配直到内存耗尽
  std::cout << "\n开始持续分配1页内存块，直到耗尽..." << std::endl;
  for (int i = 0; i < 1000; ++i) {  // 防止无限循环
    void* ptr = buddy_->Alloc(0);   // 分配1页
    if (ptr == nullptr) {
      std::cout << "\n内存耗尽，共分配了 " << allocated_blocks.size() << " 页"
                << std::endl;
      buddy_->print();
      break;  // 内存耗尽
    }

    // 填充随机数据并保存
    FillRandomData(ptr, 0, gen);
    auto data = SaveData(ptr, 0);
    allocated_blocks.emplace_back(ptr, std::move(data));

    // 每50个分配打印一次地址信息
    if (i < 10 || (i + 1) % 50 == 0) {
      auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
      size_t page_num = offset / AllocatorBase::kPageSize;
      std::cout << "第" << (i + 1) << "个分配: " << ptr << " (页" << page_num
                << ")" << std::endl;
    }
  }

  EXPECT_GT(allocated_blocks.size(), 0) << "应该能分配至少一些内存";
  EXPECT_LE(allocated_blocks.size(), test_pages_) << "分配的页数不应超过总页数";

  // 记录实际分配的页数，用于调试
  std::cout << "实际分配了 " << allocated_blocks.size() << " 页，总共 "
            << test_pages_ << " 页" << std::endl;

  // 验证再次分配失败
  void* ptr = buddy_->Alloc(0);
  EXPECT_EQ(ptr, nullptr) << "内存耗尽后应该无法继续分配";

  // 验证所有内存数据的完整性
  std::cout << "\n验证所有分配的内存数据完整性..." << std::endl;
  size_t verified_count = 0;
  for (const auto& [allocated_ptr, expected_data] : allocated_blocks) {
    if (VerifyData(allocated_ptr, 0, expected_data)) {
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
    buddy_->Free(allocated_ptr, 0);
  }
  std::cout << "所有内存释放完成，当前状态:" << std::endl;
  buddy_->print();

  // 验证释放后可以重新分配
  ptr = buddy_->Alloc(0);
  EXPECT_NE(ptr, nullptr) << "释放内存后应该能重新分配";
  if (ptr) {
    auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
    size_t page_num = offset / AllocatorBase::kPageSize;
    std::cout << "重新分配成功: " << ptr << " (页" << page_num << ")"
              << std::endl;

    // 测试新分配的内存可以正常使用
    FillRandomData(ptr, 0, gen);
    auto new_data = SaveData(ptr, 0);
    EXPECT_TRUE(VerifyData(ptr, 0, new_data)) << "新分配的内存应该可以正常读写";
    buddy_->Free(ptr, 0);
    std::cout << "释放重新分配的内存: " << ptr << std::endl;
  }

  std::cout << "=== MemoryExhaustion 测试结束 ===\n" << std::endl;
}

/**
 * @brief 测试buddy合并功能
 */
TEST_F(BuddyTest, BuddyMerging) {
  std::cout << "\n=== BuddyMerging 测试开始 ===" << std::endl;
  std::cout << "初始状态:" << std::endl;
  buddy_->print();

  // 分配两个相邻的1页块
  std::cout << "\n分配第一个1页块..." << std::endl;
  void* ptr1 = buddy_->Alloc(0);
  ASSERT_NE(ptr1, nullptr);
  auto offset1 = static_cast<char*>(ptr1) - static_cast<char*>(test_memory_);
  size_t page1 = offset1 / AllocatorBase::kPageSize;
  std::cout << "✓ 分配成功: ptr1 = " << ptr1 << " (页" << page1 << ")"
            << std::endl;
  buddy_->print();

  std::cout << "\n分配第二个1页块..." << std::endl;
  void* ptr2 = buddy_->Alloc(0);
  ASSERT_NE(ptr2, nullptr);
  auto offset2 = static_cast<char*>(ptr2) - static_cast<char*>(test_memory_);
  size_t page2 = offset2 / AllocatorBase::kPageSize;
  std::cout << "✓ 分配成功: ptr2 = " << ptr2 << " (页" << page2 << ")"
            << std::endl;

  // 检查是否相邻
  if (abs(static_cast<long>(page1) - static_cast<long>(page2)) == 1) {
    std::cout << "ℹ 两个块相邻，适合测试buddy合并" << std::endl;
  } else {
    std::cout << "ℹ 两个块不相邻 (页间距: "
              << abs(static_cast<long>(page1) - static_cast<long>(page2)) << ")"
              << std::endl;
  }
  buddy_->print();

  // 释放这两个块
  std::cout << "\n释放第一个1页块..." << std::endl;
  std::cout << "释放地址: " << ptr1 << " (页" << page1 << ")" << std::endl;
  buddy_->Free(ptr1, 0);
  buddy_->print();

  std::cout << "\n释放第二个1页块（应该触发合并）..." << std::endl;
  std::cout << "释放地址: " << ptr2 << " (页" << page2 << ")" << std::endl;
  buddy_->Free(ptr2, 0);
  buddy_->print();

  // 现在应该能分配一个2页的块（如果buddy合并正常工作）
  std::cout << "\n尝试分配2页块（验证合并是否成功）..." << std::endl;
  void* large_ptr = buddy_->Alloc(1);
  if (large_ptr != nullptr) {
    auto large_offset =
        static_cast<char*>(large_ptr) - static_cast<char*>(test_memory_);
    size_t large_page = large_offset / AllocatorBase::kPageSize;
    std::cout << "✓ 分配成功: " << large_ptr << " (页" << large_page << "~"
              << (large_page + 1) << ")" << std::endl;
    EXPECT_NE(large_ptr, nullptr) << "buddy合并后应该能分配更大的块";
  } else {
    std::cout << "✗ 分配失败: 可能buddy合并未成功" << std::endl;
  }
  buddy_->print();

  if (large_ptr) {
    std::cout << "\n释放2页块..." << std::endl;
    std::cout << "释放地址: " << large_ptr << std::endl;
    buddy_->Free(large_ptr, 1);
    buddy_->print();
  }

  std::cout << "=== BuddyMerging 测试结束 ===\n" << std::endl;
}

/**
 * @brief 测试分配和释放的匹配性
 */
TEST_F(BuddyTest, AllocFreePairMatching) {
  // 分配不同大小的块
  std::vector<std::pair<void*, size_t>> allocated_blocks;

  for (size_t order = 0; order <= 4; ++order) {
    void* ptr = buddy_->Alloc(order);
    if (ptr != nullptr) {
      allocated_blocks.emplace_back(ptr, order);
    }
  }

  EXPECT_GT(allocated_blocks.size(), 0) << "应该能分配一些内存块";

  // 以相同的order释放所有块
  for (const auto& [ptr, order] : allocated_blocks) {
    buddy_->Free(ptr, order);
  }
}

/**
 * @brief 测试内存写入和读取
 */
TEST_F(BuddyTest, MemoryReadWrite) {
  std::random_device rd;
  std::mt19937 gen(rd());

  void* ptr = buddy_->Alloc(0);  // 分配1页
  ASSERT_NE(ptr, nullptr);

  // 填充整页随机数据并验证
  FillRandomData(ptr, 0, gen);
  auto full_page_data = SaveData(ptr, 0);
  EXPECT_TRUE(VerifyData(ptr, 0, full_page_data)) << "整页随机数据读写测试失败";

  // 测试特定的数据模式
  const char test_data[] = "Hello, Buddy Allocator!";
  std::memcpy(ptr, test_data, sizeof(test_data));

  // 读取并验证数据
  char read_buffer[sizeof(test_data)];
  std::memcpy(read_buffer, ptr, sizeof(test_data));
  EXPECT_STREQ(read_buffer, test_data) << "字符串读写测试失败";

  // 测试数值数据
  auto* int_ptr = static_cast<uint32_t*>(ptr);
  std::uniform_int_distribution<uint32_t> int_dist;
  std::vector<uint32_t> test_ints;

  size_t int_count = AllocatorBase::kPageSize / sizeof(uint32_t);
  for (size_t i = 0; i < int_count; ++i) {
    uint32_t random_int = int_dist(gen);
    int_ptr[i] = random_int;
    test_ints.push_back(random_int);
  }

  // 验证数值数据
  for (size_t i = 0; i < int_count; ++i) {
    EXPECT_EQ(int_ptr[i], test_ints[i]) << "数值数据读写测试失败，位置=" << i;
  }

  std::cout << "验证了 " << int_count << " 个随机整数的读写" << std::endl;

  buddy_->Free(ptr, 0);
}

/**
 * @brief 压力测试：随机分配和释放
 */
TEST_F(BuddyTest, StressTest) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> order_dist(0, 3);  // order 0-3
  std::uniform_real_distribution<> action_dist(0.0, 1.0);

  // 存储分配的块信息：指针、大小、数据
  std::vector<std::tuple<void*, size_t, std::vector<uint8_t>>> allocated_blocks;

  const int operations = 500;  // 减少操作数以提高测试速度
  int alloc_count = 0;
  int free_count = 0;
  int verify_count = 0;

  for (int i = 0; i < operations; ++i) {
    if (allocated_blocks.empty() || action_dist(gen) < 0.6) {
      // 60%概率分配，或者当前无已分配块时必须分配
      size_t order = order_dist(gen);
      void* ptr = buddy_->Alloc(order);
      if (ptr != nullptr) {
        // 填充随机数据
        FillRandomData(ptr, order, gen);
        auto data = SaveData(ptr, order);
        allocated_blocks.emplace_back(ptr, order, std::move(data));
        alloc_count++;

        // 立即验证数据正确性
        if (VerifyData(ptr, order, std::get<2>(allocated_blocks.back()))) {
          verify_count++;
        }
      }
    } else {
      // 40%概率释放
      if (!allocated_blocks.empty()) {
        std::uniform_int_distribution<> index_dist(0,
                                                   allocated_blocks.size() - 1);
        size_t index = index_dist(gen);

        auto [ptr, order, expected_data] = allocated_blocks[index];

        // 释放前验证数据完整性
        EXPECT_TRUE(VerifyData(ptr, order, expected_data))
            << "释放前数据完整性验证失败，order=" << order;

        buddy_->Free(ptr, order);
        allocated_blocks.erase(allocated_blocks.begin() + index);
        free_count++;
      }
    }

    // 每100次操作验证一次所有分配块的数据完整性
    if (i % 100 == 0 && !allocated_blocks.empty()) {
      size_t integrity_check_count = 0;
      for (const auto& [ptr, order, expected_data] : allocated_blocks) {
        if (VerifyData(ptr, order, expected_data)) {
          integrity_check_count++;
        }
      }
      EXPECT_EQ(integrity_check_count, allocated_blocks.size())
          << "第" << i << "次操作后数据完整性检查失败";
    }
  }

  // 最终验证所有剩余块的数据完整性
  std::cout << "\n最终数据完整性验证..." << std::endl;
  size_t final_verify_count = 0;
  for (const auto& [ptr, order, expected_data] : allocated_blocks) {
    if (VerifyData(ptr, order, expected_data)) {
      final_verify_count++;
    }
  }
  EXPECT_EQ(final_verify_count, allocated_blocks.size())
      << "最终数据完整性验证失败";
  std::cout << "验证了 " << final_verify_count << " 个内存块的数据完整性"
            << std::endl;

  // 清理剩余的分配块
  for (const auto& [ptr, order, data] : allocated_blocks) {
    buddy_->Free(ptr, order);
    free_count++;
  }

  EXPECT_GT(alloc_count, 0) << "压力测试应该进行了一些分配操作";
  EXPECT_GT(free_count, 0) << "压力测试应该进行了一些释放操作";
  EXPECT_GT(verify_count, 0) << "压力测试应该进行了数据验证";

  std::cout << "压力测试统计: 分配=" << alloc_count << ", 释放=" << free_count
            << ", 验证=" << verify_count << std::endl;
}

/**
 * @brief 测试不同order的分配大小
 */
TEST_F(BuddyTest, DifferentOrderSizes) {
  std::cout << "\n=== DifferentOrderSizes 测试开始 ===" << std::endl;
  std::cout << "初始状态:" << std::endl;
  buddy_->print();

  std::random_device rd;
  std::mt19937 gen(rd());
  std::vector<std::tuple<void*, size_t, std::vector<uint8_t>>> ptrs;

  // 分配不同order的内存块
  for (size_t order = 0; order <= 5; ++order) {
    size_t pages = 1 << order;
    std::cout << "\n分配 order=" << order << " (" << pages << " 页)..."
              << std::endl;
    void* ptr = buddy_->Alloc(order);
    if (ptr != nullptr) {
      // 进行地址有效性检查
      std::string test_name = "order=" + std::to_string(order) + "分配";
      EXPECT_TRUE(ValidateAllocatedAddress(ptr, order, test_name.c_str()))
          << "地址验证失败，order=" << order;

      auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
      size_t start_page = offset / AllocatorBase::kPageSize;
      size_t end_page = start_page + pages - 1;
      std::cout << "✓ 分配成功: " << ptr << " (页" << start_page;
      if (pages > 1) {
        std::cout << "~" << end_page;
      }
      std::cout << ", " << pages << "页)" << std::endl;

      // 填充随机数据
      FillRandomData(ptr, order, gen);
      auto data = SaveData(ptr, order);
      ptrs.emplace_back(ptr, order, std::move(data));
      buddy_->print();

      // 立即验证数据完整性
      EXPECT_TRUE(VerifyData(ptr, order, std::get<2>(ptrs.back())))
          << "分配后立即验证数据失败，order=" << order;

      std::cout << "已填充并验证 " << pages << " 页的随机数据" << std::endl;
    } else {
      std::cout << "✗ 分配 order=" << order << " 失败（内存不足）" << std::endl;
    }
  }

  EXPECT_GT(ptrs.size(), 0) << "应该能分配一些不同大小的内存块";

  // 中间验证：再次检查所有内存块的数据完整性
  std::cout << "\n中间验证：检查所有内存块的数据完整性..." << std::endl;
  size_t mid_verify_count = 0;
  for (const auto& [ptr, order, expected_data] : ptrs) {
    if (VerifyData(ptr, order, expected_data)) {
      mid_verify_count++;
    } else {
      std::cout << "警告: order=" << order << " 的内存块数据完整性验证失败"
                << std::endl;
    }
  }
  EXPECT_EQ(mid_verify_count, ptrs.size()) << "中间验证失败";
  std::cout << "中间验证通过：" << mid_verify_count << "/" << ptrs.size()
            << " 个内存块" << std::endl;

  std::cout << "\n开始验证数据完整性并释放内存..." << std::endl;
  // 验证数据完整性并释放
  for (const auto& [ptr, order, expected_data] : ptrs) {
    size_t pages = 1 << order;
    auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
    size_t start_page = offset / AllocatorBase::kPageSize;
    size_t end_page = start_page + pages - 1;

    // 验证随机数据完整性
    bool integrity_ok = VerifyData(ptr, order, expected_data);
    EXPECT_TRUE(integrity_ok)
        << "释放前数据完整性验证失败，order=" << order << ", pages=" << pages;

    if (integrity_ok) {
      std::cout << "✓ order=" << order << " (" << pages
                << " 页) 数据完整性验证通过" << std::endl;
    } else {
      std::cout << "✗ order=" << order << " (" << pages
                << " 页) 数据完整性验证失败" << std::endl;
    }

    std::cout << "释放 order=" << order << " (" << pages << " 页): " << ptr
              << " (页" << start_page;
    if (pages > 1) {
      std::cout << "~" << end_page;
    }
    std::cout << ")" << std::endl;
    buddy_->Free(ptr, order);
    buddy_->print();
  }

  std::cout << "=== DifferentOrderSizes 测试结束 ===\n" << std::endl;
}

/**
 * @brief 测试释放无效地址（应该安全）
 */
TEST_F(BuddyTest, FreeInvalidAddress) {
  // 释放不在管理范围内的地址应该是安全的
  // buddy分配器会检查地址范围，对于超出范围的地址直接返回

  // 测试nullptr（地址0，通常不在管理范围内）
  EXPECT_NO_FATAL_FAILURE(buddy_->Free(nullptr, 0));
  EXPECT_NO_FATAL_FAILURE(buddy_->Free(nullptr, 5));

  // 测试其他无效地址
  void* invalid_addr1 = (void*)0x1000000;  // 远超管理范围的地址
  void* invalid_addr2 =
      (void*)((char*)test_memory_ - 4096);  // 管理范围之前的地址

  EXPECT_NO_FATAL_FAILURE(buddy_->Free(invalid_addr1, 0));
  EXPECT_NO_FATAL_FAILURE(buddy_->Free(invalid_addr2, 1));
}

/**
 * @brief 测试GetUsedCount和GetFreeCount方法
 */
TEST_F(BuddyTest, UsedAndFreeCount) {
  // 初始状态：所有内存都应该是空闲的
  size_t initial_free = buddy_->GetFreeCount();
  size_t initial_used = buddy_->GetUsedCount();

  EXPECT_EQ(initial_used, 0) << "初始已使用页数应该为0";
  EXPECT_EQ(initial_free, test_pages_) << "初始空闲页数应该等于总页数";
  EXPECT_EQ(initial_free + initial_used, test_pages_)
      << "空闲页数+已使用页数应该等于总页数";

  // 分配一些内存块
  void* ptr1 = buddy_->Alloc(0);  // 1页
  void* ptr2 = buddy_->Alloc(1);  // 2页
  void* ptr3 = buddy_->Alloc(2);  // 4页

  ASSERT_NE(ptr1, nullptr);
  ASSERT_NE(ptr2, nullptr);
  ASSERT_NE(ptr3, nullptr);

  // 检查计数变化
  size_t after_alloc_free = buddy_->GetFreeCount();
  size_t after_alloc_used = buddy_->GetUsedCount();

  EXPECT_EQ(after_alloc_used, 7) << "分配1+2+4=7页后，已使用页数应该为7";
  EXPECT_EQ(after_alloc_free + after_alloc_used, test_pages_)
      << "空闲页数+已使用页数应该等于总页数";
  EXPECT_LT(after_alloc_free, initial_free) << "分配后空闲页数应该减少";

  // 释放部分内存
  buddy_->Free(ptr1, 0);  // 释放1页

  size_t after_partial_free_free = buddy_->GetFreeCount();
  size_t after_partial_free_used = buddy_->GetUsedCount();

  EXPECT_EQ(after_partial_free_used, 6) << "释放1页后，已使用页数应该为6";
  EXPECT_EQ(after_partial_free_free + after_partial_free_used, test_pages_)
      << "空闲页数+已使用页数应该等于总页数";

  // 释放剩余内存
  buddy_->Free(ptr2, 1);
  buddy_->Free(ptr3, 2);

  size_t final_free = buddy_->GetFreeCount();
  size_t final_used = buddy_->GetUsedCount();

  EXPECT_EQ(final_used, 0) << "释放所有内存后，已使用页数应该为0";
  EXPECT_EQ(final_free, test_pages_)
      << "释放所有内存后，空闲页数应该等于总页数";
}

/**
 * @brief 测试分配器构造参数验证
 */
TEST_F(BuddyTest, ConstructorValidation) {
  std::cout << "\n=== ConstructorValidation 测试开始 ===" << std::endl;

  // 测试用很小的内存创建分配器
  size_t small_size = AllocatorBase::kPageSize * 4;  // 4页
  void* small_memory = std::aligned_alloc(AllocatorBase::kPageSize, small_size);
  ASSERT_NE(small_memory, nullptr);

  std::memset(small_memory, 0, small_size);

  // 创建小内存的buddy分配器（使用带调试功能的版本）
  auto small_buddy =
      std::make_unique<BuddyDebugHelper>("small_buddy", small_memory, 4);

  std::cout << "小内存池（4页）初始状态:" << std::endl;
  small_buddy->print();

  // 测试在小内存中的分配
  std::cout << "\n在小内存池中分配1页..." << std::endl;
  void* ptr1 = small_buddy->Alloc(0);  // 1页
  EXPECT_NE(ptr1, nullptr) << "应该能在小内存池中分配1页";
  if (ptr1) {
    // 对小内存池的地址进行验证 - 需要相对于小内存池的基地址
    auto offset = static_cast<char*>(ptr1) - static_cast<char*>(small_memory);
    size_t page = offset / AllocatorBase::kPageSize;
    std::cout << "✓ 分配成功: " << ptr1 << " (页" << page << ")" << std::endl;

    // 基本有效性检查
    EXPECT_GE(static_cast<char*>(ptr1), static_cast<char*>(small_memory))
        << "地址应该在小内存池范围内";
    EXPECT_LT(static_cast<char*>(ptr1),
              static_cast<char*>(small_memory) + small_size)
        << "地址应该在小内存池范围内";
    EXPECT_EQ(offset % AllocatorBase::kPageSize, 0) << "地址应该按页对齐";
    EXPECT_LT(page, 4) << "页号应该在0-3范围内";
    EXPECT_GE(page, 0) << "页号应该非负";

    std::cout << "✓ 小内存池地址验证通过" << std::endl;
  }
  small_buddy->print();

  std::cout << "\n在小内存池中分配2页..." << std::endl;
  void* ptr2 = small_buddy->Alloc(1);  // 2页
  EXPECT_NE(ptr2, nullptr) << "应该能在小内存池中分配2页";
  if (ptr2) {
    auto offset = static_cast<char*>(ptr2) - static_cast<char*>(small_memory);
    size_t start_page = offset / AllocatorBase::kPageSize;
    std::cout << "✓ 分配成功: " << ptr2 << " (页" << start_page << "~"
              << (start_page + 1) << ")" << std::endl;

    // 基本有效性检查
    EXPECT_GE(static_cast<char*>(ptr2), static_cast<char*>(small_memory))
        << "地址应该在小内存池范围内";
    EXPECT_LT(static_cast<char*>(ptr2),
              static_cast<char*>(small_memory) + small_size)
        << "地址应该在小内存池范围内";
    EXPECT_EQ(offset % AllocatorBase::kPageSize, 0) << "地址应该按页对齐";
    EXPECT_EQ(start_page % 2, 0) << "2页分配应该按2页边界对齐";
    EXPECT_LT(start_page + 1, 4) << "结束页号应该在范围内";
    EXPECT_GE(start_page, 0) << "起始页号应该非负";

    std::cout << "✓ 小内存池2页分配验证通过" << std::endl;

    // 检查两个分配的地址不重叠
    if (ptr1 && ptr2) {
      EXPECT_NE(ptr1, ptr2) << "两个分配的地址不应该相同";
      auto offset1 =
          static_cast<char*>(ptr1) - static_cast<char*>(small_memory);
      auto offset2 =
          static_cast<char*>(ptr2) - static_cast<char*>(small_memory);
      auto distance = abs(static_cast<long>(offset2 - offset1));
      EXPECT_GE(distance, AllocatorBase::kPageSize) << "地址间距应该至少一页";
      std::cout << "地址间距: " << distance << " 字节" << std::endl;
    }
  }
  small_buddy->print();

  // 现在应该没有更多空间了
  std::cout << "\n尝试再分配2页（应该失败）..." << std::endl;
  void* ptr3 = small_buddy->Alloc(1);  // 再分配2页
  EXPECT_EQ(ptr3, nullptr) << "小内存池应该已经耗尽";
  if (ptr3 == nullptr) {
    std::cout << "✓ 预期失败: 返回 nullptr" << std::endl;
  } else {
    std::cout << "✗ 意外成功: " << ptr3 << std::endl;
  }
  small_buddy->print();

  // 清理
  std::cout << "\n清理小内存池..." << std::endl;
  if (ptr1) {
    std::cout << "释放1页: " << ptr1 << std::endl;
    small_buddy->Free(ptr1, 0);
    std::cout << "释放1页后:" << std::endl;
    small_buddy->print();
  }
  if (ptr2) {
    std::cout << "释放2页: " << ptr2 << std::endl;
    small_buddy->Free(ptr2, 1);
    std::cout << "释放2页后:" << std::endl;
    small_buddy->print();
  }

  small_buddy.reset();
  std::free(small_memory);

  std::cout << "=== ConstructorValidation 测试结束 ===\n" << std::endl;
}

/**
 * @brief 测试 print 功能的演示
 */
TEST_F(BuddyTest, BuddyPrintDemo) {
  std::cout << "\n=== Buddy Print Demo ===" << std::endl;

  std::cout << "\n1. 初始状态:" << std::endl;
  buddy_->print();

  std::cout << "\n2. 分配一些内存块后:" << std::endl;
  void* ptr1 = buddy_->Alloc(0);  // 1页
  void* ptr2 = buddy_->Alloc(1);  // 2页
  void* ptr3 = buddy_->Alloc(2);  // 4页

  ASSERT_NE(ptr1, nullptr);
  ASSERT_NE(ptr2, nullptr);
  ASSERT_NE(ptr3, nullptr);

  std::cout << "分配的地址:" << std::endl;
  std::cout << "  ptr1 (1页): " << ptr1 << std::endl;
  std::cout << "  ptr2 (2页): " << ptr2 << std::endl;
  std::cout << "  ptr3 (4页): " << ptr3 << std::endl;

  buddy_->print();

  std::cout << "\n3. 释放部分内存后:" << std::endl;
  std::cout << "释放 ptr1: " << ptr1 << std::endl;
  buddy_->Free(ptr1, 0);  // 释放1页
  buddy_->print();

  std::cout << "\n4. 释放所有内存后:" << std::endl;
  std::cout << "释放 ptr2: " << ptr2 << std::endl;
  std::cout << "释放 ptr3: " << ptr3 << std::endl;
  buddy_->Free(ptr2, 1);
  buddy_->Free(ptr3, 2);
  buddy_->print();

  std::cout << "\n=== Demo 结束 ===" << std::endl;
}

/**
 * @brief 专门测试随机数据完整性
 */
TEST_F(BuddyTest, RandomDataIntegrityTest) {
  std::cout << "\n=== RandomDataIntegrityTest 测试开始 ===" << std::endl;

  std::random_device rd;
  std::mt19937 gen(rd());

  // 测试不同大小的内存块
  for (size_t order = 0; order <= 4; ++order) {
    std::cout << "\n测试 order=" << order << " (" << (1 << order) << " 页)"
              << std::endl;

    void* ptr = buddy_->Alloc(order);
    if (ptr == nullptr) {
      std::cout << "跳过 order=" << order << "（内存不足）" << std::endl;
      continue;
    }

    // 地址有效性验证
    std::string test_name = "随机数据测试order=" + std::to_string(order);
    EXPECT_TRUE(ValidateAllocatedAddress(ptr, order, test_name.c_str()))
        << "随机数据测试地址验证失败，order=" << order;

    auto offset = static_cast<char*>(ptr) - static_cast<char*>(test_memory_);
    size_t start_page = offset / AllocatorBase::kPageSize;
    size_t end_page = start_page + (1 << order) - 1;
    std::cout << "✓ 分配成功: " << ptr << " (页" << start_page;
    if ((1 << order) > 1) {
      std::cout << "~" << end_page;
    }
    std::cout << ")" << std::endl;

    // 填充随机数据
    FillRandomData(ptr, order, gen);
    auto original_data = SaveData(ptr, order);

    // 立即验证
    EXPECT_TRUE(VerifyData(ptr, order, original_data))
        << "立即验证失败，order=" << order;

    // 多次读取验证数据稳定性
    for (int i = 0; i < 10; ++i) {
      EXPECT_TRUE(VerifyData(ptr, order, original_data))
          << "第" << i << "次重复验证失败，order=" << order;
    }

    // 测试数据模式：边界数据
    size_t pages = 1 << order;
    auto* byte_ptr = static_cast<uint8_t*>(ptr);
    size_t total_size = pages * AllocatorBase::kPageSize;

    // 在内存的第一个和最后一个字节写入特殊值
    uint8_t first_byte = byte_ptr[0];
    uint8_t last_byte = byte_ptr[total_size - 1];

    byte_ptr[0] = 0xAA;
    byte_ptr[total_size - 1] = 0x55;

    // 验证修改生效
    EXPECT_EQ(byte_ptr[0], 0xAA) << "首字节修改失败";
    EXPECT_EQ(byte_ptr[total_size - 1], 0x55) << "末字节修改失败";

    // 恢复原始数据
    byte_ptr[0] = first_byte;
    byte_ptr[total_size - 1] = last_byte;

    // 验证数据恢复
    EXPECT_TRUE(VerifyData(ptr, order, original_data))
        << "数据恢复后验证失败，order=" << order;

    std::cout << "✓ order=" << order << " 随机数据完整性测试通过" << std::endl;

    std::cout << "释放内存: " << ptr << std::endl;
    buddy_->Free(ptr, order);
  }

  std::cout << "\n=== RandomDataIntegrityTest 测试结束 ===" << std::endl;
}

/**
 * @brief 专门测试地址有效性检查
 */
TEST_F(BuddyTest, AddressValidationTest) {
  std::cout << "\n=== AddressValidationTest 测试开始 ===" << std::endl;

  // 测试各种order的地址验证
  std::vector<std::pair<void*, size_t>> allocated_ptrs;

  for (size_t order = 0; order <= 4; ++order) {
    std::cout << "\n--- 测试 order=" << order << " 的地址验证 ---" << std::endl;
    void* ptr = buddy_->Alloc(order);

    if (ptr != nullptr) {
      allocated_ptrs.emplace_back(ptr, order);

      // 执行详细的地址验证
      std::string test_name = "地址验证测试order=" + std::to_string(order);
      bool validation_result =
          ValidateAllocatedAddress(ptr, order, test_name.c_str());
      EXPECT_TRUE(validation_result) << "order=" << order << " 地址验证失败";

      // 额外的边界测试
      std::cout << "\n额外边界检查:" << std::endl;
      size_t pages = 1 << order;
      size_t block_size = pages * AllocatorBase::kPageSize;

      // 测试内存块的第一个和最后一个字节
      auto* byte_ptr = static_cast<uint8_t*>(ptr);

      // 写入测试模式
      uint8_t test_pattern_start = 0xA5;
      uint8_t test_pattern_end = 0x5A;

      byte_ptr[0] = test_pattern_start;
      byte_ptr[block_size - 1] = test_pattern_end;

      // 验证写入成功
      EXPECT_EQ(byte_ptr[0], test_pattern_start) << "内存块起始位置写入失败";
      EXPECT_EQ(byte_ptr[block_size - 1], test_pattern_end)
          << "内存块结束位置写入失败";

      std::cout << "✓ 内存块边界读写测试通过" << std::endl;

      // 测试对齐要求
      auto addr_value = reinterpret_cast<uintptr_t>(ptr);
      auto base_addr = reinterpret_cast<uintptr_t>(test_memory_);

      EXPECT_EQ(addr_value % AllocatorBase::kPageSize, 0)
          << "地址未按页大小对齐";
      EXPECT_EQ((addr_value - base_addr) % (pages * AllocatorBase::kPageSize),
                0)
          << "地址未按order要求对齐";

      // 验证地址范围
      EXPECT_GE(addr_value, base_addr) << "地址应该大于等于基地址";
      EXPECT_LT(addr_value, base_addr + test_memory_size_)
          << "地址应该小于结束地址";

      // 验证内存块不超出边界
      EXPECT_LE(addr_value + block_size, base_addr + test_memory_size_)
          << "内存块结束地址不应该超出管理范围";

      std::cout << "✓ 对齐要求检查通过" << std::endl;

    } else {
      std::cout << "无法分配 order=" << order << " 的内存块" << std::endl;
    }
  }

  // 交叉验证所有分配的地址
  std::cout << "\n--- 交叉验证所有分配地址 ---" << std::endl;
  for (size_t i = 0; i < allocated_ptrs.size(); ++i) {
    for (size_t j = i + 1; j < allocated_ptrs.size(); ++j) {
      auto [ptr1, order1] = allocated_ptrs[i];
      auto [ptr2, order2] = allocated_ptrs[j];

      std::string name1 = "order" + std::to_string(order1);
      std::string name2 = "order" + std::to_string(order2);

      // 使用新的验证函数
      ValidateNoOverlap(ptr1, order1, ptr2, order2, name1.c_str(),
                        name2.c_str());

      // 比较地址位置
      CompareAddresses(ptr1, ptr2, name1.c_str(), name2.c_str());
    }
  }

  // 清理
  std::cout << "\n--- 清理所有分配 ---" << std::endl;
  for (auto [ptr, order] : allocated_ptrs) {
    std::cout << "释放 order=" << order << " 地址: " << ptr << std::endl;
    buddy_->Free(ptr, order);
  }

  std::cout << "\n=== AddressValidationTest 测试结束 ===" << std::endl;
}

}  // namespace bmalloc
