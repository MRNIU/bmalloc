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
 * - freeList[i]: 管理大小为2^i个页面的空闲块链表
 * - 每个空闲块的开头存储指向下一个空闲块的指针
 * - 第一个页面用于存储freeList数组本身
 *
 * 优点：
 * - 减少外部碎片
 * - 分配和释放速度快
 * - 实现相对简单
 *
 * 缺点：
 * - 存在内部碎片（只能分配2的幂次方大小）
 * - 需要额外的元数据存储空间
 */

#include "buddy.h"

#include <cmath>

namespace bmalloc {

// 全局变量定义
void* buddySpace;      // buddy分配器管理的内存空间起始地址
int numOfEntries;      // 空闲链表数组的条目数（对应不同大小的块）
int startingBlockNum;  // 初始总块数

void** freeList;  // 空闲链表数组，每个索引对应一种大小的空闲块链表

/**
 * @brief 初始化buddy分配器
 * @param space 要管理的内存空间起始地址
 * @param block_num 总块数（每块大小为kPageSize）
 *
 * 功能说明：
 * 1. 使用第一个页面存储buddy分配器的元数据（freeList数组）
 * 2. 将剩余内存按2的幂次方大小组织成空闲链表
 * 3. 采用贪心策略：优先分配最大的块，剩余部分继续分配次大的块
 */
void Buddy::buddy_init(void* space, int block_num) {
  // 参数检查：空间不能为空，块数至少为2（1块用于buddy管理，至少1块用于分配）
  if (space == nullptr || block_num < 2)
    exit(1);  // broj blokova mora biti veci od 1 (1 blok odlazi na buddy)

  startingBlockNum = block_num;  // 保存初始块数
  buddySpace = space;            // 保存管理空间的起始地址

  // 跳过第一个页面，用于存储buddy分配器的元数据（freeList数组）
  space =
      ((char*)space + kPageSize);  // ostatak memorije se koristi za alokaciju
  block_num--;  // 减去用于buddy管理的1个块 // prvi blok ide za potrebe buddy
                // alokatora

  int i = 1;
  // 计算需要的空闲链表条目数：log2(block_num) + 1
  // 例如：8个块需要4个条目（1,2,4,8块大小的链表）
  numOfEntries = log2(block_num) + 1;

  // 将第一个页面用作空闲链表数组
  freeList = (void**)buddySpace;
  // 初始化所有空闲链表为空
  for (i = 0; i < numOfEntries; i++) freeList[i] = nullptr;

  // 初始化空闲块：将可用内存按最大可能的块大小组织到空闲链表中
  int maxLength = numOfEntries - 1;      // 最大块大小的索引
  int maxLengthBlocks = 1 << maxLength;  // 最大块包含的页面数

  // 贪心分配：优先分配最大的块，剩余部分继续分配次大的块
  while (block_num > 0) {
    // 计算当前最大块的起始地址
    void* addr = (char*)space + (block_num - maxLengthBlocks) * kPageSize;

    // 将该块添加到对应大小的空闲链表头部
    freeList[maxLength] = addr;
    *(void**)addr = nullptr;  // 该块的next指针设为null

    block_num -= maxLengthBlocks;  // 减去已分配的块数

    // 如果还有剩余块，计算下一个最大可能的块大小
    if (block_num > 0) {
      i = 1;
      maxLength = 0;
      // 找到不超过剩余块数的最大2的幂
      while (true) {
        if (i <= block_num && 2 * i > block_num) break;
        i = i * 2;
        maxLength++;
      }
      maxLengthBlocks = 1 << maxLength;  // 计算新的最大块大小
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
void* Buddy::buddy_alloc(int n) {
  // 参数检查：n必须在有效范围内
  if (n < 0 || n >= numOfEntries) return nullptr;

  void* returningSpace = nullptr;

  // 情况1：直接有合适大小的空闲块
  if (freeList[n] != nullptr) {
    // 从空闲链表头部取出一个块
    returningSpace = freeList[n];
    freeList[n] = *(void**)returningSpace;  // 更新链表头
    *(void**)returningSpace = nullptr;      // 清空返回块的next指针
  } else {
    // 情况2：没有合适大小的块，需要分割更大的块
    for (int i = n + 1; i < numOfEntries; i++) {
      if (freeList[i] != nullptr) {
        // 找到一个更大的块，将其分割
        void* ptr1 = freeList[i];     // 取出大块
        freeList[i] = *(void**)ptr1;  // 更新大块链表
        void* ptr2 = (char*)ptr1 +
                     kPageSize * (1 << (i - 1));  // 计算分割后的第二个块地址

        // 将分割后的两个块加入到小一级的空闲链表中
        *(void**)ptr1 = ptr2;             // ptr1的next指向ptr2
        *(void**)ptr2 = freeList[i - 1];  // ptr2的next指向原链表头
        freeList[i - 1] = ptr1;           // 更新链表头为ptr1

        // 递归分配，直到得到合适大小的块
        returningSpace = buddy_alloc(n);
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
inline bool Buddy::isValid(
    void* space,
    int n)  // check if starting address (space1) is valid for length 2^n
{
  int length = 1 << n;                              // 块大小（页面数）
  int num = ((startingBlockNum - 1) % length) + 1;  // 计算对齐要求
  int i = ((char*)space - (char*)buddySpace) /
          kPageSize;  // 计算块编号 // num of first block

  // 检查块编号是否满足对齐要求：对于大小为2^n的块，起始位置必须是2^n的倍数
  if (i % length ==
      num %
          length)  // if starting block number is valid for length 2^n then true
    return true;

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
void Buddy::buddy_free(void* space, int n) {
  // 参数检查：n必须在有效范围内
  if (n < 0 || n >= numOfEntries) return;

  int bNum = 1 << n;  // 计算块大小（页面数）

  // 情况1：该大小的空闲链表为空，直接插入
  if (freeList[n] == nullptr) {
    freeList[n] = space;
    *(void**)space = nullptr;
  } else {
    // 情况2：尝试与相邻的buddy块合并
    void* prev = nullptr;
    void* curr = freeList[n];

    // 遍历同大小的空闲链表，寻找buddy块
    while (curr != nullptr) {
      // 检查是否为右buddy（当前块的右边相邻块）
      if (curr == (void*)((char*)space +
                          kPageSize * bNum))  // right buddy potentially found
      {
        if (isValid(space,
                    n + 1))  // 验证是否为有效的buddy // right buddy found
        {
          // 从链表中移除找到的buddy块
          if (prev == nullptr) {
            freeList[n] = *(void**)freeList[n];
          } else {
            *(void**)prev = *(void**)curr;
          }

          // 递归释放合并后的更大块
          buddy_free(space, n + 1);

          return;
        }
      } else if (space ==
                 (void*)((char*)curr +
                         kPageSize * bNum))  // 检查是否为左buddy（当前块的左边相邻块）
                                             // // left buddy potentially found
      {
        if (isValid(curr, n + 1))  // 验证是否为有效的buddy // left buddy found
        {
          // 从链表中移除找到的buddy块
          if (prev == nullptr) {
            freeList[n] = *(void**)freeList[n];
          } else {
            *(void**)prev = *(void**)curr;
          }

          // 递归释放合并后的更大块（使用左buddy的地址作为起始地址）
          buddy_free(curr, n + 1);

          return;
        }
      }

      // 继续遍历链表
      prev = curr;
      curr = *(void**)curr;
    }

    // 没有找到可合并的buddy，直接插入到链表头部
    *(void**)space = freeList[n];
    freeList[n] = space;
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
