/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_BMALLOC_HPP_
#define BMALLOC_SRC_INCLUDE_BMALLOC_HPP_

#include <concepts>
#include <cstddef>
#include <cstdint>

#include "allocator_base.hpp"
#include "buddy.hpp"

namespace bmalloc {

/**
 * @brief 单层内存管理架构的 Malloc 实现
 * @tparam Allocator 分配器类型，直接处理用户的内存请求
 */
template <class Allocator, class LogFunc, class Lock = LockBase>
  // requires std::derived_from<Allocator, AllocatorBase<LogFunc, Lock>>
class Malloc {
 public:
  /**
   * @brief 构造 Malloc 实例
   * @param start_addr 分配器管理的内存地址起点
   * @param length 分配器管理的内存长度，单位为字节
   * @param name 分配器名称（可选），默认为 "Malloc Allocator"
   */
  explicit Malloc(uint64_t start_addr, size_t length,
                  const char* name = "Malloc Allocator")
      : allocator_(name, start_addr, length) {}

  Malloc() = default;
  Malloc(const Malloc&) = delete;
  Malloc(Malloc&&) = default;
  auto operator=(const Malloc&) -> Malloc& = delete;
  auto operator=(Malloc&&) -> Malloc& = default;
  ~Malloc() = default;

  /**
   * @brief 分配指定大小的内存块
   * @param size 要分配的内存大小（字节）
   * @return void* 分配成功时返回内存地址，失败时返回nullptr
   */
  [[nodiscard]] auto malloc(size_t size) -> void*;

  /**
   * @brief 分配并初始化内存块
   * @param num 要分配的元素个数
   * @param size 每个元素的大小（字节）
   * @return void* 分配成功时返回内存地址，失败时返回nullptr
   * @note 分配的内存会被初始化为0
   */
  [[nodiscard]] auto calloc(size_t num, size_t size) -> void*;

  /**
   * @brief 重新分配内存块大小
   * @param ptr 要重新分配的内存指针，可以为nullptr
   * @param new_size 新的内存大小（字节）
   * @return void* 重新分配成功时返回新内存地址，失败时返回nullptr
   * @note 如果ptr为nullptr，则等同于malloc(new_size)
   * @note 如果new_size为0，则等同于free(ptr)并返回nullptr
   */
  [[nodiscard]] auto realloc(void* ptr, size_t new_size) -> void*;

  /**
   * @brief 释放内存块
   * @param ptr 要释放的内存指针，可以为nullptr
   * @note 如果ptr为nullptr，则不执行任何操作
   */
  void free(void* ptr);

  /**
   * @brief 分配对齐的内存块
   * @param alignment 内存对齐要求（必须是2的幂）
   * @param size 要分配的内存大小（字节）
   * @return void* 分配成功时返回对齐的内存地址，失败时返回nullptr
   */
  [[nodiscard]] auto aligned_alloc(size_t alignment, size_t size) -> void*;

  /**
   * @brief 获取内存块的实际大小
   * @param ptr 内存指针
   * @return size_t 内存块的实际大小，如果ptr无效则返回0
   */
  [[nodiscard]] auto malloc_size(void* ptr) -> size_t;

 private:
  /// 分配器实例，直接响应 malloc/free 等接口调用
  Allocator allocator_;

  /**
   * @brief 将用户地址转换为内部地址
   * @param user_ptr 用户指针
   * @return uint64_t 内部地址
   */
  [[nodiscard]] auto UserPtrToInternalAddr(void* user_ptr) const -> uint64_t;

  /**
   * @brief 将内部地址转换为用户地址
   * @param internal_addr 内部地址
   * @return void* 用户指针
   */
  [[nodiscard]] auto InternalAddrToUserPtr(uint64_t internal_addr) const
      -> void*;

  /**
   * @brief 获取内存块头信息的大小
   * @return size_t 头信息大小
   */
  [[nodiscard]] static constexpr auto GetHeaderSize() -> size_t;
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_BMALLOC_HPP_ */
