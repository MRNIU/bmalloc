/**
 * Copyright The bmalloc Contributors
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <vector>

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

/**
 * @brief 使用 std::malloc 的页分配器实现
 *
 * 这个分配器使用标准库的 malloc/free 来管理内存页
 */
template <class LogFunc = std::nullptr_t, class Lock = LockBase>
  requires std::derived_from<Lock, LockBase> || std::is_same_v<Lock, LockBase>
class MallocPageAllocator : public AllocatorBase<LogFunc, Lock> {
 private:
  using Base = AllocatorBase<LogFunc, Lock>;

  // 记录已分配的内存块，用于释放时查找大小
  struct AllocRecord {
    void* ptr;
    size_t size;
  };

  std::vector<AllocRecord> allocated_blocks_;

 public:
  /**
   * @brief 构造函数
   * @param name 分配器名称
   * @param start_addr 起始地址（在这个实现中会被忽略）
   * @param page_count 页数（在这个实现中会被忽略）
   */
  explicit MallocPageAllocator(const char* name, void* start_addr = nullptr,
                               size_t page_count = 0)
      : Base(name, start_addr, page_count) {
    // 由于使用 malloc，我们不需要预分配的内存区域
    // 将 free_count 设置为一个大数值，表示可用内存很多
    this->free_count_ = SIZE_MAX / kPageSize;
    this->used_count_ = 0;
  }

  ~MallocPageAllocator() {
    // 释放所有未释放的内存块
    for (const auto& record : allocated_blocks_) {
      std::free(record.ptr);
    }
  }

 protected:
  /**
   * @brief 分配内存实现
   * @param order 阶数，实际分配 2^order 个页面
   * @return 分配的内存地址，失败返回 nullptr
   */
  auto AllocImpl(size_t order) -> void* override {
    // 计算实际需要分配的页数
    size_t page_count = 1ULL << order;
    
    // 检查有效的阶数
    if (order > 20) {  // 防止过大的分配请求
      if constexpr (!std::is_same_v<LogFunc, std::nullptr_t>) {
        LogFunc{}("MallocPageAllocator: Order %zu too large\n", order);
      }
      return nullptr;
    }

    size_t size = page_count * kPageSize;

    // 使用 aligned_alloc 确保页对齐
    void* ptr = std::aligned_alloc(kPageSize, size);

    if (ptr != nullptr) {
      // 记录分配信息
      allocated_blocks_.push_back({ptr, size});

      // 更新计数器
      this->used_count_ += page_count;
      if (this->free_count_ >= page_count) {
        this->free_count_ -= page_count;
      }

      // 初始化内存为0
      std::memset(ptr, 0, size);

      if constexpr (!std::is_same_v<LogFunc, std::nullptr_t>) {
        LogFunc{}(
            "MallocPageAllocator: Allocated %zu pages (%zu bytes) at %p (order %zu)\n",
            page_count, size, ptr, order);
      }
    }

    return ptr;
  }

  /**
   * @brief 释放内存实现
   * @param addr 要释放的内存地址
   * @param order 要释放的阶数（如果为0，会自动查找）
   */
  void FreeImpl(void* addr, size_t order = 0) override {
    (void)order;  // 标记参数为已使用，避免警告
    if (addr == nullptr) {
      return;
    }

    // 查找并移除记录
    auto it = std::find_if(
        allocated_blocks_.begin(), allocated_blocks_.end(),
        [addr](const AllocRecord& record) { return record.ptr == addr; });

    if (it != allocated_blocks_.end()) {
      size_t actual_page_count = it->size / kPageSize;

      if constexpr (!std::is_same_v<LogFunc, std::nullptr_t>) {
        LogFunc{}("MallocPageAllocator: Freeing %zu pages (%zu bytes) at %p\n",
                  actual_page_count, it->size, addr);
      }

      std::free(addr);

      // 更新计数器
      this->used_count_ -= actual_page_count;
      this->free_count_ += actual_page_count;

      // 移除记录
      allocated_blocks_.erase(it);
    } else {
      if constexpr (!std::is_same_v<LogFunc, std::nullptr_t>) {
        LogFunc{}(
            "MallocPageAllocator: Warning - trying to free unknown address "
            "%p\n",
            addr);
      }
    }
  }
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

// 测试夹具
class SlabMallocTest : public ::testing::Test {
 protected:
  static constexpr size_t kTestPages = 32;  // 测试用的页数

  void SetUp() override {
    // 使用 MallocPageAllocator 不需要预分配内存
  }

  void TearDown() override {
    // MallocPageAllocator 会自动清理
  }
};

/**
 * @brief 测试 MallocPageAllocator 的基本功能
 */
TEST_F(SlabMallocTest, MallocPageAllocatorBasicTest) {
  MallocPageAllocator<TestLogger, TestLock> allocator("test_malloc_allocator");

  // 测试分配
  void* ptr1 = allocator.Alloc(1);  // 分配1页
  ASSERT_NE(ptr1, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr1) % kPageSize, 0);  // 检查页对齐

  void* ptr2 = allocator.Alloc(2);  // 分配2页
  ASSERT_NE(ptr2, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % kPageSize, 0);  // 检查页对齐

  // 测试释放
  allocator.Free(ptr1);
  allocator.Free(ptr2);

  std::cout << "MallocPageAllocator basic test passed!\n";
}

/**
 * @brief Slab 分配器与 MallocPageAllocator 组合测试
 */
TEST_F(SlabMallocTest, SlabWithMallocAllocatorTest) {
  using MyMallocAllocator = MallocPageAllocator<TestLogger, TestLock>;
  using MySlab = TestableSlab<MyMallocAllocator, TestLogger, TestLock>;

  // 创建 Slab 分配器，使用 MallocPageAllocator
  // 为了让 Slab 正常工作，我们需要提供一个合理的页数
  // 分配一个初始内存区域给 Slab 使用
  void* initial_memory = std::aligned_alloc(kPageSize, 32 * kPageSize);
  ASSERT_NE(initial_memory, nullptr);

  MySlab slab("test_slab_malloc", initial_memory, 32);

  // 验证 Slab 初始化成功
  EXPECT_GT(slab.GetFreeCount(), 0);

  std::cout << "Slab with MallocPageAllocator initialized successfully!\n";

  // 清理
  std::free(initial_memory);
}

/**
 * @brief 测试 kmem_cache_create 功能
 */
TEST_F(SlabMallocTest, KmemCacheCreateTest) {
  using MyMallocAllocator = MallocPageAllocator<TestLogger, TestLock>;
  using MySlab = TestableSlab<MyMallocAllocator, TestLogger, TestLock>;

  // 分配初始内存给 Slab
  void* initial_memory = std::aligned_alloc(kPageSize, 32 * kPageSize);
  ASSERT_NE(initial_memory, nullptr);

  MySlab slab("test_slab_malloc", initial_memory, 32);

  // 测试构造函数和析构函数
  auto ctor = [](void* ptr) {
    // 简单的构造函数，将内存初始化为0x42
    memset(ptr, 0x42, sizeof(int));
  };

  auto dtor = [](void* ptr) {
    // 简单的析构函数，将内存清零
    memset(ptr, 0, sizeof(int));
  };

  // 创建缓存
  auto* cache =
      slab.find_create_kmem_cache("test_int_cache", sizeof(int), ctor, dtor);
  ASSERT_NE(cache, nullptr);
  EXPECT_STREQ(cache->name_, "test_int_cache");
  EXPECT_EQ(cache->objectSize_, sizeof(int));

  std::cout << "kmem_cache_create with malloc allocator test passed!\n";

  // 清理
  std::free(initial_memory);
}

/**
 * @brief 测试内存分配和释放
 */
TEST_F(SlabMallocTest, AllocationTest) {
  using MyMallocAllocator = MallocPageAllocator<TestLogger, TestLock>;
  using MySlab = TestableSlab<MyMallocAllocator, TestLogger, TestLock>;

  // 分配初始内存给 Slab
  void* initial_memory = std::aligned_alloc(kPageSize, 32 * kPageSize);
  ASSERT_NE(initial_memory, nullptr);

  MySlab slab("test_slab_malloc", initial_memory, 32);

  // 创建一个 int 类型的缓存
  auto* cache =
      slab.find_create_kmem_cache("int_cache", sizeof(int), nullptr, nullptr);
  ASSERT_NE(cache, nullptr);

  // 分配一些对象
  std::vector<void*> allocated_objects;
  for (int i = 0; i < 10; ++i) {
    void* obj = slab.kmem_cache_alloc(cache);
    ASSERT_NE(obj, nullptr);
    allocated_objects.push_back(obj);

    // 写入数据验证内存可用
    *static_cast<int*>(obj) = i;
  }

  // 验证数据
  for (size_t i = 0; i < allocated_objects.size(); ++i) {
    EXPECT_EQ(*static_cast<int*>(allocated_objects[i]), static_cast<int>(i));
  }

  // 释放对象
  for (void* obj : allocated_objects) {
    slab.kmem_cache_free(cache, obj);
  }

  std::cout << "Allocation test with malloc allocator passed!\n";

  // 清理
  std::free(initial_memory);
}

/**
 * @brief 测试大量分配和释放
 */
TEST_F(SlabMallocTest, StressTest) {
  using MyMallocAllocator = MallocPageAllocator<TestLogger, TestLock>;
  using MySlab = TestableSlab<MyMallocAllocator, TestLogger, TestLock>;

  // 分配初始内存给 Slab
  void* initial_memory =
      std::aligned_alloc(kPageSize, 64 * kPageSize);  // 更多内存用于压力测试
  ASSERT_NE(initial_memory, nullptr);

  MySlab slab("stress_test_slab", initial_memory, 64);

  // 创建不同大小的缓存
  auto* small_cache =
      slab.find_create_kmem_cache("small_cache", 32, nullptr, nullptr);
  auto* medium_cache =
      slab.find_create_kmem_cache("medium_cache", 128, nullptr, nullptr);
  auto* large_cache =
      slab.find_create_kmem_cache("large_cache", 512, nullptr, nullptr);

  ASSERT_NE(small_cache, nullptr);
  ASSERT_NE(medium_cache, nullptr);
  ASSERT_NE(large_cache, nullptr);

  std::vector<void*> small_objects, medium_objects, large_objects;

  // 分配大量对象
  for (int i = 0; i < 50; ++i) {
    void* small_obj = slab.kmem_cache_alloc(small_cache);
    void* medium_obj = slab.kmem_cache_alloc(medium_cache);
    void* large_obj = slab.kmem_cache_alloc(large_cache);

    ASSERT_NE(small_obj, nullptr);
    ASSERT_NE(medium_obj, nullptr);
    ASSERT_NE(large_obj, nullptr);

    small_objects.push_back(small_obj);
    medium_objects.push_back(medium_obj);
    large_objects.push_back(large_obj);
  }

  // 释放所有对象
  for (void* obj : small_objects) {
    slab.kmem_cache_free(small_cache, obj);
  }
  for (void* obj : medium_objects) {
    slab.kmem_cache_free(medium_cache, obj);
  }
  for (void* obj : large_objects) {
    slab.kmem_cache_free(large_cache, obj);
  }

  std::cout << "Stress test with malloc allocator passed!\n";

  // 清理
  std::free(initial_memory);
}

/**
 * @brief 测试多线程环境下的分配器
 */
TEST_F(SlabMallocTest, MultithreadTest) {
  using MyMallocAllocator = MallocPageAllocator<TestLogger, TestLock>;
  using MySlab = TestableSlab<MyMallocAllocator, TestLogger, TestLock>;

  // 分配初始内存给 Slab
  void* initial_memory =
      std::aligned_alloc(kPageSize, 64 * kPageSize);  // 更多内存用于多线程测试
  ASSERT_NE(initial_memory, nullptr);

  MySlab slab("multithread_test_slab", initial_memory, 64);

  auto* cache =
      slab.find_create_kmem_cache("mt_cache", sizeof(int), nullptr, nullptr);
  ASSERT_NE(cache, nullptr);

  const int num_threads = 4;
  const int allocations_per_thread = 25;
  std::vector<std::thread> threads;
  std::atomic<int> success_count{0};

  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back(
        [&slab, cache, allocations_per_thread, &success_count]() {
          std::vector<void*> local_objects;

          // 分配
          for (int i = 0; i < allocations_per_thread; ++i) {
            void* obj = slab.kmem_cache_alloc(cache);
            if (obj != nullptr) {
              local_objects.push_back(obj);
              *static_cast<int*>(obj) = i;
            }
          }

          // 验证
          bool all_valid = true;
          for (size_t i = 0; i < local_objects.size(); ++i) {
            if (*static_cast<int*>(local_objects[i]) != static_cast<int>(i)) {
              all_valid = false;
              break;
            }
          }

          // 释放
          for (void* obj : local_objects) {
            slab.kmem_cache_free(cache, obj);
          }

          if (all_valid && local_objects.size() == allocations_per_thread) {
            success_count.fetch_add(1);
          }
        });
  }

  // 等待所有线程完成
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(success_count.load(), num_threads);
  std::cout << "Multithread test with malloc allocator passed!\n";

  // 清理
  std::free(initial_memory);
}

/**
 * @brief 测试内存对齐
 */
TEST_F(SlabMallocTest, AlignmentTest) {
  using MyMallocAllocator = MallocPageAllocator<TestLogger, TestLock>;
  using MySlab = TestableSlab<MyMallocAllocator, TestLogger, TestLock>;

  // 分配初始内存给 Slab
  void* initial_memory = std::aligned_alloc(kPageSize, 32 * kPageSize);
  ASSERT_NE(initial_memory, nullptr);

  MySlab slab("alignment_test_slab", initial_memory, 32);

  // 测试基本类型的对齐 - 这些通常由slab分配器自然对齐
  auto* cache_int = slab.find_create_kmem_cache(
      "int_cache", sizeof(int), nullptr, nullptr);
  auto* cache_long = slab.find_create_kmem_cache(
      "long_cache", sizeof(long), nullptr, nullptr);
  auto* cache_double = slab.find_create_kmem_cache(
      "double_cache", sizeof(double), nullptr, nullptr);

  ASSERT_NE(cache_int, nullptr);
  ASSERT_NE(cache_long, nullptr);
  ASSERT_NE(cache_double, nullptr);

  // 分配并检查基本对齐
  void* obj_int = slab.kmem_cache_alloc(cache_int);
  void* obj_long = slab.kmem_cache_alloc(cache_long);
  void* obj_double = slab.kmem_cache_alloc(cache_double);

  ASSERT_NE(obj_int, nullptr);
  ASSERT_NE(obj_long, nullptr);
  ASSERT_NE(obj_double, nullptr);

  // 检查基本数据类型的自然对齐
  EXPECT_EQ(reinterpret_cast<uintptr_t>(obj_int) % alignof(int), 0);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(obj_long) % alignof(long), 0);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(obj_double) % alignof(double), 0);

  // 测试分配的对象是否可用
  *static_cast<int*>(obj_int) = 42;
  *static_cast<long*>(obj_long) = 12345678L;
  *static_cast<double*>(obj_double) = 3.14159;

  // 验证数据
  EXPECT_EQ(*static_cast<int*>(obj_int), 42);
  EXPECT_EQ(*static_cast<long*>(obj_long), 12345678L);
  EXPECT_DOUBLE_EQ(*static_cast<double*>(obj_double), 3.14159);

  // 测试多个对象的分配，确保它们都有合适的对齐
  std::vector<void*> int_objects;
  for (int i = 0; i < 10; ++i) {
    void* obj = slab.kmem_cache_alloc(cache_int);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(obj) % alignof(int), 0);
    int_objects.push_back(obj);
  }

  // 释放内存
  slab.kmem_cache_free(cache_int, obj_int);
  slab.kmem_cache_free(cache_long, obj_long);
  slab.kmem_cache_free(cache_double, obj_double);
  
  for (void* obj : int_objects) {
    slab.kmem_cache_free(cache_int, obj);
  }

  std::cout << "Alignment test with malloc allocator passed!\n";

  // 清理
  std::free(initial_memory);
}

/**
 * @brief 测试 4K 页分配和数据验证
 */
TEST_F(SlabMallocTest, Page4KAllocationTest) {
  MallocPageAllocator<TestLogger, TestLock> allocator("test_4k_page_allocator");

  // 测试分配单个 4K 页
  void* page1 = allocator.Alloc(1);
  ASSERT_NE(page1, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(page1) % kPageSize, 0);  // 检查 4K 对齐

  // 测试分配多个 4K 页
  void* page2 = allocator.Alloc(2);  // 8K (2 pages)
  ASSERT_NE(page2, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(page2) % kPageSize, 0);  // 检查 4K 对齐

  void* page4 = allocator.Alloc(4);  // 16K (4 pages)
  ASSERT_NE(page4, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(page4) % kPageSize, 0);  // 检查 4K 对齐

  // 数据验证测试 - 写入特定模式并验证
  {
    // 对单页进行数据验证
    uint32_t* data1 = static_cast<uint32_t*>(page1);
    const uint32_t pattern1 = 0x12345678;

    // 写入模式 - 填充整个 4K 页
    for (size_t i = 0; i < kPageSize / sizeof(uint32_t); ++i) {
      data1[i] = pattern1 + static_cast<uint32_t>(i);
    }

    // 验证数据
    for (size_t i = 0; i < kPageSize / sizeof(uint32_t); ++i) {
      EXPECT_EQ(data1[i], pattern1 + static_cast<uint32_t>(i))
          << "Data mismatch at offset " << i;
    }
  }

  {
    // 对 2 页进行数据验证
    uint64_t* data2 = static_cast<uint64_t*>(page2);
    const uint64_t pattern2 = 0xABCDEF0123456789ULL;
    const size_t total_elements = (2 * kPageSize) / sizeof(uint64_t);

    // 写入递增模式
    for (size_t i = 0; i < total_elements; ++i) {
      data2[i] = pattern2 + i;
    }

    // 验证数据
    for (size_t i = 0; i < total_elements; ++i) {
      EXPECT_EQ(data2[i], pattern2 + i)
          << "Data mismatch in 2-page allocation at offset " << i;
    }
  }

  {
    // 对 4 页进行复杂数据验证
    struct TestData {
      uint32_t id;
      uint32_t checksum;
      char padding[24];  // 总共 32 字节
    };

    TestData* data4 = static_cast<TestData*>(page4);
    const size_t total_structs = (4 * kPageSize) / sizeof(TestData);

    // 写入结构化数据
    for (size_t i = 0; i < total_structs; ++i) {
      data4[i].id = static_cast<uint32_t>(i);
      data4[i].checksum =
          static_cast<uint32_t>(i * 0x9E3779B9);  // 简单的校验和
      // 填充区域用特定模式
      memset(data4[i].padding, static_cast<int>(i & 0xFF),
             sizeof(data4[i].padding));
    }

    // 验证结构化数据
    for (size_t i = 0; i < total_structs; ++i) {
      EXPECT_EQ(data4[i].id, static_cast<uint32_t>(i))
          << "ID mismatch in 4-page allocation at struct " << i;
      EXPECT_EQ(data4[i].checksum, static_cast<uint32_t>(i * 0x9E3779B9))
          << "Checksum mismatch in 4-page allocation at struct " << i;

      // 验证填充区域
      for (size_t j = 0; j < sizeof(data4[i].padding); ++j) {
        EXPECT_EQ(static_cast<unsigned char>(data4[i].padding[j]),
                  static_cast<unsigned char>(i & 0xFF))
            << "Padding mismatch in 4-page allocation at struct " << i
            << ", byte " << j;
      }
    }
  }

  // 测试页边界 - 确保不同页的数据不会互相干扰
  {
    uint8_t* byte_ptr1 = static_cast<uint8_t*>(page1);
    uint8_t* byte_ptr2 = static_cast<uint8_t*>(page2);

    // 在第一页的最后一个字节写入特定值
    byte_ptr1[kPageSize - 1] = 0xAA;

    // 确保这不会影响第二页的数据
    // （注意：这个测试假设page1和page2是不同的内存区域）
    if (byte_ptr2 != byte_ptr1) {
      byte_ptr2[0] = 0x55;
      EXPECT_EQ(byte_ptr1[kPageSize - 1], 0xAA);
      EXPECT_EQ(byte_ptr2[0], 0x55);
    }
  }

  // 测试内存清零功能
  {
    void* zero_page = allocator.Alloc(1);
    ASSERT_NE(zero_page, nullptr);

    uint8_t* zero_bytes = static_cast<uint8_t*>(zero_page);
    bool all_zero = true;

    // 检查新分配的页是否已清零
    for (size_t i = 0; i < kPageSize; ++i) {
      if (zero_bytes[i] != 0) {
        all_zero = false;
        break;
      }
    }

    EXPECT_TRUE(all_zero) << "Newly allocated page should be zero-initialized";

    allocator.Free(zero_page);
  }

  // 释放所有分配的页
  allocator.Free(page1);
  allocator.Free(page2);
  allocator.Free(page4);

  std::cout << "4K page allocation and data validation test passed!\n";
}
