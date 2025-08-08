
/**
 * @file slab.h
 * @brief slab 分配器头文件
 * @author Zone.N (Zone.Niuzh@hotmail.com)
 * @version 1.0
 * @date 2021-09-18
 * @copyright MIT LICENSE
 * https://github.com/Simple-XX/SimpleKernel
 * @par change log:
 * <table>
 * <tr><th>Date<th>Author<th>Description
 * <tr><td>2021-09-18<td>digmouse233<td>迁移到 doxygen
 * </table>
 */

#ifndef SIMPLEKERNEL_SLAB_H
#define SIMPLEKERNEL_SLAB_H

#include "allocator_base.h"
#include "cstddef"
#include "cstdint"
#include "cstdio"

/**
 * @brief SLAB 分配器
 * 只使用了 AllocatorBase 的部分变量/函数
 * @note 针对常用的 size 分别建立含有三个链表对象(full, part, free)的数组
 * 需要分配时在对应 size 数组中寻找一个合适的 chunk
 * 查找方法 首先查 part，再查 free，再没有就申请新的空间，再在 free 中查
 */
class SLAB : AllocatorBase {
 public:
  /**
   * @brief 构造函数
   * @param  name            分配器名称
   * @param  addr            管理地址起始
   * @param  length          要管理的长度
   */
  SLAB(const char* name, uint64_t addr, size_t length);

  ~SLAB(void);

  /**
   * @brief 分配内存
   * @param  length          长度，以 byte 为单位
   * @return uint64_t        分配到的内存地址
   */
  [[nodiscard]] auto Alloc(size_t length) -> uint64_t override;

  // slab 不支持这个函数
  auto Alloc(uint64_t addr, size_t length) -> bool override;

  /**
   * @brief 释放内存
   * @param  addr            要释放的地址
   * @param  length          要释放的长度
   * @note slab 不使用第二个参数
   */
  void Free(uint64_t addr, size_t length) override;

  /**
   * @brief 获取已使用的内存数量
   * @return size_t          已使用的数量
   */
  [[nodiscard]] auto GetUsedCount() const -> size_t override;

  /**
   * @brief 获取空闲的内存数量
   * @return size_t          空闲的数量
   */
  [[nodiscard]] auto GetFreeCount() const -> size_t override;

 private:
  /**
   * @brief 块抽象
   * 两级结构
   * 第二级保存相同大小的内存块的使用情况
   */
  struct chunk_t {
    /// 头节点标识
    static constexpr const uintptr_t HEAD = 0xCDCD;
    // chunk_t 结构的物理地址
    uintptr_t addr;
    // 长度，不包括自身大小 单位为 byte
    // 记录的是实际使用的长度
    // 按照 8byte 对齐
    size_t len;
    /// 双向循环链表指针
    chunk_t* prev;
    chunk_t* next;

    /**
     * @brief 构造函数只会在 SLAB 初始化时调用，且只用于构造头节点
     */
    chunk_t(void);

    ~chunk_t(void);

    /**
     * @brief 插入新节点
     * @param  _new_node       要插入的节点
     */
    void push_back(chunk_t* _new_node);

    /**
     * @brief 获取链表长度
     * @return size_t          desc
     */
    size_t size(void) const;

    bool operator==(const chunk_t& _node) const;
    bool operator!=(const chunk_t& _node) const;

    /**
     * @brief 返回相对头节点的第 _idx 项
     * @param  _idx            第几项
     * @return chunk_t&        chunk 对象
     */
    chunk_t& operator[](size_t _idx) const;
  };

  /**
   * @brief slab chche 抽象
   * 第一级保存不同长度的内存块
   */
  class slab_cache_t {
   private:
    /**
     * @brief 移动节点
     * @param  _list           目标链表
     * @param  _node           要移动的节点
     */
    void move(chunk_t& _list, chunk_t* _node);

    /**
     * @brief 申请物理内存，返回申请到的地址起点，已经初始化过，且加入 free
     * 链表
     * @param  _len            要申请的长度
     * @return chunk_t*        申请到的 chunk
     */
    chunk_t* alloc_pmm(size_t _len);

    /**
     * @brief 释放内存
     */
    void free_pmm(void);

    /**
     * @brief 分割一个节点
     * @param  _node           要分割的节点
     * @param  _len            要保留的长度
     * @note 如果剩余部分符合要求，新建节点并加入 part 链表
     * 同时将 _node->len 设置为 _len
     */
    void split(chunk_t* _node, size_t _len);

    /**
     * @brief 合并 part 中地址连续的链表项，如果有可回收的回调用 free_pmm
     * 进行回收
     */
    void merge(void);

    /**
     * @brief 在 _which 链表中查找长度符合的
     * @param  _which          要查找的链表
     * @param  _len            需要的长度
     * @param  _alloc          如果未找到是否分配
     * @return chunk_t*        未找到返回 nullptr
     */
    chunk_t* find(chunk_t& _which, size_t _len, bool _alloc);

   protected:
   public:
    // 当前 cache 的长度，单位为 byte
    size_t len;
    // 这三个作为头节点使用，不会实际使用
    // full/part/free 是相对于 pmm
    // 分配的一个或多个连续的页而言的
    // 已经分配的 len 长度的内存，当然 len 不一定全部使用，真实使用的长度由
    // chunk 记录
    chunk_t full;
    // 申请的内存使用了一部分
    chunk_t part;
    // 一整段申请的内存都没有使用
    chunk_t free;

    // 查找长度符合的
    chunk_t* find(size_t _len);

    /**
     * @brief 释放一个 full 的链表项
     * @param  _node           要释放的节点
     */
    void remove(chunk_t* _node);
  };

  /// chunk 大小
  /// @note 32bit: 0x10，64bit: 0x20
  static constexpr const size_t CHUNK_SIZE = sizeof(chunk_t);

  /**
   * @brief 管理不同长度的内存，根据下标计算
   */
  enum LEN {
    LEN256 = 0,
    LEN512,
    LEN1024,
    LEN2048,
    LEN4096,
    LEN8192,
    LEN16384,
    LEN32768,
    LEN65536 = 8,
  };

  /// 最小为 256 bytes
  static constexpr const size_t MIN = 256;
  static constexpr const size_t SHIFT = 8;
  /// 支持 256(256<<(CACHAE_LEN-1)) bytes
  /// slab_cache[0] 即为内存为 256 字节的 chunk_t 结构链表
  static constexpr const size_t CACHAE_LEN = 9;
  slab_cache_t slab_cache[CACHAE_LEN];

  /**
   * @brief 根据 _len 获取对应的 slab_cache 下标
   * @param  _len            长度
   * @return size_t          对应的下标
   */
  size_t get_idx(size_t _len) const;
};

#endif /* SIMPLEKERNEL_SLAB_H */
