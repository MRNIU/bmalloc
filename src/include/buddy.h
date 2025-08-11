/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_BUDDY_H_
#define BMALLOC_SRC_INCLUDE_BUDDY_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "allocator_base.h"

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
class Buddy : public AllocatorBase {
 public:
  /**
   * @brief 构造 Buddy 分配器
   * @param name 分配器名称
   * @param start_addr 管理的内存起始地址
   * @param total_pages 管理的总页数
   * @note 内部会将length_设置为最大阶数级别(log2(total_pages)+1)，而不是页数
   */
  explicit Buddy(const char* name, void* start_addr, size_t total_pages);

  /// @name 构造/析构函数
  /// @{
  Buddy() = default;
  Buddy(const Buddy&) = delete;
  Buddy(Buddy&&) = default;
  auto operator=(const Buddy&) -> Buddy& = delete;
  auto operator=(Buddy&&) -> Buddy& = default;
  ~Buddy() override = default;
  /// @}

  /**
   * @brief 分配2的幂次方页数的内存
   * @param order 阶数，实际分配 2^order 个页面
   * @return void* 分配的内存起始地址，失败时返回 nullptr
   * @note 例如：order=0分配1页，order=1分配2页，order=2分配4页
   */
  [[nodiscard]] auto Alloc(size_t order) -> void* override;

  /**
   * 不支持指定地址分配
   */
  auto Alloc(void*, size_t) -> bool override;

  /**
   * @brief 释放2的幂次方页数的内存
   * @param addr 要释放的内存起始地址
   * @param order 阶数，实际释放 2^order 个页面
   * @note 必须与分配时使用的order值相同
   */
  void Free(void* addr, size_t order) override;

  /**
   * @brief 获取已使用的页数
   * @return size_t 已使用的页数
   */
  [[nodiscard]] auto GetUsedCount() const -> size_t override;

  /**
   * @brief 获取空闲的页数
   * @return size_t 空闲的页数
   */
  [[nodiscard]] auto GetFreeCount() const -> size_t override;

 protected:
  // 常量定义
  // 最大支持2^31个页面，足够大部分应用
  static constexpr size_t kMaxFreeListEntries = 32;

  /**
   * @brief 简化的空闲块节点结构
   * 
   * 设计原则：保持简单，只包含必要的功能
   * - 仅包含指向下一个节点的指针
   * - 链表操作移到 Buddy 类中处理
   * - 移除复杂的转换操作符和验证逻辑
   */
  struct FreeBlockNode {
    FreeBlockNode* next;
    
    // 默认构造函数
    FreeBlockNode() : next(nullptr) {}
    
    // 禁用拷贝和移动（这是一个链表节点，不应该被拷贝）
    FreeBlockNode(const FreeBlockNode&) = delete;
    FreeBlockNode& operator=(const FreeBlockNode&) = delete;
    FreeBlockNode(FreeBlockNode&&) = delete;
    FreeBlockNode& operator=(FreeBlockNode&&) = delete;
    
    /**
     * @brief 静态工厂方法：从内存地址创建节点
     * @param addr 内存地址
     * @return FreeBlockNode* 节点指针
     */
    static FreeBlockNode* FromAddress(void* addr) {
      return static_cast<FreeBlockNode*>(addr);
    }
  };

  /**
   * @brief 将节点插入到链表头部
   * @param list_head 链表头部的引用
   * @param node 要插入的节点
   */
  void InsertToFreeList(FreeBlockNode*& list_head, FreeBlockNode* node) {
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
    if (list_head == nullptr || target == nullptr) {
      return false;
    }

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

    return false;
  }

  /**
   * @brief 检查给定地址是否为大小为 2^order 的块的有效起始地址
   * @param addr 要检查的地址
   * @param order 块大小的指数（块大小为 2^order）
   * @return true 如果地址有效
   * @return false 如果地址无效
   */
  bool IsValidBlockAddress(const void* addr, size_t order) const {
    // 计算块大小（页数）
    size_t block_pages = 1 << order;

    // 计算地址相对于起始地址的字节偏移
    auto addr_offset = static_cast<const char*>(addr) - 
                       static_cast<const char*>(start_addr_);

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

  /**
   * @brief 遍历所有空闲块链表，对每个空闲块执行给定的操作
   * @param func 对每个空闲块执行的函数，参数为 (order, block_addr, block_count)
   *             - order: 块的阶数 (0, 1, 2, ...)
   *             - block_addr: 块的起始地址
   *             - block_count: 当前阶数下的块数量
   */
  template <typename Func>
  void ForEachFreeBlock(Func&& func) const {
    // 遍历所有阶数的空闲链表
    for (size_t order = 0; order < length_; order++) {
      size_t block_count = 0;

      // 遍历当前阶数的空闲链表
      FreeBlockNode* current = free_block_lists_[order];
      while (current != nullptr) {
        // 对每个空闲块执行函数
        func(order, static_cast<void*>(current), block_count);
        block_count++;
        current = current->next;
      }
    }
  }

  // 固定大小的数组，避免占用管理的内存空间
  // 空闲块链表数组，free_block_lists_[i]管理大小为2^i页的空闲块链表
  std::array<FreeBlockNode*, kMaxFreeListEntries> free_block_lists_{};
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_BUDDY_H_ */

/// @todo 为遍历 free_block_lists_ 的操作添加一个接口，接受一个 lambda 函数作为参数 ✓ 已完成
/// @todo FreeBlockNode 太复杂了，尝试简化 ✓ 已简化
/// @todo 将 FreeBlockNode 的实现移动到 cpp



