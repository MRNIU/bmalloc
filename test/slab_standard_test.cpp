/**
 * Copyright The bmalloc Contributors
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

#include "slab.hpp"
#include "standard_allocator.hpp"

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
class SlabStandardTest : public ::testing::Test {
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

// 派生类用于访问 Slab 的 protected 方法
template <class PageAllocator, class LogFunc = std::nullptr_t,
          class Lock = LockBase>
  requires std::derived_from<PageAllocator, AllocatorBase<LogFunc, Lock>>
class TestableSlab : public Slab<PageAllocator, LogFunc, Lock> {
 public:
  using Base = Slab<PageAllocator, LogFunc, Lock>;
  using Base::Base;  // 继承构造函数

  // 公开 protected 方法用于测试
  using Base::find_buffers_cache;
  using Base::find_create_kmem_cache;
  using Base::kmem_cache_alloc;
  using Base::kmem_cache_destroy;
  using Base::kmem_cache_free;
  using Base::kmem_cache_shrink;
};

/**
 * @brief Slab 分配器实例化示例
 *
 * 这个测试展示了如何正确实例化 Slab 分配器：
 * 1. PageAllocator: 使用 StandardAllocator<TestLogger, TestLock> 作为页级分配器
 * 2. LogFunc: 使用 TestLogger 作为日志函数
 * 3. Lock: 使用 TestLock 作为锁机制
 */
TEST_F(SlabStandardTest, InstantiationExample) {
  // 定义具体的分配器类型 - 注意模板参数要匹配
  using MyStandardAllocator = StandardAllocator<TestLogger, TestLock>;
  using MySlab = TestableSlab<MyStandardAllocator, TestLogger, TestLock>;

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
TEST_F(SlabStandardTest, DifferentTemplateParameterCombinations) {
  // 1. 使用默认模板参数的简化版本
  using SimpleStandardAllocator =
      StandardAllocator<>;  // 使用默认的 std::nullptr_t 和 LockBase
  using SimpleSlab =
      TestableSlab<SimpleStandardAllocator>;  // 使用默认的 LogFunc 和 Lock

  SimpleSlab simple_slab("simple_slab", test_memory_, kTestPages);
  EXPECT_EQ(simple_slab.GetFreeCount(), kTestPages);

  // 2. 只指定日志函数，使用默认锁
  using LoggedStandardAllocator = StandardAllocator<TestLogger>;
  using LoggedSlab = TestableSlab<LoggedStandardAllocator, TestLogger>;

  LoggedSlab logged_slab("logged_slab", test_memory_, kTestPages);
  EXPECT_EQ(logged_slab.GetFreeCount(), kTestPages);

  // 3. 完整指定所有模板参数
  using FullStandardAllocator = StandardAllocator<TestLogger, TestLock>;
  using FullSlab = TestableSlab<FullStandardAllocator, TestLogger, TestLock>;

  FullSlab full_slab("full_slab", test_memory_, kTestPages);
  EXPECT_EQ(full_slab.GetFreeCount(), kTestPages);

  std::cout << "All template parameter combinations work correctly!\n";
}

/**
 * @brief 测试 find_create_kmem_cache 函数
 */
TEST_F(SlabStandardTest, KmemCacheCreateTest) {
  // 使用简单的配置
  using MyStandardAllocator = StandardAllocator<TestLogger>;
  using MySlab = TestableSlab<MyStandardAllocator, TestLogger>;

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
  auto cache1 =
      slab.find_create_kmem_cache("test_cache_1", sizeof(int), ctor, dtor);
  ASSERT_NE(cache1, nullptr);
  EXPECT_STREQ(cache1->name_, "test_cache_1");
  EXPECT_EQ(cache1->objectSize_, sizeof(int));
  EXPECT_EQ(cache1->ctor_, ctor);
  EXPECT_EQ(cache1->dtor_, dtor);
  EXPECT_EQ(cache1->num_active_, 0);
  EXPECT_GT(cache1->objectsInSlab_, 0);
  EXPECT_EQ(cache1->error_code_, 0);

  // 2. 测试创建不同大小的 cache
  auto cache2 = slab.find_create_kmem_cache("test_cache_2", sizeof(double),
                                            nullptr, nullptr);
  ASSERT_NE(cache2, nullptr);
  EXPECT_STREQ(cache2->name_, "test_cache_2");
  EXPECT_EQ(cache2->objectSize_, sizeof(double));
  EXPECT_EQ(cache2->ctor_, nullptr);
  EXPECT_EQ(cache2->dtor_, nullptr);

  // 3. 测试创建大对象的 cache（需要更高的 order）
  auto cache3 =
      slab.find_create_kmem_cache("large_cache", 8192, nullptr, nullptr);
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
TEST_F(SlabStandardTest, KmemCacheShrinkTest) {
  // 使用简单的配置
  using MyStandardAllocator = StandardAllocator<TestLogger>;
  using MySlab = TestableSlab<MyStandardAllocator, TestLogger>;

  MySlab slab("test_slab", test_memory_, kTestPages);

  // 1. 测试对 nullptr 的处理
  int result1 = slab.kmem_cache_shrink(nullptr);
  EXPECT_EQ(result1, 0);  // 对 nullptr 应该返回 0

  // 2. 创建一个测试缓存
  auto cache = slab.find_create_kmem_cache("shrink_test_cache", sizeof(int),
                                           nullptr, nullptr);
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
TEST_F(SlabStandardTest, KmemCacheAllocTest) {
  // 使用简单的配置
  using MyStandardAllocator = StandardAllocator<TestLogger>;
  using MySlab = TestableSlab<MyStandardAllocator, TestLogger>;

  MySlab slab("test_slab", test_memory_, kTestPages);

  // 1. 测试对 nullptr 的处理
  void* result1 = slab.kmem_cache_alloc(nullptr);
  EXPECT_EQ(result1, nullptr);

  // 2. 创建一个测试缓存
  auto cache = slab.find_create_kmem_cache("alloc_test_cache", sizeof(int),
                                           nullptr, nullptr);
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
TEST_F(SlabStandardTest, KmemCacheFreeTest) {
  // 使用简单的配置
  using MyStandardAllocator = StandardAllocator<TestLogger>;
  using MySlab = TestableSlab<MyStandardAllocator, TestLogger>;

  MySlab slab("test_slab", test_memory_, kTestPages);

  // 1. 测试对 nullptr 的处理
  auto cache = slab.find_create_kmem_cache("free_test_cache", sizeof(int),
                                           nullptr, nullptr);
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
TEST_F(SlabStandardTest, KmallocTest) {
  std::cout << "\n=== Starting Alloc tests ===\n";

  using MyStandardAllocator = StandardAllocator<TestLogger, TestLock>;
  using MySlab = TestableSlab<MyStandardAllocator, TestLogger, TestLock>;

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

  std::cout << "Boundary condition test passed\n";

  // 4. 测试零大小分配
  std::cout << "4. Zero size allocation test\n";
  void* ptr_zero = slab.Alloc(0);
  EXPECT_EQ(ptr_zero, nullptr) << "Zero size allocation should return nullptr";

  std::cout << "Zero size allocation test passed\n";

  // 5. 测试大量分配
  std::cout << "5. Multiple allocation test\n";
  std::vector<void*> ptrs;

  for (int i = 0; i < 100; i++) {
    void* ptr = slab.Alloc(64);
    ASSERT_NE(ptr, nullptr) << "Failed allocation " << i;
    ptrs.push_back(ptr);
  }

  // 验证所有地址都不同
  for (size_t i = 0; i < ptrs.size(); i++) {
    for (size_t j = i + 1; j < ptrs.size(); j++) {
      EXPECT_NE(ptrs[i], ptrs[j])
          << "Duplicate addresses at " << i << " and " << j;
    }
  }

  std::cout << "Multiple allocation test passed\n";

  std::cout << "\n=== Alloc tests completed successfully ===\n";
}

/**
 * @brief 测试 find_buffers_cache 函数
 */
TEST_F(SlabStandardTest, FindBuffersCacheTest) {
  std::cout << "\n=== Starting find_buffers_cache tests ===\n";

  using MyStandardAllocator = StandardAllocator<TestLogger>;
  using MySlab = TestableSlab<MyStandardAllocator, TestLogger>;

  MySlab slab("test_slab", test_memory_, kTestPages);

  // 1. 测试标准大小的查找
  std::cout << "1. Standard size tests\n";

  void* ptr32 = slab.Alloc(32);
  ASSERT_NE(ptr32, nullptr) << "Failed to allocate 32 bytes";
  auto cache32 = slab.find_buffers_cache(ptr32);
  ASSERT_NE(cache32, nullptr) << "Failed to find cache for 32-byte object";
  EXPECT_EQ(cache32->objectSize_, 32);

  void* ptr64 = slab.Alloc(64);
  ASSERT_NE(ptr64, nullptr) << "Failed to allocate 64 bytes";
  auto cache64 = slab.find_buffers_cache(ptr64);
  ASSERT_NE(cache64, nullptr) << "Failed to find cache for 64-byte object";
  EXPECT_EQ(cache64->objectSize_, 64);

  void* ptr128 = slab.Alloc(128);
  ASSERT_NE(ptr128, nullptr) << "Failed to allocate 128 bytes";
  auto cache128 = slab.find_buffers_cache(ptr128);
  ASSERT_NE(cache128, nullptr) << "Failed to find cache for 128-byte object";
  EXPECT_EQ(cache128->objectSize_, 128);

  std::cout << "Standard size tests passed\n";

  // 2. 测试非标准大小的查找（应该向上取整到最近的2的幂）
  std::cout << "2. Non-standard size tests\n";

  void* ptr33 = slab.Alloc(33);
  ASSERT_NE(ptr33, nullptr) << "Failed to allocate 33 bytes";
  auto cache33 = slab.find_buffers_cache(ptr33);
  ASSERT_NE(cache33, nullptr) << "Failed to find cache for 33-byte object";
  EXPECT_EQ(cache33->objectSize_, 64) << "Size 33 should map to cache 64";

  void* ptr65 = slab.Alloc(65);
  ASSERT_NE(ptr65, nullptr) << "Failed to allocate 65 bytes";
  auto cache65 = slab.find_buffers_cache(ptr65);
  ASSERT_NE(cache65, nullptr) << "Failed to find cache for 65-byte object";
  EXPECT_EQ(cache65->objectSize_, 128) << "Size 65 should map to cache 128";

  void* ptr100 = slab.Alloc(100);
  ASSERT_NE(ptr100, nullptr) << "Failed to allocate 100 bytes";
  auto cache100 = slab.find_buffers_cache(ptr100);
  ASSERT_NE(cache100, nullptr) << "Failed to find cache for 100-byte object";
  EXPECT_EQ(cache100->objectSize_, 128) << "Size 100 should map to cache 128";

  std::cout << "Non-standard size tests passed\n";

  // 3. 测试大尺寸缓存
  std::cout << "3. Large size tests\n";

  void* ptr1024 = slab.Alloc(1024);
  ASSERT_NE(ptr1024, nullptr) << "Failed to allocate 1024 bytes";
  auto cache1024 = slab.find_buffers_cache(ptr1024);
  ASSERT_NE(cache1024, nullptr) << "Failed to find cache for 1024-byte object";
  EXPECT_EQ(cache1024->objectSize_, 1024);

  void* ptr2048 = slab.Alloc(2048);
  ASSERT_NE(ptr2048, nullptr) << "Failed to allocate 2048 bytes";
  auto cache2048 = slab.find_buffers_cache(ptr2048);
  ASSERT_NE(cache2048, nullptr) << "Failed to find cache for 2048-byte object";
  EXPECT_EQ(cache2048->objectSize_, 2048);

  std::cout << "Large size tests passed\n";

  // 4. 测试边界条件
  std::cout << "4. Boundary condition tests\n";

  // 测试最小大小
  void* ptr_min = slab.Alloc(32);  // 使用最小支持的大小
  ASSERT_NE(ptr_min, nullptr) << "Failed to allocate 32 bytes";
  auto cache_min = slab.find_buffers_cache(ptr_min);
  ASSERT_NE(cache_min, nullptr) << "Failed to find cache for 32-byte object";
  EXPECT_EQ(cache_min->objectSize_, 32) << "Size 32 should map to cache 32";

  // 测试 nullptr
  auto cache_null = slab.find_buffers_cache(nullptr);
  EXPECT_EQ(cache_null, nullptr) << "nullptr should return nullptr";

  std::cout << "Boundary condition tests passed\n";

  // 5. 测试缓存复用（相同大小的分配应该使用相同的缓存）
  std::cout << "5. Cache reuse tests\n";

  void* ptr32_again = slab.Alloc(32);
  ASSERT_NE(ptr32_again, nullptr);
  auto cache32_again = slab.find_buffers_cache(ptr32_again);
  EXPECT_EQ(cache32, cache32_again) << "Same size should return same cache";

  void* ptr40 = slab.Alloc(40);  // 另一个映射到64的大小
  ASSERT_NE(ptr40, nullptr);
  auto cache40 = slab.find_buffers_cache(ptr40);
  EXPECT_EQ(cache64, cache40)
      << "Different sizes mapping to same cache should return same cache";

  std::cout << "Cache reuse tests passed\n";

  // 6. 验证缓存属性
  std::cout << "6. Cache property validation\n";

  // 验证缓存名称包含 "size-"
  std::string cache32_name(cache32->name_);
  EXPECT_TRUE(cache32_name.find("size-") != std::string::npos)
      << "Cache name should contain 'size-': " << cache32_name;

  // 验证 objectsInSlab_ 大于 0
  EXPECT_GT(cache32->objectsInSlab_, 0) << "objectsInSlab_ should be > 0";
  EXPECT_GT(cache64->objectsInSlab_, 0) << "objectsInSlab_ should be > 0";

  std::cout << "Cache property validation passed\n";

  // 清理分配的内存
  slab.Free(ptr32);
  slab.Free(ptr64);
  slab.Free(ptr128);
  slab.Free(ptr33);
  slab.Free(ptr65);
  slab.Free(ptr100);
  slab.Free(ptr1024);
  slab.Free(ptr2048);
  slab.Free(ptr_min);
  slab.Free(ptr32_again);
  slab.Free(ptr40);

  std::cout << "\n=== find_buffers_cache tests completed successfully ===\n";
}

/**
 * @brief 测试 Free 函数
 */
TEST_F(SlabStandardTest, KfreeTest) {
  std::cout << "\n=== Starting Free tests ===\n";

  using MyStandardAllocator = StandardAllocator<TestLogger, TestLock>;
  using MySlab = TestableSlab<MyStandardAllocator, TestLogger, TestLock>;

  MySlab slab("slab_kfree_test", test_memory_, kTestPages);

  // 1. 基本分配和释放测试
  std::cout << "1. Basic allocation and free test\n";

  void* ptr32 = slab.Alloc(32);
  ASSERT_NE(ptr32, nullptr) << "Failed to allocate 32 bytes";

  // 获取分配前的缓存状态
  auto cache32 = slab.find_buffers_cache(ptr32);
  ASSERT_NE(cache32, nullptr);
  size_t active_before = cache32->num_active_;

  std::cout << "Before free: num_active_ = " << active_before << "\n";

  // 释放内存
  slab.Free(ptr32);

  // 验证缓存状态
  EXPECT_EQ(cache32->num_active_, active_before - 1)
      << "num_active_ should decrease by 1 after free";

  std::cout << "After free: num_active_ = " << cache32->num_active_ << "\n";
  std::cout << "Basic allocation and free test passed\n";

  // 2. 多个对象的分配和释放
  std::cout << "2. Multiple allocation and free test\n";

  std::vector<void*> ptrs;
  const size_t num_allocs = 10;

  // 分配多个对象
  for (size_t i = 0; i < num_allocs; i++) {
    void* ptr = slab.Alloc(64);
    ASSERT_NE(ptr, nullptr) << "Failed allocation " << i;
    ptrs.push_back(ptr);
  }

  auto cache64 = slab.find_buffers_cache(ptrs[0]);  // 使用第一个分配的指针
  ASSERT_NE(cache64, nullptr);
  size_t expected_active = cache64->num_active_;

  std::cout << "After allocations: num_active_ = " << expected_active << "\n";

  // 释放一半对象
  for (size_t i = 0; i < num_allocs / 2; i++) {
    slab.Free(ptrs[i]);
    expected_active--;
  }

  EXPECT_EQ(cache64->num_active_, expected_active);
  std::cout << "After freeing half: num_active_ = " << cache64->num_active_
            << "\n";

  // 释放剩余对象
  for (size_t i = num_allocs / 2; i < num_allocs; i++) {
    slab.Free(ptrs[i]);
    expected_active--;
  }

  EXPECT_EQ(cache64->num_active_, expected_active);
  std::cout << "After freeing all: num_active_ = " << cache64->num_active_
            << "\n";
  std::cout << "Multiple allocation and free test passed\n";

  // 3. 不同大小的混合分配和释放
  std::cout << "3. Mixed size allocation and free test\n";

  void* ptr_mix1 = slab.Alloc(32);
  void* ptr_mix2 = slab.Alloc(64);
  void* ptr_mix3 = slab.Alloc(128);
  void* ptr_mix4 = slab.Alloc(256);

  ASSERT_NE(ptr_mix1, nullptr);
  ASSERT_NE(ptr_mix2, nullptr);
  ASSERT_NE(ptr_mix3, nullptr);
  ASSERT_NE(ptr_mix4, nullptr);

  // 验证地址不同
  EXPECT_NE(ptr_mix1, ptr_mix2);
  EXPECT_NE(ptr_mix2, ptr_mix3);
  EXPECT_NE(ptr_mix3, ptr_mix4);

  // 获取各自的缓存
  auto cache_mix32 = slab.find_buffers_cache(ptr_mix1);
  auto cache_mix64 = slab.find_buffers_cache(ptr_mix2);
  auto cache_mix128 = slab.find_buffers_cache(ptr_mix3);
  auto cache_mix256 = slab.find_buffers_cache(ptr_mix4);

  size_t active32_before = cache_mix32->num_active_;
  size_t active64_before = cache_mix64->num_active_;
  size_t active128_before = cache_mix128->num_active_;
  size_t active256_before = cache_mix256->num_active_;

  // 释放所有对象
  slab.Free(ptr_mix1);
  slab.Free(ptr_mix2);
  slab.Free(ptr_mix3);
  slab.Free(ptr_mix4);

  // 验证各自缓存的状态
  EXPECT_EQ(cache_mix32->num_active_, active32_before - 1);
  EXPECT_EQ(cache_mix64->num_active_, active64_before - 1);
  EXPECT_EQ(cache_mix128->num_active_, active128_before - 1);
  EXPECT_EQ(cache_mix256->num_active_, active256_before - 1);

  std::cout << "Mixed size allocation and free test passed\n";

  // 4. 测试空指针释放
  std::cout << "4. Null pointer free test\n";

  // 释放空指针应该安全
  slab.Free(nullptr);  // 应该安全返回，不崩溃

  std::cout << "Null pointer free test passed\n";

  // 5. 大对象的分配和释放
  std::cout << "5. Large object allocation and free test\n";

  void* large_ptr1 = slab.Alloc(1024);
  void* large_ptr2 = slab.Alloc(2048);
  void* large_ptr3 = slab.Alloc(4096);

  ASSERT_NE(large_ptr1, nullptr);
  ASSERT_NE(large_ptr2, nullptr);
  ASSERT_NE(large_ptr3, nullptr);

  auto cache1024 = slab.find_buffers_cache(large_ptr1);
  auto cache2048 = slab.find_buffers_cache(large_ptr2);
  auto cache4096 = slab.find_buffers_cache(large_ptr3);

  size_t active1024_before = cache1024->num_active_;
  size_t active2048_before = cache2048->num_active_;
  size_t active4096_before = cache4096->num_active_;

  slab.Free(large_ptr1);
  slab.Free(large_ptr2);
  slab.Free(large_ptr3);

  EXPECT_EQ(cache1024->num_active_, active1024_before - 1);
  EXPECT_EQ(cache2048->num_active_, active2048_before - 1);
  EXPECT_EQ(cache4096->num_active_, active4096_before - 1);

  std::cout << "Large object allocation and free test passed\n";

  // 6. 内存重用测试
  std::cout << "6. Memory reuse test\n";

  void* reuse1 = slab.Alloc(64);
  void* reuse2 = slab.Alloc(64);
  ASSERT_NE(reuse1, nullptr);
  ASSERT_NE(reuse2, nullptr);
  EXPECT_NE(reuse1, reuse2);

  slab.Free(reuse1);
  slab.Free(reuse2);

  // 再次分配，应该重用之前的内存
  void* reuse3 = slab.Alloc(64);
  void* reuse4 = slab.Alloc(64);
  ASSERT_NE(reuse3, nullptr);
  ASSERT_NE(reuse4, nullptr);

  // 新分配的地址应该是之前释放的地址之一
  EXPECT_TRUE(reuse3 == reuse1 || reuse3 == reuse2);
  EXPECT_TRUE(reuse4 == reuse1 || reuse4 == reuse2);
  EXPECT_NE(reuse3, reuse4);

  std::cout << "Memory addresses:\n";
  std::cout << "  - original: " << reuse1 << ", " << reuse2 << "\n";
  std::cout << "  - reused: " << reuse3 << ", " << reuse4 << "\n";

  slab.Free(reuse3);
  slab.Free(reuse4);

  std::cout << "Memory reuse test passed\n";

  std::cout << "\n=== Free tests completed successfully ===\n";
}

/**
 * @brief 测试 kmem_cache_destroy 函数
 */
TEST_F(SlabStandardTest, KmemCacheDestroyTest) {
  std::cout << "\n=== Starting kmem_cache_destroy tests ===\n";

  using MyStandardAllocator = StandardAllocator<TestLogger>;
  using MySlab = TestableSlab<MyStandardAllocator, TestLogger>;

  MySlab slab("test_slab", test_memory_, kTestPages);

  // 1. 测试对 nullptr 的处理
  std::cout << "1. Null pointer handling test\n";
  slab.kmem_cache_destroy(nullptr);  // 应该安全处理 nullptr
  std::cout << "Null pointer handling test passed\n";

  // 2. 测试销毁空缓存
  std::cout << "2. Empty cache destroy test\n";

  auto empty_cache =
      slab.find_create_kmem_cache("empty_cache", sizeof(int), nullptr, nullptr);
  ASSERT_NE(empty_cache, nullptr);
  EXPECT_EQ(empty_cache->num_active_, 0)
      << "New cache should have no active objects";

  slab.kmem_cache_destroy(empty_cache);  // 应该成功销毁
  std::cout << "Empty cache destroy test passed\n";

  // 3. 测试销毁有活动对象的缓存
  std::cout << "3. Active objects cache destroy test\n";

  auto active_cache = slab.find_create_kmem_cache(
      "active_cache", sizeof(double), nullptr, nullptr);
  ASSERT_NE(active_cache, nullptr);

  // 分配一些对象但不释放
  void* obj1 = slab.kmem_cache_alloc(active_cache);
  void* obj2 = slab.kmem_cache_alloc(active_cache);
  ASSERT_NE(obj1, nullptr);
  ASSERT_NE(obj2, nullptr);
  EXPECT_EQ(active_cache->num_active_, 2) << "Should have 2 active objects";

  // 尝试销毁带有活动对象的缓存 - 这应该是不安全的，但我们不检查返回值
  // 在实际实现中，可能会有错误处理或断言
  std::cout << "Active objects cache destroy test completed\n";

  // 4. 测试销毁已释放对象的缓存
  std::cout << "4. Freed objects cache destroy test\n";

  auto freed_cache = slab.find_create_kmem_cache("freed_cache", sizeof(char),
                                                 nullptr, nullptr);
  ASSERT_NE(freed_cache, nullptr);

  // 分配对象后立即释放
  void* obj3 = slab.kmem_cache_alloc(freed_cache);
  void* obj4 = slab.kmem_cache_alloc(freed_cache);
  ASSERT_NE(obj3, nullptr);
  ASSERT_NE(obj4, nullptr);

  slab.kmem_cache_free(freed_cache, obj3);
  slab.kmem_cache_free(freed_cache, obj4);
  EXPECT_EQ(freed_cache->num_active_, 0)
      << "Should have no active objects after free";

  slab.kmem_cache_destroy(freed_cache);  // 应该成功销毁
  std::cout << "Freed objects cache destroy test passed\n";

  // 5. 测试带构造/析构函数的缓存销毁
  std::cout << "5. Constructor/destructor cache destroy test\n";

  static int ctor_calls = 0;
  static int dtor_calls = 0;

  auto ctor = +[](void* ptr) {
    ctor_calls++;
    *(int*)ptr = 123;
  };

  auto dtor = +[](void* ptr) {
    dtor_calls++;
    *(int*)ptr = -1;
  };

  auto ctor_dtor_cache =
      slab.find_create_kmem_cache("ctor_dtor_cache", sizeof(int), ctor, dtor);
  ASSERT_NE(ctor_dtor_cache, nullptr);

  // 分配和释放对象以测试构造/析构函数
  void* obj5 = slab.kmem_cache_alloc(ctor_dtor_cache);
  ASSERT_NE(obj5, nullptr);
  EXPECT_GT(ctor_calls, 0) << "Constructor should be called";

  slab.kmem_cache_free(ctor_dtor_cache, obj5);
  EXPECT_GT(dtor_calls, 0) << "Destructor should be called";
  EXPECT_EQ(ctor_dtor_cache->num_active_, 0);

  slab.kmem_cache_destroy(ctor_dtor_cache);  // 应该成功销毁
  std::cout << "Constructor/destructor cache destroy test passed\n";

  // 6. 测试大对象缓存的销毁
  std::cout << "6. Large object cache destroy test\n";

  auto large_cache = slab.find_create_kmem_cache("large_cache", 8192, nullptr,
                                                 nullptr);  // 使用更大的对象
  ASSERT_NE(large_cache, nullptr);
  EXPECT_GT(large_cache->order_, 0) << "Large objects should have higher order";

  void* large_obj = slab.kmem_cache_alloc(large_cache);
  ASSERT_NE(large_obj, nullptr);
  slab.kmem_cache_free(large_cache, large_obj);
  EXPECT_EQ(large_cache->num_active_, 0);

  slab.kmem_cache_destroy(large_cache);  // 应该成功销毁
  std::cout << "Large object cache destroy test passed\n";

  // 7. 测试多个缓存的销毁
  std::cout << "7. Multiple cache destroy test\n";

  std::vector<decltype(empty_cache)> caches;

  for (int i = 0; i < 5; i++) {
    std::string name = "multi_cache_" + std::to_string(i);
    auto cache = slab.find_create_kmem_cache(
        name.c_str(), sizeof(long) * (i + 1), nullptr, nullptr);
    ASSERT_NE(cache, nullptr);
    caches.push_back(cache);

    // 分配和释放一个对象
    void* obj = slab.kmem_cache_alloc(cache);
    ASSERT_NE(obj, nullptr);
    slab.kmem_cache_free(cache, obj);
    EXPECT_EQ(cache->num_active_, 0);
  }

  // 销毁所有缓存
  for (auto cache : caches) {
    slab.kmem_cache_destroy(cache);  // 每个缓存都应该成功销毁
  }

  std::cout << "Multiple cache destroy test passed\n";

  // 清理剩余的有活动对象的缓存
  slab.kmem_cache_free(active_cache, obj1);
  slab.kmem_cache_free(active_cache, obj2);
  slab.kmem_cache_destroy(active_cache);

  std::cout << "\n=== kmem_cache_destroy tests completed successfully ===\n";
}
