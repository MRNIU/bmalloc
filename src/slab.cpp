/**
 * Copyright The bmalloc Contributors
 *
 * Slab分配器实现
 *
 * Slab分配器是一种高效的内存管理算法，特别适用于频繁分配和释放相同大小对象的场景。
 *
 * 核心概念：
 * - Cache: 管理特定大小对象的高级结构，包含多个slab
 * - Slab: 实际的内存块，包含多个相同大小的对象
 * - Object: 实际分配给用户的内存单元
 *
 * 主要特性：
 * 1. 减少内存碎片
 * 2. 快速分配/释放（O(1)时间复杂度）
 * 3. 支持对象构造器和析构器
 * 4. 缓存行对齐优化
 * 5. 线程安全
 */

#include <cstring>
#include <iostream>

#include "slab.h"

namespace bmalloc {

using namespace std;
/*
错误码定义 (error_code值的含义):
0 - 无错误
1 - kmem_cache_create函数中传入了无效参数
2 - 分配新slab时空间不足
3 - 用户无权访问cache_cache
4 - kmem_cache_error函数传入了空指针参数
5 - kmem_cache_destroy传入的cache在cache_cache中不存在
6 - kmem_cache_free传入的对象在cache_cache中不存在
7 - 对象释放时传入了无效指针

ERROR CODES: (error_cod value)
0 - no error
1 - invalid arguments in function kmem_cache_create
2 - no enough space for allocating new slab
3 - users don't have access to cache_cache
4 - nullpointer argument passed to func kmem_cache_error
5 - cache passed by func kmem_cache_destroy does not exists in cache_cache
6 - object passed by func kmem_cache_free does not exists in cache_cache
7 - invalid pointer passed for object dealocation

*/

/**
 * 初始化内存管理系统
 *
 * @param space 可用内存空间的起始地址
 * @param page_count 内存块数量
 *
 * 功能：
 * 1. 初始化底层buddy分配器
 * 2. 创建并初始化cache_cache（管理kmem_cache_t结构的特殊cache）
 * 3. 分配第一个slab用于cache_cache
 * 4. 设置缓存行对齐参数
 */
// AAA::AAA(void* start_addr, int page_count)
//     : global_buddy(Buddy("slab_buddy", start_addr, page_count)) {
//   // 为cache_cache分配第一个slab
//   void* ptr = global_buddy.Alloc(CACHE_CACHE_ORDER);
//   if (ptr == nullptr) exit(1);
//   slab_t* slab = (slab_t*)ptr;

//   // 初始化cache_cache的slab链表
//   cache_cache.slabs_free = slab;
//   cache_cache.slabs_full = nullptr;
//   cache_cache.slabs_partial = nullptr;

//   // 设置cache_cache的基本属性
//   strcpy(cache_cache.name, "kmem_cache");
//   cache_cache.objectSize = sizeof(kmem_cache_t);
//   cache_cache.order = CACHE_CACHE_ORDER;

//   cache_cache.growing = false;
//   cache_cache.ctor = nullptr;
//   cache_cache.dtor = nullptr;
//   cache_cache.error_code = 0;
//   cache_cache.next = nullptr;

//   // 初始化slab结构
//   slab->colouroff = 0;
//   slab->freeList = (int*)((char*)ptr + sizeof(slab_t));
//   slab->nextFreeObj = 0;
//   slab->inuse = 0;
//   slab->next = nullptr;
//   slab->prev = nullptr;
//   slab->myCache = &cache_cache;

//   // 计算每个slab能容纳的对象数量
//   long memory = (1 << cache_cache.order) * kPageSize;
//   memory -= sizeof(slab_t);
//   int n = 0;
//   while ((long)(memory - sizeof(uint32_t) - cache_cache.objectSize) >= 0) {
//     n++;
//     memory -= sizeof(uint32_t) + cache_cache.objectSize;
//   }

//   // 设置对象数组起始位置
//   slab->objects = (void*)((char*)ptr + sizeof(slab_t) + sizeof(uint32_t) * n);
//   kmem_cache_t* list = (kmem_cache_t*)slab->objects;

//   // 初始化空闲对象链表
//   for (int i = 0; i < n; i++) {
//     *list[i].name = '\0';
//     // memcpy(&list[i].cache_mutex, &mutex(), sizeof(mutex));
//     new (&list[i].cache_mutex) mutex;  // 就地构造mutex
//     slab->freeList[i] = i + 1;
//   }
//   // slab->freeList[n - 1] = -1;

//   // 设置cache_cache的对象统计信息
//   cache_cache.objectsInSlab = n;
//   cache_cache.num_active = 0;
//   cache_cache.num_allocations = n;

//   // 设置缓存行对齐参数
//   cache_cache.colour_max = memory / CACHE_L1_LINE_SIZE;
//   if (cache_cache.colour_max > 0)
//     cache_cache.colour_next = 1;
//   else
//     cache_cache.colour_next = 0;

//   // 将cache_cache加入全局cache链表
//   allCaches = &cache_cache;
// }

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
// kmem_cache_t* AAA::kmem_cache_create(const char* name, size_t size,
//                                      void (*ctor)(void*),
//                                      void (*dtor)(void*))  // Allocate cache
// {
//   // 参数验证
//   if (name == nullptr || *name == '\0' || (long)size <= 0) {
//     cache_cache.error_code = 1;
//     return nullptr;
//   }

//   // 禁止创建与cache_cache同名的cache
//   if (strcmp(name, cache_cache.name) == 0) {
//     cache_cache.error_code = 3;
//     return nullptr;
//   }

//   lock_guard<mutex> guard(cache_cache.cache_mutex);

//   kmem_cache_t* ret = nullptr;
//   cache_cache.error_code = 0;  // reset error code

//   slab_t* s;

//   // 第一种方法：在全局cache链表中查找是否已存在相同的cache
//   ret = allCaches;
//   while (ret != nullptr) {
//     if (strcmp(ret->name, name) == 0 && ret->objectSize == size) return ret;
//     ret = ret->next;
//   }

//   // 第二种方法（已注释）：在cache_cache的slab中查找
//   /*
//   s = cache_cache.slabs_full;
//   while (s != nullptr)	// check if cache already exists in slabs_full list
//   {
//           kmem_cache_t* list = (kmem_cache_t*)s->objects;
//           for (int i = 0; i < cache_cache.objectsInSlab; i++)
//           {
//                   if (strcmp(list[i].name, name) == 0 && list[i].objectSize ==
//   size) return &list[i];
//           }
//           s = s->next;
//   }

//   s = cache_cache.slabs_partial;
//   while (s != nullptr)	// check if cache already exists in slabs_partial list
//   {
//           kmem_cache_t* list = (kmem_cache_t*)s->objects;
//           for (int i = 0; i < cache_cache.objectsInSlab; i++)
//           {
//                   if (strcmp(list[i].name, name) == 0 && list[i].objectSize ==
//   size) return &list[i];
//           }
//           s = s->next;
//   }
//   */

//   // cache不存在，需要创建新的

//   // 寻找可用的slab来分配kmem_cache_t结构
//   s = cache_cache.slabs_partial;
//   if (s == nullptr) s = cache_cache.slabs_free;

//   if (s == nullptr)  // 没有足够空间，需要为cache_cache分配更多空间
//   {
//     lock_guard<mutex> guard(buddy_mutex);
//     void* ptr = global_buddy.Alloc(CACHE_CACHE_ORDER);
//     if (ptr == nullptr) {
//       cache_cache.error_code = 2;
//       return nullptr;
//     }
//     s = (slab_t*)ptr;

//     cache_cache.slabs_partial = s;

//     // 设置缓存行对齐偏移
//     s->colouroff = cache_cache.colour_next;
//     cache_cache.colour_next =
//         (cache_cache.colour_next + 1) % (cache_cache.colour_max + 1);

//     // 初始化新slab
//     s->freeList = (int*)((char*)ptr + sizeof(slab_t));
//     s->nextFreeObj = 0;
//     s->inuse = 0;
//     s->next = nullptr;
//     s->prev = nullptr;
//     s->myCache = &cache_cache;

//     s->objects = (void*)((char*)ptr + sizeof(slab_t) +
//                          sizeof(uint32_t) * cache_cache.objectsInSlab +
//                          CACHE_L1_LINE_SIZE * s->colouroff);
//     kmem_cache_t* list = (kmem_cache_t*)s->objects;

//     // 初始化对象数组
//     for (size_t i = 0; i < cache_cache.objectsInSlab; i++) {
//       *list[i].name = '\0';
//       // memcpy(&list[i].cache_mutex, &mutex(), sizeof(mutex));
//       new (&list[i].cache_mutex) mutex;
//       s->freeList[i] = i + 1;
//     }
//     // s->freeList[cache_cache.objectsInSlab - 1] = -1;

//     cache_cache.num_allocations += cache_cache.objectsInSlab;

//     cache_cache.growing = true;
//   }

//   // 从slab中分配一个kmem_cache_t对象
//   kmem_cache_t* list = (kmem_cache_t*)s->objects;
//   ret = &list[s->nextFreeObj];
//   s->nextFreeObj = s->freeList[s->nextFreeObj];
//   s->inuse++;
//   cache_cache.num_active++;

//   // 更新slab链表状态
//   if (s == cache_cache.slabs_free) {
//     cache_cache.slabs_free = s->next;
//     if (cache_cache.slabs_free != nullptr)
//       cache_cache.slabs_free->prev = nullptr;

//     if (s->inuse != cache_cache.objectsInSlab)  // from free to partial
//     {
//       s->next = cache_cache.slabs_partial;
//       if (cache_cache.slabs_partial != nullptr)
//         cache_cache.slabs_partial->prev = s;
//       cache_cache.slabs_partial = s;
//     } else  // from free to full
//     {
//       s->next = cache_cache.slabs_full;
//       if (cache_cache.slabs_full != nullptr) cache_cache.slabs_full->prev = s;
//       cache_cache.slabs_full = s;
//     }
//   } else {
//     if (s->inuse == cache_cache.objectsInSlab)  // from partial to full
//     {
//       cache_cache.slabs_partial = s->next;
//       if (cache_cache.slabs_partial != nullptr)
//         cache_cache.slabs_partial->prev = nullptr;

//       s->next = cache_cache.slabs_full;
//       if (cache_cache.slabs_full != nullptr) cache_cache.slabs_full->prev = s;
//       cache_cache.slabs_full = s;
//     }
//   }

//   // 初始化新cache
//   strcpy(ret->name, name);

//   ret->slabs_full = nullptr;
//   ret->slabs_partial = nullptr;
//   ret->slabs_free = nullptr;

//   ret->growing = false;
//   ret->ctor = ctor;
//   ret->dtor = dtor;
//   ret->error_code = 0;
//   ret->next = allCaches;
//   allCaches = ret;

//   // 计算新cache的order值（使一个slab能容纳至少一个对象）
//   long memory = kPageSize;
//   int order = 0;
//   while ((long)(memory - sizeof(slab_t) - sizeof(uint32_t) - size) < 0) {
//     order++;
//     memory *= 2;
//   }

//   ret->objectSize = size;
//   ret->order = order;

//   // 计算每个slab中的对象数量
//   memory -= sizeof(slab_t);
//   int n = 0;
//   while ((long)(memory - sizeof(uint32_t) - size) >= 0) {
//     n++;
//     memory -= sizeof(uint32_t) + size;
//   }

//   ret->objectsInSlab = n;
//   ret->num_active = 0;
//   ret->num_allocations = 0;

//   // 设置缓存行对齐参数
//   ret->colour_max = memory / CACHE_L1_LINE_SIZE;
//   ret->colour_next = 0;

//   return ret;
// }

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
// int AAA::kmem_cache_shrink(kmem_cache_t* cachep)  // Shrink cache
// {
//   if (cachep == nullptr) return 0;

//   lock_guard<mutex> guard(cachep->cache_mutex);

//   int blocksFreed = 0;
//   cachep->error_code = 0;
//   if (cachep->slabs_free != nullptr &&
//       cachep->growing == false)  // 只有当存在空闲slab且cache不在增长时才收缩
//   {
//     lock_guard<mutex> guard(buddy_mutex);
//     int n = 1 << cachep->order;  // 每个slab包含的内存块数
//     slab_t* s;
//     while (cachep->slabs_free != nullptr) {
//       s = cachep->slabs_free;
//       cachep->slabs_free = s->next;
//       global_buddy.Free(s, cachep->order);  // 释放slab到buddy分配器
//       blocksFreed += n;
//       cachep->num_allocations -= cachep->objectsInSlab;
//     }
//   }
//   cachep->growing = false;  // 重置增长标志
//   return blocksFreed;
// }

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
void* AAA::kmem_cache_alloc(
    kmem_cache_t* cachep)  // Allocate one object from cache
{
  if (cachep == nullptr || *cachep->name == '\0') return nullptr;

  lock_guard<mutex> guard(cachep->cache_mutex);

  void* retObject = nullptr;
  cachep->error_code = 0;

  // 查找可用的slab：优先使用部分使用的slab，然后是空闲slab
  slab_t* s = cachep->slabs_partial;
  if (s == nullptr) s = cachep->slabs_free;

  if (s == nullptr)  // 需要分配新slab
  {
    lock_guard<mutex> guard(buddy_mutex);

    void* ptr = global_buddy.Alloc(cachep->order);
    if (ptr == nullptr) {
      cachep->error_code = 2;
      return nullptr;
    }
    s = (slab_t*)ptr;

    // 新分配的slab将被放入partial链表（因为即将从中分配对象）
    cachep->slabs_partial = s;

    // 设置缓存行对齐偏移
    s->colouroff = cachep->colour_next;
    cachep->colour_next = (cachep->colour_next + 1) % (cachep->colour_max + 1);

    // 初始化slab结构
    s->freeList = (int*)((char*)ptr + sizeof(slab_t));
    s->nextFreeObj = 0;
    s->inuse = 0;
    s->next = nullptr;
    s->prev = nullptr;
    s->myCache = cachep;

    // 设置对象数组位置（考虑缓存行对齐）
    s->objects = (void*)((char*)ptr + sizeof(slab_t) +
                         sizeof(uint32_t) * cachep->objectsInSlab +
                         CACHE_L1_LINE_SIZE * s->colouroff);
    void* obj = s->objects;

    // 初始化所有对象（调用构造函数）并设置空闲链表
    for (size_t i = 0; i < cachep->objectsInSlab; i++) {
      if (cachep->ctor) cachep->ctor(obj);  // 调用对象构造函数
      obj = (void*)((char*)obj + cachep->objectSize);
      s->freeList[i] = i + 1;
    }
    // s->freeList[cachep->objectsInSlab - 1] = -1;

    cachep->num_allocations += cachep->objectsInSlab;
    cachep->growing = true;
  }

  // 从slab中分配对象
  retObject = (void*)((char*)s->objects + s->nextFreeObj * cachep->objectSize);
  s->nextFreeObj = s->freeList[s->nextFreeObj];
  s->inuse++;
  cachep->num_active++;

  // 更新slab链表状态
  if (s == cachep->slabs_free) {
    // 从free链表中移除
    cachep->slabs_free = s->next;
    if (cachep->slabs_free != nullptr) cachep->slabs_free->prev = nullptr;

    if (s->inuse != cachep->objectsInSlab)  // 移动到partial链表
    {
      s->next = cachep->slabs_partial;
      if (cachep->slabs_partial != nullptr) cachep->slabs_partial->prev = s;
      cachep->slabs_partial = s;
    } else  // 移动到full链表
    {
      s->next = cachep->slabs_full;
      if (cachep->slabs_full != nullptr) cachep->slabs_full->prev = s;
      cachep->slabs_full = s;
    }
  } else {
    if (s->inuse == cachep->objectsInSlab)  // 从partial移动到full
    {
      cachep->slabs_partial = s->next;
      if (cachep->slabs_partial != nullptr)
        cachep->slabs_partial->prev = nullptr;

      s->next = cachep->slabs_full;
      if (cachep->slabs_full != nullptr) cachep->slabs_full->prev = s;
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
void AAA::kmem_cache_free(kmem_cache_t* cachep,
                          void* objp)  // Deallocate one object from cache
{
  if (cachep == nullptr || *cachep->name == '\0' || objp == nullptr) return;

  lock_guard<mutex> guard(cachep->cache_mutex);

  cachep->error_code = 0;
  slab_t* s;

  // 查找对象所属的slab
  int slabSize = kPageSize * (1 << cachep->order);
  bool inFullList = true;  // 标记slab是否在full链表中

  // 首先在full链表中查找
  s = cachep->slabs_full;
  while (s != nullptr) {
    if ((void*)objp > (void*)s && (void*)objp < (void*)((char*)s + slabSize))
      break;
    s = s->next;
  }

  // 如果在full链表中没找到，在partial链表中查找
  if (s == nullptr) {
    inFullList = false;
    s = cachep->slabs_partial;
    while (s != nullptr) {
      if ((void*)objp > (void*)s && (void*)objp < (void*)((char*)s + slabSize))
        break;
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
  int i = ((char*)objp - (char*)s->objects) / cachep->objectSize;

  // 验证对象地址是否对齐
  if (objp != (void*)((char*)s->objects + i * cachep->objectSize)) {
    cachep->error_code = 7;
    return;
  }

  // 将对象加入空闲链表
  s->freeList[i] = s->nextFreeObj;
  s->nextFreeObj = i;

  // 调用析构函数
  if (cachep->dtor != nullptr) cachep->dtor(objp);

  // 注释：析构函数负责将对象恢复到初始化状态
  // if (cachep->ctor != nullptr) cachep->ctor(objp);

  // 检查slab现在是否为空闲或部分使用状态，并更新链表
  if (inFullList)  // slab原本在full链表中
  {
    slab_t *prev, *next;

    // 从full链表中删除slab
    prev = s->prev;
    next = s->next;
    s->prev = nullptr;

    if (prev != nullptr) prev->next = next;
    if (next != nullptr) next->prev = prev;
    if (cachep->slabs_full == s) cachep->slabs_full = next;

    if (s->inuse != 0)  // 插入到partial链表
    {
      s->next = cachep->slabs_partial;
      if (cachep->slabs_partial != nullptr) cachep->slabs_partial->prev = s;
      cachep->slabs_partial = s;
    } else  // 插入到free链表
    {
      s->next = cachep->slabs_free;
      if (cachep->slabs_free != nullptr) cachep->slabs_free->prev = s;
      cachep->slabs_free = s;
    }
  } else  // slab原本在partial链表中
  {
    if (s->inuse == 0) {  // 现在变成完全空闲
      slab_t *prev, *next;

      // 从partial链表中删除slab
      prev = s->prev;
      next = s->next;
      s->prev = nullptr;

      if (prev != nullptr) prev->next = next;
      if (next != nullptr) next->prev = prev;
      if (cachep->slabs_partial == s) cachep->slabs_partial = next;

      // 插入到free链表
      s->next = cachep->slabs_free;
      if (cachep->slabs_free != nullptr) cachep->slabs_free->prev = s;
      cachep->slabs_free = s;
    }
  }
}

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
void* AAA::kmalloc(size_t size)  // Alloacate one small memory buffer
{
  if (size < 32 || size > 131072) return nullptr;

  // 将size向上舍入到最近的2的幂次方
  // int j = 1 << (int)(ceil(log2(size)));
  size_t j = 32;
  while (j < size) j <<= 1;

  char num[7];
  void* buff = nullptr;

  // 生成cache名称，格式为"size-XXX"
  char name[20];
  strcpy(name, "size-");
  sprintf(num, "%ld", j);
  strcat(name, num);

  // 创建或获取对应大小的cache
  kmem_cache_t* buffCachep = kmem_cache_create(name, j, nullptr, nullptr);

  // 从cache中分配对象
  buff = kmem_cache_alloc(buffCachep);

  return buff;
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
kmem_cache_t* AAA::find_buffers_cache(const void* objp) {
  lock_guard<mutex> guard(cache_cache.cache_mutex);

  kmem_cache_t* curr = allCaches;
  slab_t* s;

  while (curr != nullptr) {
    if (strstr(curr->name, "size-") != nullptr)  // 找到小内存缓冲区cache
    {
      // 在full slab中查找
      s = curr->slabs_full;
      int slabSize = kPageSize * (1 << curr->order);
      while (s != nullptr) {
        if ((void*)objp > (void*)s &&
            (void*)objp < (void*)((char*)s + slabSize))  // 找到包含对象的cache
          return curr;

        s = s->next;
      }

      // 在partial slab中查找
      s = curr->slabs_partial;
      while (s != nullptr) {
        if ((void*)objp > (void*)s &&
            (void*)objp < (void*)((char*)s + slabSize))  // 找到包含对象的cache
          return curr;

        s = s->next;
      }
    }
    curr = curr->next;
  }

  return nullptr;
}

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
void AAA::kfree(const void* objp)  // Deallocate one small memory buffer
{
  if (objp == nullptr) return;

  // 查找包含该对象的cache
  kmem_cache_t* buffCachep = find_buffers_cache(objp);

  if (buffCachep == nullptr) return;

  // 释放对象
  kmem_cache_free(buffCachep, (void*)objp);

  // 如果cache有空闲slab，尝试收缩以节省内存
  if (buffCachep->slabs_free != nullptr) kmem_cache_shrink(buffCachep);
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
void AAA::kmem_cache_destroy(kmem_cache_t* cachep)  // Deallocate cache
{
  if (cachep == nullptr || *cachep->name == '\0') return;

  // 获取三个互斥锁：cache锁、cache_cache锁、buddy锁
  lock_guard<mutex> guard1(cachep->cache_mutex);
  lock_guard<mutex> guard2(cache_cache.cache_mutex);
  lock_guard<mutex> guard3(buddy_mutex);

  slab_t* s;
  void* ptr;
  cache_cache.error_code = 0;

  // 从allCaches链表中删除cache
  kmem_cache_t *prev = nullptr, *curr = allCaches;
  while (curr != cachep) {
    prev = curr;
    curr = curr->next;
  }

  if (curr == nullptr)  // cache不在cache链中（意味着对象也不在cache_cache中）
  {
    cache_cache.error_code = 5;
    return;
  }

  if (prev == nullptr)
    allCaches = allCaches->next;
  else
    prev->next = curr->next;
  curr->next = nullptr;

  // 在cache_cache中查找拥有该cache对象的slab
  int slabSize = kPageSize * (1 << cache_cache.order);
  bool inFullList = true;  // 标记slab是否在full链表中
  s = cache_cache.slabs_full;
  while (s != nullptr) {
    if ((void*)cachep > (void*)s &&
        (void*)cachep < (void*)((char*)s + slabSize))
      break;
    s = s->next;
  }

  if (s == nullptr) {
    inFullList = false;  // slab在partial链表中
    s = cache_cache.slabs_partial;
    while (s != nullptr) {
      if ((void*)cachep > (void*)s &&
          (void*)cachep < (void*)((char*)s + slabSize))
        break;
      s = s->next;
    }
  }

  if (s == nullptr)  // 在cache_cache中没找到拥有该cache的slab
  {
    cache_cache.error_code = 5;
    return;
  }

  // 找到拥有该cache的slab

  // 重置cache字段并更新cache_cache字段
  s->inuse--;
  cache_cache.num_active--;
  int i = cachep - (kmem_cache_t*)s->objects;
  s->freeList[i] = s->nextFreeObj;
  s->nextFreeObj = i;
  *cachep->name = '\0';  // 清空cache名称
  cachep->objectSize = 0;

  // 释放cache中使用的所有slab

  // 释放full slab链表
  slab_t* freeTemp = cachep->slabs_full;
  while (freeTemp != nullptr) {
    ptr = freeTemp;
    freeTemp = freeTemp->next;
    global_buddy.Free(ptr, cachep->order);
  }

  // 释放partial slab链表
  freeTemp = cachep->slabs_partial;
  while (freeTemp != nullptr) {
    ptr = freeTemp;
    freeTemp = freeTemp->next;
    global_buddy.Free(ptr, cachep->order);
  }

  // 释放free slab链表
  freeTemp = cachep->slabs_free;
  while (freeTemp != nullptr) {
    ptr = freeTemp;
    freeTemp = freeTemp->next;
    global_buddy.Free(ptr, cachep->order);
  }

  // 检查cache_cache中的slab现在是否为空闲或部分使用状态
  if (inFullList)  // slab原本在full链表中
  {
    slab_t *prev, *next;

    // 从full链表中删除slab
    prev = s->prev;
    next = s->next;
    s->prev = nullptr;

    if (prev != nullptr) prev->next = next;
    if (next != nullptr) next->prev = prev;
    if (cache_cache.slabs_full == s) cache_cache.slabs_full = next;

    if (s->inuse != 0)  // 插入到partial链表
    {
      s->next = cache_cache.slabs_partial;
      if (cache_cache.slabs_partial != nullptr)
        cache_cache.slabs_partial->prev = s;
      cache_cache.slabs_partial = s;
    } else  // 插入到free链表
    {
      s->next = cache_cache.slabs_free;
      if (cache_cache.slabs_free != nullptr) cache_cache.slabs_free->prev = s;
      cache_cache.slabs_free = s;
    }
  } else  // slab原本在partial链表中
  {
    if (s->inuse == 0) {
      slab_t *prev, *next;

      // 从partial链表中删除slab
      prev = s->prev;
      next = s->next;
      s->prev = nullptr;

      if (prev != nullptr) prev->next = next;
      if (next != nullptr) next->prev = prev;
      if (cache_cache.slabs_partial == s) cache_cache.slabs_partial = next;

      // 插入到free链表
      s->next = cache_cache.slabs_free;
      if (cache_cache.slabs_free != nullptr) cache_cache.slabs_free->prev = s;
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
      global_buddy.Free(s, cache_cache.order);
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
void AAA::kmem_cache_info(kmem_cache_t* cachep)  // Print cache info
{
  // lock_guard<mutex> guard1(cout_mutex);

  if (cachep == nullptr) {
    cout << "NullPointer passed as argument" << endl;
    return;
  }

  lock_guard<mutex> guard2(cachep->cache_mutex);

  int i = 0;

  // 统计free slab数量
  slab_t* s = cachep->slabs_free;
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
  if (cachep->num_allocations > 0)
    perc = 100 * (double)cachep->num_active / cachep->num_allocations;

  // 打印cache信息
  cout << "*** CACHE INFO: ***" << endl
       << "Name:\t\t\t\t" << cachep->name << endl
       << "Size of one object (in bytes):\t" << cachep->objectSize << endl
       << "Size of cache (in blocks):\t" << cacheSize << endl
       << "Number of slabs:\t\t" << i << endl
       << "Number of objects in one slab:\t" << cachep->objectsInSlab << endl
       << "Percentage occupancy of cache:\t" << perc << " %" << endl;
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
int AAA::kmem_cache_error(kmem_cache_t* cachep)  // Print error message
{
  // lock_guard<mutex> guard1(cout_mutex);

  if (cachep == nullptr) {
    cout << "Nullpointer argument passed" << endl;
    return 4;
  }

  lock_guard<mutex> guard2(cachep->cache_mutex);

  int error_code = cachep->error_code;

  if (error_code == 0) {
    cout << "NO ERROR" << endl;
    return 0;
  }

  cout << "ERROR: ";
  switch (error_code) {
    case 1:
      cout << "Invalid arguments passed in function kmem_cache_create" << endl;
      break;
    case 2:
      cout << "No enough space for allocating new slab" << endl;
      break;
    case 3:
      cout << "Access to cache_cache isn't allowed" << endl;
      break;
    case 4:
      cout << "NullPointer argument passed to func kmem_cache_error" << endl;
      break;
    case 5:
      cout << "Cache passed by func kmem_cache_destroy does not exists in "
              "kmem_cache"
           << endl;
      break;
    case 6:
      cout << "Object passed by func kmem_cache_free does not exists in "
              "kmem_cache"
           << endl;
      break;
    case 7:
      cout << "Invalid pointer passed for object dealocation" << endl;
      break;
    default:
      cout << "Undefined error" << endl;
      break;
  }

  return error_code;
}

/**
 * 打印所有cache的信息
 * 遍历allCaches链表，调用kmem_cache_info打印每个cache的详细信息
 */
void AAA::kmem_cache_allInfo() {
  kmem_cache_t* curr = allCaches;
  while (curr != nullptr) {
    kmem_cache_info(curr);
    cout << endl;
    curr = curr->next;
  }
}

/*

ERROR CODES: (error_cod value)
0 - no error
1 - invalid arguments in function kmem_cache_create
2 - no enough space for allocating new slab
3 - users don't have access to cache_cache
4 - nullpointer argument passed to func kmem_cache_error
5 - cache passed by func kmem_cache_destroy does not exists in cache_cache
6 - object passed by func kmem_cache_free does not exists in cache_cache
7 - invalid pointer passed for object dealocation

*/

}  // namespace bmalloc
