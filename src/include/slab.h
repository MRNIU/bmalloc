/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_SLAB_H_
#define BMALLOC_SRC_INCLUDE_SLAB_H_

#include <cstdlib>
#include <mutex>

#include "allocator_base.h"
namespace bmalloc {
using namespace std;

struct kmem_cache_t;
#define BLOCK_SIZE (4096)
#define CACHE_L1_LINE_SIZE (64)

// 缓存名称的最大长度
#define CACHE_NAMELEN (20)
// cache_cache的order值，表示管理kmem_cache_t结构体的cache使用的内存块大小
#define CACHE_CACHE_ORDER (0)

/**
 * Slab结构体 - 表示一个内存slab
 *
 * 每个slab包含多个相同大小的对象，通过链表管理空闲对象
 */
struct slab_t {
  // offset for this slab - 用于缓存行对齐的偏移量
  unsigned int colouroff;
  // starting adress of objects - 对象数组的起始地址
  void *objects;
  // list of free objects - 空闲对象索引列表
  int *freeList;
  // next free object - 下一个空闲对象的索引
  int nextFreeObj;
  // number of active objects in this slab - 当前使用的对象数量
  unsigned int inuse;
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
  unsigned int objectSize;
  // num of objects in one slab - 每个slab中的对象数量
  unsigned int objectsInSlab;
  // num of active objects in cache - 活跃对象数量
  unsigned long num_active;
  // num of total objects in cache - 总对象数量
  unsigned long num_allocations;
  // mutex (uses to lock the cache) - 缓存互斥锁
  mutex cache_mutex;
  // order of one slab (one slab has 2^order blocks) - slab的order值
  unsigned int order;
  // maximum multiplier for offset of first object in slab - 最大颜色偏移乘数
  unsigned int colour_max;
  // multiplier for next slab offset - 下一个slab的颜色偏移
  unsigned int colour_next;
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

void kmem_init(void *space, int block_num);

kmem_cache_t *kmem_cache_create(const char *name, size_t size,
                                void (*ctor)(void *),
                                void (*dtor)(void *));  // Allocate cache

int kmem_cache_shrink(kmem_cache_t *cachep);  // Shrink cache

void *kmem_cache_alloc(kmem_cache_t *cachep);  // Allocate one object from cache

void kmem_cache_free(kmem_cache_t *cachep,
                     void *objp);  // Deallocate one object from cache

void *kmalloc(size_t size);  // Alloacate one small memory buffer

void kfree(const void *objp);  // Deallocate one small memory buffer

void kmem_cache_destroy(kmem_cache_t *cachep);  // Deallocate cache

void kmem_cache_info(kmem_cache_t *cachep);  // Print cache info

int kmem_cache_error(kmem_cache_t *cachep);  // Print error message
void kmem_cache_allInfo();
}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_SLAB_H_ */
