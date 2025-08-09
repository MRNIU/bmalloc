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
   * @brief 空闲块节点结构
   */
  struct FreeBlockNode {
    // 指向下一个空闲块的指针
    FreeBlockNode* next;

    /**
     * @brief 隐式转换为 void*
     */
    operator void*() { return static_cast<void*>(this); }

    /**
     * @brief 隐式转换为 const void*
     */
    operator const void*() const { return static_cast<const void*>(this); }

    /**
     * @brief 隐式转换为 char*
     */
    operator char*() { return static_cast<char*>(static_cast<void*>(this)); }

    /**
     * @brief 隐式转换为 const char*
     */
    operator const char*() const {
      return static_cast<const char*>(static_cast<const void*>(this));
    }

    /**
     * @brief 从地址创建 FreeBlockNode*（类似构造函数的静态工厂）
     */
    static FreeBlockNode* make_node(void* addr) {
      return static_cast<FreeBlockNode*>(addr);
    }

    /**
     * @brief 从地址创建 FreeBlockNode*（类似构造函数的静态工厂）
     */
    static FreeBlockNode* make_node(char* addr) {
      return static_cast<FreeBlockNode*>(static_cast<void*>(addr));
    }

    /**
     * @brief 检查给定节点是否为大小为 2^order 的块的有效起始地址
     * @param allocator_start 分配器管理的内存起始地址
     * @param allocator_length 分配器管理的最大阶数级别
     * @param order 块大小的指数（块大小为 2^order）
     * @return true 如果节点有效
     * @return false 如果节点无效
     */
    bool isValid(const void* allocator_start, size_t allocator_length,
                 size_t order) const {
      // 计算块大小（页数）
      size_t block_pages = 1 << order;

      // 计算节点相对于起始地址的字节偏移
      auto node_offset =
          static_cast<const char*>(static_cast<const void*>(this)) -
          static_cast<const char*>(allocator_start);

      // 计算节点相对于起始地址的页偏移
      auto page_offset = node_offset / kPageSize;

      // 检查地址对齐：块的起始地址必须是块大小的整数倍
      if (page_offset % block_pages != 0) {
        return false;
      }

      // 检查边界：确保块不超出管理的内存范围
      size_t max_pages = 1 << (allocator_length - 1);
      return page_offset + block_pages <= max_pages;
    }

    /**
     * @brief 将节点插入到链表头部
     * @param list_head 链表头部的引用
     */
    void insertToList(FreeBlockNode*& list_head) {
      this->next = list_head;
      list_head = this;
    }

    /**
     * @brief 从链表中移除节点
     * @param list_head 链表头部的引用
     * @param target 要移除的目标节点
     * @return true 如果成功移除
     * @return false 如果节点不在链表中
     */
    static bool removeFromList(FreeBlockNode*& list_head,
                               FreeBlockNode* target) {
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
  };

  // 固定大小的数组，避免占用管理的内存空间
  // 空闲块链表数组，free_block_lists_[i]管理大小为2^i页的空闲块链表
  std::array<FreeBlockNode*, kMaxFreeListEntries> free_block_lists_{};
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_BUDDY_H_ */
