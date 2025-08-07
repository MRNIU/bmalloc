/**
 * @copyright Copyright The SimpleKernel Contributors
 * @brief First Fit 内存分配器实现
 */

#include "first_fit.h"

#include <bitset>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kernel_log.hpp"

FirstFit::FirstFit(const char* name, uint64_t start_addr, size_t page_count)
    : AllocatorBase(name, start_addr, page_count) {
  // 检查页数是否超过最大支持数
  if (page_count > kMaxPages) {
    klog::Err("FirstFit: page_count (%zu) exceeds maximum (%zu)\n", page_count,
              kMaxPages);
    length_ = kMaxPages;
  }

  // 初始化位图为全0 (所有页面空闲)
  bitmap_.reset();

  // 初始化统计信息
  free_count_ = length_;
  used_count_ = 0;

  klog::Info("FirstFit: %s initialized - addr: 0x%lx, pages: %zu\n", name_,
             start_addr_, length_);
}

auto FirstFit::Alloc(size_t page_count) -> uint64_t {
  if (page_count == 0 || page_count > free_count_) {
    return 0;
  }

  // 在位图中寻找连续的空闲页面
  size_t start_idx = FindConsecutiveBits(page_count, false);
  if (start_idx == SIZE_MAX) {
    klog::Warn("FirstFit: No enough continuous memory for %zu pages\n",
               page_count);
    return 0;
  }

  // 标记这些页面为已使用
  for (size_t i = start_idx; i < start_idx + page_count; ++i) {
    bitmap_[i] = true;
  }

  // 计算实际物理地址
  uint64_t allocated_addr = start_addr_ + (kPageSize * start_idx);

  // 更新统计信息
  free_count_ -= page_count;
  used_count_ += page_count;

  klog::Debug("FirstFit: Allocated %zu pages at 0x%lx (index %zu)\n",
              page_count, allocated_addr, start_idx);

  return allocated_addr;
}

auto FirstFit::Alloc(uint64_t addr, size_t page_count) -> bool {
  // 检查地址是否在管理范围内
  if (addr < start_addr_ || addr >= start_addr_ + length_ * kPageSize) {
    klog::Warn("FirstFit: Address 0x%lx out of managed range\n", addr);
    return false;
  }

  // 计算页面索引
  size_t start_idx = (addr - start_addr_) / kPageSize;

  // 检查是否超出边界
  if (start_idx + page_count > length_) {
    klog::Warn("FirstFit: Allocation exceeds managed pages\n");
    return false;
  }

  // 检查目标页面是否都是空闲的
  for (size_t i = start_idx; i < start_idx + page_count; ++i) {
    if (bitmap_[i]) {
      klog::Warn("FirstFit: Page %zu already allocated\n", i);
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

  klog::Debug("FirstFit: Allocated %zu pages at fixed addr 0x%lx\n", page_count,
              addr);

  return true;
}

void FirstFit::Free(uint64_t addr, size_t page_count) {
  // 检查地址是否在管理范围内
  if (addr < start_addr_ || addr >= start_addr_ + length_ * kPageSize) {
    klog::Warn("FirstFit: Free address 0x%lx out of managed range\n", addr);
    return;
  }

  // 计算页面索引
  size_t start_idx = (addr - start_addr_) / kPageSize;

  // 检查是否超出边界
  if (start_idx + page_count > length_) {
    klog::Warn("FirstFit: Free operation exceeds managed pages\n");
    return;
  }

  // 标记页面为空闲
  for (size_t i = start_idx; i < start_idx + page_count; ++i) {
    bitmap_[i] = false;
  }
  // 更新统计信息
  free_count_ += page_count;
  used_count_ -= page_count;

  klog::Debug("FirstFit: Freed %zu pages at 0x%lx\n", page_count, addr);
}

auto FirstFit::GetUsedCount() const -> size_t { return used_count_; }

auto FirstFit::GetFreeCount() const -> size_t { return free_count_; }

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
