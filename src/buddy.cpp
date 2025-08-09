/**
 * Copyright The bmalloc Contributors
 */

#include "buddy.h"

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

  // 将 total_pages 按二进制位分解，每个位对应一个大小的块
  auto remaining_pages = total_pages;
  // 当前地址偏移（以页为单位）
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

      // 创建节点并插入链表头部
      auto* node = FreeBlockNode::make_node(block_addr);
      node->next = free_block_lists_[order];
      free_block_lists_[order] = node;

      // 更新地址偏移和剩余页数
      current_addr_offset += block_size;
      remaining_pages -= block_size;
    }
  }
}

auto Buddy::Alloc(size_t order) -> void* {
  // 参数检查：order 必须在有效范围内
  if (order >= length_) {
    return nullptr;
  }

  // 寻找第一个可用的块（从目标大小开始向上查找）
  for (auto current_order = order; current_order < length_; current_order++) {
    if (free_block_lists_[current_order] != nullptr) {
      // 从空闲链表头部取出一个块
      auto* node = free_block_lists_[current_order];
      void* block = *node;  // 使用隐式转换
      free_block_lists_[current_order] = node->next;

      // 如果找到的块正好是目标大小，直接返回
      if (current_order == order) {
        return block;
      }

      // 否则需要分割成目标大小，将多余的块放回对应的空闲链表
      while (current_order > order) {
        current_order--;
        // 计算 buddy 块地址（分割后的第二个块）
        void* buddy_block =
            static_cast<char*>(block) + kPageSize * (1 << current_order);

        // 将 buddy 块插入到对应的空闲链表头部
        auto* buddy_node = FreeBlockNode::make_node(buddy_block);
        buddy_node->next = free_block_lists_[current_order];
        free_block_lists_[current_order] = buddy_node;
      }

      return block;
    }
  }

  return nullptr;
}

auto Buddy::Alloc(void*, size_t) -> bool { return false; }

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
    auto* node = FreeBlockNode::make_node(addr);
    node->insertToList(free_block_lists_[order]);
  } else {
    // 情况 2：尝试与相邻的 buddy 块合并
    FreeBlockNode* prev = nullptr;
    FreeBlockNode* curr = free_block_lists_[order];

    // 遍历同大小的空闲链表，寻找 buddy 块
    while (curr != nullptr) {
      // 检查是否为右 buddy（当前块的右边相邻块）
      // right buddy potentially found
      if (*curr ==
          static_cast<void*>(static_cast<char*>(addr) + kPageSize * bNum)) {
        // 验证是否为有效的 buddy
        // right buddy found
        auto* node = FreeBlockNode::make_node(addr);
        if (node->isValid(start_addr_, length_, order + 1)) {
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
      } else if (addr == static_cast<void*>(static_cast<char*>(*curr) +
                                            kPageSize * bNum)) {
        // 检查是否为左 buddy（当前块的左边相邻块）
        // left buddy potentially found
        // 验证是否为有效的 buddy
        // left buddy found
        if (curr->isValid(start_addr_, length_, order + 1)) {
          // 从链表中移除找到的 buddy 块
          if (prev == nullptr) {
            free_block_lists_[order] = curr->next;
          } else {
            prev->next = curr->next;
          }

          // 递归释放合并后的更大块（使用左 buddy 的地址作为起始地址）
          Free(*curr, order + 1);  // 使用隐式转换
          return;
        }
      }

      // 继续遍历链表
      prev = curr;
      curr = curr->next;
    }

    // 没有找到可合并的 buddy，直接插入到链表头部
    auto* node = FreeBlockNode::make_node(addr);
    node->insertToList(free_block_lists_[order]);
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

}  // namespace bmalloc
