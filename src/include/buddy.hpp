/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_BUDDY_HPP_
#define BMALLOC_SRC_INCLUDE_BUDDY_HPP_

#include <array>
#include <cstddef>
#include <cstdint>

#include "allocator_base.hpp"

namespace bmalloc {

/**
 * Buddy 内存分配器实现
 *
 * 算法原理：
 * 1. 将内存按 2 的幂次方大小进行分割和管理
 * 2. 维护多个空闲链表，每个链表管理特定大小 (2^i) 的空闲块
 * 3. 分配时：如果没有合适大小的块，就分割更大的块
 * 4. 释放时：尝试与相邻的 buddy 块合并成更大的块
 *
 * 数据结构：
 * - free_block_lists_[i]: 管理大小为 2^i 个页面的空闲块链表(静态数组)
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
template <class LogFunc = std::nullptr_t, class Lock = LockBase>
class Buddy : public AllocatorBase<LogFunc, Lock> {
 public:
  using AllocatorBase<LogFunc, Lock>::Alloc;
  using AllocatorBase<LogFunc, Lock>::Free;
  using AllocatorBase<LogFunc, Lock>::GetFreeCount;
  using AllocatorBase<LogFunc, Lock>::GetUsedCount;

  /**
   * @brief 构造 Buddy 分配器
   * @param name 分配器名称
   * @param start_addr 管理的内存起始地址
   * @param total_pages 管理的总页数
   * @param log_func printf 风格的日志函数指针（可选）
   * @param lock 锁接口指针（可选，默认使用无操作锁）
   * @note 内部会将length_设置为最大阶数级别(log2(total_pages)+1)，而不是页数
   */
  explicit Buddy(const char* name, void* start_addr, size_t total_pages)
      : AllocatorBase<LogFunc, Lock>(name, start_addr, log2(total_pages) + 1) {
    if (total_pages < 1) {
      Log("Buddy allocator '%s' initialization failed: total_pages < 1\n",
          name);
      return;
    }

    // 检查是否超出静态数组大小
    if (length_ > kMaxFreeListEntries) {
      Log("Buddy allocator '%s' initialization failed: required order %zu > "
          "max "
          "%zu\n",
          name, length_, kMaxFreeListEntries);
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

  /// @name 构造/析构函数
  /// @{
  Buddy() = default;
  Buddy(const Buddy&) = delete;
  Buddy(Buddy&&) = default;
  auto operator=(const Buddy&) -> Buddy& = delete;
  auto operator=(Buddy&&) -> Buddy& = default;
  ~Buddy() override = default;
  /// @}

 protected:
  // 最大支持 2^31 个页面，足够大部分应用
  static constexpr size_t kMaxFreeListEntries = 32;

  struct FreeBlockNode {
    FreeBlockNode* next{};
  };

  using AllocatorBase<LogFunc, Lock>::Log;
  using AllocatorBase<LogFunc, Lock>::name_;
  using AllocatorBase<LogFunc, Lock>::start_addr_;
  using AllocatorBase<LogFunc, Lock>::length_;
  using AllocatorBase<LogFunc, Lock>::free_count_;
  using AllocatorBase<LogFunc, Lock>::used_count_;
  using AllocatorBase<LogFunc, Lock>::kPageSize;

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

  /**
   * @brief 分配 2 的幂次方页数的内存
   * @param order 阶数，实际分配 2^order 个页面
   * @return void* 分配的内存起始地址，失败时返回 nullptr
   * @note 例如：order=0 分配 1 页，order=1 分配 2 页，order=2 分配 4 页
   */
  [[nodiscard]] auto AllocImpl(size_t order) -> void* override {
    // 参数检查
    if (order >= length_) {
      Log("Buddy allocator '%s' allocation failed: order %zu >= max_order "
          "%zu\n",
          name_, order, length_);
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

    Log("Buddy allocator '%s' allocation failed: no available blocks for "
        "order=%zu\n",
        name_, order);
    return nullptr;
  }

  /**
   * @brief 释放 2 的幂次方页数的内存
   * @param addr 要释放的内存起始地址
   * @param order 阶数，实际释放 2^order 个页面
   * @note 必须与分配时使用的order值相同
   */
  void FreeImpl(void* addr, size_t order) override {
    // 参数检查
    if (order >= length_) {
      Log("Buddy allocator '%s' free failed: order %zu >= max_order %zu\n",
          name_, order, length_);
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

  /**
   * @brief 内部释放函数，用于递归合并
   * @param addr 要释放的内存起始地址
   * @param order 阶数，实际释放 2^order 个页面
   * @param update_counter 是否更新计数器(只有初始调用时为true)
   */
  void Free(void* addr, size_t order, bool update_counter) {
    // 只有在需要更新计数器时才更新(即初始调用时)
    if (update_counter) {
      size_t freed_pages = 1 << order;
      used_count_ -= freed_pages;
      free_count_ += freed_pages;
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
          Free(addr, order + 1, false);
          return;
        }
      }
      // 检查是否为左 buddy(当前要释放的块的左边相邻块)
      else if (curr_addr == left_buddy_start) {
        if (IsValidBlockAddress(curr_addr, block_size)) {
          // 从链表中移除找到的 buddy 块并递归合并(使用左 buddy 的地址)
          RemoveFromFreeList(free_block_lists_[order], curr);
          Free(curr_addr, order + 1, false);
          return;
        }
      }
    }

    // 没有找到可合并的 buddy，直接插入到链表头部
    InsertToFreeList(free_block_lists_[order], addr);
  }

  /**
   * @brief 将节点插入到链表头部
   * @param list_head 链表头部的引用
   * @param addr 要插入的内存地址
   */
  void InsertToFreeList(FreeBlockNode*& list_head, void* addr) {
    auto* node = static_cast<FreeBlockNode*>(addr);
    node->next = list_head;
    list_head = node;
  }

  /**
   * @brief 从链表中移除节点
   * @param list_head 链表头部的引用
   * @param target 要移除的目标节点
   * @return true 如果成功移除
   * @return false 如果节点不在链表中
   */
  bool RemoveFromFreeList(FreeBlockNode*& list_head, FreeBlockNode* target) {
    // 如果要移除的是头节点
    if (list_head == target) {
      list_head = target->next;
      return true;
    }

    // 遍历链表查找目标节点
    for (auto* curr = list_head; curr->next != nullptr; curr = curr->next) {
      if (curr->next == target) {
        curr->next = target->next;
        return true;
      }
    }

    Log("Buddy allocator '%s' failed to remove node from free list: addr=%p "
        "not "
        "found\n",
        name_, target);
    return false;
  }

  /**
   * @brief 检查给定地址是否为指定大小块的有效起始地址
   * @param addr 要检查的地址
   * @param block_pages 块大小(页数)
   * @return true 如果地址有效
   * @return false 如果地址无效
   */
  bool IsValidBlockAddress(const void* addr, size_t block_pages) const {
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

  // 空闲块链表数组，free_block_lists_[i] 管理大小为 2^i 页的空闲块链表
  std::array<FreeBlockNode*, kMaxFreeListEntries> free_block_lists_{};
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_BUDDY_HPP_ */
