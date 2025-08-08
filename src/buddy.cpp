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
 * - freeList[i]: 管理大小为2^i个页面的空闲块链表（静态数组）
 * - 每个空闲块的开头存储指向下一个空闲块的指针
 * - 使用静态数组存储freeList，所有管理的内存都可用于分配
 *
 * 优点：
 * - 减少外部碎片
 * - 分配和释放速度快
 * - 实现相对简单
 * - 不占用管理的内存空间（使用静态数组）
 *
 * 缺点：
 * - 存在内部碎片（只能分配2的幂次方大小）
 * - freeList数组大小固定，限制了最大支持的内存大小
 */

#include "buddy.h"

#include <cmath>

namespace bmalloc {

/**
 * @brief 初始化buddy分配器
 * @param start_addr 要管理的内存空间起始地址
 * @param pages 总页数（每块大小为kPageSize）
 *
 * 功能说明：
 * 1. 使用静态数组存储freeList，不占用管理的内存空间
 * 2. 将所有内存按2的幂次方大小组织成空闲链表
 * 3. 采用贪心策略：优先分配最大的块，剩余部分继续分配次大的块
 */
Buddy::Buddy(const char* name, void* start_addr, size_t pages)
    : AllocatorBase(name, start_addr, pages) {
  // 参数检查：空间不能为空，页数至少为1
  if (start_addr == nullptr || pages < 1) {
    exit(1);
  }

  // 保存初始块数
  startingBlockNum = pages;
  // 保存管理空间的起始地址
  buddySpace = start_addr;

  // 现在所有的内存都可以用于分配，不需要预留第一个页面
  // start_addr 保持不变，block_num 也保持不变

  size_t i = 1;
  // 计算需要的空闲链表条目数：log2(pages) + 1
  // 例如：8个块需要4个条目（1,2,4,8块大小的链表）
  numOfEntries = log2(pages) + 1;

  // 检查是否超出静态数组大小
  if (numOfEntries > kMaxFreeListEntries) {
    // 内存块数量超出支持范围
    exit(1);
  }

  // 初始化所有空闲链表为空
  for (i = 0; i < numOfEntries; i++) {
    freeList[i] = nullptr;
  }

  // 初始化空闲块：将可用内存按最大可能的块大小组织到空闲链表中
  // 最大块大小的索引
  int maxLength = numOfEntries - 1;
  // 最大块包含的页面数
  int maxLengthBlocks = 1 << maxLength;

  // 贪心分配：优先分配最大的块，剩余部分继续分配次大的块
  while (pages > 0) {
    // 计算当前最大块的起始地址（现在从space开始，不需要跳过第一个页面）
    void* addr = (char*)start_addr + (pages - maxLengthBlocks) * kPageSize;

    // 将该块添加到对应大小的空闲链表头部
    freeList[maxLength] = addr;
    // 该块的next指针设为null
    *(void**)addr = nullptr;

    // 减去已分配的块数
    pages -= maxLengthBlocks;

    // 如果还有剩余块，计算下一个最大可能的块大小
    if (pages > 0) {
      i = 1;
      maxLength = 0;
      // 找到不超过剩余块数的最大2的幂
      while (true) {
        if (i <= pages && 2 * i > pages) {
          break;
        }
        i = i * 2;
        maxLength++;
      }
      // 计算新的最大块大小
      maxLengthBlocks = 1 << maxLength;
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
void* Buddy::Alloc(size_t n) {
  // 参数检查：n必须在有效范围内
  if (n >= numOfEntries) {
    return nullptr;
  }

  void* returningSpace = nullptr;

  // 情况1：直接有合适大小的空闲块
  if (freeList[n] != nullptr) {
    // 从空闲链表头部取出一个块
    returningSpace = freeList[n];
    // 更新链表头
    freeList[n] = *(void**)returningSpace;
    // 清空返回块的next指针
    *(void**)returningSpace = nullptr;
  } else {
    // 情况2：没有合适大小的块，需要分割更大的块
    for (auto i = n + 1; i < numOfEntries; i++) {
      if (freeList[i] != nullptr) {
        // 找到一个更大的块，将其分割
        // 取出大块
        void* ptr1 = freeList[i];
        // 更新大块链表
        freeList[i] = *(void**)ptr1;
        // 计算分割后的第二个块地址
        void* ptr2 = (char*)ptr1 + kPageSize * (1 << (i - 1));

        // 将分割后的两个块加入到小一级的空闲链表中
        // ptr1的next指向ptr2
        *(void**)ptr1 = ptr2;
        // ptr2的next指向原链表头
        *(void**)ptr2 = freeList[i - 1];
        // 更新链表头为ptr1
        freeList[i - 1] = ptr1;

        // 递归分配，直到得到合适大小的块
        returningSpace = Alloc(n);
        break;
      }
    }
  }

  return returningSpace;
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
inline bool Buddy::isValid(void* space, int n) {
  // 块大小（页面数）
  int length = 1 << n;
  // 计算对齐要求（简化计算，因为现在从0开始）
  int num = (startingBlockNum % length);
  // 计算块编号（现在直接从buddySpace开始计算）
  int i = ((char*)space - (char*)buddySpace) / kPageSize;

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
void Buddy::Free(void* addr, size_t pages) {
  // 参数检查：n必须在有效范围内
  if (pages >= numOfEntries) {
    return;
  }

  // 计算块大小（页面数）
  int bNum = 1 << pages;

  // 情况1：该大小的空闲链表为空，直接插入
  if (freeList[pages] == nullptr) {
    freeList[pages] = addr;
    *(void**)addr = nullptr;
  } else {
    // 情况2：尝试与相邻的buddy块合并
    void* prev = nullptr;
    void* curr = freeList[pages];

    // 遍历同大小的空闲链表，寻找buddy块
    while (curr != nullptr) {
      // 检查是否为右buddy（当前块的右边相邻块）
      // right buddy potentially found
      if (curr == (void*)((char*)addr + kPageSize * bNum)) {
        // 验证是否为有效的buddy
        // right buddy found
        if (isValid(addr, pages + 1)) {
          // 从链表中移除找到的buddy块
          if (prev == nullptr) {
            freeList[pages] = *(void**)freeList[pages];
          } else {
            *(void**)prev = *(void**)curr;
          }

          // 递归释放合并后的更大块
          Free(addr, pages + 1);
          return;
        }
      } else if (addr == (void*)((char*)curr + kPageSize * bNum)) {
        // 检查是否为左buddy（当前块的左边相邻块）
        // left buddy potentially found
        // 验证是否为有效的buddy
        // left buddy found
        if (isValid(curr, pages + 1)) {
          // 从链表中移除找到的buddy块
          if (prev == nullptr) {
            freeList[pages] = *(void**)freeList[pages];
          } else {
            *(void**)prev = *(void**)curr;
          }

          // 递归释放合并后的更大块（使用左buddy的地址作为起始地址）
          Free(curr, pages + 1);
          return;
        }
      }

      // 继续遍历链表
      prev = curr;
      curr = *(void**)curr;
    }

    // 没有找到可合并的buddy，直接插入到链表头部
    *(void**)addr = freeList[pages];
    freeList[pages] = addr;
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
  //   for (int i = 0; i < numOfEntries; i++) {
  //     int size = 1 << i;
  //     cout << "entry[" << i << "] (size " << size << ") -> ";
  //     void* curr = freeList[i];

  //     while (curr != nullptr) {
  //       int first = ((char*)curr - (char*)buddySpace) / kPageSize;
  //       cout << "(" << first << "," << first + size - 1 << ") -> ";
  //       curr = *(void**)curr;
  //     }
  //     cout << "NULL" << endl;
  //   }
}

}  // namespace bmalloc
