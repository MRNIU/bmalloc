/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_BUDDY_H_
#define BMALLOC_SRC_INCLUDE_BUDDY_H_

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
   * @param pages 管理的页数
   */
  explicit Buddy(const char* name, void* start_addr, size_t pages);

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
   * @brief 分配指定页数的内存
   * @param pages 要分配的页数
   * @return void* 分配的内存起始地址，失败时返回 nullptr
   */
  [[nodiscard]] auto Alloc(size_t pages) -> void* override;

  /**
   * @brief 在指定地址分配指定页数的内存
   * @param addr 指定的地址
   * @param pages 要分配的页数
   * @return true 分配成功
   * @return false 分配失败
   */
  auto Alloc(void* addr, size_t pages) -> bool override;

  /**
   * @brief 释放指定地址的内存
   * @param addr 要释放的内存起始地址
   * @param pages 要释放的页数
   */
  void Free(void* addr, size_t pages) override;

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
  static constexpr size_t kMaxFreeListEntries =
      32;                // 最大支持2^31个页面，足够大部分应用
                         // 全局变量定义
  void* buddySpace;      // buddy分配器管理的内存空间起始地址
  size_t numOfEntries;   // 当前使用的空闲链表数组条目数（对应不同大小的块）
  int startingBlockNum;  // 初始总块数

  // 改为固定大小的静态数组，避免占用管理的内存空间
  static void* freeList
      [kMaxFreeListEntries];  // 空闲链表数组，每个索引对应一种大小的空闲块链表

  void buddy_init(void* space, int block_num);  // allocate buddy

  void* buddy_alloc(int n);  // allocate page (size of page is 2^n)

  void buddy_free(
      void* space,
      int n);  // free page (starting address is space, size of page is 2^n)

  void buddy_print();  // print current state of buddy

  inline bool isValid(void* space, int n);
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_BUDDY_H_ */
