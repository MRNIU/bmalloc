/**
 * Copyright The bmalloc Contributors
 */

#include "first_fit.h"

namespace bmalloc {

FirstFit::FirstFit(const char* name, void* start_addr, size_t page_count,
                   int (*log_func)(const char*, ...))
    : AllocatorBase(name, start_addr, page_count, log_func) {
  Log("FirstFit allocator '%s' initializing: start_addr=%p, page_count=%zu\n",
      name, start_addr, page_count);

  // 初始化位图为全0 (所有页面空闲)
  bitmap_.reset();

  // 初始化统计信息
  free_count_ = length_;
  used_count_ = 0;

  Log("FirstFit allocator '%s' initialization completed: free_pages=%zu, "
      "used_pages=%zu\n",
      name_, free_count_, used_count_);
}

auto FirstFit::AllocImpl(size_t page_count) -> void* {
  Log("FirstFit allocator '%s' allocation request: page_count=%zu\n", name_,
      page_count);

  if (page_count == 0 || page_count > free_count_) {
    Log("FirstFit allocator '%s' allocation failed: invalid page_count=%zu "
        "(free_count=%zu)\n",
        name_, page_count, free_count_);
    return nullptr;
  }

  // 在位图中寻找连续的空闲页面
  size_t start_idx = FindConsecutiveBits(page_count, false);
  if (start_idx == SIZE_MAX) {
    Log("FirstFit allocator '%s' allocation failed: no consecutive free pages "
        "found for %zu pages\n",
        name_, page_count);
    return nullptr;
  }

  Log("FirstFit allocator '%s' found consecutive free pages: start_idx=%zu, "
      "count=%zu\n",
      name_, start_idx, page_count);

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

  Log("FirstFit allocator '%s' allocation successful: addr=%p, pages=%zu, "
      "free_count=%zu, used_count=%zu\n",
      name_, allocated_addr, page_count, free_count_, used_count_);

  return allocated_addr;
}

void FirstFit::FreeImpl(void* addr, size_t page_count) {
  Log("FirstFit allocator '%s' free request: addr=%p, page_count=%zu\n", name_,
      addr, page_count);

  // 将 void* 转换为 uintptr_t 进行地址计算
  uintptr_t target_addr = reinterpret_cast<uintptr_t>(addr);
  uintptr_t start_addr = reinterpret_cast<uintptr_t>(start_addr_);

  // 检查地址是否在管理范围内
  if (target_addr < start_addr ||
      target_addr >= start_addr + length_ * kPageSize) {
    Log("FirstFit allocator '%s' free failed: addr=%p out of range [%p, %p)\n",
        name_, addr, start_addr_,
        static_cast<void*>(static_cast<char*>(const_cast<void*>(start_addr_)) +
                           length_ * kPageSize));
    return;
  }

  // 计算页面索引
  size_t start_idx = (target_addr - start_addr) / kPageSize;

  // 检查是否超出边界
  if (start_idx + page_count > length_) {
    Log("FirstFit allocator '%s' free failed: start_idx=%zu + page_count=%zu > "
        "length=%zu\n",
        name_, start_idx, page_count, length_);
    return;
  }

  Log("FirstFit allocator '%s' freeing pages: start_idx=%zu, count=%zu\n",
      name_, start_idx, page_count);

  // 标记页面为空闲
  for (size_t i = start_idx; i < start_idx + page_count; ++i) {
    bitmap_[i] = false;
  }
  // 更新统计信息
  free_count_ += page_count;
  used_count_ -= page_count;

  Log("FirstFit allocator '%s' free successful: addr=%p, pages=%zu, "
      "free_count=%zu, used_count=%zu\n",
      name_, addr, page_count, free_count_, used_count_);
}

auto FirstFit::FindConsecutiveBits(size_t length, bool value) const -> size_t {
  Log("FirstFit allocator '%s' searching for %zu consecutive %s pages\n", name_,
      length, value ? "used" : "free");

  if (length == 0 || length > length_) {
    Log("FirstFit allocator '%s' search failed: invalid length=%zu (max=%zu)\n",
        name_, length, length_);
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
        Log("FirstFit allocator '%s' found %zu consecutive %s pages at "
            "start_idx=%zu\n",
            name_, length, value ? "used" : "free", start_idx);
        return start_idx;
      }
    } else {
      count = 0;
    }
  }

  Log("FirstFit allocator '%s' search failed: no %zu consecutive %s pages "
      "found\n",
      name_, length, value ? "used" : "free");
  return SIZE_MAX;
}

}  // namespace bmalloc
