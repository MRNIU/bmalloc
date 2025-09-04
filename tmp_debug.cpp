// g++ -std=c++23 -g -ggdb ../tmp_debug.cpp

/*
➜  build git:(main) ✗ ./a.out
Buddy allocator '4k_page_buddy_test_slab' found block at order 7,
node=0xaaaae65d2000, node->next=(nil) Buddy allocator '4k_page_buddy_test_slab'
found block at order 0, node=0xaaaae65d3000, node->next=(nil) Allocated page 0
at 0xaaaae65d303c (page offset: 0x3c, 4K offset: 0x3c) Buddy allocator
'4k_page_buddy_test_slab' found block at order 1, node=0xaaaae65d4000,
node->next=0xbeef03f2beef03f1 Allocated page 1 at 0xaaaae65d407c (page offset:
0x7c, 4K offset: 0x7c) Buddy allocator '4k_page_buddy_test_slab' found block at
order 0, node=0xaaaae65d5000, node->next=0xbeef03e3beef03e2 Allocated page 2 at
0xaaaae65d50bc (page offset: 0xbc, 4K offset: 0xbc) [1]    346520 segmentation
fault  ./a.out
*/

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

#include "src/include/buddy.hpp"
#include "src/include/slab.hpp"

using namespace bmalloc;

static constexpr const size_t kTestMemorySize = kPageSize * 128;
static constexpr const size_t kTestPages = kTestMemorySize / kPageSize;
alignas(4096) static void* kTestMemory[kTestMemorySize]{};
const size_t num_ints = kPageSize / sizeof(uint32_t);

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
  using Base::kmem_cache_allInfo;
  using Base::kmem_cache_alloc;
  using Base::kmem_cache_destroy;
  using Base::kmem_cache_free;
  using Base::kmem_cache_shrink;
};

/**
 * @brief 测试 4K 页面分配和数据验证 (使用 Buddy 分配器)
 *
 * 这个测试用例专门测试4K大小的页面分配，包括：
 * 1. 分配4K大小的对象
 * 2. 验证数据写入和读取的正确性
 * 3. 边界检查和内存安全验证
 * 4. 多个4K页面的并发分配
 * 5. Buddy分配器特定的验证
 *
 * 注意：Buddy分配器可能提供更好的对齐保证
 */
int main(int, char**) {
  // 使用带日志的Buddy分配器配置
  using MyBuddy = Buddy<TestLogger>;
  using MySlab = TestableSlab<MyBuddy, TestLogger>;

  MySlab slab("4k_page_buddy_test_slab", kTestMemory, kTestPages);
  auto page_4k_cache = slab.find_create_kmem_cache("4k_page_buddy_cache",
                                                   kPageSize, nullptr, nullptr);
  std::vector<void*> allocated_pages;
  std::vector<uint32_t> page_patterns;

  // 分配多个页面
  const size_t max_pages = std::min(static_cast<size_t>(8),
                                    kTestPages / (page_4k_cache->order_ + 1));

  for (size_t i = 0; i < max_pages; i++) {
    void* page = slab.kmem_cache_alloc(page_4k_cache);
    if (page == nullptr) {
      printf("Could only allocate %zu additional pages\n", i);
      break;
    }

    allocated_pages.push_back(page);
    uint32_t pattern = 0xBEEF0000 + static_cast<uint32_t>(i);  // Buddy特定模式
    page_patterns.push_back(pattern);

    // 记录页面地址信息
    uintptr_t addr = reinterpret_cast<uintptr_t>(page);
    printf(
        "Allocated page %zu at 0x%lx (page offset: 0x%lx, 4K offset: 0x%lx), "
        "pattern: 0x%08x\n",
        i, addr, addr % kPageSize, addr % kPageSize, pattern);

    // 写入唯一模式
    uint32_t* page_ints = static_cast<uint32_t*>(page);
    for (size_t j = 0; j < num_ints; j++) {
      page_ints[j] = pattern + static_cast<uint32_t>(j);
    }

    // slab.kmem_cache_allInfo();
  }

  std::cout << "   Successfully allocated " << allocated_pages.size()
            << " additional 4K pages using Buddy allocator\n";

  return 0;
}
