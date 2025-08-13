/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_SLAB_HPP_
#define BMALLOC_SRC_INCLUDE_SLAB_HPP_

#include "allocator_base.hpp"
#include "buddy.hpp"

namespace bmalloc {

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
    Lock cache_lock;
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
    auto *slab = static_cast<slab_t *>(ptr);

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
    slab->freeList =
        reinterpret_cast<int *>(static_cast<char *>(ptr) + sizeof(slab_t));
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
    slab->objects = static_cast<void *>(static_cast<char *>(ptr) +
                                        sizeof(slab_t) + sizeof(uint32_t) * n);
    auto *list = static_cast<kmem_cache_t *>(slab->objects);

    // 初始化空闲对象链表
    for (int i = 0; i < n; i++) {
      *list[i].name = '\0';
      new (&list[i].cache_lock) Lock;
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

  /**
   * 创建一个新的cache
   *
   * @param name cache的名称
   * @param size 每个对象的大小（字节）
   * @param ctor 对象构造函数（可选）
   * @param dtor 对象析构函数（可选）
   * @return 成功返回cache指针，失败返回nullptr
   *
   * 功能：
   * 1. 参数验证
   * 2. 检查是否已存在相同的cache
   * 3. 从cache_cache中分配kmem_cache_t结构
   * 4. 计算最优的slab大小（order值）
   * 5. 计算每个slab能容纳的对象数量
   * 6. 设置缓存行对齐参数
   */
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

    LockGuard guard(cache_cache.cache_lock);

    kmem_cache_t *ret = nullptr;
    // reset error code
    cache_cache.error_code = 0;

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
    // 没有足够空间，需要为cache_cache分配更多空间
    if (s == nullptr) {
      void *ptr = page_allocator_.Alloc(CACHE_CACHE_ORDER);
      if (ptr == nullptr) {
        cache_cache.error_code = 2;
        return nullptr;
      }
      s = static_cast<slab_t *>(ptr);

      cache_cache.slabs_partial = s;

      // 设置缓存行对齐偏移
      s->colouroff = cache_cache.colour_next;
      cache_cache.colour_next =
          (cache_cache.colour_next + 1) % (cache_cache.colour_max + 1);

      // 初始化新slab
      s->freeList =
          reinterpret_cast<int *>(static_cast<char *>(ptr) + sizeof(slab_t));
      s->nextFreeObj = 0;
      s->inuse = 0;
      s->next = nullptr;
      s->prev = nullptr;
      s->myCache = &cache_cache;

      s->objects =
          static_cast<void *>(static_cast<char *>(ptr) + sizeof(slab_t) +
                              sizeof(uint32_t) * cache_cache.objectsInSlab +
                              CACHE_L1_LINE_SIZE * s->colouroff);
      auto *list = static_cast<kmem_cache_t *>(s->objects);

      // 初始化对象数组
      for (size_t i = 0; i < cache_cache.objectsInSlab; i++) {
        *list[i].name = '\0';
        new (&list[i].cache_lock) Lock;
        s->freeList[i] = i + 1;
      }
      // s->freeList[cache_cache.objectsInSlab - 1] = -1;

      cache_cache.num_allocations += cache_cache.objectsInSlab;

      cache_cache.growing = true;
    }

    // 从slab中分配一个kmem_cache_t对象
    auto *list = static_cast<kmem_cache_t *>(s->objects);
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
      } else {
        // from free to full
        s->next = cache_cache.slabs_full;
        if (cache_cache.slabs_full != nullptr) {
          cache_cache.slabs_full->prev = s;
        }
        cache_cache.slabs_full = s;
      }
    } else {
      // from partial to full
      if (s->inuse == cache_cache.objectsInSlab) {
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

  /**
   * 收缩cache - 释放空闲的slab以节省内存
   *
   * @param cachep 要收缩的cache指针
   * @return 释放的内存块数量
   *
   * 功能：
   * 1. 释放cache中所有完全空闲的slab
   * 2. 只在cache不处于增长状态时执行
   * 3. 返回释放的内存块总数
   */
  int kmem_cache_shrink(kmem_cache_t *cachep) {
    if (cachep == nullptr) {
      return 0;
    }
    LockGuard guard(cachep->cache_lock);

    int blocksFreed = 0;
    cachep->error_code = 0;
    // 只有当存在空闲slab且cache不在增长时才收缩
    if (cachep->slabs_free != nullptr && cachep->growing == false) {
      // 每个slab包含的内存块数
      int n = 1 << cachep->order;
      slab_t *s;
      while (cachep->slabs_free != nullptr) {
        s = cachep->slabs_free;
        cachep->slabs_free = s->next;
        // 释放slab到buddy分配器
        page_allocator_.Free(s, cachep->order);
        blocksFreed += n;
        cachep->num_allocations -= cachep->objectsInSlab;
      }
    }
    // 重置增长标志
    cachep->growing = false;
    return blocksFreed;
  }

  /**
   * 从cache分配一个对象
   *
   * @param cachep cache指针
   * @return 成功返回对象指针，失败返回nullptr
   *
   * 功能：
   * 1. 查找可用的slab（优先从partial，然后free）
   * 2. 如果没有可用slab，分配新的slab
   * 3. 从slab中分配一个对象
   * 4. 更新slab链表状态（free->partial->full）
   * 5. 调用对象构造函数（如果存在）
   */
  void *kmem_cache_alloc(kmem_cache_t *cachep) {
    if (cachep == nullptr || *cachep->name == '\0') {
      return nullptr;
    }

    LockGuard guard(cachep->cache_lock);

    void *retObject = nullptr;
    cachep->error_code = 0;

    // 查找可用的slab：优先使用部分使用的slab，然后是空闲slab
    slab_t *s = cachep->slabs_partial;
    if (s == nullptr) {
      s = cachep->slabs_free;
    }
    // 需要分配新slab
    if (s == nullptr) {
      void *ptr = page_allocator_.Alloc(cachep->order);
      if (ptr == nullptr) {
        cachep->error_code = 2;
        return nullptr;
      }
      s = static_cast<slab_t *>(ptr);

      // 新分配的slab将被放入partial链表（因为即将从中分配对象）
      cachep->slabs_partial = s;

      // 设置缓存行对齐偏移
      s->colouroff = cachep->colour_next;
      cachep->colour_next =
          (cachep->colour_next + 1) % (cachep->colour_max + 1);

      // 初始化slab结构
      s->freeList =
          reinterpret_cast<int *>(static_cast<char *>(ptr) + sizeof(slab_t));
      s->nextFreeObj = 0;
      s->inuse = 0;
      s->next = nullptr;
      s->prev = nullptr;
      s->myCache = cachep;

      // 设置对象数组位置（考虑缓存行对齐）
      s->objects =
          static_cast<void *>(static_cast<char *>(ptr) + sizeof(slab_t) +
                              sizeof(uint32_t) * cachep->objectsInSlab +
                              CACHE_L1_LINE_SIZE * s->colouroff);
      void *obj = s->objects;

      // 初始化所有对象（调用构造函数）并设置空闲链表
      for (size_t i = 0; i < cachep->objectsInSlab; i++) {
        // 调用对象构造函数
        if (cachep->ctor) {
          cachep->ctor(obj);
        }
        obj =
            static_cast<void *>(static_cast<char *>(obj) + cachep->objectSize);
        s->freeList[i] = i + 1;
      }
      // s->freeList[cachep->objectsInSlab - 1] = -1;

      cachep->num_allocations += cachep->objectsInSlab;
      cachep->growing = true;
    }

    // 从slab中分配对象
    retObject = static_cast<void *>(static_cast<char *>(s->objects) +
                                    s->nextFreeObj * cachep->objectSize);
    s->nextFreeObj = s->freeList[s->nextFreeObj];
    s->inuse++;
    cachep->num_active++;

    // 更新slab链表状态
    if (s == cachep->slabs_free) {
      // 从free链表中移除
      cachep->slabs_free = s->next;
      if (cachep->slabs_free != nullptr) {
        cachep->slabs_free->prev = nullptr;
      }
      // 移动到partial链表
      if (s->inuse != cachep->objectsInSlab) {
        s->next = cachep->slabs_partial;
        if (cachep->slabs_partial != nullptr) {
          cachep->slabs_partial->prev = s;
        }
        cachep->slabs_partial = s;
      } else {
        // 移动到full链表
        s->next = cachep->slabs_full;
        if (cachep->slabs_full != nullptr) {
          cachep->slabs_full->prev = s;
        }
        cachep->slabs_full = s;
      }
    } else {
      // from partial to full
      if (s->inuse == cachep->objectsInSlab) {
        cachep->slabs_partial = s->next;
        if (cachep->slabs_partial != nullptr) {
          cachep->slabs_partial->prev = nullptr;
        }

        s->next = cachep->slabs_full;
        if (cachep->slabs_full != nullptr) {
          cachep->slabs_full->prev = s;
        }
        cachep->slabs_full = s;
      }
    }

    return retObject;
  }

  /**
   * 释放cache中的一个对象
   *
   * @param cachep cache指针
   * @param objp 要释放的对象指针
   *
   * 功能：
   * 1. 查找对象所属的slab
   * 2. 验证对象地址的有效性
   * 3. 将对象返回到slab的空闲链表
   * 4. 调用对象析构函数（如果存在）
   * 5. 更新slab链表状态（full->partial->free）
   */
  void kmem_cache_free(kmem_cache_t *cachep, void *objp) {
    if (cachep == nullptr || *cachep->name == '\0' || objp == nullptr) {
      return;
    }

    LockGuard guard(cachep->cache_lock);

    cachep->error_code = 0;
    slab_t *s;

    // 查找对象所属的slab
    int slabSize = kPageSize * (1 << cachep->order);
    // 标记slab是否在full链表中
    bool inFullList = true;

    // 首先在full链表中查找
    s = cachep->slabs_full;
    while (s != nullptr) {
      if (objp > s &&
          objp < static_cast<void *>(
                     static_cast<char *>(static_cast<void *>(s)) + slabSize)) {
        break;
      }
      s = s->next;
    }

    // 如果在full链表中没找到，在partial链表中查找
    if (s == nullptr) {
      inFullList = false;
      s = cachep->slabs_partial;
      while (s != nullptr) {
        if (objp > s && objp < static_cast<void *>(
                                   static_cast<char *>(static_cast<void *>(s)) +
                                   slabSize)) {
          break;
        }
        s = s->next;
      }
    }

    // 没找到对应的slab
    if (s == nullptr) {
      cachep->error_code = 6;
      return;
    }

    // 找到slab，将对象返回到slab中
    s->inuse--;
    cachep->num_active--;

    // 计算对象在数组中的索引
    int i = (static_cast<char *>(objp) - static_cast<char *>(s->objects)) /
            cachep->objectSize;

    // 验证对象地址是否对齐
    if (objp != static_cast<void *>(static_cast<char *>(s->objects) +
                                    i * cachep->objectSize)) {
      cachep->error_code = 7;
      return;
    }

    // 将对象加入空闲链表
    s->freeList[i] = s->nextFreeObj;
    s->nextFreeObj = i;

    // 调用析构函数
    if (cachep->dtor != nullptr) {
      cachep->dtor(objp);
    }

    // 检查slab现在是否为空闲或部分使用状态，并更新链表
    // slab原本在full链表中
    if (inFullList) {
      slab_t *prev, *next;

      // 从full链表中删除slab
      prev = s->prev;
      next = s->next;
      s->prev = nullptr;

      if (prev != nullptr) {
        prev->next = next;
      }
      if (next != nullptr) {
        next->prev = prev;
      }
      if (cachep->slabs_full == s) {
        cachep->slabs_full = next;
      }

      // 插入到partial链表
      if (s->inuse != 0) {
        s->next = cachep->slabs_partial;
        if (cachep->slabs_partial != nullptr) {
          cachep->slabs_partial->prev = s;
        }
        cachep->slabs_partial = s;
      } else {
        // 插入到free链表
        s->next = cachep->slabs_free;
        if (cachep->slabs_free != nullptr) {
          cachep->slabs_free->prev = s;
        }
        cachep->slabs_free = s;
      }
    } else {
      // slab原本在partial链表中
      // 现在变成完全空闲
      if (s->inuse == 0) {
        slab_t *prev, *next;

        // 从partial链表中删除slab
        prev = s->prev;
        next = s->next;
        s->prev = nullptr;

        if (prev != nullptr) {
          prev->next = next;
        }
        if (next != nullptr) {
          next->prev = prev;
        }
        if (cachep->slabs_partial == s) {
          cachep->slabs_partial = next;
        }

        // 插入到free链表
        s->next = cachep->slabs_free;
        if (cachep->slabs_free != nullptr) {
          cachep->slabs_free->prev = s;
        }
        cachep->slabs_free = s;
      }
    }
  }

  /**
   * 查找包含指定对象的小内存缓冲区cache
   *
   * @param objp 对象指针
   * @return 成功返回cache指针，失败返回nullptr
   *
   * 功能：
   * 1. 遍历所有cache，查找名称以"size-"开头的cache
   * 2. 在每个小内存cache的slab中查找指定对象
   * 3. 检查对象地址是否在slab的地址范围内
   */
  kmem_cache_t *find_buffers_cache(const void *objp) {
    LockGuard guard(cache_cache.cache_lock);

    kmem_cache_t *curr = allCaches;
    slab_t *s;

    while (curr != nullptr) {
      // 找到小内存缓冲区cache
      if (strstr(curr->name, "size-") != nullptr) {
        // 在full slab中查找
        s = curr->slabs_full;
        int slabSize = kPageSize * (1 << curr->order);
        while (s != nullptr) {
          // 找到包含对象的cache
          if (objp > s &&
              objp <
                  static_cast<const void *>(
                      static_cast<char *>(static_cast<void *>(s)) + slabSize)) {
            return curr;
          }

          s = s->next;
        }

        // 在partial slab中查找
        s = curr->slabs_partial;
        while (s != nullptr) {
          // 找到包含对象的cache
          if (objp > s &&
              objp <
                  static_cast<const void *>(
                      static_cast<char *>(static_cast<void *>(s)) + slabSize)) {
            return curr;
          }

          s = s->next;
        }
      }
      curr = curr->next;
    }

    return nullptr;
  }

  /**
   * 销毁cache - 释放cache及其所有slab
   *
   * @param cachep 要销毁的cache指针
   *
   * 功能：
   * 1. 从全局cache链表中移除cache
   * 2. 在cache_cache中查找并释放该cache对象
   * 3. 释放cache中的所有slab（full、partial、free）
   * 4. 更新cache_cache的链表状态
   * 5. 清理cache_cache中多余的空闲slab
   */
  void kmem_cache_destroy(kmem_cache_t *cachep) {
    if (cachep == nullptr || *cachep->name == '\0') {
      return;
    }

    // 获取三个互斥锁：cache锁、cache_cache锁、buddy锁
    LockGuard guard1(cachep->cache_lock);
    LockGuard guard2(cache_cache.cache_lock);

    slab_t *s;
    void *ptr;
    cache_cache.error_code = 0;

    // 从allCaches链表删除cache
    kmem_cache_t *prev = nullptr, *curr = allCaches;
    while (curr != cachep) {
      prev = curr;
      curr = curr->next;
    }
    // cache不在cache链中（意味着对象也不在cache_cache中）
    if (curr == nullptr) {
      cache_cache.error_code = 5;
      return;
    }

    if (prev == nullptr) {
      allCaches = allCaches->next;
    } else {
      prev->next = curr->next;
    }
    curr->next = nullptr;

    // 在cache_cache中查找拥有该cache对象的slab
    int slabSize = kPageSize * (1 << cache_cache.order);
    // 标记slab是否在full链表中
    bool inFullList = true;
    s = cache_cache.slabs_full;
    while (s != nullptr) {
      if (static_cast<const void *>(cachep) > static_cast<void *>(s) &&
          static_cast<const void *>(cachep) <
              static_cast<void *>(static_cast<char *>(static_cast<void *>(s)) +
                                  slabSize)) {
        break;
      }
      s = s->next;
    }

    if (s == nullptr) {
      // slab在partial链表中
      inFullList = false;
      s = cache_cache.slabs_partial;
      while (s != nullptr) {
        if (static_cast<const void *>(cachep) > static_cast<void *>(s) &&
            static_cast<const void *>(cachep) <
                static_cast<void *>(
                    static_cast<char *>(static_cast<void *>(s)) + slabSize)) {
          break;
        }
        s = s->next;
      }
    }
    // 在cache_cache中没找到拥有该cache的slab
    if (s == nullptr) {
      cache_cache.error_code = 5;
      return;
    }

    // 找到拥有该cache的slab

    // 重置cache字段并更新cache_cache字段
    s->inuse--;
    cache_cache.num_active--;
    int i = cachep - static_cast<kmem_cache_t *>(s->objects);
    s->freeList[i] = s->nextFreeObj;
    s->nextFreeObj = i;
    // 清空cache名称
    *cachep->name = '\0';
    cachep->objectSize = 0;

    // 释放cache中使用的所有slab

    // 释放full slab链表
    slab_t *freeTemp = cachep->slabs_full;
    while (freeTemp != nullptr) {
      ptr = freeTemp;
      freeTemp = freeTemp->next;
      page_allocator_.Free(ptr, cachep->order);
    }

    // 释放partial slab链表
    freeTemp = cachep->slabs_partial;
    while (freeTemp != nullptr) {
      ptr = freeTemp;
      freeTemp = freeTemp->next;
      page_allocator_.Free(ptr, cachep->order);
    }

    // 释放free slab链表
    freeTemp = cachep->slabs_free;
    while (freeTemp != nullptr) {
      ptr = freeTemp;
      freeTemp = freeTemp->next;
      page_allocator_.Free(ptr, cachep->order);
    }

    // 检查cache_cache中的slab现在是否为空闲或部分使用状态
    // slab原本在full链表中
    if (inFullList) {
      slab_t *prev, *next;

      // 从full链表中删除slab
      prev = s->prev;
      next = s->next;
      s->prev = nullptr;

      if (prev != nullptr) {
        prev->next = next;
      }
      if (next != nullptr) {
        next->prev = prev;
      }
      if (cache_cache.slabs_full == s) {
        cache_cache.slabs_full = next;
      }
      // 插入到partial链表
      if (s->inuse != 0) {
        s->next = cache_cache.slabs_partial;
        if (cache_cache.slabs_partial != nullptr) {
          cache_cache.slabs_partial->prev = s;
        }
        cache_cache.slabs_partial = s;
      } else {
        // 插入到free链表
        s->next = cache_cache.slabs_free;
        if (cache_cache.slabs_free != nullptr) {
          cache_cache.slabs_free->prev = s;
        }
        cache_cache.slabs_free = s;
      }
    } else {
      // slab原本在partial链表中
      if (s->inuse == 0) {
        slab_t *prev, *next;

        // 从partial链表中删除slab
        prev = s->prev;
        next = s->next;
        s->prev = nullptr;

        if (prev != nullptr) {
          prev->next = next;
        }
        if (next != nullptr) {
          next->prev = prev;
        }
        if (cache_cache.slabs_partial == s) {
          cache_cache.slabs_partial = next;
        }

        // 插入到free链表
        s->next = cache_cache.slabs_free;
        if (cache_cache.slabs_free != nullptr) {
          cache_cache.slabs_free->prev = s;
        }
        cache_cache.slabs_free = s;
      }
    }

    // 如果free链表中有多个slab，释放多余的slab以节省内存
    if (cache_cache.slabs_free != nullptr) {
      s = cache_cache.slabs_free;
      i = 0;
      while (s != nullptr) {
        i++;
        s = s->next;
      }

      // 保留一个空闲slab，释放其余的
      while (i > 1) {
        i--;
        s = cache_cache.slabs_free;
        cache_cache.slabs_free = cache_cache.slabs_free->next;
        s->next = nullptr;
        cache_cache.slabs_free->prev = nullptr;
        page_allocator_.Free(s, cache_cache.order);
        cache_cache.num_allocations -= cache_cache.objectsInSlab;
      }
    }
  }

  /**
   * 打印cache的详细信息
   *
   * @param cachep cache指针
   *
   * 功能：
   * 1. 统计cache中所有slab的数量
   * 2. 计算cache的总大小和使用率
   * 3. 打印cache的各项统计信息
   */
  void kmem_cache_info(kmem_cache_t *cachep) {
    if (cachep == nullptr) {
      Log("NullPointer passed as argument\n");
      return;
    }

    LockGuard guard2(cachep->cache_lock);

    int i = 0;

    // 统计free slab数量
    slab_t *s = cachep->slabs_free;
    while (s != nullptr) {
      i++;
      s = s->next;
    }

    // 统计partial slab数量
    s = cachep->slabs_partial;
    while (s != nullptr) {
      i++;
      s = s->next;
    }

    // 统计full slab数量
    s = cachep->slabs_full;
    while (s != nullptr) {
      i++;
      s = s->next;
    }

    // 计算cache总大小（以内存块为单位）
    uint32_t cacheSize = i * (1 << cachep->order);

    // 计算使用率百分比
    double perc = 0;
    if (cachep->num_allocations > 0) {
      perc = 100 * (double)cachep->num_active / cachep->num_allocations;
    }

    // 打印cache信息
    Log("*** CACHE INFO: ***\n");
    Log("Name:\t\t\t\t%s\n", cachep->name);
    Log("Size of one object (in bytes):\t%zu\n", cachep->objectSize);
    Log("Size of cache (in blocks):\t%d\n", cacheSize);
    Log("Number of slabs:\t\t%d\n", i);
    Log("Number of objects in one slab:\t%d\n", cachep->objectsInSlab);
    Log("Percentage occupancy of cache:\t%.2f %%\n", perc);
  }

  /**
   * 打印cache的错误信息
   *
   * @param cachep cache指针
   * @return 错误码
   *
   * 功能：
   * 1. 获取cache的错误码
   * 2. 根据错误码打印相应的错误信息
   * 3. 返回错误码供调用者使用
   */
  int kmem_cache_error(kmem_cache_t *cachep) {
    if (cachep == nullptr) {
      Log("Nullpointer argument passed\n");
      return 4;
    }

    LockGuard guard2(cachep->cache_lock);

    int error_code = cachep->error_code;

    if (error_code == 0) {
      Log("NO ERROR\n");
      return 0;
    }

    Log("ERROR: ");
    switch (error_code) {
      case 1:
        Log("Invalid arguments passed in function kmem_cache_create\n");
        break;
      case 2:
        Log("No enough space for allocating new slab\n");
        break;
      case 3:
        Log("Access to cache_cache isn't allowed\n");
        break;
      case 4:
        Log("NullPointer argument passed to func kmem_cache_error\n");
        break;
      case 5:
        Log("Cache passed by func kmem_cache_destroy does not exists in "
            "kmem_cache\n");
        break;
      case 6:
        Log("Object passed by func kmem_cache_free does not exists in "
            "kmem_cache\n");
        break;
      case 7:
        Log("Invalid pointer passed for object dealocation\n");
        break;
      default:
        Log("Undefined error\n");
        break;
    }

    return error_code;
  }

  /**
   * 打印所有cache的信息
   * 遍历allCaches链表，调用kmem_cache_info打印每个cache的详细信息
   */
  void kmem_cache_allInfo() {
    kmem_cache_t *curr = allCaches;
    while (curr != nullptr) {
      kmem_cache_info(curr);
      Log("\n");
      curr = curr->next;
    }
  }

 protected:
  using AllocatorBase<LogFunc, Lock>::Log;
  using AllocatorBase<LogFunc, Lock>::name_;
  using AllocatorBase<LogFunc, Lock>::start_addr_;
  using AllocatorBase<LogFunc, Lock>::length_;
  using AllocatorBase<LogFunc, Lock>::free_count_;
  using AllocatorBase<LogFunc, Lock>::used_count_;

  PageAllocator page_allocator_;

  // cache_cache: 管理kmem_cache_t结构体的特殊cache
  kmem_cache_t cache_cache;

  // 所有cache的链表头
  kmem_cache_t *allCaches = nullptr;

  // 复制字符串
  static char *strcpy(char *dest, const char *src) {
    char *address = dest;
    while ((*dest++ = *src++) != '\0') {
      ;
    }
    return address;
  }

  // 连接字符串
  static char *strcat(char *dest, const char *src) {
    char *add_d = dest;
    if (dest != 0 && src != 0) {
      while (*add_d) {
        add_d++;
      }
      while (*src) {
        *add_d++ = *src++;
      }
    }
    return dest;
  }

  // 比较字符串
  static int strcmp(const char *s1, const char *s2) {
    while (*s2 && *s1 && (*s2 == *s1)) {
      s2++;
      s1++;
    }
    return *s2 - *s1;
  }

  // 计算字符串长度
  static size_t strlen(const char *s) {
    if (s == nullptr) return 0;
    size_t len = 0;
    while (s[len] != '\0') {
      len++;
    }
    return len;
  }

  // 计算字符串长度（带最大长度限制）
  static size_t strnlen(const char *s, size_t maxlen) {
    if (s == nullptr) return 0;
    size_t len = 0;
    while (len < maxlen && s[len] != '\0') {
      len++;
    }
    return len;
  }

  // 比较内存块
  static int memcmp(const void *s1, const void *s2, size_t n) {
    if (s1 == nullptr || s2 == nullptr) return 0;
    const auto *p1 = static_cast<const unsigned char *>(s1);
    const auto *p2 = static_cast<const unsigned char *>(s2);
    for (size_t i = 0; i < n; i++) {
      if (p1[i] != p2[i]) {
        return p1[i] - p2[i];
      }
    }
    return 0;
  }

  // 在字符串中查找子字符串
  static char *strstr(const char *s1, const char *s2) {
    // 空指针检查
    if (s1 == nullptr || s2 == nullptr) {
      return nullptr;
    }

    // 如果s2是空字符串，返回s1
    if (*s2 == '\0') {
      return const_cast<char *>(s1);
    }

    size_t l1 = strlen(s1);
    size_t l2 = strlen(s2);

    // 如果s1比s2短，不可能找到
    if (l1 < l2) {
      return nullptr;
    }

    // 搜索子字符串
    for (size_t i = 0; i <= l1 - l2; i++) {
      if (memcmp(s1 + i, s2, l2) == 0) {
        return const_cast<char *>(s1 + i);
      }
    }

    return nullptr;
  }

  // 将整数转换为字符串
  static char *itoa(size_t value, char *str) {
    if (value == 0) {
      str[0] = '0';
      str[1] = '\0';
      return str;
    }

    int i = 0;
    size_t temp = value;

    // 先计算数字位数
    while (temp > 0) {
      temp /= 10;
      i++;
    }
    // 字符串结束符
    str[i] = '\0';

    // 从后往前填充数字
    while (value > 0) {
      str[--i] = '0' + (value % 10);
      value /= 10;
    }

    return str;
  }

  /**
   * @brief 分配指定页数的内存
   * @param bytes 要分配的页数
   * @return void* 分配的内存起始地址，失败时返回0
   */
  /**
   * 分配小内存缓冲区 - 通用分配接口
   *
   * @param size 请求的内存大小（字节）
   * @return 成功返回内存指针，失败返回nullptr
   *
   * 功能：
   * 1. 将请求大小向上舍入到2的幂次方
   * 2. 创建或查找对应大小的cache（命名为"size-XXX"）
   * 3. 从cache中分配对象
   *
   * 支持的大小范围：32字节到131072字节
   */
  [[nodiscard]] auto AllocImpl(size_t bytes) -> void * override {
    if (bytes < 32 || bytes > 131072) {
      return nullptr;
    }

    // 将size向上舍入到最近的2的幂次方
    // int j = 1 << (int)(ceil(log2(bytes)));
    size_t j = 32;
    while (j < bytes) {
      j <<= 1;
    }

    char num[7];
    void *buff = nullptr;

    // 生成cache名称，格式为"size-XXX"
    char name[20];
    strcpy(name, "size-");
    itoa(j, num);
    strcat(name, num);

    // 创建或获取对应大小的cache
    kmem_cache_t *buffCachep = kmem_cache_create(name, j, nullptr, nullptr);

    // 从cache中分配对象
    buff = kmem_cache_alloc(buffCachep);

    return buff;
  }

  /**
   * @brief 释放指定地址的内存
   * @param addr 要释放的内存起始地址
   * @param page_count 要释放的页数
   */

  /**
   * 释放小内存缓冲区 - 通用释放接口
   *
   * @param objp 要释放的对象指针
   *
   * 功能：
   * 1. 查找包含该对象的小内存cache
   * 2. 释放对象到对应的cache
   * 3. 尝试收缩cache以节省内存
   */
  void FreeImpl(void *addr, size_t) override {
    if (addr == nullptr) {
      return;
    }

    // 查找包含该对象的cache
    kmem_cache_t *buffCachep = find_buffers_cache(addr);

    if (buffCachep == nullptr) {
      return;
    }

    // 释放对象
    kmem_cache_free(buffCachep, addr);

    // 如果cache有空闲slab，尝试收缩以节省内存
    if (buffCachep->slabs_free != nullptr) {
      kmem_cache_shrink(buffCachep);
    }
  }
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_SLAB_HPP_ */
