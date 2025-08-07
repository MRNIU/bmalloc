/**
 * Copyright The malloc Contributors
 */

#ifndef MALLOC_SRC_INCLUDE_SLAB_H_
#define MALLOC_SRC_INCLUDE_SLAB_H_

#include <cstddef>
#include <cstdint>

#include "allocator_base.h"

class Slab : public AllocatorBase {
 public:
  /**
   * @brief 构造 Slab 分配器
   * @param name 分配器名称
   * @param start_addr 管理的内存起始地址
   * @param bytes 管理的字节数
   */
  explicit Slab(const char* name, uint64_t start_addr, size_t bytes);

  /// @name 构造/析构函数
  /// @{
  Slab() = default;
  Slab(const Slab&) = delete;
  Slab(Slab&&) = default;
  auto operator=(const Slab&) -> Slab& = delete;
  auto operator=(Slab&&) -> Slab& = default;
  ~Slab() override = default;
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
};

#endif /* MALLOC_SRC_INCLUDE_SLAB_H_ */
