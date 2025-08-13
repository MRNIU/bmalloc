/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_FIRST_FIT_HPP_
#define BMALLOC_SRC_INCLUDE_FIRST_FIT_HPP_

#include <bitset>
#include <cstddef>
#include <cstdint>

#include "allocator_base.hpp"

namespace bmalloc {

/**
 * @brief First Fit 算法内存分配器
 * @details 使用位图来跟踪内存页的使用情况，支持分配和释放指定页数的内存。
 */
template <class LogFunc = std::nullptr_t, class Lock = LockBase>
class FirstFit : public AllocatorBase<LogFunc, Lock> {
 public:
  using AllocatorBase<LogFunc, Lock>::Alloc;
  using AllocatorBase<LogFunc, Lock>::Free;
  using AllocatorBase<LogFunc, Lock>::GetFreeCount;
  using AllocatorBase<LogFunc, Lock>::GetUsedCount;

  /**
   * @brief 构造First Fit分配器
   * @param name 分配器名称
   * @param start_addr 管理的内存起始地址
   * @param page_count 管理的页数
   * @param log_func printf 风格的日志函数指针（可选）
   */
  explicit FirstFit(const char* name, void* start_addr, size_t page_count)
      : AllocatorBase<LogFunc, Lock>(name, start_addr, page_count) {
    // 初始化位图为全0 (所有页面空闲)
    bitmap_.reset();
  }

  /// @name 构造/析构函数
  /// @{
  FirstFit() = default;
  FirstFit(const FirstFit&) = delete;
  FirstFit(FirstFit&&) = default;
  auto operator=(const FirstFit&) -> FirstFit& = delete;
  auto operator=(FirstFit&&) -> FirstFit& = default;
  ~FirstFit() override = default;
  /// @}

 protected:
  /// @todo 用 bitset 表示太浪费空间了
  static constexpr size_t kMaxPages = 1024;
  /// 位图，每一位表示一页内存，1 表示已使用，0 表示未使用
  std::bitset<kMaxPages> bitmap_;

  using AllocatorBase<LogFunc, Lock>::Log;
  using AllocatorBase<LogFunc, Lock>::name_;
  using AllocatorBase<LogFunc, Lock>::start_addr_;
  using AllocatorBase<LogFunc, Lock>::length_;
  using AllocatorBase<LogFunc, Lock>::free_count_;
  using AllocatorBase<LogFunc, Lock>::used_count_;

  /**
   * @brief 分配指定页数的内存
   * @param page_count 要分配的页数
   * @return void* 分配的内存起始地址，失败时返回0
   */
  [[nodiscard]] auto AllocImpl(size_t page_count) -> void* override {
    if (page_count == 0 || page_count > free_count_) {
      Log("FirstFit allocator '%s' allocation failed: invalid page_count=%zu "
          "(free_count=%zu)\n",
          name_, page_count, free_count_);
      return nullptr;
    }

    // 在位图中寻找连续的空闲页面
    size_t start_idx = FindConsecutiveBits(page_count, false);
    if (start_idx == SIZE_MAX) {
      Log("FirstFit allocator '%s' allocation failed: no consecutive free "
          "pages "
          "found for %zu pages\n",
          name_, page_count);
      return nullptr;
    }

    // 标记这些页面为已使用
    for (size_t i = start_idx; i < start_idx + page_count; ++i) {
      bitmap_[i] = true;
    }

    // 计算实际物理地址
    void* allocated_addr = static_cast<char*>(const_cast<void*>(start_addr_)) +
                           (kPageSize * start_idx);

    // 更新统计信息
    free_count_ -= page_count;
    used_count_ += page_count;

    return allocated_addr;
  }

  /**
   * @brief 释放指定地址的内存
   * @param addr 要释放的内存起始地址
   * @param page_count 要释放的页数
   */
  void FreeImpl(void* addr, size_t page_count) override {
    // 将 void* 转换为 uintptr_t 进行地址计算
    uintptr_t target_addr = reinterpret_cast<uintptr_t>(addr);
    uintptr_t start_addr = reinterpret_cast<uintptr_t>(start_addr_);

    // 检查地址是否在管理范围内
    if (target_addr < start_addr ||
        target_addr >= start_addr + length_ * kPageSize) {
      Log("FirstFit allocator '%s' free failed: addr=%p out of range [%p, "
          "%p)\n",
          name_, addr, start_addr_,
          static_cast<void*>(
              static_cast<char*>(const_cast<void*>(start_addr_)) +
              length_ * kPageSize));
      return;
    }

    // 计算页面索引
    size_t start_idx = (target_addr - start_addr) / kPageSize;

    // 检查是否超出边界
    if (start_idx + page_count > length_) {
      Log("FirstFit allocator '%s' free failed: start_idx=%zu + page_count=%zu "
          "> "
          "length=%zu\n",
          name_, start_idx, page_count, length_);
      return;
    }

    // 标记页面为空闲
    for (size_t i = start_idx; i < start_idx + page_count; ++i) {
      bitmap_[i] = false;
    }
    // 更新统计信息
    free_count_ += page_count;
    used_count_ -= page_count;
  }

  /**
   * @brief 查找连续的指定值的位序列
   * @param length 需要连续的位数
   * @param value 要查找的位值
   * @return size_t 开始索引，如果未找到返回SIZE_MAX
   */
  [[nodiscard]] auto FindConsecutiveBits(size_t length, bool value) const
      -> size_t {
    if (length == 0 || length > length_) {
      Log("FirstFit allocator '%s' search failed: invalid length=%zu "
          "(max=%zu)\n",
          name_, length, length_);
      return SIZE_MAX;
    }

    size_t count = 0;
    size_t start_idx = 0;

    // 遍历位图寻找连续的指定值位
    for (size_t i = 0; i < length_; ++i) {
      if (bitmap_[i] == value) {
        if (count == 0) {
          start_idx = i;
        }
        ++count;
        if (count == length) {
          return start_idx;
        }
      } else {
        count = 0;
      }
    }

    Log("FirstFit allocator '%s' search failed: no %zu consecutive %s pages "
        "found\n",
        name_, length, value ? "used" : "free");
    return SIZE_MAX;
  }
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_FIRST_FIT_HPP_ */
