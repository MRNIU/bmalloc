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
        page_allocator_(name, start_addr, page_count) {
    // 为cache_cache分配第一个slab
    void *ptr = page_allocator_.Alloc(CACHE_CACHE_ORDER);
    if (ptr == nullptr) {
      return;
    }
    slab_t *slab = (slab_t *)ptr;

    // 初始化cache_cache的slab链表
    cache_cache.slabs_free = slab;
    cache_cache.slabs_full = nullptr;
    cache_cache.slabs_partial = nullptr;

    // 设置cache_cache的基本属性
    strcpy(cache_cache.name, "kmem_cache");
    cache_cache.objectSize = sizeof(kmem_cache_t);
    cache_cache.order = CACHE_CACHE_ORDER;

    cache_cache.growing = false;
    cache_cache.ctor = nullptr;
    cache_cache.dtor = nullptr;
    cache_cache.error_code = 0;
    cache_cache.next = nullptr;

    // 初始化slab结构
    slab->colouroff = 0;
    slab->freeList = (int *)((char *)ptr + sizeof(slab_t));
    slab->nextFreeObj = 0;
    slab->inuse = 0;
    slab->next = nullptr;
    slab->prev = nullptr;
    slab->myCache = &cache_cache;

    // 计算每个slab能容纳的对象数量
    long memory = (1 << cache_cache.order) * kPageSize;
    memory -= sizeof(slab_t);
    int n = 0;
    while ((long)(memory - sizeof(uint32_t) - cache_cache.objectSize) >= 0) {
      n++;
      memory -= sizeof(uint32_t) + cache_cache.objectSize;
    }

    // 设置对象数组起始位置
    slab->objects =
        (void *)((char *)ptr + sizeof(slab_t) + sizeof(uint32_t) * n);
    kmem_cache_t *list = (kmem_cache_t *)slab->objects;

    // 初始化空闲对象链表
    for (int i = 0; i < n; i++) {
      *list[i].name = '\0';
      new (&list[i].cache_mutex) Lock;
      slab->freeList[i] = i + 1;
    }

    // 设置cache_cache的对象统计信息
    cache_cache.objectsInSlab = n;
    cache_cache.num_active = 0;
    cache_cache.num_allocations = n;

    // 设置缓存行对齐参数
    cache_cache.colour_max = memory / CACHE_L1_LINE_SIZE;
    if (cache_cache.colour_max > 0)
      cache_cache.colour_next = 1;
    else
      cache_cache.colour_next = 0;

    // 将cache_cache加入全局cache链表
    allCaches = &cache_cache;
  }

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
                                  void (*ctor)(void *), void (*dtor)(void *)) {
    // 参数验证
    if (name == nullptr || *name == '\0' || (long)size <= 0) {
      cache_cache.error_code = 1;
      return nullptr;
    }

    // 禁止创建与cache_cache同名的cache
    if (strcmp(name, cache_cache.name) == 0) {
      cache_cache.error_code = 3;
      return nullptr;
    }

    LockGuard guard(cache_cache.cache_mutex);

    kmem_cache_t *ret = nullptr;
    cache_cache.error_code = 0;  // reset error code

    slab_t *s;

    // 第一种方法：在全局cache链表中查找是否已存在相同的cache
    ret = allCaches;
    while (ret != nullptr) {
      if (strcmp(ret->name, name) == 0 && ret->objectSize == size) {
        return ret;
      }
      ret = ret->next;
    }

    // cache不存在，需要创建新的

    // 寻找可用的slab来分配kmem_cache_t结构
    s = cache_cache.slabs_partial;
    if (s == nullptr) {
      s = cache_cache.slabs_free;
    }

    if (s == nullptr)  // 没有足够空间，需要为cache_cache分配更多空间
    {
      LockGuard guard(buddy_mutex);
      void *ptr = page_allocator_.Alloc(CACHE_CACHE_ORDER);
      if (ptr == nullptr) {
        cache_cache.error_code = 2;
        return nullptr;
      }
      s = (slab_t *)ptr;

      cache_cache.slabs_partial = s;

      // 设置缓存行对齐偏移
      s->colouroff = cache_cache.colour_next;
      cache_cache.colour_next =
          (cache_cache.colour_next + 1) % (cache_cache.colour_max + 1);

      // 初始化新slab
      s->freeList = (int *)((char *)ptr + sizeof(slab_t));
      s->nextFreeObj = 0;
      s->inuse = 0;
      s->next = nullptr;
      s->prev = nullptr;
      s->myCache = &cache_cache;

      s->objects = (void *)((char *)ptr + sizeof(slab_t) +
                            sizeof(uint32_t) * cache_cache.objectsInSlab +
                            CACHE_L1_LINE_SIZE * s->colouroff);
      kmem_cache_t *list = (kmem_cache_t *)s->objects;

      // 初始化对象数组
      for (size_t i = 0; i < cache_cache.objectsInSlab; i++) {
        *list[i].name = '\0';
        // memcpy(&list[i].cache_mutex, &mutex(), sizeof(mutex));
        new (&list[i].cache_mutex) Lock;
        s->freeList[i] = i + 1;
      }
      // s->freeList[cache_cache.objectsInSlab - 1] = -1;

      cache_cache.num_allocations += cache_cache.objectsInSlab;

      cache_cache.growing = true;
    }

    // 从slab中分配一个kmem_cache_t对象
    kmem_cache_t *list = (kmem_cache_t *)s->objects;
    ret = &list[s->nextFreeObj];
    s->nextFreeObj = s->freeList[s->nextFreeObj];
    s->inuse++;
    cache_cache.num_active++;

    // 更新slab链表状态
    if (s == cache_cache.slabs_free) {
      cache_cache.slabs_free = s->next;
      if (cache_cache.slabs_free != nullptr) {
        cache_cache.slabs_free->prev = nullptr;
      }
      // from free to partial
      if (s->inuse != cache_cache.objectsInSlab) {
        s->next = cache_cache.slabs_partial;
        if (cache_cache.slabs_partial != nullptr)
          cache_cache.slabs_partial->prev = s;
        cache_cache.slabs_partial = s;
      } else  // from free to full
      {
        s->next = cache_cache.slabs_full;
        if (cache_cache.slabs_full != nullptr) {
          cache_cache.slabs_full->prev = s;
        }
        cache_cache.slabs_full = s;
      }
    } else {
      if (s->inuse == cache_cache.objectsInSlab)  // from partial to full
      {
        cache_cache.slabs_partial = s->next;
        if (cache_cache.slabs_partial != nullptr) {
          cache_cache.slabs_partial->prev = nullptr;
        }

        s->next = cache_cache.slabs_full;
        if (cache_cache.slabs_full != nullptr) {
          cache_cache.slabs_full->prev = s;
        }
        cache_cache.slabs_full = s;
      }
    }

    // 初始化新cache
    strcpy(ret->name, name);

    ret->slabs_full = nullptr;
    ret->slabs_partial = nullptr;
    ret->slabs_free = nullptr;

    ret->growing = false;
    ret->ctor = ctor;
    ret->dtor = dtor;
    ret->error_code = 0;
    ret->next = allCaches;
    allCaches = ret;

    // 计算新cache的order值（使一个slab能容纳至少一个对象）
    long memory = kPageSize;
    int order = 0;
    while ((long)(memory - sizeof(slab_t) - sizeof(uint32_t) - size) < 0) {
      order++;
      memory *= 2;
    }

    ret->objectSize = size;
    ret->order = order;

    // 计算每个slab中的对象数量
    memory -= sizeof(slab_t);
    int n = 0;
    while ((long)(memory - sizeof(uint32_t) - size) >= 0) {
      n++;
      memory -= sizeof(uint32_t) + size;
    }

    ret->objectsInSlab = n;
    ret->num_active = 0;
    ret->num_allocations = 0;

    // 设置缓存行对齐参数
    ret->colour_max = memory / CACHE_L1_LINE_SIZE;
    ret->colour_next = 0;

    return ret;
  }

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
