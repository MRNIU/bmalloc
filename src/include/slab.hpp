/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_SLAB_HPP_
#define BMALLOC_SRC_INCLUDE_SLAB_HPP_

#include "allocator_base.hpp"

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
  // cache_cache 的 order 值，表示管理 kmem_cache_t 结构体的 cache
  // 使用的内存块大小
  static constexpr size_t CACHE_CACHE_ORDER = 0;

  /**
   * Slab 结构体 - 表示一个内存 slab
   *
   * 每个 slab 包含多个相同大小的对象，通过链表管理空闲对象
   */
  struct slab_t {
    // offset for this slab - 用于缓存行对齐的偏移量
    uint32_t colouroff_ = 0;
    // starting adress of objects - 对象数组的起始地址
    void *objects = nullptr;
    // list of free objects - 空闲对象索引列表
    int *freeList_ = nullptr;
    // next free object - 下一个空闲对象的索引
    int nextFreeObj_ = 0;
    // number of active objects in this slab - 当前使用的对象数量
    uint32_t inuse_ = 0;
    // next slab in chain - 链表中的下一个slab
    slab_t *next_ = nullptr;
    // previous slab in chain - 链表中的前一个slab
    slab_t *prev_ = nullptr;
    // cache - owner - 拥有此slab的cache
    kmem_cache_t *myCache_ = nullptr;

    /**
     * @brief slab_t 构造函数
     * @param cache 拥有此slab的cache指针
     * @param start_addr slab的内存起始地址
     * @param object_count 此slab中的对象数量
     * @param colour_offset 缓存行对齐偏移量
     */
    slab_t(kmem_cache_t *cache, void *start_addr, size_t object_count,
           uint32_t colour_offset)
        : colouroff_(colour_offset),
          nextFreeObj_(0),
          inuse_(0),
          next_(nullptr),
          prev_(nullptr),
          myCache_(cache) {
      // 设置freeList位置（紧跟在slab_t结构后面）
      freeList_ = reinterpret_cast<int *>(static_cast<char *>(start_addr) +
                                          sizeof(slab_t));

      // 设置对象数组位置（考虑缓存行对齐）
      objects = static_cast<void *>(
          static_cast<char *>(start_addr) + sizeof(slab_t) +
          sizeof(uint32_t) * object_count + CACHE_L1_LINE_SIZE * colouroff_);

      // 初始化空闲对象链表
      for (size_t i = 0; i < object_count; i++) {
        freeList_[i] = i + 1;
      }
      // 最后一个对象的索引设为-1，表示链表结束
      if (object_count > 0) {
        freeList_[object_count - 1] = -1;
      }

      // 初始化所有对象（调用构造函数）
      if (cache && cache->ctor_) {
        void *obj = objects;
        for (size_t i = 0; i < object_count; i++) {
          cache->ctor_(obj);
          obj = static_cast<void *>(static_cast<char *>(obj) +
                                    cache->objectSize_);
        }
      }
    }

    /**
     * @brief 默认构造函数（保持向后兼容）
     */
    slab_t() = default;
  };

  /**
   * Cache 结构体 - 管理特定大小对象的高级结构
   *
   * 每个 cache 管理一种特定大小的对象，内部包含三种状态的 slab 链表：
   * - slabs_full_: 完全使用的 slab
   * - slabs_partial_: 部分使用的 slab
   * - slabs_free_: 完全空闲的 slab
   */
  struct kmem_cache_t {
    // list of full slabs - 满 slab 链表
    slab_t *slabs_full_ = nullptr;
    // list of partial slabs - 部分使用 slab 链表
    slab_t *slabs_partial_ = nullptr;
    // list of free slabs - 空闲 slab 链表
    slab_t *slabs_free_ = nullptr;
    // cache name_ - 缓存名称
    char name_[CACHE_NAMELEN]{};
    // size of one object - 单个对象大小
    size_t objectSize_ = 0;
    // num of objects in one slab - 每个 slab 中的对象数量
    size_t objectsInSlab_ = 0;
    // num of active objects in cache - 活跃对象数量
    size_t num_active_ = 0;
    // num of total objects in cache - 总对象数量
    size_t num_allocations_ = 0;
    // mutex (uses to lock the cache) - 缓存互斥锁
    Lock cache_lock_;
    // order of one slab (one slab has 2^order blocks) - slab 的 order 值
    uint32_t order_ = CACHE_CACHE_ORDER;
    // maximum multiplier for offset of first object in slab - 最大颜色偏移乘数
    uint32_t colour_max_ = 0;
    // multiplier for next slab offset - 下一个 slab 的颜色偏移
    uint32_t colour_next_ = 0;
    // false - cache is not growing_ / true - cache is growing_ - 是否正在增长
    bool growing_ = false;
    // objects constructor - 对象构造函数
    void (*ctor_)(void *) = nullptr;
    // objects destructor - 对象析构函数
    void (*dtor_)(void *) = nullptr;
    // last error that happened while working with cache - 最后的错误码
    int error_code_ = 0;
    // next cache in chain - 下一个 cache
    kmem_cache_t *next_ = nullptr;

    kmem_cache_t() = default;
    explicit kmem_cache_t(const char *name, size_t size, void (*ctor)(void *),
                          void (*dtor)(void *))
        : ctor_(ctor), dtor_(dtor), next_(nullptr) {
      strcpy(name_, name);

      // 计算新 cache 的 order 值（使一个 slab 能容纳至少一个对象）
      long memory = kPageSize;
      int order = 0;
      while ((long)(memory - sizeof(slab_t) - sizeof(uint32_t) - size) < 0) {
        order++;
        memory *= 2;
      }

      objectSize_ = size;
      order_ = order;

      // 计算每个 slab 中的对象数量
      memory -= sizeof(slab_t);
      int n = 0;
      while ((long)(memory - sizeof(uint32_t) - size) >= 0) {
        n++;
        memory -= sizeof(uint32_t) + size;
      }
      objectsInSlab_ = n;

      // 设置缓存行对齐参数
      colour_max_ = memory / CACHE_L1_LINE_SIZE;
    }

    void add_slab(slab_t *slab) {
      // 更新 slab 链表状态
      if (slab == slabs_free_) {
        slabs_free_ = slab->next_;
        if (slabs_free_ != nullptr) {
          slabs_free_->prev_ = nullptr;
        }
        // from free to partial
        if (slab->inuse_ != objectsInSlab_) {
          slab->next_ = slabs_partial_;
          if (slabs_partial_ != nullptr) {
            slabs_partial_->prev_ = slab;
          }
          slabs_partial_ = slab;
        } else {
          // from free to full
          slab->next_ = slabs_full_;
          if (slabs_full_ != nullptr) {
            slabs_full_->prev_ = slab;
          }
          slabs_full_ = slab;
        }
      } else {
        // from partial to full
        if (slab->inuse_ == objectsInSlab_) {
          slabs_partial_ = slab->next_;
          if (slabs_partial_ != nullptr) {
            slabs_partial_->prev_ = nullptr;
          }

          slab->next_ = slabs_full_;
          if (slabs_full_ != nullptr) {
            slabs_full_->prev_ = slab;
          }
          slabs_full_ = slab;
        }
      }
    }
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

    auto *slab = new (ptr) slab_t;

    // 初始化 cache_cache 的 slab 链表
    cache_cache.slabs_free_ = slab;

    // 设置 cache_cache 的基本属性
    strcpy(cache_cache.name_, "kmem_cache");
    cache_cache.objectSize_ = sizeof(kmem_cache_t);

    // 初始化 slab 结构
    slab->freeList_ =
        reinterpret_cast<int *>(static_cast<char *>(ptr) + sizeof(slab_t));
    slab->myCache_ = &cache_cache;

    // 计算每个 slab 能容纳的对象数量
    long memory = (1 << cache_cache.order_) * kPageSize;
    memory -= sizeof(slab_t);
    int n = 0;
    while ((long)(memory - sizeof(uint32_t) - cache_cache.objectSize_) >= 0) {
      n++;
      memory -= sizeof(uint32_t) + cache_cache.objectSize_;
    }

    // 设置对象数组起始位置
    slab->objects = static_cast<void *>(static_cast<char *>(ptr) +
                                        sizeof(slab_t) + sizeof(uint32_t) * n);
    auto *list = static_cast<kmem_cache_t *>(slab->objects);

    // 初始化空闲对象链表
    for (int i = 0; i < n; i++) {
      new (&list[i]) kmem_cache_t;
      slab->freeList_[i] = i + 1;
    }

    // 设置 cache_cache 的对象统计信息
    cache_cache.objectsInSlab_ = n;
    cache_cache.num_allocations_ = n;

    // 设置缓存行对齐参数
    cache_cache.colour_max_ = memory / CACHE_L1_LINE_SIZE;
    if (cache_cache.colour_max_ > 0) {
      cache_cache.colour_next_ = 1;
    } else {
      cache_cache.colour_next_ = 0;
    }

    // 将 cache_cache 加入全局 cache 链表
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
   * 创建一个新的 cache
   *
   * @param name cache 的名称
   * @param size 每个对象的大小（字节）
   * @param ctor 对象构造函数（可选）
   * @param dtor 对象析构函数（可选）
   * @return 成功返回 cache 指针，失败返回 nullptr
   *
   * 功能：
   * 1. 参数验证
   * 2. 检查是否已存在相同的 cache
   * 3. 从 cache_cache 中分配 kmem_cache_t 结构
   * 4. 计算最优的 slab 大小（order 值）
   * 5. 计算每个 slab 能容纳的对象数量
   * 6. 设置缓存行对齐参数
   */
  kmem_cache_t *kmem_cache_create(const char *name, size_t size,
                                  void (*ctor)(void *), void (*dtor)(void *)) {
    // 参数验证
    if (name == nullptr || *name == '\0' || (long)size <= 0) {
      cache_cache.error_code_ = 1;
      return nullptr;
    }

    // 禁止创建与 cache_cache 同名的 cache
    if (strcmp(name, cache_cache.name_) == 0) {
      cache_cache.error_code_ = 3;
      return nullptr;
    }

    LockGuard guard(cache_cache.cache_lock_);

    // reset error code
    cache_cache.error_code_ = 0;

    // 在全局 cache 链表中查找是否已存在相同的 cache
    auto ret = allCaches;
    while (ret != nullptr) {
      if (strcmp(ret->name_, name) == 0 && ret->objectSize_ == size) {
        return ret;
      }
      ret = ret->next_;
    }

    auto slab = find_alloc_slab(cache_cache);

    // 从slab中分配一个 kmem_cache_t 对象
    auto *list = static_cast<kmem_cache_t *>(slab->objects);
    // 初始化新 cache
    ret = new (&list[slab->nextFreeObj_]) kmem_cache_t(name, size, ctor, dtor);
    ret->next_ = allCaches;
    allCaches = ret;

    slab->nextFreeObj_ = slab->freeList_[slab->nextFreeObj_];
    slab->inuse_++;
    cache_cache.num_active_++;
    cache_cache.add_slab(slab);

    return ret;
  }

  /**
   * 收缩 cache - 释放空闲的 slab 以节省内存
   *
   * @param cachep 要收缩的 cache 指针
   * @return 释放的内存块数量
   *
   * 功能：
   * 1. 释放 cache 中所有完全空闲的 slab
   * 2. 只在 cache 不处于增长状态时执行
   * 3. 返回释放的内存块总数
   */
  int kmem_cache_shrink(kmem_cache_t *cachep) {
    if (cachep == nullptr) {
      return 0;
    }
    LockGuard guard(cachep->cache_lock_);

    int blocksFreed = 0;
    cachep->error_code_ = 0;
    // 只有当存在空闲 slab 且 cache 不在增长时才收缩
    if (cachep->slabs_free_ != nullptr && cachep->growing_ == false) {
      // 每个 slab 包含的内存块数
      int n = 1 << cachep->order_;
      slab_t *slab;
      while (cachep->slabs_free_ != nullptr) {
        slab = cachep->slabs_free_;
        cachep->slabs_free_ = slab->next_;
        // 释放 slab 到 buddy 分配器
        page_allocator_.Free(slab, cachep->order_);
        blocksFreed += n;
        cachep->num_allocations_ -= cachep->objectsInSlab_;
      }
    }
    // 重置增长标志
    cachep->growing_ = false;
    return blocksFreed;
  }

  /**
   * 从 cache 分配一个对象
   *
   * @param cachep cache 指针
   * @return 成功返回对象指针，失败返回 nullptr
   *
   * 功能：
   * 1. 查找可用的 slab（优先从partial，然后free）
   * 2. 如果没有可用slab，分配新的slab
   * 3. 从slab中分配一个对象
   * 4. 更新slab链表状态（free->partial->full）
   * 5. 调用对象构造函数（如果存在）
   */
  void *kmem_cache_alloc(kmem_cache_t *cachep) {
    if (cachep == nullptr || *cachep->name_ == '\0') {
      return nullptr;
    }

    LockGuard guard(cachep->cache_lock_);

    cachep->error_code_ = 0;

    auto slab = find_alloc_slab(*cachep);

    // 从slab中分配对象
    auto retObject =
        static_cast<void *>(static_cast<char *>(slab->objects) +
                            slab->nextFreeObj_ * cachep->objectSize_);

    slab->nextFreeObj_ = slab->freeList_[slab->nextFreeObj_];
    slab->inuse_++;
    cachep->num_active_++;
    cachep->add_slab(slab);

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
    if (cachep == nullptr || *cachep->name_ == '\0' || objp == nullptr) {
      return;
    }

    LockGuard guard(cachep->cache_lock_);

    cachep->error_code_ = 0;
    slab_t *slab;

    // 查找对象所属的slab
    int slabSize = kPageSize * (1 << cachep->order_);
    // 标记slab是否在full链表中
    bool inFullList = true;

    // 首先在full链表中查找
    slab = cachep->slabs_full_;
    while (slab != nullptr) {
      if (objp > slab &&
          objp <
              static_cast<void *>(
                  static_cast<char *>(static_cast<void *>(slab)) + slabSize)) {
        break;
      }
      slab = slab->next_;
    }

    // 如果在full链表中没找到，在partial链表中查找
    if (slab == nullptr) {
      inFullList = false;
      slab = cachep->slabs_partial_;
      while (slab != nullptr) {
        if (objp > slab &&
            objp < static_cast<void *>(
                       static_cast<char *>(static_cast<void *>(slab)) +
                       slabSize)) {
          break;
        }
        slab = slab->next_;
      }
    }

    // 没找到对应的slab
    if (slab == nullptr) {
      cachep->error_code_ = 6;
      return;
    }

    // 找到slab，将对象返回到slab中
    slab->inuse_--;
    cachep->num_active_--;

    // 计算对象在数组中的索引
    int i = (static_cast<char *>(objp) - static_cast<char *>(slab->objects)) /
            cachep->objectSize_;

    // 验证对象地址是否对齐
    if (objp != static_cast<void *>(static_cast<char *>(slab->objects) +
                                    i * cachep->objectSize_)) {
      cachep->error_code_ = 7;
      return;
    }

    // 将对象加入空闲链表
    slab->freeList_[i] = slab->nextFreeObj_;
    slab->nextFreeObj_ = i;

    // 调用析构函数
    if (cachep->dtor_ != nullptr) {
      cachep->dtor_(objp);
    }

    // 检查slab现在是否为空闲或部分使用状态，并更新链表
    // slab原本在full链表中
    if (inFullList) {
      slab_t *prev, *next;

      // 从full链表中删除slab
      prev = slab->prev_;
      next = slab->next_;
      slab->prev_ = nullptr;

      if (prev != nullptr) {
        prev->next_ = next;
      }
      if (next != nullptr) {
        next->prev_ = prev;
      }
      if (cachep->slabs_full_ == slab) {
        cachep->slabs_full_ = next;
      }

      // 插入到partial链表
      if (slab->inuse_ != 0) {
        slab->next_ = cachep->slabs_partial_;
        if (cachep->slabs_partial_ != nullptr) {
          cachep->slabs_partial_->prev_ = slab;
        }
        cachep->slabs_partial_ = slab;
      } else {
        // 插入到free链表
        slab->next_ = cachep->slabs_free_;
        if (cachep->slabs_free_ != nullptr) {
          cachep->slabs_free_->prev_ = slab;
        }
        cachep->slabs_free_ = slab;
      }
    } else {
      // slab原本在partial链表中
      // 现在变成完全空闲
      if (slab->inuse_ == 0) {
        slab_t *prev, *next;

        // 从partial链表中删除slab
        prev = slab->prev_;
        next = slab->next_;
        slab->prev_ = nullptr;

        if (prev != nullptr) {
          prev->next_ = next;
        }
        if (next != nullptr) {
          next->prev_ = prev;
        }
        if (cachep->slabs_partial_ == slab) {
          cachep->slabs_partial_ = next;
        }

        // 插入到free链表
        slab->next_ = cachep->slabs_free_;
        if (cachep->slabs_free_ != nullptr) {
          cachep->slabs_free_->prev_ = slab;
        }
        cachep->slabs_free_ = slab;
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
    LockGuard guard(cache_cache.cache_lock_);

    kmem_cache_t *curr = allCaches;
    slab_t *slab;

    while (curr != nullptr) {
      // 找到小内存缓冲区cache
      if (strstr(curr->name_, "size-") != nullptr) {
        // 在full slab中查找
        slab = curr->slabs_full_;
        int slabSize = kPageSize * (1 << curr->order_);
        while (slab != nullptr) {
          // 找到包含对象的cache
          if (objp > slab &&
              objp < static_cast<const void *>(
                         static_cast<char *>(static_cast<void *>(slab)) +
                         slabSize)) {
            return curr;
          }

          slab = slab->next_;
        }

        // 在partial slab中查找
        slab = curr->slabs_partial_;
        while (slab != nullptr) {
          // 找到包含对象的cache
          if (objp > slab &&
              objp < static_cast<const void *>(
                         static_cast<char *>(static_cast<void *>(slab)) +
                         slabSize)) {
            return curr;
          }

          slab = slab->next_;
        }
      }
      curr = curr->next_;
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
    if (cachep == nullptr || *cachep->name_ == '\0') {
      return;
    }

    // 获取三个互斥锁：cache锁、cache_cache锁、buddy锁
    LockGuard guard1(cachep->cache_lock_);
    LockGuard guard2(cache_cache.cache_lock_);

    slab_t *slab;
    void *ptr;
    cache_cache.error_code_ = 0;

    // 从allCaches链表删除cache
    kmem_cache_t *prev = nullptr, *curr = allCaches;
    while (curr != cachep) {
      prev = curr;
      curr = curr->next_;
    }
    // cache不在cache链中（意味着对象也不在cache_cache中）
    if (curr == nullptr) {
      cache_cache.error_code_ = 5;
      return;
    }

    if (prev == nullptr) {
      allCaches = allCaches->next_;
    } else {
      prev->next_ = curr->next_;
    }
    curr->next_ = nullptr;

    // 在cache_cache中查找拥有该cache对象的slab
    int slabSize = kPageSize * (1 << cache_cache.order_);
    // 标记slab是否在full链表中
    bool inFullList = true;
    slab = cache_cache.slabs_full_;
    while (slab != nullptr) {
      if (static_cast<const void *>(cachep) > static_cast<void *>(slab) &&
          static_cast<const void *>(cachep) <
              static_cast<void *>(
                  static_cast<char *>(static_cast<void *>(slab)) + slabSize)) {
        break;
      }
      slab = slab->next_;
    }

    if (slab == nullptr) {
      // slab在partial链表中
      inFullList = false;
      slab = cache_cache.slabs_partial_;
      while (slab != nullptr) {
        if (static_cast<const void *>(cachep) > static_cast<void *>(slab) &&
            static_cast<const void *>(cachep) <
                static_cast<void *>(
                    static_cast<char *>(static_cast<void *>(slab)) +
                    slabSize)) {
          break;
        }
        slab = slab->next_;
      }
    }
    // 在cache_cache中没找到拥有该cache的slab
    if (slab == nullptr) {
      cache_cache.error_code_ = 5;
      return;
    }

    // 找到拥有该cache的slab

    // 重置cache字段并更新cache_cache字段
    slab->inuse_--;
    cache_cache.num_active_--;
    int i = cachep - static_cast<kmem_cache_t *>(slab->objects);
    slab->freeList_[i] = slab->nextFreeObj_;
    slab->nextFreeObj_ = i;
    // 清空cache名称
    *cachep->name_ = '\0';
    cachep->objectSize_ = 0;

    // 释放cache中使用的所有slab

    // 释放full slab链表
    slab_t *freeTemp = cachep->slabs_full_;
    while (freeTemp != nullptr) {
      ptr = freeTemp;
      freeTemp = freeTemp->next_;
      page_allocator_.Free(ptr, cachep->order_);
    }

    // 释放partial slab链表
    freeTemp = cachep->slabs_partial_;
    while (freeTemp != nullptr) {
      ptr = freeTemp;
      freeTemp = freeTemp->next_;
      page_allocator_.Free(ptr, cachep->order_);
    }

    // 释放free slab链表
    freeTemp = cachep->slabs_free_;
    while (freeTemp != nullptr) {
      ptr = freeTemp;
      freeTemp = freeTemp->next_;
      page_allocator_.Free(ptr, cachep->order_);
    }

    // 检查cache_cache中的slab现在是否为空闲或部分使用状态
    // slab原本在full链表中
    if (inFullList) {
      slab_t *prev, *next;

      // 从full链表中删除slab
      prev = slab->prev_;
      next = slab->next_;
      slab->prev_ = nullptr;

      if (prev != nullptr) {
        prev->next_ = next;
      }
      if (next != nullptr) {
        next->prev_ = prev;
      }
      if (cache_cache.slabs_full_ == slab) {
        cache_cache.slabs_full_ = next;
      }
      // 插入到partial链表
      if (slab->inuse_ != 0) {
        slab->next_ = cache_cache.slabs_partial_;
        if (cache_cache.slabs_partial_ != nullptr) {
          cache_cache.slabs_partial_->prev_ = slab;
        }
        cache_cache.slabs_partial_ = slab;
      } else {
        // 插入到free链表
        slab->next_ = cache_cache.slabs_free_;
        if (cache_cache.slabs_free_ != nullptr) {
          cache_cache.slabs_free_->prev_ = slab;
        }
        cache_cache.slabs_free_ = slab;
      }
    } else {
      // slab原本在partial链表中
      if (slab->inuse_ == 0) {
        slab_t *prev, *next;

        // 从partial链表中删除slab
        prev = slab->prev_;
        next = slab->next_;
        slab->prev_ = nullptr;

        if (prev != nullptr) {
          prev->next_ = next;
        }
        if (next != nullptr) {
          next->prev_ = prev;
        }
        if (cache_cache.slabs_partial_ == slab) {
          cache_cache.slabs_partial_ = next;
        }

        // 插入到free链表
        slab->next_ = cache_cache.slabs_free_;
        if (cache_cache.slabs_free_ != nullptr) {
          cache_cache.slabs_free_->prev_ = slab;
        }
        cache_cache.slabs_free_ = slab;
      }
    }

    // 如果free链表中有多个slab，释放多余的slab以节省内存
    if (cache_cache.slabs_free_ != nullptr) {
      slab = cache_cache.slabs_free_;
      i = 0;
      while (slab != nullptr) {
        i++;
        slab = slab->next_;
      }

      // 保留一个空闲slab，释放其余的
      while (i > 1) {
        i--;
        slab = cache_cache.slabs_free_;
        cache_cache.slabs_free_ = cache_cache.slabs_free_->next_;
        slab->next_ = nullptr;
        cache_cache.slabs_free_->prev_ = nullptr;
        page_allocator_.Free(slab, cache_cache.order_);
        cache_cache.num_allocations_ -= cache_cache.objectsInSlab_;
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

    LockGuard guard2(cachep->cache_lock_);

    int i = 0;

    // 统计free slab数量
    slab_t *slab = cachep->slabs_free_;
    while (slab != nullptr) {
      i++;
      slab = slab->next_;
    }

    // 统计partial slab数量
    slab = cachep->slabs_partial_;
    while (slab != nullptr) {
      i++;
      slab = slab->next_;
    }

    // 统计full slab数量
    slab = cachep->slabs_full_;
    while (slab != nullptr) {
      i++;
      slab = slab->next_;
    }

    // 计算cache总大小（以内存块为单位）
    uint32_t cacheSize = i * (1 << cachep->order_);

    // 计算使用率百分比
    double perc = 0;
    if (cachep->num_allocations_ > 0) {
      perc = 100 * (double)cachep->num_active_ / cachep->num_allocations_;
    }

    // 打印cache信息
    Log("*** CACHE INFO: ***\n");
    Log("Name:\t\t\t\t%slab\n", cachep->name_);
    Log("Size of one object (in bytes):\t%zu\n", cachep->objectSize_);
    Log("Size of cache (in blocks):\t%d\n", cacheSize);
    Log("Number of slabs:\t\t%d\n", i);
    Log("Number of objects in one slab:\t%d\n", cachep->objectsInSlab_);
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

    LockGuard guard2(cachep->cache_lock_);

    int error_code_ = cachep->error_code_;

    if (error_code_ == 0) {
      Log("NO ERROR\n");
      return 0;
    }

    Log("ERROR: ");
    switch (error_code_) {
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

    return error_code_;
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
      curr = curr->next_;
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
  static size_t strlen(const char *slab) {
    if (slab == nullptr) return 0;
    size_t len = 0;
    while (slab[len] != '\0') {
      len++;
    }
    return len;
  }

  // 计算字符串长度（带最大长度限制）
  static size_t strnlen(const char *slab, size_t maxlen) {
    if (slab == nullptr) return 0;
    size_t len = 0;
    while (len < maxlen && slab[len] != '\0') {
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
    if (buffCachep->slabs_free_ != nullptr) {
      kmem_cache_shrink(buffCachep);
    }
  }

  slab_t *find_alloc_slab(kmem_cache_t &kmem_cache) {
    // cache 不存在，需要创建新的
    // 寻找可用的 slab 来分配 kmem_cache_t 结构
    auto slab = kmem_cache.slabs_partial_;
    if (slab == nullptr) {
      slab = kmem_cache.slabs_free_;
    }

    // 没有足够空间，需要为 kmem_cache 分配更多空间
    if (slab == nullptr) {
      auto ptr = page_allocator_.Alloc(CACHE_CACHE_ORDER);
      if (ptr == nullptr) {
        kmem_cache.error_code_ = 2;
        return nullptr;
      }

      slab = new (ptr) slab_t(&kmem_cache, ptr, kmem_cache.objectsInSlab_,
                              kmem_cache.colour_next_);

      // 新分配的slab将被放入partial链表（因为即将从中分配对象）
      kmem_cache.slabs_partial_ = slab;

      // 更新缓存行对齐偏移
      kmem_cache.colour_next_ =
          (kmem_cache.colour_next_ + 1) % (kmem_cache.colour_max_ + 1);

      kmem_cache.num_allocations_ += kmem_cache.objectsInSlab_;
      kmem_cache.growing_ = true;
    }

    return slab;
  }
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_SLAB_HPP_ */
