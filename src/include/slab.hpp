/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_SLAB_HPP_
#define BMALLOC_SRC_INCLUDE_SLAB_HPP_

#include "allocator_base.hpp"
#include "buddy.hpp"

namespace bmalloc {
using namespace std;

template <class PageAllocator, class LogFunc = std::nullptr_t,
          class Lock = LockBase>
  requires std::derived_from<PageAllocator, AllocatorBase<LogFunc, LockBase>>
class Slab : public AllocatorBase<LogFunc, Lock> {
 public:
  using AllocatorBase<LogFunc, Lock>::Alloc;
  using AllocatorBase<LogFunc, Lock>::Free;
  using AllocatorBase<LogFunc, Lock>::GetFreeCount;
  using AllocatorBase<LogFunc, Lock>::GetUsedCount;

  struct kmem_cache_t;
  static constexpr size_t CACHE_L1_LINE_SIZE = 64;
  // 缓存名称的最大长度
  static constexpr size_t CACHE_NAMELEN = 20;
  // cache_cache的order值，表示管理kmem_cache_t结构体的cache使用的内存块大小
  static constexpr size_t CACHE_CACHE_ORDER = 0;

  /**
   * Slab结构体 - 表示一个内存slab
   *
   * 每个slab包含多个相同大小的对象，通过链表管理空闲对象
   */
  struct slab_t {
    // offset for this slab - 用于缓存行对齐的偏移量
    uint32_t colouroff;
    // starting adress of objects - 对象数组的起始地址
    void *objects;
    // list of free objects - 空闲对象索引列表
    int *freeList;
    // next free object - 下一个空闲对象的索引
    int nextFreeObj;
    // number of active objects in this slab - 当前使用的对象数量
    uint32_t inuse;
    // next slab in chain - 链表中的下一个slab
    slab_t *next;
    // previous slab in chain - 链表中的前一个slab
    slab_t *prev;
    // cache - owner - 拥有此slab的cache
    kmem_cache_t *myCache;
  };

  /**
   * Cache结构体 - 管理特定大小对象的高级结构
   *
   * 每个cache管理一种特定大小的对象，内部包含三种状态的slab链表：
   * - slabs_full: 完全使用的slab
   * - slabs_partial: 部分使用的slab
   * - slabs_free: 完全空闲的slab
   */
  struct kmem_cache_t {
    // list of full slabs - 满slab链表
    slab_t *slabs_full;
    // list of partial slabs - 部分使用slab链表
    slab_t *slabs_partial;
    // list of free slabs - 空闲slab链表
    slab_t *slabs_free;
    // cache name - 缓存名称
    char name[CACHE_NAMELEN];
    // size of one object - 单个对象大小
    size_t objectSize;
    // num of objects in one slab - 每个slab中的对象数量
    size_t objectsInSlab;
    // num of active objects in cache - 活跃对象数量
    size_t num_active;
    // num of total objects in cache - 总对象数量
    size_t num_allocations;
    // mutex (uses to lock the cache) - 缓存互斥锁
    Lock cache_mutex;
    // order of one slab (one slab has 2^order blocks) - slab的order值
    uint32_t order;
    // maximum multiplier for offset of first object in slab - 最大颜色偏移乘数
    uint32_t colour_max;
    // multiplier for next slab offset - 下一个slab的颜色偏移
    uint32_t colour_next;
    // false - cache is not growing / true - cache is growing - 是否正在增长
    bool growing;
    // objects constructor - 对象构造函数
    void (*ctor)(void *);
    // objects destructor - 对象析构函数
    void (*dtor)(void *);
    // last error that happened while working with cache - 最后的错误码
    int error_code;
    // next cache in chain - 下一个cache
    kmem_cache_t *next;
  };

  /**
   * @brief 构造 Slab 分配器
   * @param name 分配器名称
   * @param start_addr 管理的内存起始地址
   * @param page_count 管理的页数
   */
  explicit Slab(const char *name, void *start_addr, size_t page_count)
      : AllocatorBase<LogFunc, Lock>(name, start_addr, page_count),
        page_allocator_(name, start_addr, page_count),
        cache_cache() {}

  /// @name 构造/析构函数
  /// @{
  Slab() = default;
  Slab(const Slab &) = delete;
  Slab(Slab &&) = default;
  auto operator=(const Slab &) -> Slab & = delete;
  auto operator=(Slab &&) -> Slab & = default;
  ~Slab() override = default;
  /// @}

  kmem_cache_t *kmem_cache_create(const char *name, size_t size,
                                  void (*ctor)(void *),
                                  void (*dtor)(void *));  // Allocate cache

  int kmem_cache_shrink(kmem_cache_t *cachep);  // Shrink cache

  void *kmem_cache_alloc(
      kmem_cache_t *cachep);  // Allocate one object from cache

  void kmem_cache_free(kmem_cache_t *cachep,
                       void *objp);  // Deallocate one object from cache

  void *kmalloc(size_t size);  // Alloacate one small memory buffer

  void kfree(const void *objp);  // Deallocate one small memory buffer

  void kmem_cache_destroy(kmem_cache_t *cachep);  // Deallocate cache

  void kmem_cache_info(kmem_cache_t *cachep);  // Print cache info

  int kmem_cache_error(kmem_cache_t *cachep);  // Print error message
  void kmem_cache_allInfo();

 protected:
  using AllocatorBase<LogFunc, Lock>::Log;
  using AllocatorBase<LogFunc, Lock>::name_;
  using AllocatorBase<LogFunc, Lock>::start_addr_;
  using AllocatorBase<LogFunc, Lock>::length_;
  using AllocatorBase<LogFunc, Lock>::free_count_;
  using AllocatorBase<LogFunc, Lock>::used_count_;

  // guarding buddy alocator - 保护buddy分配器的互斥锁
  Lock buddy_mutex;
  // guarding cout - 保护输出的互斥锁
  // mutex cout_mutex;

  PageAllocator page_allocator_;

  // cache_cache: 管理kmem_cache_t结构体的特殊cache
  kmem_cache_t cache_cache;

  // 所有cache的链表头
  kmem_cache_t *allCaches = nullptr;

  kmem_cache_t *find_buffers_cache(const void *objp);

  /**
   * @brief 分配指定页数的内存
   * @param page_count 要分配的页数
   * @return void* 分配的内存起始地址，失败时返回0
   */
  [[nodiscard]] auto AllocImpl(size_t page_count) -> void * override {
    (void)page_count;
    return nullptr;
  }

  /**
   * @brief 释放指定地址的内存
   * @param addr 要释放的内存起始地址
   * @param page_count 要释放的页数
   */
  void FreeImpl(void *addr, size_t page_count) override {
    (void)addr;
    (void)page_count;
    return;
  }
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_SLAB_HPP_ */
