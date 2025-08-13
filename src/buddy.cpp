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

Buddy::Buddy(const char* name, void* start_addr, size_t total_pages,
             int (*log_func)(const char*, ...))
    : AllocatorBase(name, start_addr, log2(total_pages) + 1, log_func) {
  Log("Buddy allocator '%s' initializing: start_addr=%p, total_pages=%zu, "
      "max_order=%zu\n",
      name, start_addr, total_pages, length_ - 1);

  if (total_pages < 1) {
    Log("Buddy allocator '%s' initialization failed: total_pages < 1\n", name);
    return;
  }

  // 检查是否超出静态数组大小
  if (length_ > kMaxFreeListEntries) {
    Log("Buddy allocator '%s' initialization failed: required order %zu > max "
        "%zu\n",
        name, length_, kMaxFreeListEntries);
    return;
  }

  // 初始化计数器：所有页面开始时都是空闲的
  used_count_ = 0;
  free_count_ = total_pages;

  Log("Buddy allocator '%s' creating initial free blocks...\n", name_);

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

      Log("Buddy allocator '%s' adding free block: order=%zu, size=%zu pages, "
          "addr=%p\n",
          name_, order, block_size, block_addr);

      // 直接插入块地址到链表头部
      InsertToFreeList(free_block_lists_[order], block_addr);

      // 更新地址偏移和剩余页数
      current_addr_offset += block_size;
      remaining_pages -= block_size;
    }
  }

  Log("Buddy allocator '%s' initialization completed: free_count=%zu, "
      "used_count=%zu\n",
      name_, free_count_, used_count_);
}

auto Buddy::AllocImpl(size_t order) -> void* {
  Log("Buddy allocator '%s' allocation request: order=%zu (%zu pages)\n", name_,
      order, 1UL << order);

  // 参数检查
  if (order >= length_) {
    Log("Buddy allocator '%s' allocation failed: order %zu >= max_order %zu\n",
        name_, order, length_);
    return nullptr;
  }

  // 寻找第一个可用的块(从目标大小开始向上查找)
  for (auto current_order = order; current_order < length_; current_order++) {
    if (free_block_lists_[current_order] != nullptr) {
      Log("Buddy allocator '%s' found available block at order=%zu\n", name_,
          current_order);

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

        Log("Buddy allocator '%s' allocation successful: addr=%p, order=%zu, "
            "exact match\n",
            name_, block, order);
        return block;
      }

      Log("Buddy allocator '%s' splitting block from order=%zu to order=%zu\n",
          name_, current_order, order);

      // 否则需要分割成目标大小，将多余的块放回对应的空闲链表
      while (current_order > order) {
        current_order--;
        // 计算 buddy 块地址(分割后的第二个块)
        void* buddy_block =
            static_cast<char*>(block) + kPageSize * (1 << current_order);

        Log("Buddy allocator '%s' created buddy block: addr=%p, order=%zu\n",
            name_, buddy_block, current_order);

        // 将 buddy 块插入到对应的空闲链表头部
        InsertToFreeList(free_block_lists_[current_order], buddy_block);
      }

      // 更新计数器
      size_t allocated_pages = 1 << order;
      used_count_ += allocated_pages;
      free_count_ -= allocated_pages;

      Log("Buddy allocator '%s' allocation successful: addr=%p, order=%zu, "
          "split from larger block\n",
          name_, block, order);
      return block;
    }
  }

  Log("Buddy allocator '%s' allocation failed: no available blocks for "
      "order=%zu\n",
      name_, order);
  return nullptr;
}

void Buddy::FreeImpl(void* addr, size_t order) {
  Log("Buddy allocator '%s' free request: addr=%p, order=%zu (%zu pages)\n",
      name_, addr, order, 1UL << order);

  // 参数检查
  if (order >= length_) {
    Log("Buddy allocator '%s' free failed: order %zu >= max_order %zu\n", name_,
        order, length_);
    return;
  }

  // 检查地址是否在管理的内存范围内
  size_t maxPages = 1 << (length_ - 1);
  if (addr < start_addr_ ||
      addr >= static_cast<const void*>(static_cast<const char*>(start_addr_) +
                                       maxPages * kPageSize)) {
    Log("Buddy allocator '%s' free failed: addr=%p out of range [%p, %p)\n",
        name_, addr, start_addr_,
        static_cast<const void*>(static_cast<const char*>(start_addr_) +
                                 maxPages * kPageSize));
    return;
  }

  Free(addr, order, true);
}

void Buddy::Free(void* addr, size_t order, bool update_counter) {
  // 只有在需要更新计数器时才更新(即初始调用时)
  if (update_counter) {
    size_t freed_pages = 1 << order;
    used_count_ -= freed_pages;
    free_count_ += freed_pages;
    Log("Buddy allocator '%s' updating counters: freed %zu pages\n", name_,
        freed_pages);
  }

  // 尝试查找并合并 buddy 块
  size_t block_size = 1 << order;
  void* right_buddy = static_cast<char*>(addr) + kPageSize * block_size;
  void* left_buddy_start = static_cast<char*>(addr) - kPageSize * block_size;

  Log("Buddy allocator '%s' searching for buddy blocks: order=%zu, "
      "left_buddy=%p, right_buddy=%p\n",
      name_, order, left_buddy_start, right_buddy);

  // 遍历同大小的空闲链表，寻找 buddy 块
  for (auto* curr = free_block_lists_[order]; curr != nullptr;
       curr = curr->next) {
    void* curr_addr = static_cast<void*>(curr);

    // 检查是否为右 buddy(当前要释放的块的右边相邻块)
    if (curr_addr == right_buddy) {
      if (IsValidBlockAddress(addr, block_size)) {
        Log("Buddy allocator '%s' found right buddy, merging: order=%zu -> "
            "order=%zu, addr=%p\n",
            name_, order, order + 1, addr);
        // 从链表中移除找到的 buddy 块并递归合并
        RemoveFromFreeList(free_block_lists_[order], curr);
        Free(addr, order + 1, false);
        return;
      }
    }
    // 检查是否为左 buddy(当前要释放的块的左边相邻块)
    else if (curr_addr == left_buddy_start) {
      if (IsValidBlockAddress(curr_addr, block_size)) {
        Log("Buddy allocator '%s' found left buddy, merging: order=%zu -> "
            "order=%zu, addr=%p\n",
            name_, order, order + 1, curr_addr);
        // 从链表中移除找到的 buddy 块并递归合并(使用左 buddy 的地址)
        RemoveFromFreeList(free_block_lists_[order], curr);
        Free(curr_addr, order + 1, false);
        return;
      }
    }
  }

  // 没有找到可合并的 buddy，直接插入到链表头部
  Log("Buddy allocator '%s' no buddy found, adding to free list: order=%zu, "
      "addr=%p\n",
      name_, order, addr);
  InsertToFreeList(free_block_lists_[order], addr);
}

void Buddy::InsertToFreeList(FreeBlockNode*& list_head, void* addr) {
  auto* node = static_cast<FreeBlockNode*>(addr);
  node->next = list_head;
  list_head = node;
}

bool Buddy::RemoveFromFreeList(FreeBlockNode*& list_head,
                               FreeBlockNode* target) {
  // 如果要移除的是头节点
  if (list_head == target) {
    list_head = target->next;
    Log("Buddy allocator '%s' removed head node from free list: addr=%p\n",
        name_, target);
    return true;
  }

  // 遍历链表查找目标节点
  for (auto* curr = list_head; curr->next != nullptr; curr = curr->next) {
    if (curr->next == target) {
      curr->next = target->next;
      Log("Buddy allocator '%s' removed node from free list: addr=%p\n", name_,
          target);
      return true;
    }
  }

  Log("Buddy allocator '%s' failed to remove node from free list: addr=%p not "
      "found\n",
      name_, target);
  return false;
}

bool Buddy::IsValidBlockAddress(const void* addr, size_t block_pages) const {
  // 计算地址相对于起始地址的字节偏移
  auto addr_offset =
      static_cast<const char*>(addr) - static_cast<const char*>(start_addr_);

  // 计算地址相对于起始地址的页偏移
  auto page_offset = addr_offset / kPageSize;

  // 检查地址对齐：块的起始地址必须是块大小的整数倍
  if (page_offset % block_pages != 0) {
    return false;
  }

  // 检查边界：确保块不超出管理的内存范围
  size_t max_pages = 1 << (length_ - 1);
  return page_offset + block_pages <= max_pages;
}

}  // namespace bmalloc
