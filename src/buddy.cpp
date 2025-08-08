/**
 * Copyright The bmalloc Contributors
 */

#include "buddy.h"

#include <cstdio>
#include <iterator>

namespace bmalloc {

/**
 * @brief 整数 log2 函数实现
 * @param value 输入值（必须 > 0）
 * @return size_t log2(value) 的结果
 */
static inline size_t log2(size_t value) {
  if (value == 0) {
    return 0;
  }

  size_t result = 0;
  while (value >>= 1) {
    result++;
  }
  return result;
}

Buddy::Buddy(const char* name, void* start_addr, size_t total_pages)
    : AllocatorBase(name, start_addr, log2(total_pages) + 1) {
  if (total_pages < 1) {
    return;
  }

  // 检查是否超出静态数组大小
  if (length_ > kMaxFreeListEntries) {
    return;
  }

  // 最大阶数（order）级别，对应最大块的索引
  auto max_order = length_ - 1;
  // 最大块包含的页面数，即 2^max_order
  auto max_block_pages = 1 << max_order;
  // 剩余页数，用于贪心分配
  auto remaining_pages = total_pages;

  while (remaining_pages > 0) {
    // 计算当前最大块的起始地址
    auto block_addr = const_cast<char*>(static_cast<const char*>(start_addr_)) +
                      (remaining_pages - max_block_pages) * kPageSize;

    // 将该块添加到对应大小的空闲链表头部
    auto* node = FreeBlockNode::FromAddress(block_addr);
    node->next = free_block_lists_[max_order];
    free_block_lists_[max_order] = node;

    // 减去已分配的块数
    remaining_pages -= max_block_pages;

    // 如果还有剩余块，计算下一个最大可能的块大小
    if (remaining_pages > 0) {
      size_t power_of_2 = 1;
      max_order = 0;
      // 找到不超过剩余块数的最大 2 的幂
      while (true) {
        if (power_of_2 <= remaining_pages && 2 * power_of_2 > remaining_pages) {
          break;
        }
        power_of_2 = power_of_2 * 2;
        max_order++;
      }
      // 计算新的最大块大小
      max_block_pages = 1 << max_order;
    }
  }
}

auto Buddy::Alloc(size_t order) -> void* {
  // 参数检查：order 必须在有效范围内
  if (order >= length_) {
    return nullptr;
  }

  void* allocated_block = nullptr;

  // 情况 1：直接有合适大小的空闲块
  if (free_block_lists_[order] != nullptr) {
    // 从空闲链表头部取出一个块
    auto* node = free_block_lists_[order];
    allocated_block = node->ToAddress();
    // 更新链表头
    free_block_lists_[order] = node->next;
  } else {
    // 情况 2：没有合适大小的块，需要分割更大的块
    for (auto current_order = order + 1; current_order < length_;
         current_order++) {
      if (free_block_lists_[current_order] != nullptr) {
        // 找到一个更大的块，将其分割
        // 取出大块
        auto* large_node = free_block_lists_[current_order];
        void* large_block = large_node->ToAddress();
        // 更新大块链表
        free_block_lists_[current_order] = large_node->next;
        // 计算分割后的第二个块地址
        void* buddy_block = static_cast<char*>(large_block) +
                            kPageSize * (1 << (current_order - 1));

        // 将分割后的两个块加入到小一级的空闲链表中
        auto* large_block_node = FreeBlockNode::FromAddress(large_block);
        auto* buddy_block_node = FreeBlockNode::FromAddress(buddy_block);

        // large_block 的 next 指向 buddy_block
        large_block_node->next = buddy_block_node;
        // buddy_block 的 next 指向原链表头
        buddy_block_node->next = free_block_lists_[current_order - 1];
        // 更新链表头为 large_block
        free_block_lists_[current_order - 1] = large_block_node;

        // 递归分配，直到得到合适大小的块
        allocated_block = Alloc(order);
        break;
      }
    }
  }

  return allocated_block;
}

auto Buddy::Alloc(void*, size_t) -> bool { return false; }

inline bool Buddy::isValid(void* addr, size_t order) const {
  // 块大小（页面数）
  auto block_size_pages = static_cast<size_t>(1 << order);
  // 计算实际管理的最大页数：2^(length_-1)
  auto total_managed_pages = static_cast<size_t>(1 << (length_ - 1));
  // 计算对齐偏移量
  auto alignment_offset =
      static_cast<size_t>(total_managed_pages % block_size_pages);
  // 计算块编号（现在直接从 start_addr 开始计算）
  auto block_index = static_cast<size_t>(
      (static_cast<const char*>(addr) - static_cast<const char*>(start_addr_)) /
      kPageSize);

  // 检查块编号是否满足对齐要求：对于大小为 2^order 的块，起始位置必须是 2^order
  // 的倍数 if starting block number is valid for length 2^order then true
  if (block_index % block_size_pages == alignment_offset % block_size_pages) {
    return true;
  }

  return false;
}

/**
 * @brief 释放大小为 2^n 个页面的内存块
 * @param addr 要释放的内存块起始地址
 * @param order 块大小的指数（释放 2^order 个页面）
 *
 * 算法说明：
 * 1. 首先尝试找到相邻的 buddy 块进行合并
 * 2. buddy 块的特点：两个相邻的同大小块，地址相差一个块的大小
 * 3. 如果找到 buddy 且可以合并，递归合并成更大的块
 * 4. 否则直接将块插入对应大小的空闲链表
 */
void Buddy::Free(void* addr, size_t order) {
  // 参数检查：order 必须在有效范围内
  if (order >= length_) {
    return;
  }

  // 参数检查：地址必须在管理的内存范围内
  // 计算实际管理的最大页数：2^(length_-1)
  size_t maxPages = 1 << (length_ - 1);
  if (addr < start_addr_ ||
      addr >= static_cast<const void*>(static_cast<const char*>(start_addr_) +
                                       maxPages * kPageSize)) {
    return;
  }

  // 计算块大小（页面数）
  int bNum = 1 << order;

  // 情况 1：该大小的空闲链表为空，直接插入
  if (free_block_lists_[order] == nullptr) {
    auto* node = FreeBlockNode::FromAddress(addr);
    node->next = nullptr;
    free_block_lists_[order] = node;
  } else {
    // 情况 2：尝试与相邻的 buddy 块合并
    FreeBlockNode* prev = nullptr;
    FreeBlockNode* curr = free_block_lists_[order];

    // 遍历同大小的空闲链表，寻找 buddy 块
    while (curr != nullptr) {
      // 检查是否为右 buddy（当前块的右边相邻块）
      // right buddy potentially found
      if (curr->ToAddress() ==
          static_cast<void*>(static_cast<char*>(addr) + kPageSize * bNum)) {
        // 验证是否为有效的 buddy
        // right buddy found
        if (isValid(addr, order + 1)) {
          // 从链表中移除找到的 buddy 块
          if (prev == nullptr) {
            free_block_lists_[order] = curr->next;
          } else {
            prev->next = curr->next;
          }

          // 递归释放合并后的更大块
          Free(addr, order + 1);
          return;
        }
      } else if (addr ==
                 static_cast<void*>(static_cast<char*>(curr->ToAddress()) +
                                    kPageSize * bNum)) {
        // 检查是否为左 buddy（当前块的左边相邻块）
        // left buddy potentially found
        // 验证是否为有效的 buddy
        // left buddy found
        if (isValid(curr->ToAddress(), order + 1)) {
          // 从链表中移除找到的 buddy 块
          if (prev == nullptr) {
            free_block_lists_[order] = curr->next;
          } else {
            prev->next = curr->next;
          }

          // 递归释放合并后的更大块（使用左 buddy 的地址作为起始地址）
          Free(curr->ToAddress(), order + 1);
          return;
        }
      }

      // 继续遍历链表
      prev = curr;
      curr = curr->next;
    }

    // 没有找到可合并的 buddy，直接插入到链表头部
    auto* node = FreeBlockNode::FromAddress(addr);
    node->next = free_block_lists_[order];
    free_block_lists_[order] = node;
  }
}

/**
 * @brief 获取已使用的页数
 * @return size_t 已使用的页数
 *
 * 实现说明：
 * 通过计算实际管理的总页数减去空闲页数来得到已使用页数
 * length_ 现在表示最大阶数级别，实际管理的最大页数为 2^(length_-1)
 */
auto Buddy::GetUsedCount() const -> size_t {
  // 计算实际管理的最大页数
  size_t maxPages = (length_ > 0) ? (1 << (length_ - 1)) : 0;
  return maxPages - GetFreeCount();
}

/**
 * @brief 获取空闲的页数
 * @return size_t 空闲的页数
 *
 * 实现说明：
 * 遍历所有空闲链表，统计空闲块的总页数
 * - free_block_lists_[i] 中的每个块包含 2^i 个页面
 * - 需要遍历每个链表，计算块数并乘以对应的页面数
 */
auto Buddy::GetFreeCount() const -> size_t {
  size_t total_free_pages = 0;

  // 遍历所有阶数的空闲链表
  for (auto it = free_block_lists_.begin();
       it != free_block_lists_.begin() + length_; ++it) {
    size_t order = std::distance(free_block_lists_.begin(), it);
    size_t pages_per_block = 1 << order;
    size_t block_count = 0;

    // 遍历当前阶数的空闲链表，统计块数
    FreeBlockNode* current = *it;
    while (current != nullptr) {
      block_count++;
      current = current->next;
    }

    // 累加当前阶数的空闲页数
    total_free_pages += block_count * pages_per_block;
  }

  return total_free_pages;
}

void Buddy::buddy_print() const {
  printf("Buddy current state (first block,last block):\n");
  for (size_t i = 0; i < length_; i++) {
    auto size = static_cast<size_t>(1 << i);
    printf("entry[%zu] (size %zu) -> ", i, size);
    FreeBlockNode* curr = free_block_lists_[i];

    while (curr != nullptr) {
      auto first =
          static_cast<size_t>((static_cast<const char*>(curr->ToAddress()) -
                               static_cast<const char*>(start_addr_)) /
                              kPageSize);
      printf("(%zu,%zu) -> ", first, first + size - 1);
      curr = curr->next;
    }
    printf("NULL\n");
  }
}

}  // namespace bmalloc
