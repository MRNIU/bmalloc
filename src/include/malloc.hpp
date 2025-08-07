/**
 * Copyright The malloc Contributors
 */

#ifndef MALLOC_SRC_INCLUDE_MALLOC_HPP_
#define MALLOC_SRC_INCLUDE_MALLOC_HPP_

#include <concepts>
#include <cstddef>
#include <cstdint>

#include "allocator_base.h"

namespace malloc {

/**
 * @brief 两层内存管理架构的 Malloc 实现
 * @details
 * 架构说明：
 * 1. 主分配器（如 Slab）：直接响应 malloc/free 等用户接口调用
 * 2. 后备分配器（如 FirstFit）：当主分配器内存不足时，向其提供更多内存
 *
 * 工作流程示例：
 * malloc -> slab查询 -> slab发现现有cache中可用空间不足 ->
 * slab使用firstfit申请内存 -> firstfit返回 ->
 * slab将刚刚申请的内存纳入管理 -> slab返回malloc的结果
 *
 * @tparam PrimaryAllocator 主分配器类型，直接处理用户的内存请求
 * @tparam BackendAllocator 后备分配器类型，为主分配器提供内存池扩展
 */
template <class PrimaryAllocator, class BackendAllocator>
  requires std::derived_from<PrimaryAllocator, AllocatorBase> &&
           std::derived_from<BackendAllocator, AllocatorBase>
class Malloc {
 public:
  /**
   * @brief 构造 Malloc 实例
   * @param primary_start_addr 主分配器管理的内存地址起点
   * @param primary_length 主分配器管理的内存长度，单位为字节
   * @param backend_start_addr 后备分配器管理的内存地址起点
   * @param backend_length 后备分配器管理的内存长度，单位为字节
   * @param primary_name 主分配器名称（可选），默认为 "PrimaryAllocator"
   * @param backend_name 后备分配器名称（可选），默认为 "BackendAllocator"
   */
  explicit Malloc(uint64_t primary_start_addr, size_t primary_length,
                  uint64_t backend_start_addr, size_t backend_length,
                  const char* primary_name = "PrimaryAllocator",
                  const char* backend_name = "BackendAllocator")
      : primary_allocator_(primary_name, primary_start_addr, primary_length),
        backend_allocator_(backend_name, backend_start_addr, backend_length) {
    // 建立主分配器与后备分配器之间的连接
    SetupBackendConnection();
  }

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
   * @note
   * 请求直接发送给主分配器，主分配器内存不足时会自动从后备分配器获取更多内存
   */
  [[nodiscard]] auto malloc(size_t size) -> void*;

  /**
   * @brief 分配并初始化内存块
   * @param num 要分配的元素个数
   * @param size 每个元素的大小（字节）
   * @return void* 分配成功时返回内存地址，失败时返回nullptr
   * @note 分配的内存会被初始化为0，实际分配由主分配器处理
   */
  [[nodiscard]] auto calloc(size_t num, size_t size) -> void*;

  /**
   * @brief 重新分配内存块大小
   * @param ptr 要重新分配的内存指针，可以为nullptr
   * @param new_size 新的内存大小（字节）
   * @return void* 重新分配成功时返回新内存地址，失败时返回nullptr
   * @note 如果ptr为nullptr，则等同于malloc(new_size)
   * @note 如果new_size为0，则等同于free(ptr)并返回nullptr
   * @note 操作由主分配器处理，必要时会使用后备分配器扩展内存池
   */
  [[nodiscard]] auto realloc(void* ptr, size_t new_size) -> void*;

  /**
   * @brief 释放内存块
   * @param ptr 要释放的内存指针，可以为nullptr
   * @note 如果ptr为nullptr，则不执行任何操作
   * @note
   * 内存释放由主分配器处理，主分配器可能选择将大块未使用内存归还给后备分配器
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
  /// 主分配器实例，直接响应 malloc/free 等接口调用
  PrimaryAllocator primary_allocator_;
  /// 后备分配器实例，当主分配器内存不足时提供内存
  BackendAllocator backend_allocator_;

  /**
   * @brief 主分配器内存不足时，从后备分配器申请内存
   * @param size 需要申请的内存大小
   * @return uint64_t 申请到的内存地址，失败时返回0
   * @note 这个方法会被主分配器调用，用于扩展其管理的内存池
   */
  [[nodiscard]] auto AllocateFromBackend(size_t size) -> uint64_t {
    return backend_allocator_.Alloc(size);
  }

  /**
   * @brief 将内存归还给后备分配器
   * @param addr 要归还的内存地址
   * @param size 要归还的内存大小
   * @note 当主分配器释放大块内存时，可以将其归还给后备分配器
   */
  void ReturnToBackend(uint64_t addr, size_t size) {
    backend_allocator_.Free(addr, size);
  }

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

}  // namespace malloc

#endif /* MALLOC_SRC_INCLUDE_MALLOC_HPP_ */
