/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_
#define BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_

#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <unordered_set>

#include "src/include/allocator_base.hpp"
#ifndef BUDDY_ALLOC_IMPLEMENTATION
#define BUDDY_ALLOC_IMPLEMENTATION
#endif

#include "buddy_alloc.h"
#undef BUDDY_ALLOC_IMPLEMENTATION

/**
 * @brief 基于标准C库内存接口的分配器
 * @details 直接使用标准库的 malloc、free、calloc、realloc、aligned_alloc 等函数
 * @tparam LogFunc printf 风格的日志函数类型
 * @tparam Lock 锁类型
 */
template <class LogFunc = std::nullptr_t, class Lock = bmalloc::LockBase>
class TMPAllocator : public bmalloc::AllocatorBase<LogFunc, Lock> {
  static_assert(std::is_base_of_v<bmalloc::LockBase, Lock> ||
                    std::is_same_v<Lock, bmalloc::LockBase>,
                "Lock must be derived from LockBase or be LockBase itself");

 public:
  /**
   * @brief 构造标准分配器
   * @param name 分配器名称
   * @param addr 内存地址（忽略，标准分配器不使用预分配内存）
   * @param length 内存长度（忽略，标准分配器不使用预分配内存）
   */
  explicit TMPAllocator(const char* name = "TMPAllocator", void* addr = nullptr,
                        size_t length = 0)
      : bmalloc::AllocatorBase<LogFunc, Lock>(name, addr, length) {
    auto* buddy_metadata = (uint8_t*)malloc(buddy_sizeof(length*4096));
    buddy = buddy_init(buddy_metadata, (uint8_t*)addr, length*4096);
  }

  /// @name 构造/析构函数
  /// @{
  TMPAllocator(const TMPAllocator&) = delete;
  TMPAllocator(TMPAllocator&&) = default;
  auto operator=(const TMPAllocator&) -> TMPAllocator& = delete;
  auto operator=(TMPAllocator&&) -> TMPAllocator& = default;
  ~TMPAllocator() override = default;
  /// @}

 protected:
  /**
   * @brief 分配指定长度的内存的实际实现（线程不安全）
   * @param order 要分配的页面数的对数（如 order=2 表示分配 2^2=4 个页面）
   * @return void* 分配到的地址，失败时返回nullptr
   */
  [[nodiscard]] auto AllocImpl(size_t order) -> void* override {
    // 将 order 转换为字节数：2^order 个页面，每个页面 4096 字节
    size_t pages = 1ULL << order;
    size_t length = pages * 4096;  // kPageSize = 4096

    if (length == 0) {
      return nullptr;
    }

    return buddy_malloc(buddy, length);
  }

  /**
   * @brief 释放指定地址的内存的实际实现（线程不安全）
   * @param addr 要释放的地址
   * @param order 要释放的页面数的对数（未使用，标准分配器不需要大小信息）
   */
  void FreeImpl(void* addr, [[maybe_unused]] size_t order = 0) override {
    buddy_free(buddy, addr);
  }

 private:
  struct buddy* buddy;
};

#endif /* BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_ */
