/**
 * @file buddy.cpp
 * @brief 二进制伙伴(Binary Buddy)内存分配器实现
 *
 * 算法原理：
 * 1. 将内存按2的幂次方大小进行分割和管理
 * 2. 维护多个空闲链表，每个链表管理特定大小(2^i)的空闲块
 * 3. 分配时：如果没有合适大小的块，就分割更大的块
 * 4. 释放时：尝试与相邻的buddy块合并成更大的块
 *
 * 数据结构：
 * - free_block_lists_[i]: 管理大小为2^i个页面的空闲块链表（静态数组）
 * - 每个空闲块的开头存储指向下一个空闲块的指针
 * - 使用静态数组存储free_block_lists_，所有管理的内存都可用于分配
 *
 * 重要设计说明：
 * - length_字段被重新定义为最大阶数级别，而不是页数
 * - 对于管理N页内存，length_ = log2(N) + 1
 * - 实际管理的最大页数为：2^(length_-1)
 * - order范围：0 到 length_-1
 *
 * 分配单位说明：
 * - 参数order表示2的幂次方的指数
 * - order=0: 分配1页 (2^0=1)
 * - order=1: 分配2页 (2^1=2)
 * - order=2: 分配4页 (2^2=4)
 * - order=3: 分配8页 (2^3=8)
 * - 以此类推...
 */

#include "buddy.h"

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
 * @brief 初始化buddy分配器
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
  for (size_t i = 0; i < length_; i++) {
    free_block_lists_[i] = nullptr;
  }

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
    // 该块的next指针设为null
    *(void**)block_addr = nullptr;

    // 减去已分配的块数
    remaining_pages -= max_block_pages;

    // 如果还有剩余块，计算下一个最大可能的块大小
    if (remaining_pages > 0) {
      size_t power_of_2 = 1;
      max_order = 0;
      // 找到不超过剩余块数的最大2的幂
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
 * @brief 分配大小为2^n个页面的内存块
 * @param n 指数，分配2^n个页面
 * @return void* 分配的内存地址，失败返回nullptr
 *
 * 算法说明：
 * 1. 如果有合适大小的空闲块，直接从链表头部取出
 * 2. 如果没有，找到最小的更大块进行分割
 * 3. 分割过程：将大块一分为二，放入小一级的链表，递归分配
 */
auto Buddy::Alloc(size_t order) -> void* {
  // 参数检查：order必须在有效范围内
  if (order >= length_) {
    return nullptr;
  }

  void* returningSpace = nullptr;

  // 情况1：直接有合适大小的空闲块
  if (free_block_lists_[order] != nullptr) {
    // 从空闲链表头部取出一个块
    returningSpace = free_block_lists_[order];
    // 更新链表头
    free_block_lists_[order] = *(void**)returningSpace;
    // 清空返回块的next指针
    *(void**)returningSpace = nullptr;
  } else {
    // 情况2：没有合适大小的块，需要分割更大的块
    for (auto i = order + 1; i < length_; i++) {
      if (free_block_lists_[i] != nullptr) {
        // 找到一个更大的块，将其分割
        // 取出大块
        void* ptr1 = free_block_lists_[i];
        // 更新大块链表
        free_block_lists_[i] = *(void**)ptr1;
        // 计算分割后的第二个块地址
        void* ptr2 = (char*)ptr1 + kPageSize * (1 << (i - 1));

        // 将分割后的两个块加入到小一级的空闲链表中
        // ptr1的next指向ptr2
        *(void**)ptr1 = ptr2;
        // ptr2的next指向原链表头
        *(void**)ptr2 = free_block_lists_[i - 1];
        // 更新链表头为ptr1
        free_block_lists_[i - 1] = ptr1;

        // 递归分配，直到得到合适大小的块
        returningSpace = Alloc(order);
        break;
      }
    }
  }

  return returningSpace;
}

/**
 * @brief 在指定地址分配2的幂次方页数的内存
 * @param addr 指定的地址
 * @param order 阶数，实际分配 2^order 个页面
 * @return true 分配成功
 * @return false 分配失败
 *
 * 实现说明：
 * buddy分配器通常不支持指定地址分配，因为：
 * 1. buddy算法依赖于特定的内存布局和对齐要求
 * 2. 指定地址可能破坏buddy块的对齐和合并规则
 * 3. 实现复杂度高，且与buddy算法的设计初衷不符
 */
auto Buddy::Alloc(void* addr, size_t order) -> bool {
  // buddy分配器不支持指定地址分配
  // 这与buddy算法的设计原理不符
  (void)addr;   // 避免未使用参数警告
  (void)order;  // 避免未使用参数警告
  return false;
}

/**
 * @brief 检查给定地址是否为大小为2^n的块的有效起始地址
 * @param space 要检查的地址
 * @param n 块大小的指数（块大小为2^n）
 * @return true 如果地址有效
 * @return false 如果地址无效
 *
 * 算法说明：
 * buddy分配器要求块的起始地址必须满足对齐要求：
 * 对于大小为2^n的块，其起始地址必须是2^n的倍数
 */
inline bool Buddy::isValid(void* space, int n) const {
  // 块大小（页面数）
  int length = 1 << n;
  // 计算实际管理的最大页数：2^(length_-1)
  size_t maxPages = 1 << (length_ - 1);
  // 计算对齐要求
  int num = (maxPages % length);
  // 计算块编号（现在直接从start_addr开始计算）
  int i = ((char*)space - (char*)start_addr_) / kPageSize;

  // 检查块编号是否满足对齐要求：对于大小为2^n的块，起始位置必须是2^n的倍数
  // if starting block number is valid for length 2^n then true
  if (i % length == num % length) {
    return true;
  }

  return false;
}

/**
 * @brief 释放大小为2^n个页面的内存块
 * @param space 要释放的内存块起始地址
 * @param n 块大小的指数（释放2^n个页面）
 *
 * 算法说明：
 * 1. 首先尝试找到相邻的buddy块进行合并
 * 2. buddy块的特点：两个相邻的同大小块，地址相差一个块的大小
 * 3. 如果找到buddy且可以合并，递归合并成更大的块
 * 4. 否则直接将块插入对应大小的空闲链表
 */
void Buddy::Free(void* addr, size_t order) {
  // 参数检查：order必须在有效范围内
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

  // 情况1：该大小的空闲链表为空，直接插入
  if (free_block_lists_[order] == nullptr) {
    free_block_lists_[order] = addr;
    *(void**)addr = nullptr;
  } else {
    // 情况2：尝试与相邻的buddy块合并
    void* prev = nullptr;
    void* curr = free_block_lists_[order];

    // 遍历同大小的空闲链表，寻找buddy块
    while (curr != nullptr) {
      // 检查是否为右buddy（当前块的右边相邻块）
      // right buddy potentially found
      if (curr == (void*)((char*)addr + kPageSize * bNum)) {
        // 验证是否为有效的buddy
        // right buddy found
        if (isValid(addr, order + 1)) {
          // 从链表中移除找到的buddy块
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
        // 检查是否为左buddy（当前块的左边相邻块）
        // left buddy potentially found
        // 验证是否为有效的buddy
        // left buddy found
        if (isValid(curr, order + 1)) {
          // 从链表中移除找到的buddy块
          if (prev == nullptr) {
            free_block_lists_[order] = *(void**)free_block_lists_[order];
          } else {
            *(void**)prev = *(void**)curr;
          }

          // 递归释放合并后的更大块（使用左buddy的地址作为起始地址）
          Free(curr, order + 1);
          return;
        }
      }

      // 继续遍历链表
      prev = curr;
      curr = *(void**)curr;
    }

    // 没有找到可合并的buddy，直接插入到链表头部
    *(void**)addr = free_block_lists_[order];
    free_block_lists_[order] = addr;
  }
}

/**
 * @brief 打印buddy分配器当前状态（调试用）
 *
 * 功能说明：
 * 遍历所有空闲链表，打印每个链表中的空闲块信息
 * 包括块的起始和结束页面编号
 *
 * 注：当前实现被注释掉了，可能是为了避免依赖iostream
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
 * length_现在表示最大阶数级别，实际管理的最大页数为2^(length_-1)
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
  for (size_t order = 0; order < length_; ++order) {
    size_t pages_per_block = 1 << order;  // 2^order 个页面
    size_t block_count = 0;

    // 遍历当前阶数的空闲链表，统计块数
    void* current = free_block_lists_[order];
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
