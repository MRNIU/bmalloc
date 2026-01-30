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
/// 分配器的页面大小
static constexpr size_t kPageSize = 4096;

/**
 * @brief 锁接口抽象基类
 * @details 用于在 freestanding 环境中提供锁的抽象接口
 */
class LockBase {
 public:
  virtual ~LockBase() = default;
  virtual void Lock() {}
  virtual void Unlock() {}
};

/**
 * @brief RAII 风格的锁守卫
 * @details 类似于 std::lock_guard，用于自动管理锁的获取和释放
 */
class LockGuard {
 public:
  explicit LockGuard(LockBase& lock) : lock_(lock) { lock_.Lock(); }

  ~LockGuard() { lock_.Unlock(); }

  // 禁止复制和移动
  LockGuard(const LockGuard&) = delete;
  LockGuard(LockGuard&&) = delete;
  auto operator=(const LockGuard&) -> LockGuard& = delete;
  auto operator=(LockGuard&&) -> LockGuard& = delete;

 private:
  LockBase& lock_;
};

/**
 * @brief 内存分配器抽象基类
 * @details 定义了所有内存分配器的通用接口
 * @tparam LogFunc printf 风格的日志函数类型
 * @tparam Lock 锁类型
 */

template <class LogFunc, class Lock>
  requires std::derived_from<Lock, LockBase> || std::is_same_v<Lock, LockBase>
class AllocatorBase {
 public:
  /**
   * @brief 构造内存分配器
   * @param  name            分配器名
   * @param  addr            要管理的内存开始地址
   * @param  length          要管理的内存长度，单位以具体实现为准
   */
  explicit AllocatorBase(const char* name, void* addr, size_t length)
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
   * @return void*          分配到的地址，失败时返回nullptr
   */
  [[nodiscard]] auto Alloc(size_t length) -> void* {
    LockGuard guard(lock_);
    return AllocImpl(length);
  }

  /**
   * @brief 释放指定地址和长度的内存
   * @param  addr            要释放的地址
   * @param  length          要释放的长度
   */
  void Free(void* addr, size_t length = 0) {
    LockGuard guard(lock_);
    FreeImpl(addr, length);
  }

  /**
   * @brief 获取一个内存块在内存池中占用的实际字节数
   * @param  addr            内存块地址
   * @return size_t          内存块实际占用的字节数，addr 为nullptr 时返回 0
   */
  [[nodiscard]] size_t AllocSize(void* addr) const {
    return AllocSizeImpl(addr);
  }

  /**
   * @brief 获取已使用的内存数量
   * @return size_t          已使用的数量
   */
  [[nodiscard]] auto GetUsedCount() const -> size_t { return used_count_; }

  /**
   * @brief 获取空闲的内存数量
   * @return size_t          空闲的数量
   */
  [[nodiscard]] auto GetFreeCount() const -> size_t { return free_count_; }

 protected:
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
  /// 用于线程安全的锁对象
  Lock lock_;

  /**
   * @brief 分配指定长度的内存的实际实现（线程不安全）
   * @param  length          要分配的长度
   * @return void*          分配到的地址，失败时返回nullptr
   */
  [[nodiscard]] virtual auto AllocImpl([[maybe_unused]] size_t length)
      -> void* {
    return nullptr;
  }

  /**
   * @brief 释放指定地址和长度的内存的实际实现（线程不安全）
   * @param  addr            要释放的地址
   * @param  length          要释放的长度
   */
  virtual void FreeImpl([[maybe_unused]] void* addr,
                        [[maybe_unused]] size_t length = 0) {}

  /**
   * @brief 获取一个内存块在内存池中占用的实际字节数
   * @param  addr            内存块地址
   * @return size_t          内存块实际占用的字节数，addr 为nullptr 时返回 0
   */
  [[nodiscard]] virtual size_t AllocSizeImpl(
      [[maybe_unused]] void* addr) const {
    return 0;
  }

  /**
   * @brief 记录日志信息
   * @param  format          格式化字符串
   * @param  args            可变参数
   */
  template <typename... Args>
  void Log(const char* format, Args&&... args) const {
    if constexpr (!std::is_same_v<LogFunc, std::nullptr_t>) {
      LogFunc{}(format, args...);
    }
  }
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_ALLOCATOR_BASE_HPP_ */
