/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_ALLOCATOR_BASE_HPP_
#define BMALLOC_SRC_INCLUDE_ALLOCATOR_BASE_HPP_

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace bmalloc {

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
   * @param  log_func        printf 风格的日志函数指针（可选）
   */
  explicit AllocatorBase(const char* name, void* addr, size_t length,
                         int (*log_func)(const char*, ...) = nullptr)
      : name_(name),
        start_addr_(addr),
        length_(length),
        free_count_(length),
        used_count_(0),
        log_func_(log_func) {}

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
   * @return void*          分配到的地址，失败时返回nullptr
   */
  [[nodiscard]] virtual auto Alloc([[maybe_unused]] size_t length) -> void* {
    return nullptr;
  }

  /**
   * @brief 在指定地址分配指定长度的内存
   * @param  addr            指定的地址
   * @param  length          要分配的长度
   * @return true            成功
   * @return false           失败
   */
  virtual auto Alloc([[maybe_unused]] void* addr,
                     [[maybe_unused]] size_t length) -> bool {
    return false;
  }

  /**
   * @brief 释放指定地址和长度的内存
   * @param  addr            要释放的地址
   * @param  length          要释放的长度
   */
  virtual void Free([[maybe_unused]] void* addr,
                    [[maybe_unused]] size_t length) {}

  /**
   * @brief 获取已使用的内存数量
   * @return size_t          已使用的数量
   */
  [[nodiscard]] virtual auto GetUsedCount() const -> size_t {
    return used_count_;
  }

  /**
   * @brief 获取空闲的内存数量
   * @return size_t          空闲的数量
   */
  [[nodiscard]] virtual auto GetFreeCount() const -> size_t {
    return free_count_;
  }

 protected:
  /**
   * @brief 记录日志信息
   * @param  format          格式化字符串
   * @param  args            可变参数（使用完美转发）
   */
  template <typename... Args>
  void Log(const char* format, Args&&... args) const {
    if (log_func_) {
      log_func_(format, std::forward<Args>(args)...);
    }
  }

  /// 分配器名称
  const char* name_;
  /// 当前管理的内存区域起始地址
  const void* start_addr_;
  /// 当前管理的内存区域长度
  const size_t length_;
  /// 当前管理的内存区域空闲数量
  size_t free_count_;
  /// 当前管理的内存区域已使用数量
  size_t used_count_;
  // printf 风格的日志函数指针，可以为 nullptr
  int (*log_func_)(const char*, ...) = nullptr;
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_ALLOCATOR_BASE_HPP_ */
