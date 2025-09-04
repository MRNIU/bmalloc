/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_
#define BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_

#include <cstdlib>
#include <cstring>

#include "allocator_base.hpp"

/**
 * @brief 基于标准C库内存接口的分配器
 * @details 直接使用标准库的 malloc、free、calloc、realloc、aligned_alloc 等函数
 * @tparam LogFunc printf 风格的日志函数类型
 * @tparam Lock 锁类型
 */
template <class LogFunc = std::nullptr_t, class Lock = bmalloc::LockBase>
class StandardAllocator : public AllocatorBase<LogFunc, Lock> {
  static_assert(std::is_base_of_v<LockBase, Lock> ||
                    std::is_same_v<Lock, LockBase>,
                "Lock must be derived from LockBase or be LockBase itself");

 public:
  /**
   * @brief 构造标准分配器
   * @param name 分配器名称
   */
  explicit StandardAllocator(const char* name = "StandardAllocator")
      : AllocatorBase<LogFunc, Lock>(name, nullptr, 0) {
    this->Log("StandardAllocator '%s' initialized\n", name);
  }

  /// @name 构造/析构函数
  /// @{
  StandardAllocator(const StandardAllocator&) = delete;
  StandardAllocator(StandardAllocator&&) = default;
  auto operator=(const StandardAllocator&) -> StandardAllocator& = delete;
  auto operator=(StandardAllocator&&) -> StandardAllocator& = default;
  ~StandardAllocator() override = default;
  /// @}

 protected:
  /**
   * @brief 分配指定长度的内存的实际实现（线程不安全）
   * @param length 要分配的长度（字节）
   * @return void* 分配到的地址，失败时返回nullptr
   */
  [[nodiscard]] auto AllocImpl(size_t length) -> void* override {
    if (length == 0) {
      return nullptr;
    }
    return std::malloc(length);
  }

  /**
   * @brief 释放指定地址的内存的实际实现（线程不安全）
   * @param addr 要释放的地址
   * @param length 要释放的长度（未使用）
   */
  void FreeImpl(void* addr, [[maybe_unused]] size_t length = 0) override {
    std::free(addr);
  }

 public:
  /**
   * @brief 分配并初始化为零的内存块
   * @param num 元素个数
   * @param size 每个元素的大小
   * @return void* 分配成功时返回内存地址，失败时返回nullptr
   */
  [[nodiscard]] auto calloc(size_t num, size_t size) -> void* {
    return std::calloc(num, size);
  }

  /**
   * @brief 重新分配内存块大小
   * @param ptr 要重新分配的内存指针，可以为nullptr
   * @param new_size 新的内存大小（字节）
   * @return void* 重新分配成功时返回新内存地址，失败时返回nullptr
   */
  [[nodiscard]] auto realloc(void* ptr, size_t new_size) -> void* {
    return std::realloc(ptr, new_size);
  }

  /**
   * @brief 分配对齐的内存块
   * @param alignment 内存对齐要求（必须是2的幂）
   * @param size 要分配的内存大小（字节）
   * @return void* 分配成功时返回对齐的内存地址，失败时返回nullptr
   */
  [[nodiscard]] auto aligned_alloc(size_t alignment, size_t size) -> void* {
    return std::aligned_alloc(alignment, size);
  }

  /**
   * @brief 获取内存块的实际大小（标准库不提供此功能，返回0）
   * @param ptr 内存指针
   * @return size_t 始终返回0，因为标准库没有提供获取分配大小的函数
   */
  [[nodiscard]] auto malloc_size([[maybe_unused]] void* ptr) -> size_t {
    // 标准库不提供获取分配大小的功能
    return 0;
  }
};

#endif /* BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_ */
