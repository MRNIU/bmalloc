/**
 * Copyright The bmalloc Contributors
 */

#include "buddy.h"

#include <iterator>

namespace {
/**
 * @brief 整数 log2 函数实现
 */
static __always_inline size_t log2(size_t value) {
  if (value == 0) {
    return 0;
  }

  size_t result = 0;
  while (value >>= 1) {
    result++;
  }
  return result;
}
}  // namespace

namespace bmalloc {

Buddy::Buddy(const char* name, void* start_addr, size_t total_pages)
    : AllocatorBase(name, start_addr, log2(total_pages) + 1) {
  if (total_pages < 1) {
    return;
  }

  // 检查是否超出静态数组大小
  if (length_ > kMaxFreeListEntries) {
    return;
  }

  // 初始化计数器：所有页面开始时都是空闲的
  used_count_ = 0;
  free_count_ = total_pages;

  // 将 total_pages 按二进制位分解，每个位对应一个大小的块
  auto remaining_pages = total_pages;
  // 当前地址偏移(以页为单位)
  size_t current_addr_offset = 0;

  for (size_t order = 0; order < length_ && remaining_pages > 0; order++) {
    // 当前 order 对应的块大小
    auto block_size = 1 << order;

    // 如果剩余页数的第 order 位为 1，则创建一个对应大小的块
    if (remaining_pages & block_size) {
      // 计算块地址
      auto block_addr =
          const_cast<char*>(static_cast<const char*>(start_addr_)) +
          current_addr_offset * kPageSize;

      // 直接插入块地址到链表头部
      InsertToFreeList(free_block_lists_[order], block_addr);

      // 更新地址偏移和剩余页数
      current_addr_offset += block_size;
      remaining_pages -= block_size;
    }
  }
}

auto Buddy::Alloc(size_t order) -> void* {
  // 参数检查
  if (order >= length_) {
    return nullptr;
  }

  // 寻找第一个可用的块(从目标大小开始向上查找)
  for (auto current_order = order; current_order < length_; current_order++) {
    if (free_block_lists_[current_order] != nullptr) {
      // 从空闲链表头部取出一个块
      auto* node = free_block_lists_[current_order];
      auto* block = static_cast<void*>(node);
      free_block_lists_[current_order] = node->next;

      // 如果找到的块正好是目标大小，直接返回
      if (current_order == order) {
        // 更新计数器
        size_t allocated_pages = 1 << order;
        used_count_ += allocated_pages;
        free_count_ -= allocated_pages;
        return block;
      }

      // 否则需要分割成目标大小，将多余的块放回对应的空闲链表
      while (current_order > order) {
        current_order--;
        // 计算 buddy 块地址(分割后的第二个块)
        void* buddy_block =
            static_cast<char*>(block) + kPageSize * (1 << current_order);

        // 将 buddy 块插入到对应的空闲链表头部
        InsertToFreeList(free_block_lists_[current_order], buddy_block);
      }

      // 更新计数器
      size_t allocated_pages = 1 << order;
      used_count_ += allocated_pages;
      free_count_ -= allocated_pages;

      return block;
    }
  }

  return nullptr;
}

void Buddy::Free(void* addr, size_t order) {
  // 参数检查
  if (order >= length_) {
    return;
  }

  // 检查地址是否在管理的内存范围内
  size_t maxPages = 1 << (length_ - 1);
  if (addr < start_addr_ ||
      addr >= static_cast<const void*>(static_cast<const char*>(start_addr_) +
                                       maxPages * kPageSize)) {
    return;
  }

  // 尝试查找并合并 buddy 块
  size_t block_size = 1 << order;
  void* right_buddy = static_cast<char*>(addr) + kPageSize * block_size;
  void* left_buddy_start = static_cast<char*>(addr) - kPageSize * block_size;

  // 遍历同大小的空闲链表，寻找 buddy 块
  for (auto* curr = free_block_lists_[order]; curr != nullptr;
       curr = curr->next) {
    void* curr_addr = static_cast<void*>(curr);

    // 检查是否为右 buddy(当前要释放的块的右边相邻块)
    if (curr_addr == right_buddy) {
      if (IsValidBlockAddress(addr, block_size)) {
        // 从链表中移除找到的 buddy 块并递归合并
        RemoveFromFreeList(free_block_lists_[order], curr);
        Free(addr, order + 1);
        return;
      }
    }
    // 检查是否为左 buddy(当前要释放的块的左边相邻块)
    else if (curr_addr == left_buddy_start) {
      if (IsValidBlockAddress(curr_addr, block_size)) {
        // 从链表中移除找到的 buddy 块并递归合并(使用左 buddy 的地址)
        RemoveFromFreeList(free_block_lists_[order], curr);
        Free(curr_addr, order + 1);
        return;
      }
    }
  }

  // 没有找到可合并的 buddy，直接插入到链表头部
  InsertToFreeList(free_block_lists_[order], addr);

  // 更新计数器
  size_t freed_pages = 1 << order;
  used_count_ -= freed_pages;
  free_count_ += freed_pages;
}

}  // namespace bmalloc
