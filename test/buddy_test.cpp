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
  EXPECT_TRUE(IsInManagedRange(ptr1)) << "分配的地址不在管理范围内";
  EXPECT_TRUE(IsAligned(ptr1, 0)) << "分配的地址未正确对齐";

  // 填充随机数据并保存
  FillRandomData(ptr1, 0, gen);
  auto data1 = SaveData(ptr1, 0);
  std::cout << "已填充随机数据到1页内存" << std::endl;
  buddy_->print();

  std::cout << "\n分配2页 (order=1)..." << std::endl;
  void* ptr2 = buddy_->Alloc(1);  // 分配2页 (2^1 = 2页)
  ASSERT_NE(ptr2, nullptr) << "分配2页失败";
  EXPECT_TRUE(IsInManagedRange(ptr2)) << "分配的地址不在管理范围内";
  EXPECT_TRUE(IsAligned(ptr2, 1)) << "分配的地址未正确对齐";

  // 填充随机数据并保存
  FillRandomData(ptr2, 1, gen);
  auto data2 = SaveData(ptr2, 1);
  std::cout << "已填充随机数据到2页内存" << std::endl;
  buddy_->print();

  std::cout << "\n分配4页 (order=2)..." << std::endl;
  void* ptr3 = buddy_->Alloc(2);  // 分配4页 (2^2 = 4页)
  ASSERT_NE(ptr3, nullptr) << "分配4页失败";
  EXPECT_TRUE(IsInManagedRange(ptr3)) << "分配的地址不在管理范围内";
  EXPECT_TRUE(IsAligned(ptr3, 2)) << "分配的地址未正确对齐";

  // 填充随机数据并保存
  FillRandomData(ptr3, 2, gen);
  auto data3 = SaveData(ptr3, 2);
  std::cout << "已填充随机数据到4页内存" << std::endl;
  buddy_->print();

  // 检查分配的地址不重叠
  EXPECT_NE(ptr1, ptr2) << "分配的地址重叠";
  EXPECT_NE(ptr1, ptr3) << "分配的地址重叠";
  EXPECT_NE(ptr2, ptr3) << "分配的地址重叠";

  // 验证数据完整性
  std::cout << "\n验证数据完整性..." << std::endl;
  EXPECT_TRUE(VerifyData(ptr1, 0, data1)) << "1页内存数据完整性验证失败";
  EXPECT_TRUE(VerifyData(ptr2, 1, data2)) << "2页内存数据完整性验证失败";
  EXPECT_TRUE(VerifyData(ptr3, 2, data3)) << "4页内存数据完整性验证失败";
  std::cout << "所有内存数据完整性验证通过" << std::endl;

  // 测试释放
  std::cout << "\n释放1页..." << std::endl;
  buddy_->Free(ptr1, 0);
  buddy_->print();

  std::cout << "\n释放2页..." << std::endl;
  buddy_->Free(ptr2, 1);
  buddy_->print();

  std::cout << "\n释放4页..." << std::endl;
  buddy_->Free(ptr3, 2);
  buddy_->print();

  std::cout << "=== BasicAllocAndFree 测试结束 ===\n" << std::endl;
}

/**
 * @brief 测试边界条件
 */
TEST_F(BuddyTest, BoundaryConditions) {
  // 测试分配0页（最小单位）
  void* ptr = buddy_->Alloc(0);
  ASSERT_NE(ptr, nullptr) << "分配最小单位失败";
  buddy_->Free(ptr, 0);

  // 测试分配超大块（应该失败）
  void* large_ptr = buddy_->Alloc(20);  // 2^20 = 1M页，远超测试内存
  EXPECT_EQ(large_ptr, nullptr) << "应该无法分配超大内存块";

  // 测试无效order
  void* invalid_ptr = buddy_->Alloc(100);
  EXPECT_EQ(invalid_ptr, nullptr) << "应该无法分配无效order的内存";
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
    // 测试新分配的内存可以正常使用
    FillRandomData(ptr, 0, gen);
    auto new_data = SaveData(ptr, 0);
    EXPECT_TRUE(VerifyData(ptr, 0, new_data)) << "新分配的内存应该可以正常读写";
    buddy_->Free(ptr, 0);
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
  buddy_->print();

  std::cout << "\n分配第二个1页块..." << std::endl;
  void* ptr2 = buddy_->Alloc(0);
  ASSERT_NE(ptr2, nullptr);
  buddy_->print();

  // 释放这两个块
  std::cout << "\n释放第一个1页块..." << std::endl;
  buddy_->Free(ptr1, 0);
  buddy_->print();

  std::cout << "\n释放第二个1页块（应该触发合并）..." << std::endl;
  buddy_->Free(ptr2, 0);
  buddy_->print();

  // 现在应该能分配一个2页的块（如果buddy合并正常工作）
  std::cout << "\n尝试分配2页块（验证合并是否成功）..." << std::endl;
  void* large_ptr = buddy_->Alloc(1);
  EXPECT_NE(large_ptr, nullptr) << "buddy合并后应该能分配更大的块";
  buddy_->print();

  if (large_ptr) {
    std::cout << "\n释放2页块..." << std::endl;
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
    std::cout << "\n分配 order=" << order << " (" << (1 << order) << " 页)..."
              << std::endl;
    void* ptr = buddy_->Alloc(order);
    if (ptr != nullptr) {
      // 填充随机数据
      FillRandomData(ptr, order, gen);
      auto data = SaveData(ptr, order);
      ptrs.emplace_back(ptr, order, std::move(data));
      buddy_->print();

      // 立即验证数据完整性
      EXPECT_TRUE(VerifyData(ptr, order, std::get<2>(ptrs.back())))
          << "分配后立即验证数据失败，order=" << order;

      std::cout << "已填充并验证 " << (1 << order) << " 页的随机数据"
                << std::endl;
    } else {
      std::cout << "分配 order=" << order << " 失败（内存不足）" << std::endl;
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

    std::cout << "释放 order=" << order << " (" << pages << " 页)..."
              << std::endl;
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
  EXPECT_NE(ptr1, nullptr);
  small_buddy->print();

  std::cout << "\n在小内存池中分配2页..." << std::endl;
  void* ptr2 = small_buddy->Alloc(1);  // 2页
  EXPECT_NE(ptr2, nullptr);
  small_buddy->print();

  // 现在应该没有更多空间了
  std::cout << "\n尝试再分配2页（应该失败）..." << std::endl;
  void* ptr3 = small_buddy->Alloc(1);  // 再分配2页
  EXPECT_EQ(ptr3, nullptr) << "小内存池应该已经耗尽";
  small_buddy->print();

  // 清理
  std::cout << "\n清理小内存池..." << std::endl;
  if (ptr1) {
    small_buddy->Free(ptr1, 0);
    std::cout << "释放1页后:" << std::endl;
    small_buddy->print();
  }
  if (ptr2) {
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

  buddy_->print();

  std::cout << "\n3. 释放部分内存后:" << std::endl;
  buddy_->Free(ptr1, 0);  // 释放1页
  buddy_->print();

  std::cout << "\n4. 释放所有内存后:" << std::endl;
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

    buddy_->Free(ptr, order);
  }

  std::cout << "\n=== RandomDataIntegrityTest 测试结束 ===" << std::endl;
}

}  // namespace bmalloc
