/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_FIRST_FIT_H_
#define BMALLOC_SRC_INCLUDE_FIRST_FIT_H_

#include <bitset>
#include <cstddef>
#include <cstdint>

#include "allocator_base.hpp"

namespace bmalloc {

/**
 * @brief First Fit 算法内存分配器
 * @details 使用位图来跟踪内存页的使用情况，支持分配和释放指定页数的内存。
 */
class FirstFit : public AllocatorBase {
 public:
  /**
   * @brief 构造First Fit分配器
   * @param name 分配器名称
   * @param start_addr 管理的内存起始地址
   * @param page_count 管理的页数
   * @param log_func printf 风格的日志函数指针（可选）
   */
  explicit FirstFit(const char* name, void* start_addr, size_t page_count,
                    int (*log_func)(const char*, ...) = nullptr);

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

  /**
   * @brief 分配指定页数的内存
   * @param page_count 要分配的页数
   * @return void* 分配的内存起始地址，失败时返回0
   */
  [[nodiscard]] auto AllocImpl(size_t page_count) -> void* override;

  /**
   * @brief 释放指定地址的内存
   * @param addr 要释放的内存起始地址
   * @param page_count 要释放的页数
   */
  void FreeImpl(void* addr, size_t page_count) override;

  /**
   * @brief 查找连续的指定值的位序列
   * @param length 需要连续的位数
   * @param value 要查找的位值
   * @return size_t 开始索引，如果未找到返回SIZE_MAX
   */
  [[nodiscard]] auto FindConsecutiveBits(size_t length, bool value) const
      -> size_t;
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_FIRST_FIT_H_ */
