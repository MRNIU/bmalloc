/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_BUMP_HPP_
#define BMALLOC_SRC_INCLUDE_BUMP_HPP_

#include <cstddef>
#include <cstdint>

#include "allocator_base.hpp"

namespace bmalloc {

/**
 * @brief 极简 bump allocator
 * @details
 * 只支持向前线性分配，不真正回收内存，适合作为启动阶段或测试用的最小分配器。
 *          管理单位为字节，length 参数表示可管理的总字节数。
 */
template <class LogFunc = std::nullptr_t, class Lock = LockBase>
class BumpAllocator : public AllocatorBase<LogFunc, Lock> {
 public:
  using Base = AllocatorBase<LogFunc, Lock>;
  using Base::Alloc;
  using Base::AllocSize;
  using Base::Free;

  /**
   * @brief 构造 bump allocator
   * @param name 分配器名称
   * @param start_addr 管理的内存起始地址
   * @param bytes 管理的总字节数
   */
  explicit BumpAllocator(const char* name, void* start_addr, size_t bytes)
      : Base(name, start_addr, bytes),
        current_(reinterpret_cast<uintptr_t>(start_addr)),
        end_(reinterpret_cast<uintptr_t>(start_addr) + bytes) {}

  /// @name 构造/析构函数
  /// @{
  BumpAllocator() = default;
  BumpAllocator(const BumpAllocator&) = delete;
  BumpAllocator(BumpAllocator&&) = default;
  auto operator=(const BumpAllocator&) -> BumpAllocator& = delete;
  auto operator=(BumpAllocator&&) -> BumpAllocator& = default;
  ~BumpAllocator() override = default;
  /// @}

 protected:
  using Base::Log;

  /// 当前分配指针
  uintptr_t current_ = 0;
  /// 管理区域结束地址（开区间）
  uintptr_t end_ = 0;

  /**
   * @brief 线性向前分配指定字节数的内存
   * @param bytes 要分配的字节数
   * @return void* 分配成功时返回内存地址，失败时返回nullptr
   */
  [[nodiscard]] auto AllocImpl(size_t bytes) -> void* override {
    if (bytes == 0) {
      return nullptr;
    }

    // 按 max_align_t 对齐
    constexpr size_t align = alignof(max_align_t);
    auto align_up = [](uintptr_t v, size_t a) {
      return (v + (a - 1)) & ~(static_cast<uintptr_t>(a) - 1);
    };

    auto cur = align_up(current_, align);
    auto next = cur + bytes;

    if (next > end_) {
      Log("Bump allocator '%s' out of memory: request=%zu, remain=%zu\n",
          this->name_, bytes,
          (end_ > cur) ? static_cast<size_t>(end_ - cur) : 0U);
      return nullptr;
    }

    current_ = next;

    // 统计信息以字节为单位
    this->used_count_ = static_cast<size_t>(
        current_ - reinterpret_cast<uintptr_t>(this->start_addr_));
    this->free_count_ =
        (end_ > current_) ? static_cast<size_t>(end_ - current_) : 0U;

    return reinterpret_cast<void*>(cur);
  }

  /**
   * @brief bump allocator 不支持逐块释放，Free 为空操作
   * @param addr 要释放的地址（忽略）
   * @param length 要释放的长度（忽略）
   */
  void FreeImpl([[maybe_unused]] void* addr,
                [[maybe_unused]] size_t length) override {
    // 不做任何事情；如需重置整个区域，可额外提供 Reset() 接口
  }

  /**
   * @brief 获取内存块的实际大小
   * @param addr 内存指针
   * @return size_t 由于 bump allocator 不记录单块大小，这里固定返回 0
   */
  [[nodiscard]] auto AllocSizeImpl([[maybe_unused]] void* addr) const
      -> size_t override {
    return 0;
  }
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_BUMP_HPP_ */
