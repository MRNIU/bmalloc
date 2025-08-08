/**
 * Copyright The bmalloc Contributors
 * @file buddy_test.cpp
 * @brief Buddy分配器的Google Test测试用例
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include <random>
#include <algorithm>

#include "buddy.h"

namespace bmalloc {
namespace test {

/**
 * @brief Buddy分配器测试夹具类
 * 提供测试环境的初始化和清理
 */
class BuddyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 分配测试用的内存池 (1MB = 256页)
    test_memory_size_ = 1024 * 1024;  // 1MB
    test_pages_ = test_memory_size_ / AllocatorBase::kPageSize;  // 256页
    test_memory_ = std::aligned_alloc(AllocatorBase::kPageSize, test_memory_size_);
    
    ASSERT_NE(test_memory_, nullptr) << "无法分配测试内存";
    
    // 初始化为0，便于检测内存污染
    std::memset(test_memory_, 0, test_memory_size_);
    
    // 创建buddy分配器实例
    buddy_ = std::make_unique<Buddy>("test_buddy", test_memory_, test_pages_);
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

  std::unique_ptr<Buddy> buddy_;
  void* test_memory_ = nullptr;
  size_t test_memory_size_ = 0;
  size_t test_pages_ = 0;
};

/**
 * @brief 测试基本的分配和释放功能
 */
TEST_F(BuddyTest, BasicAllocAndFree) {
  // 测试基本分配
  void* ptr1 = buddy_->Alloc(0);  // 分配1页 (2^0 = 1页)
  ASSERT_NE(ptr1, nullptr) << "分配1页失败";
  EXPECT_TRUE(IsInManagedRange(ptr1)) << "分配的地址不在管理范围内";
  EXPECT_TRUE(IsAligned(ptr1, 0)) << "分配的地址未正确对齐";

  void* ptr2 = buddy_->Alloc(1);  // 分配2页 (2^1 = 2页)
  ASSERT_NE(ptr2, nullptr) << "分配2页失败";
  EXPECT_TRUE(IsInManagedRange(ptr2)) << "分配的地址不在管理范围内";
  EXPECT_TRUE(IsAligned(ptr2, 1)) << "分配的地址未正确对齐";

  void* ptr3 = buddy_->Alloc(2);  // 分配4页 (2^2 = 4页)
  ASSERT_NE(ptr3, nullptr) << "分配4页失败";
  EXPECT_TRUE(IsInManagedRange(ptr3)) << "分配的地址不在管理范围内";
  EXPECT_TRUE(IsAligned(ptr3, 2)) << "分配的地址未正确对齐";

  // 检查分配的地址不重叠
  EXPECT_NE(ptr1, ptr2) << "分配的地址重叠";
  EXPECT_NE(ptr1, ptr3) << "分配的地址重叠";
  EXPECT_NE(ptr2, ptr3) << "分配的地址重叠";

  // 测试释放
  buddy_->Free(ptr1, 0);
  buddy_->Free(ptr2, 1);
  buddy_->Free(ptr3, 2);
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
  std::vector<void*> allocated_ptrs;
  
  // 持续分配直到内存耗尽
  for (int i = 0; i < 1000; ++i) {  // 防止无限循环
    void* ptr = buddy_->Alloc(0);  // 分配1页
    if (ptr == nullptr) {
      break;  // 内存耗尽
    }
    allocated_ptrs.push_back(ptr);
  }

  EXPECT_GT(allocated_ptrs.size(), 0) << "应该能分配至少一些内存";
  EXPECT_LT(allocated_ptrs.size(), test_pages_) << "分配的页数不应超过总页数";

  // 验证再次分配失败
  void* ptr = buddy_->Alloc(0);
  EXPECT_EQ(ptr, nullptr) << "内存耗尽后应该无法继续分配";

  // 释放所有内存
  for (void* allocated_ptr : allocated_ptrs) {
    buddy_->Free(allocated_ptr, 0);
  }

  // 验证释放后可以重新分配
  ptr = buddy_->Alloc(0);
  EXPECT_NE(ptr, nullptr) << "释放内存后应该能重新分配";
  buddy_->Free(ptr, 0);
}

/**
 * @brief 测试buddy合并功能
 */
TEST_F(BuddyTest, BuddyMerging) {
  // 分配两个相邻的1页块
  void* ptr1 = buddy_->Alloc(0);
  void* ptr2 = buddy_->Alloc(0);
  ASSERT_NE(ptr1, nullptr);
  ASSERT_NE(ptr2, nullptr);

  // 释放这两个块
  buddy_->Free(ptr1, 0);
  buddy_->Free(ptr2, 0);

  // 现在应该能分配一个2页的块（如果buddy合并正常工作）
  void* large_ptr = buddy_->Alloc(1);
  EXPECT_NE(large_ptr, nullptr) << "buddy合并后应该能分配更大的块";
  
  if (large_ptr) {
    buddy_->Free(large_ptr, 1);
  }
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
  void* ptr = buddy_->Alloc(0);  // 分配1页
  ASSERT_NE(ptr, nullptr);

  // 写入测试数据
  const char test_data[] = "Hello, Buddy Allocator!";
  std::memcpy(ptr, test_data, sizeof(test_data));

  // 读取并验证数据
  char read_buffer[sizeof(test_data)];
  std::memcpy(read_buffer, ptr, sizeof(test_data));
  EXPECT_STREQ(read_buffer, test_data) << "内存读写测试失败";

  buddy_->Free(ptr, 0);
}

/**
 * @brief 压力测试：随机分配和释放
 */
TEST_F(BuddyTest, StressTest) {
  std::vector<std::pair<void*, size_t>> allocated_blocks;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> order_dist(0, 3);  // order 0-3
  std::uniform_real_distribution<> action_dist(0.0, 1.0);

  const int operations = 500;  // 减少操作数以提高测试速度
  int alloc_count = 0;
  int free_count = 0;

  for (int i = 0; i < operations; ++i) {
    if (allocated_blocks.empty() || action_dist(gen) < 0.6) {
      // 60%概率分配，或者当前无已分配块时必须分配
      size_t order = order_dist(gen);
      void* ptr = buddy_->Alloc(order);
      if (ptr != nullptr) {
        allocated_blocks.emplace_back(ptr, order);
        alloc_count++;
        
        // 验证分配的内存可写
        *static_cast<char*>(ptr) = static_cast<char>('A' + (i % 26));
      }
    } else {
      // 40%概率释放
      if (!allocated_blocks.empty()) {
        std::uniform_int_distribution<> index_dist(0, allocated_blocks.size() - 1);
        size_t index = index_dist(gen);
        
        auto [ptr, order] = allocated_blocks[index];
        buddy_->Free(ptr, order);
        allocated_blocks.erase(allocated_blocks.begin() + index);
        free_count++;
      }
    }
  }

  // 清理剩余的分配块
  for (const auto& [ptr, order] : allocated_blocks) {
    buddy_->Free(ptr, order);
    free_count++;
  }

  EXPECT_GT(alloc_count, 0) << "压力测试应该进行了一些分配操作";
  EXPECT_GT(free_count, 0) << "压力测试应该进行了一些释放操作";
}

/**
 * @brief 测试不同order的分配大小
 */
TEST_F(BuddyTest, DifferentOrderSizes) {
  std::vector<std::pair<void*, size_t>> ptrs;

  // 分配不同order的内存块
  for (size_t order = 0; order <= 5; ++order) {
    void* ptr = buddy_->Alloc(order);
    if (ptr != nullptr) {
      ptrs.emplace_back(ptr, order);
      
      // 验证能够写入相应大小的数据
      size_t pages = 1 << order;
      size_t size = pages * AllocatorBase::kPageSize;
      
      // 在每页的开头和结尾写入标记
      for (size_t page = 0; page < pages; ++page) {
        char* page_start = static_cast<char*>(ptr) + page * AllocatorBase::kPageSize;
        *page_start = 'S';  // Start marker
        *(page_start + AllocatorBase::kPageSize - 1) = 'E';  // End marker
      }
    }
  }

  EXPECT_GT(ptrs.size(), 0) << "应该能分配一些不同大小的内存块";

  // 验证数据完整性并释放
  for (const auto& [ptr, order] : ptrs) {
    size_t pages = 1 << order;
    
    // 验证标记
    for (size_t page = 0; page < pages; ++page) {
      char* page_start = static_cast<char*>(ptr) + page * AllocatorBase::kPageSize;
      EXPECT_EQ(*page_start, 'S') << "起始标记被破坏，order=" << order << ", page=" << page;
      EXPECT_EQ(*(page_start + AllocatorBase::kPageSize - 1), 'E') << "结束标记被破坏，order=" << order << ", page=" << page;
    }
    
    buddy_->Free(ptr, order);
  }
}

/**
 * @brief 测试nullptr释放（应该安全）
 */
TEST_F(BuddyTest, FreeNullptr) {
  // 释放nullptr应该是安全的，不应该崩溃
  EXPECT_NO_FATAL_FAILURE(buddy_->Free(nullptr, 0));
  EXPECT_NO_FATAL_FAILURE(buddy_->Free(nullptr, 5));
}

/**
 * @brief 测试分配器构造参数验证
 */
TEST_F(BuddyTest, ConstructorValidation) {
  // 测试用很小的内存创建分配器
  size_t small_size = AllocatorBase::kPageSize * 4;  // 4页
  void* small_memory = std::aligned_alloc(AllocatorBase::kPageSize, small_size);
  ASSERT_NE(small_memory, nullptr);
  
  std::memset(small_memory, 0, small_size);
  
  // 创建小内存的buddy分配器
  auto small_buddy = std::make_unique<Buddy>("small_buddy", small_memory, 4);
  
  // 测试在小内存中的分配
  void* ptr1 = small_buddy->Alloc(0);  // 1页
  EXPECT_NE(ptr1, nullptr);
  
  void* ptr2 = small_buddy->Alloc(1);  // 2页
  EXPECT_NE(ptr2, nullptr);
  
  // 现在应该没有更多空间了
  void* ptr3 = small_buddy->Alloc(1);  // 再分配2页
  EXPECT_EQ(ptr3, nullptr) << "小内存池应该已经耗尽";
  
  // 清理
  if (ptr1) small_buddy->Free(ptr1, 0);
  if (ptr2) small_buddy->Free(ptr2, 1);
  
  small_buddy.reset();
  std::free(small_memory);
}

}  // namespace test
}  // namespace bmalloc
