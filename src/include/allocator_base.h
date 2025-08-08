/**
 * Copyright The SimpleKernel Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_ALLOCATOR_BASE_H_
#define BMALLOC_SRC_INCLUDE_ALLOCATOR_BASE_H_

#include <cstddef>
#include <cstdint>

/**
 * @brief 内存分配器抽象基类
 * @details 定义了所有内存分配器的通用接口
 */
class AllocatorBase {
 public:
  static constexpr size_t kPageSize = 4096;

  /**
   * @brief 构造内存分配器
   * @param  name            分配器名
   * @param  addr            要管理的内存开始地址
   * @param  length          要管理的内存长度，单位以具体实现为准
   */
  explicit AllocatorBase(const char* name, uint64_t addr, size_t length)
      : name_(name),
        start_addr_(addr),
        length_(length),
        free_count_(length),
        used_count_(0) {}

  /// @name 构造/析构函数
  /// @{
  AllocatorBase() = default;
  AllocatorBase(const AllocatorBase&) = delete;
  AllocatorBase(AllocatorBase&&) = default;
  auto operator=(const AllocatorBase&) -> AllocatorBase& = delete;
  auto operator=(AllocatorBase&&) -> AllocatorBase& = default;
  virtual ~AllocatorBase() = default;
  /// @}

  /**
   * @brief 分配指定长度的内存
   * @param  length          要分配的长度
   * @return uint64_t       分配到的地址，失败时返回0
   */
  [[nodiscard]] virtual auto Alloc(size_t length) -> uint64_t = 0;

  /**
   * @brief 在指定地址分配指定长度的内存
   * @param  addr            指定的地址
   * @param  length          要分配的长度
   * @return true            成功
   * @return false           失败
   */
  virtual auto Alloc(uint64_t addr, size_t length) -> bool = 0;

  /**
   * @brief 释放指定地址和长度的内存
   * @param  addr            要释放的地址
   * @param  length          要释放的长度
   */
  virtual void Free(uint64_t addr, size_t length) = 0;

  /**
   * @brief 获取已使用的内存数量
   * @return size_t          已使用的数量
   */
  [[nodiscard]] virtual auto GetUsedCount() const -> size_t = 0;

  /**
   * @brief 获取空闲的内存数量
   * @return size_t          空闲的数量
   */
  [[nodiscard]] virtual auto GetFreeCount() const -> size_t = 0;

 protected:
  /// 分配器名称
  const char* name_;
  /// 当前管理的内存区域起始地址
  uint64_t start_addr_;
  /// 当前管理的内存区域长度
  size_t length_;
  /// 当前管理的内存区域空闲数量
  size_t free_count_;
  /// 当前管理的内存区域已使用数量
  size_t used_count_;
};

#endif /* BMALLOC_SRC_INCLUDE_ALLOCATOR_BASE_H_ */
