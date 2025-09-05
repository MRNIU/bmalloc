/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_BMALLOC_HPP_
#define BMALLOC_SRC_INCLUDE_BMALLOC_HPP_

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "allocator_base.hpp"
#include "buddy.hpp"
#include "first_fit.hpp"
#include "slab.hpp"

namespace bmalloc {

template <class LogFunc = std::nullptr_t, class Lock = LockBase>
class Bmalloc {
 public:
  explicit Bmalloc(void* start_addr, size_t bytes)
      : allocator_("Bmalloc", start_addr, bytes) {}

  Bmalloc() = default;
  Bmalloc(const Bmalloc&) = delete;
  Bmalloc(Bmalloc&&) = default;
  auto operator=(const Bmalloc&) -> Bmalloc& = delete;
  auto operator=(Bmalloc&&) -> Bmalloc& = default;
  ~Bmalloc() = default;

  /**
   * @brief 分配指定大小的内存块
   * @param size 要分配的内存大小（字节）
   * @return void* 分配成功时返回内存地址，失败时返回nullptr
   */
  [[nodiscard]] auto malloc(size_t size) -> void* {
    if (size == 0) {
      return nullptr;
    }
    return allocator_.Alloc(size);
  }

  /**
   * @brief 分配并初始化内存块
   * @param num 要分配的元素个数
   * @param size 每个元素的大小（字节）
   * @return void* 分配成功时返回内存地址，失败时返回nullptr
   * @note 分配的内存会被初始化为0
   */
  [[nodiscard]] auto calloc(size_t num, size_t size) -> void* {
    if (num == 0 || size == 0) {
      return nullptr;
    }

    // Check for overflow
    if (num > SIZE_MAX / size) {
      return nullptr;
    }

    size_t total_size = num * size;
    void* ptr = allocator_.Alloc(total_size);

    if (ptr != nullptr) {
      std::memset(ptr, 0, total_size);
    }

    return ptr;
  }

  /**
   * @brief 重新分配内存块大小
   * @param ptr 要重新分配的内存指针，可以为nullptr
   * @param new_size 新的内存大小（字节）
   * @return void* 重新分配成功时返回新内存地址，失败时返回nullptr
   * @note 如果ptr为nullptr，则等同于malloc(new_size)
   * @note 如果new_size为0，则等同于free(ptr)并返回nullptr
   */
  [[nodiscard]] auto realloc(void* ptr, size_t new_size) -> void* {
    // If ptr is nullptr, equivalent to malloc(new_size)
    if (ptr == nullptr) {
      return malloc(new_size);
    }

    // If new_size is 0, equivalent to free(ptr) and return nullptr
    if (new_size == 0) {
      free(ptr);
      return nullptr;
    }

    // Get the current size of the memory block
    size_t old_size = malloc_size(ptr);

    // If the new size is the same or smaller and within reasonable bounds,
    // we can return the same pointer
    if (new_size <= old_size && old_size - new_size < old_size / 2) {
      return ptr;
    }

    // Allocate new memory
    void* new_ptr = malloc(new_size);
    if (new_ptr == nullptr) {
      return nullptr;
    }

    // Copy the old data to the new location
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    std::memcpy(new_ptr, ptr, copy_size);

    // Free the old memory
    free(ptr);

    return new_ptr;
  }

  /**
   * @brief 释放内存块
   * @param ptr 要释放的内存指针，可以为nullptr
   * @note 如果ptr为nullptr，则不执行任何操作
   */
  void free(void* ptr) {
    if (ptr == nullptr) {
      return;
    }
    allocator_.Free(ptr);
  }

  /**
   * @brief 分配对齐的内存块
   * @param alignment 内存对齐要求（必须是2的幂）
   * @param size 要分配的内存大小（字节）
   * @return void* 分配成功时返回对齐的内存地址，失败时返回nullptr
   */
  [[nodiscard]] auto aligned_alloc(size_t alignment, size_t size) -> void* {
    // Check that alignment is a power of 2
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
      return nullptr;
    }

    if (size == 0) {
      return nullptr;
    }

    // For small alignments, regular malloc might already provide sufficient
    // alignment
    if (alignment <= sizeof(void*)) {
      return malloc(size);
    }

    // Allocate extra space for alignment adjustment
    size_t total_size = size + alignment - 1 + sizeof(void*);
    void* raw_ptr = malloc(total_size);

    if (raw_ptr == nullptr) {
      return nullptr;
    }

    // Calculate aligned address
    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw_ptr);
    uintptr_t aligned_addr =
        (raw_addr + sizeof(void*) + alignment - 1) & ~(alignment - 1);
    void* aligned_ptr = reinterpret_cast<void*>(aligned_addr);

    // Store the original pointer just before the aligned address
    void** orig_ptr_location = reinterpret_cast<void**>(aligned_addr) - 1;
    *orig_ptr_location = raw_ptr;

    return aligned_ptr;
  }

  /**
   * @brief 获取内存块的实际大小
   * @param ptr 内存指针
   * @return size_t 内存块的实际大小，如果ptr无效则返回0
   */
  [[nodiscard]] auto malloc_size(void* ptr) const -> size_t {
    if (ptr == nullptr) {
      return 0;
    }

    return allocator_.AllocSize(ptr);
  }

 private:
  // using PageAllocator = Buddy<LogFunc, Lock>;
  using Allocator = Buddy<LogFunc, Lock>;
  Allocator allocator_;
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_BMALLOC_HPP_ */
