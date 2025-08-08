/**
 * @file buddy.cpp
 * @brief 二进制伙伴 (Binary Buddy) 内存分配器实现
 *
 * 算法原理：
 * 1. 将内存按 2 的幂次方大小进行分割和管理
 * 2. 维护多个空闲链表，每个链表管理特定大小 (2^i) 的空闲块
 * 3. 分配时：如果没有合适大小的块，就分割更大的块
 * 4. 释放时：尝试与相邻的 buddy 块合并成更大的块
 *
 * 数据结构：
 * - free_block_lists_[i]: 管理大小为 2^i 个页面的空闲块链表（静态数组）
 * - 每个空闲块的开头存储指向下一个空闲块的指针
 * - 使用静态数组存储 free_block_lists_，所有管理的内存都可用于分配
 *
 * 重要设计说明：
 * - length_ 字段被重新定义为最大阶数级别，而不是页数
 * - 对于管理 N 页内存，length_ = log2(N) + 1
 * - 实际管理的最大页数为：2^(length_-1)
 * - order 范围：0 到 length_-1
 *
 * 分配单位说明：
 * - 参数 order 表示 2 的幂次方的指数
 * - order=0: 分配 1 页 (2^0=1)
 * - order=1: 分配 2 页 (2^1=2)
 * - order=2: 分配 4 页 (2^2=4)
 * - order=3: 分配 8 页 (2^3=8)
 * - 以此类推...
 */

#include "buddy.h"

#include <algorithm>
#include <iterator>

// #include <cmath>

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

/**
 * @brief 初始化 buddy 分配器
 * @param start_addr 要管理的内存空间起始地址
 * @param total_pages 总页数（每块大小为 kPageSize）
 */
Buddy::Buddy(const char* name, void* start_addr, size_t total_pages)
    : AllocatorBase(name, start_addr, log2(total_pages) + 1) {
  if (total_pages < 1) {
    return;
  }

  // 检查是否超出静态数组大小
  if (length_ > kMaxFreeListEntries) {
    return;
  }

  // 初始化所有空闲块链表为空
  std::fill(free_block_lists_.begin(), free_block_lists_.begin() + length_,
            nullptr);

  // 最大阶数（order）级别，对应最大块的索引
  auto max_order = length_ - 1;
  // 最大块包含的页面数，即 2^max_order
  auto max_block_pages = 1 << max_order;
  // 剩余页数，用于贪心分配
  auto remaining_pages = total_pages;

  while (remaining_pages > 0) {
    // 计算当前最大块的起始地址
    auto block_addr =
        (char*)start_addr_ + (remaining_pages - max_block_pages) * kPageSize;

    // 将该块添加到对应大小的空闲链表头部
    free_block_lists_[max_order] = block_addr;
    // 该块的 next 指针设为 null
    *(void**)block_addr = nullptr;

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

/**
 * @brief 分配大小为 2^order 个页面的内存块
 * @param order 指数，分配 2^order 个页面
 * @return void* 分配的内存地址，失败返回 nullptr
 *
 * 算法说明：
 * 1. 如果有合适大小的空闲块，直接从链表头部取出
 * 2. 如果没有，找到最小的更大块进行分割
 * 3. 分割过程：将大块一分为二，放入小一级的链表，递归分配
 */
auto Buddy::Alloc(size_t order) -> void* {
  // 参数检查：order 必须在有效范围内
  if (order >= length_) {
    return nullptr;
  }

  void* allocated_block = nullptr;

  // 情况 1：直接有合适大小的空闲块
  if (free_block_lists_[order] != nullptr) {
    // 从空闲链表头部取出一个块
    allocated_block = free_block_lists_[order];
    // 更新链表头
    free_block_lists_[order] = *(void**)allocated_block;
    // 清空返回块的 next 指针
    *(void**)allocated_block = nullptr;
  } else {
    // 情况 2：没有合适大小的块，需要分割更大的块
    for (auto current_order = order + 1; current_order < length_;
         current_order++) {
      if (free_block_lists_[current_order] != nullptr) {
        // 找到一个更大的块，将其分割
        // 取出大块
        void* large_block = free_block_lists_[current_order];
        // 更新大块链表
        free_block_lists_[current_order] = *(void**)large_block;
        // 计算分割后的第二个块地址
        void* buddy_block =
            (char*)large_block + kPageSize * (1 << (current_order - 1));

        // 将分割后的两个块加入到小一级的空闲链表中s
        // large_block 的 next 指向 buddy_block
        *(void**)large_block = buddy_block;
        // buddy_block 的 next 指向原链表头
        *(void**)buddy_block = free_block_lists_[current_order - 1];
        // 更新链表头为 large_block
        free_block_lists_[current_order - 1] = large_block;

        // 递归分配，直到得到合适大小的块
        allocated_block = Alloc(order);
        break;
      }
    }
  }

  return allocated_block;
}

auto Buddy::Alloc(void*, size_t) -> bool { return false; }

/**
 * @brief 检查给定地址是否为大小为 2^order 的块的有效起始地址
 * @param space 要检查的地址
 * @param order 块大小的指数（块大小为 2^order）
 * @return true 如果地址有效
 * @return false 如果地址无效
 *
 * 算法说明：
 * buddy 分配器要求块的起始地址必须满足对齐要求：
 * 对于大小为 2^order 的块，其起始地址必须是 2^order 的倍数
 */
inline bool Buddy::isValid(void* space, int n) const {
  // 块大小（页面数）
  int length = 1 << n;
  // 计算实际管理的最大页数：2^(length_-1)
  size_t maxPages = 1 << (length_ - 1);
  // 计算对齐要求
  int num = (maxPages % length);
  // 计算块编号（现在直接从 start_addr 开始计算）
  int i = ((char*)space - (char*)start_addr_) / kPageSize;

  // 检查块编号是否满足对齐要求：对于大小为 2^order 的块，起始位置必须是 2^order
  // 的倍数 if starting block number is valid for length 2^order then true
  if (i % length == num % length) {
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
      addr >= (void*)((char*)start_addr_ + maxPages * kPageSize)) {
    return;  // 地址超出管理范围，直接返回
  }

  // 计算块大小（页面数）
  int bNum = 1 << order;

  // 情况 1：该大小的空闲链表为空，直接插入
  if (free_block_lists_[order] == nullptr) {
    free_block_lists_[order] = addr;
    *(void**)addr = nullptr;
  } else {
    // 情况 2：尝试与相邻的 buddy 块合并
    void* prev = nullptr;
    void* curr = free_block_lists_[order];

    // 遍历同大小的空闲链表，寻找 buddy 块
    while (curr != nullptr) {
      // 检查是否为右 buddy（当前块的右边相邻块）
      // right buddy potentially found
      if (curr == (void*)((char*)addr + kPageSize * bNum)) {
        // 验证是否为有效的 buddy
        // right buddy found
        if (isValid(addr, order + 1)) {
          // 从链表中移除找到的 buddy 块
          if (prev == nullptr) {
            free_block_lists_[order] = *(void**)free_block_lists_[order];
          } else {
            *(void**)prev = *(void**)curr;
          }

          // 递归释放合并后的更大块
          Free(addr, order + 1);
          return;
        }
      } else if (addr == (void*)((char*)curr + kPageSize * bNum)) {
        // 检查是否为左 buddy（当前块的左边相邻块）
        // left buddy potentially found
        // 验证是否为有效的 buddy
        // left buddy found
        if (isValid(curr, order + 1)) {
          // 从链表中移除找到的 buddy 块
          if (prev == nullptr) {
            free_block_lists_[order] = *(void**)free_block_lists_[order];
          } else {
            *(void**)prev = *(void**)curr;
          }

          // 递归释放合并后的更大块（使用左 buddy 的地址作为起始地址）
          Free(curr, order + 1);
          return;
        }
      }

      // 继续遍历链表
      prev = curr;
      curr = *(void**)curr;
    }

    // 没有找到可合并的 buddy，直接插入到链表头部
    *(void**)addr = free_block_lists_[order];
    free_block_lists_[order] = addr;
  }
}

/**
 * @brief 打印 buddy 分配器当前状态（调试用）
 *
 * 功能说明：
 * 遍历所有空闲链表，打印每个链表中的空闲块信息
 * 包括块的起始和结束页面编号
 *
 * 注：当前实现被注释掉了，可能是为了避免依赖 iostream
 */
void Buddy::buddy_print() {
  //   cout << "Buddy current state (first block,last block):" << endl;
  //   for (int i = 0; i < length_; i++) {
  //     int size = 1 << i;
  //     cout << "entry[" << i << "] (size " << size << ") -> ";
  //     void* curr = free_block_lists_[i];

  //     while (curr != nullptr) {
  //       int first = ((char*)curr - (char*)start_addr_) / kPageSize;
  //       cout << "(" << first << "," << first + size - 1 << ") -> ";
  //       curr = *(void**)curr;
  //     }
  //     cout << "NULL" << endl;
  //   }
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
    void* current = *it;
    while (current != nullptr) {
      block_count++;
      current = *(void**)current;  // 获取下一个块
    }

    // 累加当前阶数的空闲页数
    total_free_pages += block_count * pages_per_block;
  }

  return total_free_pages;
}

}  // namespace bmalloc
