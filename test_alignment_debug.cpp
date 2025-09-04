#include <iostream>
#include <iomanip>
#include "slab.hpp"

using namespace bmalloc;

template <class LogFunc = std::nullptr_t, class Lock = LockBase>
class MallocPageAllocator : public AllocatorBase<LogFunc, Lock> {
private:
  using Base = AllocatorBase<LogFunc, Lock>;
  
  struct AllocRecord {
    void* ptr;
    size_t size;
  };
  
  std::vector<AllocRecord> allocated_blocks_;

public:
  explicit MallocPageAllocator(const char* name, void* start_addr = nullptr,
                               size_t page_count = 0)
      : Base(name, start_addr, page_count) {
    this->free_count_ = SIZE_MAX / kPageSize;
    this->used_count_ = 0;
  }

  ~MallocPageAllocator() {
    for (const auto& record : allocated_blocks_) {
      std::free(record.ptr);
    }
  }

protected:
  auto AllocImpl(size_t order) -> void* override {
    size_t page_count = 1ULL << order;
    
    if (order > 20) {
      return nullptr;
    }

    size_t size = page_count * kPageSize;
    void* ptr = std::aligned_alloc(kPageSize, size);

    if (ptr != nullptr) {
      allocated_blocks_.push_back({ptr, size});
      this->used_count_ += page_count;
      if (this->free_count_ >= page_count) {
        this->free_count_ -= page_count;
      }
      std::memset(ptr, 0, size);
    }

    return ptr;
  }

  void FreeImpl(void* addr, size_t order = 0) override {
    if (addr == nullptr) return;
    
    auto it = std::find_if(
        allocated_blocks_.begin(), allocated_blocks_.end(),
        [addr](const AllocRecord& record) { return record.ptr == addr; });

    if (it != allocated_blocks_.end()) {
      std::free(addr);
      allocated_blocks_.erase(it);
    }
  }
};

template <class PageAllocator, class LogFunc = std::nullptr_t, class Lock = LockBase>
class TestableSlab : public Slab<PageAllocator, LogFunc, Lock> {
public:
  using Base = Slab<PageAllocator, LogFunc, Lock>;
  using Base::Base;
  using Base::find_create_kmem_cache;
  using Base::kmem_cache_alloc;
  using Base::kmem_cache_free;
};

int main() {
    std::cout << "=== Slab 对齐方式分析 ===" << std::endl;
    
    using MyAllocator = MallocPageAllocator<>;
    using MySlab = TestableSlab<MyAllocator>;
    
    void* initial_memory = std::aligned_alloc(kPageSize, 32 * kPageSize);
    MySlab slab("test_slab", initial_memory, 32);
    
    std::cout << "\n1. 缓存行大小: " << 64 << " 字节" << std::endl;
    std::cout << "2. 页大小: " << kPageSize << " 字节" << std::endl;
    
    // 测试不同大小的对象
    std::vector<std::pair<std::string, size_t>> test_sizes = {
        {"char", 1},
        {"int", 4}, 
        {"long", 8},
        {"double", 8},
        {"16字节", 16},
        {"32字节", 32},
        {"64字节", 64},
        {"128字节", 128}
    };
    
    for (const auto& [name, size] : test_sizes) {
        std::cout << "\n--- 测试 " << name << " (大小: " << size << " 字节) ---" << std::endl;
        
        auto* cache = slab.find_create_kmem_cache(
            (name + "_cache").c_str(), size, nullptr, nullptr);
        
        if (cache) {
            std::cout << "Cache 对象大小: " << cache->objectSize_ << " 字节" << std::endl;
            
            // 分配多个对象观察对齐
            std::vector<void*> objects;
            for (int i = 0; i < 5; ++i) {
                void* obj = slab.kmem_cache_alloc(cache);
                if (obj) {
                    objects.push_back(obj);
                    uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
                    
                    std::cout << "对象[" << i << "] 地址: 0x" << std::hex << addr 
                              << " (模 " << std::dec << size << " = " << (addr % size) << ")"
                              << " (模 8 = " << (addr % 8) << ")"
                              << " (模 16 = " << (addr % 16) << ")" << std::endl;
                }
            }
            
            // 计算相邻对象的间距
            if (objects.size() >= 2) {
                for (size_t i = 1; i < objects.size(); ++i) {
                    uintptr_t addr1 = reinterpret_cast<uintptr_t>(objects[i-1]);
                    uintptr_t addr2 = reinterpret_cast<uintptr_t>(objects[i]);
                    std::cout << "对象间距[" << (i-1) << "->" << i << "]: " 
                              << (addr2 - addr1) << " 字节" << std::endl;
                }
            }
            
            // 释放对象
            for (void* obj : objects) {
                slab.kmem_cache_free(cache, obj);
            }
        }
    }
    
    std::free(initial_memory);
    return 0;
}
