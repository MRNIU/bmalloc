/**
 * Copyright The bmalloc Contributors
 * @file slab_test.cpp
 * @brief Slab分配器的Google Test测试用例
 */

#include "slab.hpp"

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

#include "buddy.hpp"

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
class SlabTest : public ::testing::Test {
 protected:
  static constexpr size_t kTestMemorySize =
      128 * 1024;  // 128KB，支持更大的测试
  static constexpr size_t kTestPages = kTestMemorySize / kPageSize;  // 32页

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

/**
 * @brief Slab 分配器实例化示例
 *
 * 这个测试展示了如何正确实例化 Slab 分配器：
 * 1. PageAllocator: 使用 Buddy<TestLogger, TestLock> 作为页级分配器
 * 2. LogFunc: 使用 TestLogger 作为日志函数
 * 3. Lock: 使用 TestLock 作为锁机制
 */
TEST_F(SlabTest, InstantiationExample) {
  // 定义具体的分配器类型
  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger, TestLock>;

  // 实例化 Slab 分配器
  MySlab slab("test_slab", test_memory_, kTestPages);

  // 验证实例化成功
  EXPECT_EQ(slab.GetFreeCount(), kTestPages);
  EXPECT_EQ(slab.GetUsedCount(), 0);

  std::cout << "Slab allocator instantiated successfully!\n";
  std::cout << "  Name: test_slab\n";
  std::cout << "  Memory: " << test_memory_ << "\n";
  std::cout << "  Pages: " << kTestPages << "\n";
  std::cout << "  Free count: " << slab.GetFreeCount() << "\n";
  std::cout << "  Used count: " << slab.GetUsedCount() << "\n";
}

/**
 * @brief 不同模板参数组合的实例化示例
 */
TEST_F(SlabTest, DifferentTemplateParameterCombinations) {
  // 1. 使用默认模板参数的简化版本
  using SimpleBuddy = Buddy<>;  // 使用默认的 std::nullptr_t 和 LockBase
  using SimpleSlab = Slab<SimpleBuddy>;  // 使用默认的 LogFunc 和 Lock

  SimpleSlab simple_slab("simple_slab", test_memory_, kTestPages);
  EXPECT_EQ(simple_slab.GetFreeCount(), kTestPages);

  // 2. 只指定日志函数，使用默认锁
  using LoggedBuddy = Buddy<TestLogger>;
  using LoggedSlab = Slab<LoggedBuddy, TestLogger>;

  LoggedSlab logged_slab("logged_slab", test_memory_, kTestPages);
  EXPECT_EQ(logged_slab.GetFreeCount(), kTestPages);

  // 3. 完整指定所有模板参数
  using FullBuddy = Buddy<TestLogger>;
  using FullSlab = Slab<FullBuddy, TestLogger, TestLock>;

  FullSlab full_slab("full_slab", test_memory_, kTestPages);
  EXPECT_EQ(full_slab.GetFreeCount(), kTestPages);

  std::cout << "All template parameter combinations work correctly!\n";
}

/**
 * @brief 测试 find_create_kmem_cache 函数
 */
TEST_F(SlabTest, KmemCacheCreateTest) {
  // 使用简单的配置
  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger>;

  MySlab slab("test_slab", test_memory_, kTestPages);

  // 测试构造函数和析构函数
  auto ctor = [](void* ptr) {
    // 简单的构造函数，将内存初始化为0
    memset(ptr, 0, sizeof(int));
  };

  auto dtor = [](void* ptr) {
    // 简单的析构函数
    (void)ptr;  // 避免未使用参数警告
  };

  // 1. 测试正常创建 cache
  auto cache1 = slab.find_create_kmem_cache("test_cache_1", sizeof(int), ctor, dtor);
  ASSERT_NE(cache1, nullptr);
  EXPECT_STREQ(cache1->name_, "test_cache_1");
  EXPECT_EQ(cache1->objectSize_, sizeof(int));
  EXPECT_EQ(cache1->ctor_, ctor);
  EXPECT_EQ(cache1->dtor_, dtor);
  EXPECT_EQ(cache1->num_active_, 0);
  EXPECT_GT(cache1->objectsInSlab_, 0);
  EXPECT_EQ(cache1->error_code_, 0);

  // 2. 测试创建不同大小的 cache
  auto cache2 =
      slab.find_create_kmem_cache("test_cache_2", sizeof(double), nullptr, nullptr);
  ASSERT_NE(cache2, nullptr);
  EXPECT_STREQ(cache2->name_, "test_cache_2");
  EXPECT_EQ(cache2->objectSize_, sizeof(double));
  EXPECT_EQ(cache2->ctor_, nullptr);
  EXPECT_EQ(cache2->dtor_, nullptr);

  // 3. 测试创建大对象的 cache（需要更高的 order）
  auto cache3 = slab.find_create_kmem_cache("large_cache", 8192, nullptr, nullptr);
  ASSERT_NE(cache3, nullptr);
  EXPECT_STREQ(cache3->name_, "large_cache");
  EXPECT_EQ(cache3->objectSize_, 8192);
  EXPECT_GT(cache3->order_, 0);  // 大对象需要更高的 order

  // 4. 测试重复创建相同的 cache（应该返回已存在的）
  auto cache1_duplicate =
      slab.find_create_kmem_cache("test_cache_1", sizeof(int), ctor, dtor);
  EXPECT_EQ(cache1_duplicate, cache1);  // 应该返回相同的指针

  // 5. 测试错误情况：空名称
  auto invalid_cache1 =
      slab.find_create_kmem_cache("", sizeof(int), nullptr, nullptr);
  EXPECT_EQ(invalid_cache1, nullptr);
  // 由于 cache_cache 是 protected，我们通过返回值判断错误

  // 6. 测试错误情况：nullptr 名称
  auto invalid_cache2 =
      slab.find_create_kmem_cache(nullptr, sizeof(int), nullptr, nullptr);
  EXPECT_EQ(invalid_cache2, nullptr);

  // 7. 测试错误情况：非法大小
  auto invalid_cache3 =
      slab.find_create_kmem_cache("invalid_size", 0, nullptr, nullptr);
  EXPECT_EQ(invalid_cache3, nullptr);

  // 8. 测试错误情况：尝试创建与 cache_cache 同名的 cache
  auto invalid_cache4 =
      slab.find_create_kmem_cache("kmem_cache", sizeof(int), nullptr, nullptr);
  EXPECT_EQ(invalid_cache4, nullptr);

  std::cout << "find_create_kmem_cache tests completed successfully!\n";
  std::cout << "Created caches:\n";
  std::cout << "  - " << cache1->name_ << " (size: " << cache1->objectSize_
            << ", objects per slab: " << cache1->objectsInSlab_ << ")\n";
  std::cout << "  - " << cache2->name_ << " (size: " << cache2->objectSize_
            << ", objects per slab: " << cache2->objectsInSlab_ << ")\n";
  std::cout << "  - " << cache3->name_ << " (size: " << cache3->objectSize_
            << ", order: " << cache3->order_ << ")\n";
}

/**
 * @brief 测试 kmem_cache_shrink 函数
 */
TEST_F(SlabTest, KmemCacheShrinkTest) {
  // 使用简单的配置
  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger>;

  MySlab slab("test_slab", test_memory_, kTestPages);

  // 1. 测试对 nullptr 的处理
  int result1 = slab.kmem_cache_shrink(nullptr);
  EXPECT_EQ(result1, 0);  // 对 nullptr 应该返回 0

  // 2. 创建一个测试缓存
  auto cache = slab.find_create_kmem_cache("shrink_test_cache", sizeof(int), nullptr,
                                      nullptr);
  ASSERT_NE(cache, nullptr);

  std::cout << "Initial cache state:\n";
  std::cout << "  - num_allocations_: " << cache->num_allocations_ << "\n";
  std::cout << "  - slabs_free_: "
            << (cache->slabs_free_ ? "has slabs" : "nullptr") << "\n";
  std::cout << "  - growing_: " << (cache->growing_ ? "true" : "false") << "\n";

  // 3. 测试没有空闲 slab 时的收缩（新创建的cache没有分配slab）
  int result2 = slab.kmem_cache_shrink(cache);
  EXPECT_GE(result2, 0);  // 应该返回非负值

  // 验证 growing_ 标志被重置
  EXPECT_FALSE(cache->growing_);

  std::cout << "After first shrink:\n";
  std::cout << "  - blocks freed: " << result2 << "\n";
  std::cout << "  - num_allocations_: " << cache->num_allocations_ << "\n";
  std::cout << "  - growing_: " << (cache->growing_ ? "true" : "false") << "\n";

  // 4. 设置 growing_ 标志并测试
  cache->growing_ = true;
  int result3 = slab.kmem_cache_shrink(cache);
  EXPECT_GE(result3, 0);
  EXPECT_FALSE(cache->growing_);  // growing_ 标志应该被重置

  // 5. 测试多次收缩
  int result4 = slab.kmem_cache_shrink(cache);
  EXPECT_GE(result4, 0);

  int result5 = slab.kmem_cache_shrink(cache);
  EXPECT_GE(result5, 0);

  std::cout << "Multiple shrink operations completed:\n";
  std::cout << "  - 2nd shrink freed: " << result4 << " blocks\n";
  std::cout << "  - 3rd shrink freed: " << result5 << " blocks\n";

  // 6. 验证缓存仍然有效
  EXPECT_STREQ(cache->name_, "shrink_test_cache");
  EXPECT_EQ(cache->objectSize_, sizeof(int));
  EXPECT_GT(cache->objectsInSlab_, 0);

  // 7. 创建另一个大对象的缓存来测试不同的 order
  auto large_cache =
      slab.find_create_kmem_cache("large_shrink_test", 4096, nullptr, nullptr);
  ASSERT_NE(large_cache, nullptr);

  std::cout << "Large cache created:\n";
  std::cout << "  - order: " << large_cache->order_ << "\n";
  std::cout << "  - objects per slab: " << large_cache->objectsInSlab_ << "\n";

  int large_result = slab.kmem_cache_shrink(large_cache);
  EXPECT_GE(large_result, 0);

  std::cout << "Large cache shrink result: " << large_result
            << " blocks freed\n";

  std::cout << "kmem_cache_shrink tests completed successfully!\n";
}

/**
 * @brief 测试 kmem_cache_alloc 函数
 */
TEST_F(SlabTest, KmemCacheAllocTest) {
  // 使用简单的配置
  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger>;

  MySlab slab("test_slab", test_memory_, kTestPages);

  // 1. 测试对 nullptr 的处理
  void* result1 = slab.kmem_cache_alloc(nullptr);
  EXPECT_EQ(result1, nullptr);

  // 2. 创建一个测试缓存
  auto cache =
      slab.find_create_kmem_cache("alloc_test_cache", sizeof(int), nullptr, nullptr);
  ASSERT_NE(cache, nullptr);

  std::cout << "Initial cache state:\n";
  std::cout << "  - num_active_: " << cache->num_active_ << "\n";
  std::cout << "  - num_allocations_: " << cache->num_allocations_ << "\n";
  std::cout << "  - objects per slab: " << cache->objectsInSlab_ << "\n";
  std::cout << "  - slabs_free_: "
            << (cache->slabs_free_ ? "has slabs" : "nullptr") << "\n";
  std::cout << "  - slabs_partial_: "
            << (cache->slabs_partial_ ? "has slabs" : "nullptr") << "\n";
  std::cout << "  - slabs_full_: "
            << (cache->slabs_full_ ? "has slabs" : "nullptr") << "\n";

  // 3. 测试第一次分配（需要创建新slab）
  void* obj1 = slab.kmem_cache_alloc(cache);
  ASSERT_NE(obj1, nullptr);
  EXPECT_EQ(cache->num_active_, 1);
  EXPECT_GT(cache->num_allocations_, 0);
  EXPECT_TRUE(cache->growing_);  // 第一次分配会设置growing标志

  std::cout << "After first allocation:\n";
  std::cout << "  - num_active_: " << cache->num_active_ << "\n";
  std::cout << "  - num_allocations_: " << cache->num_allocations_ << "\n";
  std::cout << "  - object address: " << obj1 << "\n";
  std::cout << "  - growing_: " << (cache->growing_ ? "true" : "false") << "\n";

  // 4. 测试后续分配（使用现有slab）
  void* obj2 = slab.kmem_cache_alloc(cache);
  ASSERT_NE(obj2, nullptr);
  EXPECT_NE(obj1, obj2);  // 应该是不同的地址
  EXPECT_EQ(cache->num_active_, 2);

  void* obj3 = slab.kmem_cache_alloc(cache);
  ASSERT_NE(obj3, nullptr);
  EXPECT_NE(obj1, obj3);
  EXPECT_NE(obj2, obj3);
  EXPECT_EQ(cache->num_active_, 3);

  std::cout << "After multiple allocations:\n";
  std::cout << "  - num_active_: " << cache->num_active_ << "\n";
  std::cout << "  - obj1: " << obj1 << "\n";
  std::cout << "  - obj2: " << obj2 << "\n";
  std::cout << "  - obj3: " << obj3 << "\n";

  // 5. 验证对象地址对齐（应该按objectSize对齐）
  uintptr_t addr1 = reinterpret_cast<uintptr_t>(obj1);
  uintptr_t addr2 = reinterpret_cast<uintptr_t>(obj2);
  uintptr_t addr3 = reinterpret_cast<uintptr_t>(obj3);

  // 检查地址差值是否为objectSize的倍数
  EXPECT_EQ((addr2 - addr1) % cache->objectSize_, 0);
  EXPECT_EQ((addr3 - addr2) % cache->objectSize_, 0);

  // 6. 测试带构造函数的缓存
  static int ctor_call_count = 0;
  auto ctor = +[](void* ptr) {
    ctor_call_count++;
    *(int*)ptr = 42;  // 初始化为42
  };

  auto cache_with_ctor =
      slab.find_create_kmem_cache("ctor_cache", sizeof(int), ctor, nullptr);
  ASSERT_NE(cache_with_ctor, nullptr);

  int initial_ctor_calls = ctor_call_count;
  void* ctor_obj = slab.kmem_cache_alloc(cache_with_ctor);
  ASSERT_NE(ctor_obj, nullptr);

  // 验证构造函数被调用
  EXPECT_GT(ctor_call_count, initial_ctor_calls);

  std::cout << "Constructor cache test:\n";
  std::cout << "  - constructor calls: " << ctor_call_count << "\n";
  std::cout << "  - allocated object: " << ctor_obj << "\n";

  // 7. 测试大对象分配
  auto large_cache =
      slab.find_create_kmem_cache("large_alloc_test", 2048, nullptr, nullptr);
  ASSERT_NE(large_cache, nullptr);

  void* large_obj1 = slab.kmem_cache_alloc(large_cache);
  ASSERT_NE(large_obj1, nullptr);

  void* large_obj2 = slab.kmem_cache_alloc(large_cache);
  ASSERT_NE(large_obj2, nullptr);
  EXPECT_NE(large_obj1, large_obj2);

  std::cout << "Large object allocation:\n";
  std::cout << "  - cache order: " << large_cache->order_ << "\n";
  std::cout << "  - objects per slab: " << large_cache->objectsInSlab_ << "\n";
  std::cout << "  - large_obj1: " << large_obj1 << "\n";
  std::cout << "  - large_obj2: " << large_obj2 << "\n";

  // 8. 测试分配直到slab满的情况
  std::vector<void*> allocated_objects;

  // 清空之前的分配并重新创建缓存
  auto full_test_cache = slab.find_create_kmem_cache(
      "full_test_cache", sizeof(double), nullptr, nullptr);
  ASSERT_NE(full_test_cache, nullptr);

  // 分配所有可能的对象
  for (size_t i = 0; i < full_test_cache->objectsInSlab_; i++) {
    void* obj = slab.kmem_cache_alloc(full_test_cache);
    ASSERT_NE(obj, nullptr);
    allocated_objects.push_back(obj);
  }

  EXPECT_EQ(full_test_cache->num_active_, full_test_cache->objectsInSlab_);
  std::cout << "Filled slab test:\n";
  std::cout << "  - allocated " << allocated_objects.size() << " objects\n";
  std::cout << "  - cache num_active_: " << full_test_cache->num_active_
            << "\n";

  // 9. 再次分配应该创建新的slab
  void* overflow_obj = slab.kmem_cache_alloc(full_test_cache);
  ASSERT_NE(overflow_obj, nullptr);
  EXPECT_EQ(full_test_cache->num_active_, full_test_cache->objectsInSlab_ + 1);

  std::cout << "Overflow allocation:\n";
  std::cout << "  - overflow object: " << overflow_obj << "\n";
  std::cout << "  - new num_active_: " << full_test_cache->num_active_ << "\n";

  std::cout << "kmem_cache_alloc tests completed successfully!\n";
}

/**
 * @brief 测试 kmem_cache_free 函数
 */
TEST_F(SlabTest, KmemCacheFreeTest) {
  // 使用简单的配置
  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger>;

  MySlab slab("test_slab", test_memory_, kTestPages);

  // 1. 测试对 nullptr 的处理
  auto cache =
      slab.find_create_kmem_cache("free_test_cache", sizeof(int), nullptr, nullptr);
  ASSERT_NE(cache, nullptr);

  // 测试 nullptr cache
  slab.kmem_cache_free(nullptr, (void*)0x1000);  // 应该安全返回

  // 测试 nullptr object
  slab.kmem_cache_free(cache, nullptr);  // 应该安全返回

  std::cout << "nullptr handling tests passed\n";

  // 2. 测试基本的分配和释放
  void* obj1 = slab.kmem_cache_alloc(cache);
  ASSERT_NE(obj1, nullptr);
  EXPECT_EQ(cache->num_active_, 1);

  std::cout << "Before free: num_active_ = " << cache->num_active_ << "\n";

  // 释放对象
  slab.kmem_cache_free(cache, obj1);
  EXPECT_EQ(cache->num_active_, 0);
  EXPECT_EQ(cache->error_code_, 0);  // 没有错误

  std::cout << "After free: num_active_ = " << cache->num_active_ << "\n";

  // 3. 测试多个对象的分配和释放
  std::vector<void*> objects;
  const size_t num_objects = 10;

  // 分配多个对象
  for (size_t i = 0; i < num_objects; i++) {
    void* obj = slab.kmem_cache_alloc(cache);
    ASSERT_NE(obj, nullptr);
    objects.push_back(obj);
  }

  EXPECT_EQ(cache->num_active_, num_objects);
  std::cout << "Allocated " << num_objects
            << " objects, num_active_ = " << cache->num_active_ << "\n";

  // 释放一半对象
  for (size_t i = 0; i < num_objects / 2; i++) {
    slab.kmem_cache_free(cache, objects[i]);
  }

  EXPECT_EQ(cache->num_active_, num_objects - num_objects / 2);
  std::cout << "After freeing half: num_active_ = " << cache->num_active_
            << "\n";

  // 释放剩余对象
  for (size_t i = num_objects / 2; i < num_objects; i++) {
    slab.kmem_cache_free(cache, objects[i]);
  }

  EXPECT_EQ(cache->num_active_, 0);
  std::cout << "After freeing all: num_active_ = " << cache->num_active_
            << "\n";

  // 4. 测试带析构函数的缓存
  static int dtor_call_count = 0;
  auto dtor = +[](void* ptr) {
    dtor_call_count++;
    *(int*)ptr = -1;  // 标记为已析构
  };

  auto cache_with_dtor =
      slab.find_create_kmem_cache("dtor_cache", sizeof(int), nullptr, dtor);
  ASSERT_NE(cache_with_dtor, nullptr);

  void* dtor_obj = slab.kmem_cache_alloc(cache_with_dtor);
  ASSERT_NE(dtor_obj, nullptr);
  *(int*)dtor_obj = 123;  // 设置值

  int initial_dtor_calls = dtor_call_count;
  slab.kmem_cache_free(cache_with_dtor, dtor_obj);

  // 验证析构函数被调用
  EXPECT_GT(dtor_call_count, initial_dtor_calls);
  EXPECT_EQ(*(int*)dtor_obj, -1);  // 验证析构函数修改了值

  std::cout << "Destructor test: calls = " << dtor_call_count
            << ", value = " << *(int*)dtor_obj << "\n";

  // 5. 测试 slab 状态转换（full -> partial -> free）
  auto transition_cache = slab.find_create_kmem_cache(
      "transition_cache", sizeof(double), nullptr, nullptr);
  ASSERT_NE(transition_cache, nullptr);

  // 分配足够的对象填满一个slab
  std::vector<void*> transition_objects;
  for (size_t i = 0; i < transition_cache->objectsInSlab_; i++) {
    void* obj = slab.kmem_cache_alloc(transition_cache);
    ASSERT_NE(obj, nullptr);
    transition_objects.push_back(obj);
  }

  EXPECT_EQ(transition_cache->num_active_, transition_cache->objectsInSlab_);
  std::cout << "Filled slab: num_active_ = " << transition_cache->num_active_
            << "\n";

  // 释放一个对象（full -> partial）
  slab.kmem_cache_free(transition_cache, transition_objects[0]);
  EXPECT_EQ(transition_cache->num_active_,
            transition_cache->objectsInSlab_ - 1);

  // 释放所有剩余对象（partial -> free）
  for (size_t i = 1; i < transition_objects.size(); i++) {
    slab.kmem_cache_free(transition_cache, transition_objects[i]);
  }

  EXPECT_EQ(transition_cache->num_active_, 0);
  std::cout << "After freeing all transition objects: num_active_ = "
            << transition_cache->num_active_ << "\n";

  // 6. 测试重复分配和释放（内存复用）
  void* reuse_obj1 = slab.kmem_cache_alloc(cache);
  void* reuse_obj2 = slab.kmem_cache_alloc(cache);
  ASSERT_NE(reuse_obj1, nullptr);
  ASSERT_NE(reuse_obj2, nullptr);

  slab.kmem_cache_free(cache, reuse_obj1);
  slab.kmem_cache_free(cache, reuse_obj2);

  // 再次分配，应该重用之前的内存
  void* reuse_obj3 = slab.kmem_cache_alloc(cache);
  void* reuse_obj4 = slab.kmem_cache_alloc(cache);

  ASSERT_NE(reuse_obj3, nullptr);
  ASSERT_NE(reuse_obj4, nullptr);

  // 地址应该匹配之前释放的对象之一
  EXPECT_TRUE(reuse_obj3 == reuse_obj1 || reuse_obj3 == reuse_obj2);
  EXPECT_TRUE(reuse_obj4 == reuse_obj1 || reuse_obj4 == reuse_obj2);
  EXPECT_NE(reuse_obj3, reuse_obj4);

  std::cout << "Memory reuse test:\n";
  std::cout << "  - original obj1: " << reuse_obj1 << ", obj2: " << reuse_obj2
            << "\n";
  std::cout << "  - reused obj3: " << reuse_obj3 << ", obj4: " << reuse_obj4
            << "\n";

  // 清理
  slab.kmem_cache_free(cache, reuse_obj3);
  slab.kmem_cache_free(cache, reuse_obj4);

  // 7. 测试错误情况：释放无效对象
  void* invalid_obj = malloc(sizeof(int));  // 不是从slab分配的对象

  slab.kmem_cache_free(cache, invalid_obj);
  EXPECT_NE(cache->error_code_, 0);  // 应该有错误

  std::cout << "Invalid object free test: error_code_ = " << cache->error_code_
            << "\n";

  free(invalid_obj);

  // 8. 测试大对象的释放
  auto large_cache =
      slab.find_create_kmem_cache("large_free_test", 1024, nullptr, nullptr);
  ASSERT_NE(large_cache, nullptr);

  void* large_obj = slab.kmem_cache_alloc(large_cache);
  ASSERT_NE(large_obj, nullptr);
  EXPECT_EQ(large_cache->num_active_, 1);

  slab.kmem_cache_free(large_cache, large_obj);
  EXPECT_EQ(large_cache->num_active_, 0);
  EXPECT_EQ(large_cache->error_code_, 0);

  std::cout << "Large object free test completed\n";

  std::cout << "kmem_cache_free tests completed successfully!\n";
}

/**
 * @brief 测试 Alloc 功能
 *
 * 测试内容：
 * 1. 基本内存分配测试
 * 2. 不同大小的内存分配
 * 3. 边界条件测试
 * 4. 内存对齐测试
 * 5. 大量分配测试
 */
TEST_F(SlabTest, KmallocTest) {
  std::cout << "\n=== Starting Alloc tests ===\n";

  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger, TestLock>;

  MySlab slab("slab_kmalloc_test", test_memory_, kTestPages);

  // 1. 基本内存分配测试
  std::cout << "1. Basic allocation test\n";
  void* ptr32 = slab.Alloc(32);
  ASSERT_NE(ptr32, nullptr) << "Failed to allocate 32 bytes";

  void* ptr64 = slab.Alloc(64);
  ASSERT_NE(ptr64, nullptr) << "Failed to allocate 64 bytes";

  void* ptr128 = slab.Alloc(128);
  ASSERT_NE(ptr128, nullptr) << "Failed to allocate 128 bytes";

  // 验证分配的地址不同
  EXPECT_NE(ptr32, ptr64);
  EXPECT_NE(ptr64, ptr128);
  EXPECT_NE(ptr32, ptr128);

  std::cout << "Basic allocation test passed\n";

  // 2. 测试不同大小的内存分配（2的幂次方对齐）
  std::cout << "2. Power-of-2 alignment test\n";

  // 测试33字节应该分配到64字节的cache
  void* ptr33 = slab.Alloc(33);
  ASSERT_NE(ptr33, nullptr) << "Failed to allocate 33 bytes";

  // 测试65字节应该分配到128字节的cache
  void* ptr65 = slab.Alloc(65);
  ASSERT_NE(ptr65, nullptr) << "Failed to allocate 65 bytes";

  // 测试129字节应该分配到256字节的cache
  void* ptr129 = slab.Alloc(129);
  ASSERT_NE(ptr129, nullptr) << "Failed to allocate 129 bytes";

  std::cout << "Power-of-2 alignment test passed\n";

  // 3. 边界条件测试
  std::cout << "3. Boundary condition test\n";

  // 测试最小大小（32字节）
  void* ptr_min = slab.Alloc(32);
  ASSERT_NE(ptr_min, nullptr) << "Failed to allocate minimum size (32 bytes)";

  // 测试最大大小（使用较小的大小，如32KB）
  void* ptr_max = slab.Alloc(32768);
  ASSERT_NE(ptr_max, nullptr)
      << "Failed to allocate maximum test size (32768 bytes)";

  // 测试小于最小大小
  void* ptr_too_small = slab.Alloc(16);
  EXPECT_EQ(ptr_too_small, nullptr)
      << "Should fail to allocate size < 32 bytes";

  // 测试大于最大大小
  void* ptr_too_large = slab.Alloc(131073);
  EXPECT_EQ(ptr_too_large, nullptr)
      << "Should fail to allocate size > 131072 bytes";

  // 测试0大小
  void* ptr_zero = slab.Alloc(0);
  EXPECT_EQ(ptr_zero, nullptr) << "Should fail to allocate 0 bytes";

  std::cout << "Boundary condition test passed\n";

  // 4. 内存写入测试
  std::cout << "4. Memory write test\n";

  void* test_ptr = slab.Alloc(256);
  ASSERT_NE(test_ptr, nullptr) << "Failed to allocate 256 bytes for write test";

  // 写入测试数据
  uint8_t* byte_ptr = static_cast<uint8_t*>(test_ptr);
  for (int i = 0; i < 256; ++i) {
    byte_ptr[i] = static_cast<uint8_t>(i & 0xFF);
  }

  // 验证数据
  for (int i = 0; i < 256; ++i) {
    EXPECT_EQ(byte_ptr[i], static_cast<uint8_t>(i & 0xFF))
        << "Memory corruption at offset " << i;
  }

  std::cout << "Memory write test passed\n";

  // 5. 大量分配测试（使用较小的数量）
  std::cout << "5. Bulk allocation test\n";

  std::vector<void*> ptrs;
  const int num_allocs = 20;  // 减少分配数量

  for (int i = 0; i < num_allocs; ++i) {
    size_t size = 64 + (i % 4) * 64;  // 64, 128, 192, 256 字节（减少变化）
    void* ptr = slab.Alloc(size);
    if (ptr != nullptr) {
      ptrs.push_back(ptr);
    }
  }

  EXPECT_GT(ptrs.size(), 0) << "Should allocate at least some memory blocks";
  std::cout << "Successfully allocated " << ptrs.size() << " out of "
            << num_allocs << " requested blocks\n";

  // 验证所有指针都是唯一的
  std::set<void*> unique_ptrs(ptrs.begin(), ptrs.end());
  EXPECT_EQ(unique_ptrs.size(), ptrs.size())
      << "All allocated pointers should be unique";

  std::cout << "Bulk allocation test passed\n";

  // 6. 常用大小分配测试（减少测试大小）
  std::cout << "6. Common size allocation test\n";

  std::vector<size_t> common_sizes = {32, 64, 128, 256, 512};  // 移除大的分配
  std::vector<void*> common_ptrs;

  for (size_t size : common_sizes) {
    void* ptr = slab.Alloc(size);
    if (ptr != nullptr) {
      common_ptrs.push_back(ptr);
      // 写入一些数据验证内存可用
      memset(ptr, 0xAA, size);
    } else {
      std::cout << "Warning: Failed to allocate " << size
                << " bytes (may be out of memory)\n";
    }
  }

  EXPECT_GT(common_ptrs.size(), 0)
      << "Should allocate at least some common sizes";

  // 验证数据完整性（只对成功分配的内存）
  for (size_t i = 0; i < common_ptrs.size() && i < common_sizes.size(); ++i) {
    uint8_t* byte_ptr = static_cast<uint8_t*>(common_ptrs[i]);
    for (size_t j = 0; j < common_sizes[i]; ++j) {
      EXPECT_EQ(byte_ptr[j], 0xAA)
          << "Data corruption in " << common_sizes[i] << " byte allocation";
    }
  }

  std::cout << "Common size allocation test passed\n";

  std::cout << "=== Alloc tests completed successfully! ===\n";
}

/**
 * @brief 测试 find_buffers_cache 功能
 *
 * 测试内容：
 * 1. 查找有效的 Alloc 分配的对象
 * 2. 查找无效的指针
 * 3. 查找不同大小缓存中的对象
 * 4. 边界条件测试
 */
TEST_F(SlabTest, FindBuffersCacheTest) {
  std::cout << "\n=== Starting find_buffers_cache tests ===\n";

  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger, TestLock>;

  MySlab slab("slab_find_test", test_memory_, kTestPages);

  // 1. 测试查找通过 Alloc 分配的对象
  std::cout << "1. Basic find_buffers_cache test\n";

  void* ptr64 = slab.Alloc(64);
  ASSERT_NE(ptr64, nullptr) << "Failed to allocate 64 bytes";

  auto* cache64 = slab.find_buffers_cache(ptr64);
  ASSERT_NE(cache64, nullptr)
      << "Should find cache for valid 64-byte allocation";

  // 验证找到的缓存名称包含 "size-"
  EXPECT_NE(strstr(cache64->name_, "size-"), nullptr)
      << "Cache name should contain 'size-'";

  std::cout << "Found cache: " << cache64->name_ << " for 64-byte allocation\n";

  void* ptr128 = slab.Alloc(128);
  ASSERT_NE(ptr128, nullptr) << "Failed to allocate 128 bytes";

  auto* cache128 = slab.find_buffers_cache(ptr128);
  ASSERT_NE(cache128, nullptr)
      << "Should find cache for valid 128-byte allocation";

  std::cout << "Found cache: " << cache128->name_
            << " for 128-byte allocation\n";

  // 验证不同大小的对象找到不同的缓存（除非它们使用相同的2的幂大小）
  if (strcmp(cache64->name_, cache128->name_) != 0) {
    std::cout << "Different caches for different sizes (as expected)\n";
  } else {
    std::cout << "Same cache used for both sizes (power-of-2 alignment)\n";
  }

  std::cout << "Basic find_buffers_cache test passed\n";

  // 2. 测试查找无效指针
  std::cout << "2. Invalid pointer test\n";

  // 测试 nullptr
  auto* cache_null = slab.find_buffers_cache(nullptr);
  EXPECT_EQ(cache_null, nullptr) << "Should not find cache for nullptr";

  // 测试随机无效指针
  void* invalid_ptr = reinterpret_cast<void*>(0x12345678);
  auto* cache_invalid = slab.find_buffers_cache(invalid_ptr);
  EXPECT_EQ(cache_invalid, nullptr)
      << "Should not find cache for invalid pointer";

  // 测试指向测试内存范围外的指针
  char* out_of_range =
      static_cast<char*>(test_memory_) + kTestMemorySize + 1000;
  auto* cache_out = slab.find_buffers_cache(out_of_range);
  EXPECT_EQ(cache_out, nullptr)
      << "Should not find cache for out-of-range pointer";

  std::cout << "Invalid pointer test passed\n";

  // 3. 测试多个不同大小的分配
  std::cout << "3. Multiple allocation sizes test\n";

  std::vector<std::pair<void*, size_t>> allocations;
  std::vector<size_t> test_sizes = {32, 64, 96, 128, 200, 256, 500, 512};

  for (size_t size : test_sizes) {
    void* ptr = slab.Alloc(size);
    if (ptr != nullptr) {
      allocations.push_back({ptr, size});
    }
  }

  EXPECT_GT(allocations.size(), 0)
      << "Should allocate at least some test objects";

  // 验证每个分配都能找到对应的缓存
  for (const auto& [ptr, size] : allocations) {
    auto* cache = slab.find_buffers_cache(ptr);
    ASSERT_NE(cache, nullptr)
        << "Should find cache for " << size << "-byte allocation";

    // 验证缓存名称
    EXPECT_NE(strstr(cache->name_, "size-"), nullptr)
        << "Cache name should contain 'size-' for " << size
        << "-byte allocation";

    std::cout << "Size " << size << " -> Cache: " << cache->name_ << "\n";
  }

  std::cout << "Multiple allocation sizes test passed\n";

  // 4. 测试指针边界
  std::cout << "4. Pointer boundary test\n";

  void* test_ptr = slab.Alloc(256);
  ASSERT_NE(test_ptr, nullptr) << "Failed to allocate test object";

  auto* found_cache = slab.find_buffers_cache(test_ptr);
  ASSERT_NE(found_cache, nullptr) << "Should find cache for test object";

  // 测试指向对象内部的指针
  char* byte_ptr = static_cast<char*>(test_ptr);
  auto* cache_internal1 = slab.find_buffers_cache(byte_ptr + 1);
  auto* cache_internal2 = slab.find_buffers_cache(byte_ptr + 100);

  // 根据实现，这些可能返回相同的缓存或nullptr（取决于具体的边界检查逻辑）
  if (cache_internal1 != nullptr) {
    EXPECT_EQ(cache_internal1, found_cache)
        << "Internal pointer should find same cache";
    std::cout << "Internal pointer +1 found same cache\n";
  } else {
    std::cout << "Internal pointer +1 not found (strict boundary check)\n";
  }

  if (cache_internal2 != nullptr) {
    EXPECT_EQ(cache_internal2, found_cache)
        << "Internal pointer should find same cache";
    std::cout << "Internal pointer +100 found same cache\n";
  } else {
    std::cout << "Internal pointer +100 not found (strict boundary check)\n";
  }

  std::cout << "Pointer boundary test passed\n";

  // 5. 测试相同大小的多个分配
  std::cout << "5. Same size multiple allocations test\n";

  std::vector<void*> same_size_ptrs;
  const size_t alloc_size = 128;
  const int num_same_size = 5;

  for (int i = 0; i < num_same_size; ++i) {
    void* ptr = slab.Alloc(alloc_size);
    if (ptr != nullptr) {
      same_size_ptrs.push_back(ptr);
    }
  }

  EXPECT_GT(same_size_ptrs.size(), 0)
      << "Should allocate at least some same-size objects";

  // 验证所有相同大小的分配都找到缓存（可能是不同的缓存实例）
  auto* first_cache =
      static_cast<decltype(slab.find_buffers_cache(nullptr))>(nullptr);
  std::set<decltype(first_cache)> unique_caches;

  for (size_t i = 0; i < same_size_ptrs.size(); ++i) {
    auto* cache = slab.find_buffers_cache(same_size_ptrs[i]);
    ASSERT_NE(cache, nullptr) << "Should find cache for allocation " << i;

    if (first_cache == nullptr) {
      first_cache = cache;
    }
    unique_caches.insert(cache);
  }

  if (first_cache != nullptr) {
    if (unique_caches.size() == 1) {
      std::cout << "All " << same_size_ptrs.size()
                << " same-size allocations use same cache: "
                << first_cache->name_ << "\n";
    } else {
      std::cout << "All " << same_size_ptrs.size()
                << " same-size allocations found caches ("
                << unique_caches.size() << " unique cache instances for size "
                << alloc_size << ")\n";
    }
  }

  std::cout << "Same size multiple allocations test passed\n";

  std::cout << "=== find_buffers_cache tests completed successfully! ===\n";
}

/**
 * @brief 测试 Free 功能
 *
 * 测试内容：
 * 1. 基本释放测试
 * 2. 释放 nullptr 测试
 * 3. 释放无效指针测试
 * 4. 分配后立即释放测试
 * 5. 多次分配和释放测试
 * 6. 释放后的内存状态验证
 */
TEST_F(SlabTest, KfreeTest) {
  std::cout << "\n=== Starting Free tests ===\n";

  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger, TestLock>;

  MySlab slab("slab_kfree_test", test_memory_, kTestPages);

  // 1. 基本释放测试
  std::cout << "1. Basic Free test\n";

  void* ptr64 = slab.Alloc(64);
  ASSERT_NE(ptr64, nullptr) << "Failed to allocate 64 bytes";

  // 写入一些数据验证内存可用
  memset(ptr64, 0xAA, 64);

  // 验证能找到对应的缓存
  auto* cache_before = slab.find_buffers_cache(ptr64);
  ASSERT_NE(cache_before, nullptr) << "Should find cache before Free";

  // 记录释放前的状态
  auto active_before = cache_before->num_active_;

  // 释放内存
  slab.Free(ptr64);

  // 验证释放后缓存的活跃对象数量减少
  EXPECT_LT(cache_before->num_active_, active_before)
      << "Active count should decrease after Free";

  std::cout << "Active objects before: " << active_before
            << ", after: " << cache_before->num_active_ << "\n";
  std::cout << "Basic Free test passed\n";

  // 2. 释放 nullptr 测试
  std::cout << "2. Free nullptr test\n";

  // 这应该不会崩溃或产生错误
  slab.Free(nullptr);

  std::cout << "Free nullptr test passed\n";

  // 3. 释放无效指针测试
  std::cout << "3. Free invalid pointer test\n";

  // 测试无效指针（不会崩溃，但也不会释放任何内存）
  void* invalid_ptr = reinterpret_cast<void*>(0x12345678);
  slab.Free(invalid_ptr);

  // 测试指向测试内存范围外的指针
  char* out_of_range =
      static_cast<char*>(test_memory_) + kTestMemorySize + 1000;
  slab.Free(out_of_range);

  std::cout << "Free invalid pointer test passed\n";

  // 4. 分配后立即释放测试
  std::cout << "4. Allocate and immediate free test\n";

  std::vector<size_t> test_sizes = {32, 64, 128, 256, 512, 1024};

  for (size_t size : test_sizes) {
    void* ptr = slab.Alloc(size);
    if (ptr != nullptr) {
      // 写入数据验证内存可用
      memset(ptr, static_cast<int>(size & 0xFF), size);

      // 立即释放
      slab.Free(ptr);

      std::cout << "Successfully allocated and freed " << size << " bytes\n";
    } else {
      std::cout << "Warning: Failed to allocate " << size << " bytes\n";
    }
  }

  std::cout << "Allocate and immediate free test passed\n";

  // 5. 多次分配和释放测试
  std::cout << "5. Multiple allocate/free cycles test\n";

  std::vector<void*> ptrs;
  const int num_cycles = 10;
  const size_t alloc_size = 128;

  for (int cycle = 0; cycle < num_cycles; ++cycle) {
    // 分配一些内存
    for (int i = 0; i < 5; ++i) {
      void* ptr = slab.Alloc(alloc_size);
      if (ptr != nullptr) {
        ptrs.push_back(ptr);
        // 写入数据
        memset(ptr, cycle + i, alloc_size);
      }
    }

    // 释放一半内存
    size_t to_free = ptrs.size() / 2;
    for (size_t i = 0; i < to_free; ++i) {
      slab.Free(ptrs.back());
      ptrs.pop_back();
    }
  }

  // 释放剩余的所有内存
  for (void* ptr : ptrs) {
    slab.Free(ptr);
  }

  std::cout << "Completed " << num_cycles << " allocation/free cycles\n";
  std::cout << "Multiple allocate/free cycles test passed\n";

  // 6. 释放后的内存状态验证
  std::cout << "6. Memory state after free test\n";

  void* test_ptr = slab.Alloc(256);
  ASSERT_NE(test_ptr, nullptr) << "Failed to allocate test memory";

  auto* cache = slab.find_buffers_cache(test_ptr);
  ASSERT_NE(cache, nullptr) << "Should find cache for test memory";

  auto active_before_final = cache->num_active_;

  // 释放内存
  slab.Free(test_ptr);

  // 验证活跃对象数量减少
  EXPECT_LT(cache->num_active_, active_before_final)
      << "Active count should decrease after final Free";

  // 尝试再次查找已释放的指针（应该仍然能找到缓存，但不应该再次释放）
  auto* cache_after = slab.find_buffers_cache(test_ptr);
  if (cache_after != nullptr) {
    std::cout << "Cache still findable after Free (expected)\n";
  }

  // 双重释放测试（应该是安全的，不会崩溃）
  slab.Free(test_ptr);

  std::cout << "Memory state after free test passed\n";

  // 7. 大量分配和释放测试
  std::cout << "7. Bulk allocate and free test\n";

  std::vector<std::pair<void*, size_t>> bulk_ptrs;
  std::vector<size_t> sizes = {32, 64, 96, 128, 160, 192, 224, 256};

  // 大量分配
  for (int i = 0; i < 20; ++i) {
    size_t size = sizes[i % sizes.size()];
    void* ptr = slab.Alloc(size);
    if (ptr != nullptr) {
      bulk_ptrs.push_back({ptr, size});
      // 写入唯一标识
      memset(ptr, i & 0xFF, size);
    }
  }

  std::cout << "Allocated " << bulk_ptrs.size() << " objects for bulk test\n";

  // 随机释放
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(bulk_ptrs.begin(), bulk_ptrs.end(), g);

  for (const auto& [ptr, size] : bulk_ptrs) {
    slab.Free(ptr);
  }

  std::cout << "Freed all " << bulk_ptrs.size() << " objects\n";
  std::cout << "Bulk allocate and free test passed\n";

  std::cout << "=== Free tests completed successfully! ===\n";
}

/**
 * @brief 测试 kmem_cache_destroy 功能
 *
 * 测试内容：
 * 1. 基本缓存销毁测试
 * 2. 销毁nullptr缓存测试
 * 3. 销毁有分配对象的缓存测试
 * 4. 销毁空缓存测试
 * 5. 多个缓存销毁测试
 * 6. 销毁后的状态验证
 */
TEST_F(SlabTest, KmemCacheDestroyTest) {
  std::cout << "\n=== Starting kmem_cache_destroy tests ===\n";

  using MyBuddy = Buddy<TestLogger>;
  using MySlab = Slab<MyBuddy, TestLogger, TestLock>;

  MySlab slab("slab_destroy_test", test_memory_, kTestPages);

  // 1. 基本缓存销毁测试
  std::cout << "1. Basic cache destroy test\n";

  auto* cache1 =
      slab.find_create_kmem_cache("destroy_test_1", sizeof(int), nullptr, nullptr);
  ASSERT_NE(cache1, nullptr) << "Failed to create cache for destroy test";
  EXPECT_STREQ(cache1->name_, "destroy_test_1");

  // 记录销毁前的名称
  std::string cache_name_before(cache1->name_);

  // 销毁缓存
  slab.kmem_cache_destroy(cache1);

  // 验证缓存名称被清空（这是销毁的标志）
  EXPECT_EQ(cache1->name_[0], '\0')
      << "Cache name should be cleared after destroy";
  EXPECT_EQ(cache1->objectSize_, 0)
      << "Object size should be reset after destroy";

  std::cout << "Cache '" << cache_name_before << "' destroyed successfully\n";
  std::cout << "Basic cache destroy test passed\n";

  // 2. 销毁nullptr缓存测试
  std::cout << "2. Destroy nullptr cache test\n";

  // 这应该不会崩溃
  slab.kmem_cache_destroy(nullptr);

  std::cout << "Destroy nullptr cache test passed\n";

  // 3. 销毁有分配对象的缓存测试
  std::cout << "3. Destroy cache with allocated objects test\n";

  auto* cache2 = slab.find_create_kmem_cache("destroy_test_2", 64, nullptr, nullptr);
  ASSERT_NE(cache2, nullptr)
      << "Failed to create cache for allocated objects test";

  // 分配一些对象
  std::vector<void*> allocated_objects;
  for (int i = 0; i < 5; ++i) {
    void* obj = slab.kmem_cache_alloc(cache2);
    if (obj != nullptr) {
      allocated_objects.push_back(obj);
      memset(obj, i, 64);
    }
  }

  EXPECT_GT(allocated_objects.size(), 0) << "Should allocate some objects";
  std::cout << "Allocated " << allocated_objects.size()
            << " objects before destroy\n";

  auto active_before = cache2->num_active_;

  // 销毁有活跃对象的缓存
  slab.kmem_cache_destroy(cache2);

  // 验证缓存被销毁
  EXPECT_EQ(cache2->name_[0], '\0')
      << "Cache name should be cleared even with active objects";

  std::cout << "Cache with " << active_before << " active objects destroyed\n";
  std::cout << "Destroy cache with allocated objects test passed\n";

  // 4. 销毁空缓存测试
  std::cout << "4. Destroy empty cache test\n";

  auto* cache3 =
      slab.find_create_kmem_cache("destroy_test_3", 128, nullptr, nullptr);
  ASSERT_NE(cache3, nullptr) << "Failed to create empty cache for test";

  // 确保缓存是空的
  EXPECT_EQ(cache3->num_active_, 0) << "Cache should be empty initially";

  // 销毁空缓存
  slab.kmem_cache_destroy(cache3);

  // 验证销毁成功
  EXPECT_EQ(cache3->name_[0], '\0')
      << "Empty cache name should be cleared after destroy";

  std::cout << "Empty cache destroyed successfully\n";
  std::cout << "Destroy empty cache test passed\n";

  // 5. 多个缓存销毁测试
  std::cout << "5. Multiple cache destroy test\n";

  std::vector<std::pair<decltype(cache1), std::string>> caches_to_destroy;

  // 创建多个不同大小的缓存
  std::vector<std::pair<std::string, size_t>> cache_specs = {
      {"multi_destroy_32", 32},
      {"multi_destroy_64", 64},
      {"multi_destroy_256", 256},
      {"multi_destroy_512", 512}};

  for (const auto& [name, size] : cache_specs) {
    auto* cache = slab.find_create_kmem_cache(name.c_str(), size, nullptr, nullptr);
    if (cache != nullptr) {
      caches_to_destroy.push_back({cache, name});

      // 在一些缓存中分配对象
      if (size <= 256) {
        void* obj = slab.kmem_cache_alloc(cache);
        if (obj != nullptr) {
          memset(obj, 0xCC, size);
        }
      }
    }
  }

  std::cout << "Created " << caches_to_destroy.size()
            << " caches for multi-destroy test\n";

  // 销毁所有缓存
  for (const auto& [cache, name] : caches_to_destroy) {
    EXPECT_STRNE(cache->name_, "")
        << "Cache name should not be empty before destroy";

    slab.kmem_cache_destroy(cache);

    EXPECT_EQ(cache->name_[0], '\0')
        << "Cache " << name << " should be destroyed";
    std::cout << "Destroyed cache: " << name << "\n";
  }

  std::cout << "Multiple cache destroy test passed\n";

  // 6. 销毁后的状态验证
  std::cout << "6. Post-destroy state verification test\n";

  auto* cache4 =
      slab.find_create_kmem_cache("verify_destroy", 256, nullptr, nullptr);
  ASSERT_NE(cache4, nullptr) << "Failed to create cache for verification test";

  // 分配和释放一些对象
  void* obj1 = slab.kmem_cache_alloc(cache4);

  if (obj1 != nullptr) {
    slab.kmem_cache_free(cache4, obj1);
  }
  // 故意不释放 obj2，测试销毁有未释放对象的缓存
  slab.kmem_cache_alloc(cache4);

  auto active_before_destroy = cache4->num_active_;
  std::cout << "Cache has " << active_before_destroy
            << " active objects before destroy\n";

  // 销毁缓存
  slab.kmem_cache_destroy(cache4);

  // 验证缓存状态
  EXPECT_EQ(cache4->name_[0], '\0') << "Cache name should be cleared";
  EXPECT_EQ(cache4->objectSize_, 0) << "Object size should be reset";

  std::cout << "Post-destroy state verification test passed\n";

  // 7. 边界条件测试
  std::cout << "7. Edge case testing\n";

  // 创建一个缓存然后立即销毁
  auto* cache5 =
      slab.find_create_kmem_cache("immediate_destroy", 64, nullptr, nullptr);
  if (cache5 != nullptr) {
    slab.kmem_cache_destroy(cache5);
    EXPECT_EQ(cache5->name_[0], '\0')
        << "Immediately destroyed cache should be cleared";
    std::cout << "Immediate destroy test passed\n";
  }

  // 尝试多次销毁同一个缓存（第二次应该是安全的）
  auto* cache6 =
      slab.find_create_kmem_cache("double_destroy", 128, nullptr, nullptr);
  if (cache6 != nullptr) {
    slab.kmem_cache_destroy(cache6);
    EXPECT_EQ(cache6->name_[0], '\0') << "First destroy should clear name";

    // 第二次销毁（应该是安全的，因为名称已经为空）
    slab.kmem_cache_destroy(cache6);

    std::cout << "Double destroy test passed\n";
  }

  std::cout << "Edge case testing passed\n";

  std::cout << "=== kmem_cache_destroy tests completed successfully! ===\n";
}
