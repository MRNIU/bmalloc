/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_
#define BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_

#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "allocator_base.hpp"

/**
 * @brief 基于标准C库内存接口的分配器
 * @details 直接使用标准库的 malloc、free、calloc、realloc、aligned_alloc 等函数
 * @tparam LogFunc printf 风格的日志函数类型
 * @tparam Lock 锁类型
 */
template <class LogFunc = std::nullptr_t, class Lock = bmalloc::LockBase>
class StandardAllocator : public bmalloc::AllocatorBase<LogFunc, Lock> {
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
  explicit StandardAllocator(const char* name = "StandardAllocator", void* addr = nullptr, size_t length = 0)
      : bmalloc::AllocatorBase<LogFunc, Lock>(name, addr, length) {
    (void)addr;    // 避免未使用参数警告
    (void)length;  // 避免未使用参数警告
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
    return std::malloc(length);
  }

  /**
   * @brief 释放指定地址的内存的实际实现（线程不安全）
   * @param addr 要释放的地址
   * @param order 要释放的页面数的对数（未使用，标准分配器不需要大小信息）
   */
  void FreeImpl(void* addr, [[maybe_unused]] size_t order = 0) override {
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
