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
   * @param bytes 管理的字节数
   */
  explicit Buddy(const char* name, uint64_t start_addr, size_t bytes);

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
   * @brief 分配指定字节的内存
   * @param bytes 要分配的字节
   * @return uint64_t 分配的内存起始地址，失败时返回0
   */
  [[nodiscard]] auto Alloc(size_t bytes) -> uint64_t override;

  /**
   * @brief 在指定地址分配指定字节的内存
   * @param addr 指定的地址
   * @param bytes 要分配的字节
   * @return true 分配成功
   * @return false 分配失败
   */
  auto Alloc(uint64_t addr, size_t bytes) -> bool override;

  /**
   * @brief 释放指定地址的内存
   * @param addr 要释放的内存起始地址
   * @param bytes 要释放的字节
   */
  void Free(uint64_t addr, size_t bytes) override;

  /**
   * @brief 获取已使用的字节
   * @return size_t 已使用的字节
   */
  [[nodiscard]] auto GetUsedCount() const -> size_t override;

  /**
   * @brief 获取空闲的字节
   * @return size_t 空闲的字节
   */
  [[nodiscard]] auto GetFreeCount() const -> size_t override;

 private:
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
