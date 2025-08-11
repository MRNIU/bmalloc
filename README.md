# bmalloc

A bare metal memory allocator implementation for embedded systems and kernel development.

这是一个专为裸机环境设计的内存分配器实现，适用于嵌入式系统和内核开发。

## 功能特性

- **Buddy 内存分配器**：高效的伙伴系统算法实现
- **零依赖设计**：不依赖标准库，适合裸机环境
- **内存碎片优化**：自动合并相邻的空闲块
- **高性能实现**：使用静态数组和链表，避免额外内存开销
- **完善的测试**：包含单元测试、压力测试和边界条件测试

## Buddy 分配器

### 算法原理

Buddy 分配器是一种经典的内存管理算法，基于二进制伙伴系统：

1. **二进制分割**：将内存按 2 的幂次方大小进行分割和管理
2. **多级链表**：维护多个空闲链表，每个链表管理特定大小 (2^i) 的空闲块
3. **分裂策略**：分配时如果没有合适大小的块，就分割更大的块
4. **合并策略**：释放时尝试与相邻的 buddy 块合并成更大的块

### 关键特性

- **内存单位**：以页面（4KB）为基本单位进行管理
- **分配大小**：支持 2^order 个页面的分配（order=0: 1页，order=1: 2页，order=2: 4页...）
- **地址对齐**：所有分配的地址都按对应的块大小对齐
- **范围检查**：严格的地址范围和参数验证

### 使用示例

```cpp
#include "buddy.h"

// 初始化 buddy 分配器
void* memory_pool = aligned_alloc(4096, 1024 * 1024);  // 1MB 内存池
bmalloc::Buddy allocator("main", memory_pool, 256);    // 管理 256 页

// 分配内存
void* ptr1 = allocator.Alloc(0);  // 分配 1 页 (4KB)
void* ptr2 = allocator.Alloc(2);  // 分配 4 页 (16KB)

// 释放内存
allocator.Free(ptr1, 0);  // 释放 1 页
allocator.Free(ptr2, 2);  // 释放 4 页

// 查询状态
size_t used = allocator.GetUsedCount();   // 已使用页数
size_t free = allocator.GetFreeCount();   // 空闲页数
```

### 性能特点

- **时间复杂度**：
  - 分配：O(log n)，其中 n 是最大块大小
  - 释放：O(k)，其中 k 是同级空闲块数量
- **空间复杂度**：O(1) 额外空间开销
- **内存效率**：通过 buddy 合并机制有效减少外部碎片

### 技术实现

- **数据结构**：静态数组存储空闲链表头指针
- **链表管理**：利用空闲内存块本身存储链表节点
- **地址计算**：高效的位运算进行 buddy 地址计算
- **边界检查**：完善的参数验证和地址范围检查

### 测试覆盖

项目包含全面的测试用例：

- **基本功能测试**：分配、释放、地址验证
- **边界条件测试**：内存耗尽、无效参数处理
- **Buddy 合并测试**：验证自动合并机制
- **压力测试**：随机分配释放操作
- **数据完整性测试**：内存读写正确性验证
- **计数器测试**：内存使用统计准确性

## 构建和测试

### 构建要求

- CMake 3.27+
- C++23 兼容的编译器（GCC 12+, Clang 15+）
- Google Test（用于单元测试）

### 构建步骤

```bash
# 克隆项目
git clone <repository-url>
cd bmalloc

# 创建构建目录
mkdir build && cd build

# 配置和构建
cmake ..
make

# 运行测试
make test
# 或直接运行测试可执行文件
./bin/bmalloc_test
```

### 测试选项

```bash
# 运行所有测试
./bin/bmalloc_test

# 运行特定测试
./bin/bmalloc_test --gtest_filter="BuddyTest.BasicAllocAndFree"

# 显示详细输出
./bin/bmalloc_test --gtest_filter="BuddyTest.BuddyPrintDemo"
```

## API 参考

### 核心类

#### `bmalloc::Buddy`

主要的 Buddy 分配器类，继承自 `AllocatorBase`。

```cpp
class Buddy : public AllocatorBase {
public:
    // 构造函数
    Buddy(const char* name, void* start_addr, size_t total_pages);
    
    // 分配内存（2^order 个页面）
    void* Alloc(size_t order) override;
    
    // 释放内存
    void Free(void* addr, size_t order) override;
    
    // 获取使用统计
    size_t GetUsedCount() const;
    size_t GetFreeCount() const;
};
```

### 参数说明

- **order**：分配大小的指数，实际分配 2^order 个页面
- **total_pages**：分配器管理的总页面数
- **start_addr**：管理内存的起始地址（必须页对齐）

## 许可证

本项目采用开源许可证，详见 [LICENSE](LICENSE) 文件。

## 贡献

欢迎提交 Issue 和 Pull Request 来改进项目。在提交代码前，请确保：

1. 代码通过所有现有测试
2. 新功能包含相应的测试用例
3. 遵循项目的代码风格规范
4. 添加必要的文档说明

## 相关项目

- [SimpleKernel](https://github.com/MRNIU/SimpleKernel) - 使用本分配器的内核项目