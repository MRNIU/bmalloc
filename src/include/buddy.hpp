/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_BUDDY_HPP_
#define BMALLOC_SRC_INCLUDE_BUDDY_HPP_

#include <buddy_alloc/buddy_alloc.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "allocator_base.hpp"

namespace bmalloc {

template <class LogFunc = std::nullptr_t, class Lock = LockBase>
class Buddy : public AllocatorBase<LogFunc, Lock> {
 public:
  using AllocatorBase<LogFunc, Lock>::Alloc;
  using AllocatorBase<LogFunc, Lock>::Free;

  explicit Buddy(const char* name, void* addr, size_t bytes)
      : AllocatorBase<LogFunc, Lock>(name, addr, bytes) {
    buddy = buddy_embed(static_cast<uint8_t*>(addr), bytes);
    if (!buddy) {
      Log("Buddy allocator initialization failed for %s\n", name);
    } else {
      Log("Buddy allocator initialized: %s managing %zu bytes at %p\n", name,
          bytes, addr);
    }
  }

  /// @name 构造/析构函数
  /// @{
  Buddy() = default;
  Buddy(const Buddy&) = delete;
  Buddy(Buddy&&) = default;
  auto operator=(const Buddy&) -> Buddy& = delete;
  auto operator=(Buddy&&) -> Buddy& = default;
  ~Buddy() override = default;
  /// @}

 protected:
  using AllocatorBase<LogFunc, Lock>::Log;
  using AllocatorBase<LogFunc, Lock>::name_;
  using AllocatorBase<LogFunc, Lock>::start_addr_;
  using AllocatorBase<LogFunc, Lock>::length_;

  struct buddy* buddy{};

  [[nodiscard]] auto AllocImpl(size_t bytes) -> void* override {
    auto* ptr = buddy_malloc(buddy, bytes);
    if (!ptr) {
      Log("Buddy allocator %s failed to allocate %zu bytes\n", name_, bytes);
    }
    return ptr;
  }

  void FreeImpl(void* addr, [[maybe_unused]] size_t bytes = 0) override {
    buddy_free(buddy, addr);
  }
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_BUDDY_HPP_ */
