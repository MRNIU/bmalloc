/**
 * Copyright The bmalloc Contributors
 */

#include "first_fit.h"

namespace bmalloc {

FirstFit::FirstFit(const char* name, void* start_addr, size_t page_count)
    : AllocatorBase(name, start_addr, page_count) {
  // 初始化位图为全0 (所有页面空闲)
  bitmap_.reset();

  // 初始化统计信息
  free_count_ = length_;
  used_count_ = 0;
}

auto FirstFit::Alloc(size_t page_count) -> void* {
  if (page_count == 0 || page_count > free_count_) {
    return nullptr;
  }

  // 在位图中寻找连续的空闲页面
  size_t start_idx = FindConsecutiveBits(page_count, false);
  if (start_idx == SIZE_MAX) {
    return nullptr;
  }

  // 标记这些页面为已使用
  for (size_t i = start_idx; i < start_idx + page_count; ++i) {
    bitmap_[i] = true;
  }

  // 计算实际物理地址
  void* allocated_addr = static_cast<char*>(const_cast<void*>(start_addr_)) +
                         (kPageSize * start_idx);

  // 更新统计信息
  free_count_ -= page_count;
  used_count_ += page_count;

  return allocated_addr;
}

auto FirstFit::Alloc(void* addr, size_t page_count) -> bool {
  // 将 void* 转换为 uintptr_t 进行地址计算
  uintptr_t target_addr = reinterpret_cast<uintptr_t>(addr);
  uintptr_t start_addr = reinterpret_cast<uintptr_t>(start_addr_);

  // 检查地址是否在管理范围内
  if (target_addr < start_addr ||
      target_addr >= start_addr + length_ * kPageSize) {
    return false;
  }

  // 计算页面索引
  size_t start_idx = (target_addr - start_addr) / kPageSize;

  // 检查是否超出边界
  if (start_idx + page_count > length_) {
    return false;
  }

  // 检查目标页面是否都是空闲的
  for (size_t i = start_idx; i < start_idx + page_count; ++i) {
    if (bitmap_[i]) {
      return false;
    }
  }

  // 标记页面为已使用
  for (size_t i = start_idx; i < start_idx + page_count; ++i) {
    bitmap_[i] = true;
  }

  // 更新统计信息
  free_count_ -= page_count;
  used_count_ += page_count;

  return true;
}

void FirstFit::Free(void* addr, size_t page_count) {
  // 将 void* 转换为 uintptr_t 进行地址计算
  uintptr_t target_addr = reinterpret_cast<uintptr_t>(addr);
  uintptr_t start_addr = reinterpret_cast<uintptr_t>(start_addr_);

  // 检查地址是否在管理范围内
  if (target_addr < start_addr ||
      target_addr >= start_addr + length_ * kPageSize) {
    return;
  }

  // 计算页面索引
  size_t start_idx = (target_addr - start_addr) / kPageSize;

  // 检查是否超出边界
  if (start_idx + page_count > length_) {
    return;
  }

  // 标记页面为空闲
  for (size_t i = start_idx; i < start_idx + page_count; ++i) {
    bitmap_[i] = false;
  }
  // 更新统计信息
  free_count_ += page_count;
  used_count_ -= page_count;
}

auto FirstFit::FindConsecutiveBits(size_t length, bool value) const -> size_t {
  if (length == 0 || length > length_) {
    return SIZE_MAX;
  }

  size_t count = 0;
  size_t start_idx = 0;

  // 遍历位图寻找连续的指定值位
  for (size_t i = 0; i < length_; ++i) {
    if (bitmap_[i] == value) {
      if (count == 0) {
        start_idx = i;
      }
      ++count;
      if (count == length) {
        return start_idx;
      }
    } else {
      count = 0;
    }
  }

  return SIZE_MAX;
}

}  // namespace bmalloc
