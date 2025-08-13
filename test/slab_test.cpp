/**
 * Copyright The bmalloc Contributors
 * @file slab_test.cpp
 * @brief Slab分配器的Google Test测试用例
 */

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
#include "slab.hpp"

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
 * @brief 测试 kmem_cache_create 函数
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
  auto cache1 = slab.kmem_cache_create("test_cache_1", sizeof(int), ctor, dtor);
  ASSERT_NE(cache1, nullptr);
  EXPECT_STREQ(cache1->name, "test_cache_1");
  EXPECT_EQ(cache1->objectSize, sizeof(int));
  EXPECT_EQ(cache1->ctor, ctor);
  EXPECT_EQ(cache1->dtor, dtor);
  EXPECT_EQ(cache1->num_active, 0);
  EXPECT_GT(cache1->objectsInSlab, 0);
  EXPECT_EQ(cache1->error_code, 0);

  // 2. 测试创建不同大小的 cache
  auto cache2 =
      slab.kmem_cache_create("test_cache_2", sizeof(double), nullptr, nullptr);
  ASSERT_NE(cache2, nullptr);
  EXPECT_STREQ(cache2->name, "test_cache_2");
  EXPECT_EQ(cache2->objectSize, sizeof(double));
  EXPECT_EQ(cache2->ctor, nullptr);
  EXPECT_EQ(cache2->dtor, nullptr);

  // 3. 测试创建大对象的 cache（需要更高的 order）
  auto cache3 = slab.kmem_cache_create("large_cache", 8192, nullptr, nullptr);
  ASSERT_NE(cache3, nullptr);
  EXPECT_STREQ(cache3->name, "large_cache");
  EXPECT_EQ(cache3->objectSize, 8192);
  EXPECT_GT(cache3->order, 0);  // 大对象需要更高的 order

  // 4. 测试重复创建相同的 cache（应该返回已存在的）
  auto cache1_duplicate =
      slab.kmem_cache_create("test_cache_1", sizeof(int), ctor, dtor);
  EXPECT_EQ(cache1_duplicate, cache1);  // 应该返回相同的指针

  // 5. 测试错误情况：空名称
  auto invalid_cache1 =
      slab.kmem_cache_create("", sizeof(int), nullptr, nullptr);
  EXPECT_EQ(invalid_cache1, nullptr);
  // 由于 cache_cache 是 protected，我们通过返回值判断错误

  // 6. 测试错误情况：nullptr 名称
  auto invalid_cache2 =
      slab.kmem_cache_create(nullptr, sizeof(int), nullptr, nullptr);
  EXPECT_EQ(invalid_cache2, nullptr);

  // 7. 测试错误情况：非法大小
  auto invalid_cache3 =
      slab.kmem_cache_create("invalid_size", 0, nullptr, nullptr);
  EXPECT_EQ(invalid_cache3, nullptr);

  // 8. 测试错误情况：尝试创建与 cache_cache 同名的 cache
  auto invalid_cache4 =
      slab.kmem_cache_create("kmem_cache", sizeof(int), nullptr, nullptr);
  EXPECT_EQ(invalid_cache4, nullptr);

  std::cout << "kmem_cache_create tests completed successfully!\n";
  std::cout << "Created caches:\n";
  std::cout << "  - " << cache1->name << " (size: " << cache1->objectSize
            << ", objects per slab: " << cache1->objectsInSlab << ")\n";
  std::cout << "  - " << cache2->name << " (size: " << cache2->objectSize
            << ", objects per slab: " << cache2->objectsInSlab << ")\n";
  std::cout << "  - " << cache3->name << " (size: " << cache3->objectSize
            << ", order: " << cache3->order << ")\n";
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
  EXPECT_EQ(result1, 0); // 对 nullptr 应该返回 0
  
  // 2. 创建一个测试缓存
  auto cache = slab.kmem_cache_create("shrink_test_cache", sizeof(int), nullptr, nullptr);
  ASSERT_NE(cache, nullptr);
  
  std::cout << "Initial cache state:\n";
  std::cout << "  - num_allocations: " << cache->num_allocations << "\n";
  std::cout << "  - slabs_free: " << (cache->slabs_free ? "has slabs" : "nullptr") << "\n";
  std::cout << "  - growing: " << (cache->growing ? "true" : "false") << "\n";
  
  // 3. 测试没有空闲 slab 时的收缩（新创建的cache没有分配slab）
  int result2 = slab.kmem_cache_shrink(cache);
  EXPECT_GE(result2, 0); // 应该返回非负值
  
  // 验证 growing 标志被重置
  EXPECT_FALSE(cache->growing);
  
  std::cout << "After first shrink:\n";
  std::cout << "  - blocks freed: " << result2 << "\n";
  std::cout << "  - num_allocations: " << cache->num_allocations << "\n";
  std::cout << "  - growing: " << (cache->growing ? "true" : "false") << "\n";
  
  // 4. 设置 growing 标志并测试
  cache->growing = true;
  int result3 = slab.kmem_cache_shrink(cache);
  EXPECT_GE(result3, 0);
  EXPECT_FALSE(cache->growing); // growing 标志应该被重置
  
  // 5. 测试多次收缩
  int result4 = slab.kmem_cache_shrink(cache);
  EXPECT_GE(result4, 0);
  
  int result5 = slab.kmem_cache_shrink(cache);
  EXPECT_GE(result5, 0);
  
  std::cout << "Multiple shrink operations completed:\n";
  std::cout << "  - 2nd shrink freed: " << result4 << " blocks\n";
  std::cout << "  - 3rd shrink freed: " << result5 << " blocks\n";
  
  // 6. 验证缓存仍然有效
  EXPECT_STREQ(cache->name, "shrink_test_cache");
  EXPECT_EQ(cache->objectSize, sizeof(int));
  EXPECT_GT(cache->objectsInSlab, 0);
  
  // 7. 创建另一个大对象的缓存来测试不同的 order
  auto large_cache = slab.kmem_cache_create("large_shrink_test", 4096, nullptr, nullptr);
  ASSERT_NE(large_cache, nullptr);
  
  std::cout << "Large cache created:\n";
  std::cout << "  - order: " << large_cache->order << "\n";
  std::cout << "  - objects per slab: " << large_cache->objectsInSlab << "\n";
  
  int large_result = slab.kmem_cache_shrink(large_cache);
  EXPECT_GE(large_result, 0);
  
  std::cout << "Large cache shrink result: " << large_result << " blocks freed\n";
  
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
  auto cache = slab.kmem_cache_create("alloc_test_cache", sizeof(int), nullptr, nullptr);
  ASSERT_NE(cache, nullptr);
  
  std::cout << "Initial cache state:\n";
  std::cout << "  - num_active: " << cache->num_active << "\n";
  std::cout << "  - num_allocations: " << cache->num_allocations << "\n";
  std::cout << "  - objects per slab: " << cache->objectsInSlab << "\n";
  std::cout << "  - slabs_free: " << (cache->slabs_free ? "has slabs" : "nullptr") << "\n";
  std::cout << "  - slabs_partial: " << (cache->slabs_partial ? "has slabs" : "nullptr") << "\n";
  std::cout << "  - slabs_full: " << (cache->slabs_full ? "has slabs" : "nullptr") << "\n";
  
  // 3. 测试第一次分配（需要创建新slab）
  void* obj1 = slab.kmem_cache_alloc(cache);
  ASSERT_NE(obj1, nullptr);
  EXPECT_EQ(cache->num_active, 1);
  EXPECT_GT(cache->num_allocations, 0);
  EXPECT_TRUE(cache->growing); // 第一次分配会设置growing标志
  
  std::cout << "After first allocation:\n";
  std::cout << "  - num_active: " << cache->num_active << "\n";
  std::cout << "  - num_allocations: " << cache->num_allocations << "\n";
  std::cout << "  - object address: " << obj1 << "\n";
  std::cout << "  - growing: " << (cache->growing ? "true" : "false") << "\n";
  
  // 4. 测试后续分配（使用现有slab）
  void* obj2 = slab.kmem_cache_alloc(cache);
  ASSERT_NE(obj2, nullptr);
  EXPECT_NE(obj1, obj2); // 应该是不同的地址
  EXPECT_EQ(cache->num_active, 2);
  
  void* obj3 = slab.kmem_cache_alloc(cache);
  ASSERT_NE(obj3, nullptr);
  EXPECT_NE(obj1, obj3);
  EXPECT_NE(obj2, obj3);
  EXPECT_EQ(cache->num_active, 3);
  
  std::cout << "After multiple allocations:\n";
  std::cout << "  - num_active: " << cache->num_active << "\n";
  std::cout << "  - obj1: " << obj1 << "\n";
  std::cout << "  - obj2: " << obj2 << "\n";
  std::cout << "  - obj3: " << obj3 << "\n";
  
  // 5. 验证对象地址对齐（应该按objectSize对齐）
  uintptr_t addr1 = reinterpret_cast<uintptr_t>(obj1);
  uintptr_t addr2 = reinterpret_cast<uintptr_t>(obj2);
  uintptr_t addr3 = reinterpret_cast<uintptr_t>(obj3);
  
  // 检查地址差值是否为objectSize的倍数
  EXPECT_EQ((addr2 - addr1) % cache->objectSize, 0);
  EXPECT_EQ((addr3 - addr2) % cache->objectSize, 0);
  
  // 6. 测试带构造函数的缓存
  static int ctor_call_count = 0;
  auto ctor = +[](void* ptr) {
    ctor_call_count++;
    *(int*)ptr = 42; // 初始化为42
  };
  
  auto cache_with_ctor = slab.kmem_cache_create("ctor_cache", sizeof(int), ctor, nullptr);
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
  auto large_cache = slab.kmem_cache_create("large_alloc_test", 2048, nullptr, nullptr);
  ASSERT_NE(large_cache, nullptr);
  
  void* large_obj1 = slab.kmem_cache_alloc(large_cache);
  ASSERT_NE(large_obj1, nullptr);
  
  void* large_obj2 = slab.kmem_cache_alloc(large_cache);
  ASSERT_NE(large_obj2, nullptr);
  EXPECT_NE(large_obj1, large_obj2);
  
  std::cout << "Large object allocation:\n";
  std::cout << "  - cache order: " << large_cache->order << "\n";
  std::cout << "  - objects per slab: " << large_cache->objectsInSlab << "\n";
  std::cout << "  - large_obj1: " << large_obj1 << "\n";
  std::cout << "  - large_obj2: " << large_obj2 << "\n";
  
  // 8. 测试分配直到slab满的情况
  std::vector<void*> allocated_objects;
  
  // 清空之前的分配并重新创建缓存
  auto full_test_cache = slab.kmem_cache_create("full_test_cache", sizeof(double), nullptr, nullptr);
  ASSERT_NE(full_test_cache, nullptr);
  
  // 分配所有可能的对象
  for (size_t i = 0; i < full_test_cache->objectsInSlab; i++) {
    void* obj = slab.kmem_cache_alloc(full_test_cache);
    ASSERT_NE(obj, nullptr);
    allocated_objects.push_back(obj);
  }
  
  EXPECT_EQ(full_test_cache->num_active, full_test_cache->objectsInSlab);
  std::cout << "Filled slab test:\n";
  std::cout << "  - allocated " << allocated_objects.size() << " objects\n";
  std::cout << "  - cache num_active: " << full_test_cache->num_active << "\n";
  
  // 9. 再次分配应该创建新的slab
  void* overflow_obj = slab.kmem_cache_alloc(full_test_cache);
  ASSERT_NE(overflow_obj, nullptr);
  EXPECT_EQ(full_test_cache->num_active, full_test_cache->objectsInSlab + 1);
  
  std::cout << "Overflow allocation:\n";
  std::cout << "  - overflow object: " << overflow_obj << "\n";
  std::cout << "  - new num_active: " << full_test_cache->num_active << "\n";
  
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
  auto cache = slab.kmem_cache_create("free_test_cache", sizeof(int), nullptr, nullptr);
  ASSERT_NE(cache, nullptr);
  
  // 测试 nullptr cache
  slab.kmem_cache_free(nullptr, (void*)0x1000);  // 应该安全返回
  
  // 测试 nullptr object
  slab.kmem_cache_free(cache, nullptr);  // 应该安全返回
  
  std::cout << "nullptr handling tests passed\n";
  
  // 2. 测试基本的分配和释放
  void* obj1 = slab.kmem_cache_alloc(cache);
  ASSERT_NE(obj1, nullptr);
  EXPECT_EQ(cache->num_active, 1);
  
  std::cout << "Before free: num_active = " << cache->num_active << "\n";
  
  // 释放对象
  slab.kmem_cache_free(cache, obj1);
  EXPECT_EQ(cache->num_active, 0);
  EXPECT_EQ(cache->error_code, 0);  // 没有错误
  
  std::cout << "After free: num_active = " << cache->num_active << "\n";
  
  // 3. 测试多个对象的分配和释放
  std::vector<void*> objects;
  const size_t num_objects = 10;
  
  // 分配多个对象
  for (size_t i = 0; i < num_objects; i++) {
    void* obj = slab.kmem_cache_alloc(cache);
    ASSERT_NE(obj, nullptr);
    objects.push_back(obj);
  }
  
  EXPECT_EQ(cache->num_active, num_objects);
  std::cout << "Allocated " << num_objects << " objects, num_active = " << cache->num_active << "\n";
  
  // 释放一半对象
  for (size_t i = 0; i < num_objects / 2; i++) {
    slab.kmem_cache_free(cache, objects[i]);
  }
  
  EXPECT_EQ(cache->num_active, num_objects - num_objects / 2);
  std::cout << "After freeing half: num_active = " << cache->num_active << "\n";
  
  // 释放剩余对象
  for (size_t i = num_objects / 2; i < num_objects; i++) {
    slab.kmem_cache_free(cache, objects[i]);
  }
  
  EXPECT_EQ(cache->num_active, 0);
  std::cout << "After freeing all: num_active = " << cache->num_active << "\n";
  
  // 4. 测试带析构函数的缓存
  static int dtor_call_count = 0;
  auto dtor = +[](void* ptr) {
    dtor_call_count++;
    *(int*)ptr = -1; // 标记为已析构
  };
  
  auto cache_with_dtor = slab.kmem_cache_create("dtor_cache", sizeof(int), nullptr, dtor);
  ASSERT_NE(cache_with_dtor, nullptr);
  
  void* dtor_obj = slab.kmem_cache_alloc(cache_with_dtor);
  ASSERT_NE(dtor_obj, nullptr);
  *(int*)dtor_obj = 123;  // 设置值
  
  int initial_dtor_calls = dtor_call_count;
  slab.kmem_cache_free(cache_with_dtor, dtor_obj);
  
  // 验证析构函数被调用
  EXPECT_GT(dtor_call_count, initial_dtor_calls);
  EXPECT_EQ(*(int*)dtor_obj, -1);  // 验证析构函数修改了值
  
  std::cout << "Destructor test: calls = " << dtor_call_count << ", value = " << *(int*)dtor_obj << "\n";
  
  // 5. 测试 slab 状态转换（full -> partial -> free）
  auto transition_cache = slab.kmem_cache_create("transition_cache", sizeof(double), nullptr, nullptr);
  ASSERT_NE(transition_cache, nullptr);
  
  // 分配足够的对象填满一个slab
  std::vector<void*> transition_objects;
  for (size_t i = 0; i < transition_cache->objectsInSlab; i++) {
    void* obj = slab.kmem_cache_alloc(transition_cache);
    ASSERT_NE(obj, nullptr);
    transition_objects.push_back(obj);
  }
  
  EXPECT_EQ(transition_cache->num_active, transition_cache->objectsInSlab);
  std::cout << "Filled slab: num_active = " << transition_cache->num_active << "\n";
  
  // 释放一个对象（full -> partial）
  slab.kmem_cache_free(transition_cache, transition_objects[0]);
  EXPECT_EQ(transition_cache->num_active, transition_cache->objectsInSlab - 1);
  
  // 释放所有剩余对象（partial -> free）
  for (size_t i = 1; i < transition_objects.size(); i++) {
    slab.kmem_cache_free(transition_cache, transition_objects[i]);
  }
  
  EXPECT_EQ(transition_cache->num_active, 0);
  std::cout << "After freeing all transition objects: num_active = " << transition_cache->num_active << "\n";
  
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
  std::cout << "  - original obj1: " << reuse_obj1 << ", obj2: " << reuse_obj2 << "\n";
  std::cout << "  - reused obj3: " << reuse_obj3 << ", obj4: " << reuse_obj4 << "\n";
  
  // 清理
  slab.kmem_cache_free(cache, reuse_obj3);
  slab.kmem_cache_free(cache, reuse_obj4);
  
  // 7. 测试错误情况：释放无效对象
  void* invalid_obj = malloc(sizeof(int));  // 不是从slab分配的对象
  
  slab.kmem_cache_free(cache, invalid_obj);
  EXPECT_NE(cache->error_code, 0);  // 应该有错误
  
  std::cout << "Invalid object free test: error_code = " << cache->error_code << "\n";
  
  free(invalid_obj);
  
  // 8. 测试大对象的释放
  auto large_cache = slab.kmem_cache_create("large_free_test", 1024, nullptr, nullptr);
  ASSERT_NE(large_cache, nullptr);
  
  void* large_obj = slab.kmem_cache_alloc(large_cache);
  ASSERT_NE(large_obj, nullptr);
  EXPECT_EQ(large_cache->num_active, 1);
  
  slab.kmem_cache_free(large_cache, large_obj);
  EXPECT_EQ(large_cache->num_active, 0);
  EXPECT_EQ(large_cache->error_code, 0);
  
  std::cout << "Large object free test completed\n";
  
  std::cout << "kmem_cache_free tests completed successfully!\n";
}
