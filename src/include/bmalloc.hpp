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
      Log("malloc: size is 0, returning nullptr\n");
      return nullptr;
    }
    void* ptr = allocator_.Alloc(size);
    if (ptr == nullptr) {
      Log("malloc: failed to allocate %zu bytes\n", size);
    }
    return ptr;
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
      Log("calloc: num (%zu) or size (%zu) is 0, returning nullptr\n", num,
          size);
      return nullptr;
    }

    // Check for overflow
    if (num > SIZE_MAX / size) {
      Log("calloc: overflow detected - num (%zu) * size (%zu) exceeds "
          "SIZE_MAX\n",
          num, size);
      return nullptr;
    }

    size_t total_size = num * size;
    void* ptr = allocator_.Alloc(total_size);

    if (ptr != nullptr) {
      std::memset(ptr, 0, total_size);
    } else {
      Log("calloc: failed to allocate %zu bytes (num=%zu, size=%zu)\n",
          total_size, num, size);
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
      Log("realloc: ptr is nullptr, equivalent to malloc(%zu)\n", new_size);
      return allocator_.Alloc(new_size);
    }

    // If new_size is 0, equivalent to free(ptr) and return nullptr
    if (new_size == 0) {
      Log("realloc: new_size is 0, equivalent to free(ptr)\n");
      allocator_.Free(ptr);
      return nullptr;
    }

    // Get the current size of the memory block
    size_t old_size = allocator_.AllocSize(ptr);
    if (old_size == 0) {
      Log("realloc: ptr %p is invalid or corrupted, AllocSize returned 0\n",
          ptr);
      return nullptr;
    }

    // If the new size is the same or smaller and within reasonable bounds,
    // we can return the same pointer
    if (new_size <= old_size && old_size - new_size < old_size / 2) {
      return ptr;
    }

    // Allocate new memory
    void* new_ptr = allocator_.Alloc(new_size);
    if (new_ptr == nullptr) {
      Log("realloc: failed to allocate new memory of size %zu\n", new_size);
      return nullptr;
    }

    // Copy the old data to the new location
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    std::memcpy(new_ptr, ptr, copy_size);

    // Free the old memory
    allocator_.Free(ptr);

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
   * @return void* 分配成功时返回对齐的内存地址，失败时返回 nullptr
   * @note 对齐参数必须是2的幂次方，否则返回 nullptr
   * @note 如果 size 为 0，返回 nullptr
   * @note 分配的内存必须使用 aligned_free 释放，不能使用普通的 free
   * @note 在对齐地址前存储原始指针，用于释放时定位原始分配块
   */
  [[nodiscard]] auto aligned_alloc(size_t alignment, size_t size) -> void* {
    // 检查对齐参数是否为2的幂
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
      Log("aligned_alloc: invalid alignment %zu, must be power of 2\n",
          alignment);
      return nullptr;
    }

    if (size == 0) {
      Log("aligned_alloc: size is 0, returning nullptr\n");
      return nullptr;
    }

    // 计算需要额外分配的空间：对齐调整 + 原始指针存储空间
    size_t extra_offset = alignment - 1 + sizeof(void*);
    auto* original_ptr = allocator_.Alloc(size + extra_offset);

    if (original_ptr == nullptr) {
      Log("aligned_alloc: failed to allocate %zu bytes (requested: %zu, "
          "alignment: %zu)\n",
          size + extra_offset, size, alignment);
      return nullptr;
    }

    // 计算对齐后的地址
    auto original_addr = reinterpret_cast<uintptr_t>(original_ptr);
    auto aligned_addr = (original_addr + extra_offset) & ~(alignment - 1);
    auto* aligned_ptr = reinterpret_cast<void*>(aligned_addr);

    // 在对齐地址前存储原始指针，用于释放时找到原始分配
    auto* original_ptr_storage =
        reinterpret_cast<void**>(aligned_addr - sizeof(void*));
    *original_ptr_storage = original_ptr;

    return aligned_ptr;
  }

  /**
   * @brief 释放由 aligned_alloc 分配的对齐内存块
   * @param ptr 由 aligned_alloc 返回的对齐内存指针，可以为nullptr
   * @note 如果ptr为nullptr，则不执行任何操作
   * @note 此函数专门用于释放 aligned_alloc 分配的内存，不能用于普通 malloc
   * 分配的内存
   * @note 通过读取对齐地址前存储的原始指针来定位并释放原始分配块
   * @warning 传入非 aligned_alloc 分配的指针会导致未定义行为
   */
  void aligned_free(void* ptr) {
    if (ptr == nullptr) {
      Log("aligned_free: ptr is nullptr, no operation performed\n");
      return;
    }

    // 获取存储在对齐地址前的原始指针
    auto* original_ptr_storage = reinterpret_cast<void**>(ptr) - 1;
    auto* original_ptr = *original_ptr_storage;

    if (original_ptr == nullptr) {
      Log("aligned_free: corrupted metadata, original_ptr is nullptr\n");
      return;
    }

    // 释放原始分配的内存
    allocator_.Free(original_ptr);
  }

  /**
   * @brief 获取内存块的实际大小
   * @param ptr 内存指针
   * @return size_t 内存块的实际大小，如果ptr无效则返回0
   * @note 此函数用于获取普通 malloc 分配的内存大小
   * @note 对于 aligned_alloc 分配的内存，请使用 aligned_malloc_size
   */
  [[nodiscard]] auto malloc_size(void* ptr) const -> size_t {
    if (ptr == nullptr) {
      return 0;
    }

    return allocator_.AllocSize(ptr);
  }

  /**
   * @brief 获取由 aligned_alloc 分配的内存块的实际大小
   * @param ptr 由 aligned_alloc 返回的对齐内存指针
   * @return size_t 对齐内存块的实际大小，如果ptr无效则返回0
   * @note 此函数专门用于获取 aligned_alloc 分配的内存大小
   * @note 返回的是原始分配的内存总大小，包括对齐开销和元数据空间
   * @note 通过读取对齐地址前存储的原始指针来定位原始分配块
   * @warning 传入非 aligned_alloc 分配的指针会导致未定义行为
   */
  [[nodiscard]] auto aligned_malloc_size(void* ptr) const -> size_t {
    if (ptr == nullptr) {
      Log("aligned_malloc_size: ptr is nullptr, returning 0\n");
      return 0;
    }

    // 获取存储在对齐地址前的原始指针
    auto* original_ptr_storage = reinterpret_cast<void**>(ptr) - 1;
    auto* original_ptr = *original_ptr_storage;

    if (original_ptr == nullptr) {
      Log("aligned_malloc_size: corrupted metadata, original_ptr is "
          "nullptr\n");
      return 0;
    }

    // 返回原始分配的大小
    size_t size = allocator_.AllocSize(original_ptr);
    if (size == 0) {
      Log("aligned_malloc_size: original_ptr %p is invalid, AllocSize "
          "returned 0\n",
          original_ptr);
    }
    return size;
  }

 private:
  // using PageAllocator = Buddy<LogFunc, Lock>;
  using Allocator = Buddy<LogFunc, Lock>;
  Allocator allocator_;

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

#endif /* BMALLOC_SRC_INCLUDE_BMALLOC_HPP_ */
