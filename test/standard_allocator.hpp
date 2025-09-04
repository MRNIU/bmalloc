/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_
#define BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_

#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <unordered_set>

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
  explicit StandardAllocator(const char* name = "StandardAllocator",
                             void* addr = nullptr, size_t length = 0)
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
  ~StandardAllocator() override {
    // 释放所有未释放的内存
    for (void* addr : allocated_addresses_) {
      this->Log("StandardAllocator: Releasing leaked memory at %p\n", addr);
      std::free(addr);
    }
    if (!allocated_addresses_.empty()) {
      this->Log("StandardAllocator: Released %zu leaked allocations\n",
                allocated_addresses_.size());
    }
  }
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

    void* addr = std::malloc(length);
    if (addr != nullptr) {
      // 记录分配的地址
      allocated_addresses_.insert(addr);
      this->Log("StandardAllocator: Allocated %zu bytes at %p\n", length, addr);
    }
    return addr;
  }

  /**
   * @brief 释放指定地址的内存的实际实现（线程不安全）
   * @param addr 要释放的地址
   * @param order 要释放的页面数的对数（未使用，标准分配器不需要大小信息）
   */
  void FreeImpl(void* addr, [[maybe_unused]] size_t order = 0) override {
    if (addr != nullptr) {
      // 从记录中移除地址
      auto it = allocated_addresses_.find(addr);
      if (it != allocated_addresses_.end()) {
        allocated_addresses_.erase(it);
        this->Log("StandardAllocator: Freed memory at %p\n", addr);
      } else {
        this->Log("StandardAllocator: Warning - Freeing untracked address %p\n",
                  addr);
      }
      std::free(addr);
    }
  }

 private:
  /// @brief 记录已分配的地址
  std::unordered_set<void*> allocated_addresses_;
};

#endif /* BMALLOC_TEST_STANDARD_ALLOCATOR_HPP_ */
