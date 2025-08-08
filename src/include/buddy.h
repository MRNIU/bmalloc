/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_BUDDY_H_
#define BMALLOC_SRC_INCLUDE_BUDDY_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "allocator_base.h"

namespace bmalloc {

class Buddy : public AllocatorBase {
 public:
  /**
   * @brief 构造 Buddy 分配器
   * @param name 分配器名称
   * @param start_addr 管理的内存起始地址
   * @param total_pages 管理的总页数
   * @note 内部会将length_设置为最大阶数级别(log2(total_pages)+1)，而不是页数
   */
  explicit Buddy(const char* name, void* start_addr, size_t total_pages);

  /// @name 构造/析构函数
  /// @{
  Buddy() = default;
  Buddy(const Buddy&) = delete;
  Buddy(Buddy&&) = default;
  auto operator=(const Buddy&) -> Buddy& = delete;
  auto operator=(Buddy&&) -> Buddy& = default;
  ~Buddy() override = default;
  /// @}

  /**
   * @brief 分配2的幂次方页数的内存
   * @param order 阶数，实际分配 2^order 个页面
   * @return void* 分配的内存起始地址，失败时返回 nullptr
   * @note 例如：order=0分配1页，order=1分配2页，order=2分配4页
   */
  [[nodiscard]] auto Alloc(size_t order) -> void* override;

  /**
   * 不支持指定地址分配
   */
  auto Alloc(void*, size_t) -> bool override;

  /**
   * @brief 释放2的幂次方页数的内存
   * @param addr 要释放的内存起始地址
   * @param order 阶数，实际释放 2^order 个页面
   * @note 必须与分配时使用的order值相同
   */
  void Free(void* addr, size_t order) override;

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
  // 常量定义
  // 最大支持2^31个页面，足够大部分应用
  static constexpr size_t kMaxFreeListEntries = 32;

  // 固定大小的数组，避免占用管理的内存空间
  // 空闲块链表数组，free_block_lists_[i]管理大小为2^i页的空闲块链表
  std::array<void*, kMaxFreeListEntries> free_block_lists_{};

  // 调试用：打印buddy分配器当前状态
  void buddy_print();

  // 检查地址是否为2^order大小块的有效起始地址
  inline bool isValid(void* space, int order) const;
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_BUDDY_H_ */
