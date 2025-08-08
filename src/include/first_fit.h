/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_FIRST_FIT_H_
#define BMALLOC_SRC_INCLUDE_FIRST_FIT_H_

#include <bitset>
#include <cstddef>
#include <cstdint>

#include "allocator_base.h"

/**
 * @brief First Fit 算法内存分配器
 * @details 使用位图来跟踪内存页的使用情况，支持分配和释放指定页数的内存。
 * @tparam kMaxPages 最大页数，默认为 1024
 */
template <size_t kMaxPages = 1024>
class FirstFit : public AllocatorBase {
 public:
  /**
   * @brief 构造First Fit分配器
   * @param name 分配器名称
   * @param start_addr 管理的内存起始地址
   * @param page_count 管理的页数
   */
  explicit FirstFit(const char* name, uint64_t start_addr, size_t page_count);

  /// @name 构造/析构函数
  /// @{
  FirstFit() = default;
  FirstFit(const FirstFit&) = delete;
  FirstFit(FirstFit&&) = default;
  auto operator=(const FirstFit&) -> FirstFit& = delete;
  auto operator=(FirstFit&&) -> FirstFit& = default;
  ~FirstFit() override = default;
  /// @}

  /**
   * @brief 分配指定页数的内存
   * @param page_count 要分配的页数
   * @return uint64_t 分配的内存起始地址，失败时返回0
   */
  [[nodiscard]] auto Alloc(size_t page_count) -> uint64_t override;

  /**
   * @brief 在指定地址分配指定页数的内存
   * @param addr 指定的地址
   * @param page_count 要分配的页数
   * @return true 分配成功
   * @return false 分配失败
   */
  auto Alloc(uint64_t addr, size_t page_count) -> bool override;

  /**
   * @brief 释放指定地址的内存
   * @param addr 要释放的内存起始地址
   * @param page_count 要释放的页数
   */
  void Free(uint64_t addr, size_t page_count) override;

  /**
   * @brief 获取已使用的页数
   * @return size_t 已使用的页数
   */
  [[nodiscard]] auto GetUsedCount() const -> size_t override;

  /**
   * @brief 获取空闲的页数
   * @return size_t 空闲的页数
   */
  [[nodiscard]] auto GetFreeCount() const -> size_t override;

 private:
  /// 位图，每一位表示一页内存，1表示已使用，0表示未使用
  std::bitset<kMaxPages> bitmap_;

  /**
   * @brief 查找连续的指定值的位序列
   * @param length 需要连续的位数
   * @param value 要查找的位值
   * @return size_t 开始索引，如果未找到返回SIZE_MAX
   */
  [[nodiscard]] auto FindConsecutiveBits(size_t length, bool value) const
      -> size_t;
};

#endif /* BMALLOC_SRC_INCLUDE_FIRST_FIT_H_ */
