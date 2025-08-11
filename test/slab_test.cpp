/**
 * Copyright The bmalloc Contributors
 * @file slab_test.cpp
 * @brief Slab分配器的Google Test测试用例
 */

#include "slab.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

namespace bmalloc {

/**
 * @brief Slab分配器测试夹具类
 * 提供测试环境的初始化和清理
 */
class SlabTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 分配测试用的内存池 (2MB)
    test_memory_size_ = 2 * 1024 * 1024;               // 2MB
    test_block_num_ = test_memory_size_ / BLOCK_SIZE;  // 计算块数
    test_memory_ = std::aligned_alloc(BLOCK_SIZE, test_memory_size_);

    ASSERT_NE(test_memory_, nullptr) << "无法分配测试内存";

    // 初始化为0，便于检测内存污染
    std::memset(test_memory_, 0, test_memory_size_);

    // 初始化slab内存管理系统
    kmem_init(test_memory_, test_block_num_);

    std::cout << "Slab测试环境初始化完成:" << std::endl;
    std::cout << "  内存池大小: " << test_memory_size_ << " 字节" << std::endl;
    std::cout << "  内存块数量: " << test_block_num_ << std::endl;
    std::cout << "  内存起始地址: " << test_memory_ << std::endl;
  }

  void TearDown() override {
    if (test_memory_) {
      std::free(test_memory_);
    }
  }

  // 辅助函数：检查内存是否在管理范围内
  bool IsInManagedRange(void* ptr) const {
    if (!ptr) return false;
    auto start = static_cast<char*>(test_memory_);
    auto end = start + test_memory_size_;
    auto addr = static_cast<char*>(ptr);
    return addr >= start && addr < end;
  }

  // 辅助函数：打印cache信息
  void PrintCacheInfo(kmem_cache_t* cachep, const char* test_name = "") {
    std::cout << "\n--- Cache信息";
    if (strlen(test_name) > 0) {
      std::cout << " (" << test_name << ")";
    }
    std::cout << " ---" << std::endl;
    if (cachep) {
      kmem_cache_info(cachep);
    } else {
      std::cout << "Cache指针为空" << std::endl;
    }
  }

  // 辅助函数：检查对象地址的有效性
  bool ValidateObjectAddress(void* ptr, size_t expected_size,
                             const char* test_name = "") const {
    std::cout << "\n--- 对象地址有效性检查";
    if (strlen(test_name) > 0) {
      std::cout << " (" << test_name << ")";
    }
    std::cout << " ---" << std::endl;

    // 检查1: 非空指针
    EXPECT_NE(ptr, nullptr) << "对象指针不应该为空";
    if (ptr == nullptr) {
      std::cout << "✗ 检查失败: 指针为空" << std::endl;
      return false;
    }
    std::cout << "✓ 对象指针非空: " << ptr << std::endl;

    // 检查2: 在管理范围内
    EXPECT_TRUE(IsInManagedRange(ptr)) << "对象地址应该在管理范围内";
    if (!IsInManagedRange(ptr)) {
      std::cout << "✗ 检查失败: 地址不在管理范围内" << std::endl;
      return false;
    }
    std::cout << "✓ 对象地址在管理范围内" << std::endl;

    // 检查3: 地址可读写性测试
    try {
      volatile char* test_ptr = static_cast<char*>(ptr);
      char original = *test_ptr;  // 读取原始值
      *test_ptr = 0xAA;           // 写入测试值
      EXPECT_EQ(*test_ptr, 0xAA) << "对象地址应该可写";
      if (*test_ptr != 0xAA) {
        std::cout << "✗ 检查失败: 地址不可写" << std::endl;
        return false;
      } else {
        *test_ptr = original;  // 恢复原始值
        std::cout << "✓ 对象地址可读写" << std::endl;
      }
    } catch (...) {
      std::cout << "✗ 检查失败: 访问地址时发生异常" << std::endl;
      return false;
    }

    std::cout << "对象详细信息:" << std::endl;
    std::cout << "  地址: " << ptr << std::endl;
    std::cout << "  期望大小: " << expected_size << " 字节" << std::endl;

    return true;
  }

  // 辅助函数：填充对象数据
  void FillObjectData(void* ptr, size_t size, uint8_t pattern) {
    if (!ptr) return;
    auto* data = static_cast<uint8_t*>(ptr);
    for (size_t i = 0; i < size; ++i) {
      data[i] = pattern ^ static_cast<uint8_t>(i);  // 使用异或模式，便于检测
    }
  }

  // 辅助函数：验证对象数据
  bool VerifyObjectData(void* ptr, size_t size, uint8_t pattern) {
    if (!ptr) return false;
    auto* data = static_cast<uint8_t*>(ptr);
    for (size_t i = 0; i < size; ++i) {
      uint8_t expected = pattern ^ static_cast<uint8_t>(i);
      if (data[i] != expected) {
        std::cout << "数据验证失败 - 偏移 " << i << ": 期望 0x" << std::hex
                  << static_cast<int>(expected) << ", 实际 0x"
                  << static_cast<int>(data[i]) << std::dec << std::endl;
        return false;
      }
    }
    return true;
  }

  void* test_memory_ = nullptr;
  size_t test_memory_size_ = 0;
  size_t test_block_num_ = 0;
};

/**
 * @brief 测试cache的基本创建和销毁
 */
TEST_F(SlabTest, BasicCacheCreateDestroy) {
  std::cout << "\n=== BasicCacheCreateDestroy 测试开始 ===" << std::endl;

  // 测试创建不同大小的cache
  std::cout << "\n创建64字节cache..." << std::endl;
  kmem_cache_t* cache64 = kmem_cache_create("test64", 64, nullptr, nullptr);
  ASSERT_NE(cache64, nullptr) << "创建64字节cache失败";
  EXPECT_STREQ(cache64->name, "test64") << "cache名称不正确";
  EXPECT_EQ(cache64->objectSize, 64) << "cache对象大小不正确";
  PrintCacheInfo(cache64, "64字节cache");

  std::cout << "\n创建128字节cache..." << std::endl;
  kmem_cache_t* cache128 = kmem_cache_create("test128", 128, nullptr, nullptr);
  ASSERT_NE(cache128, nullptr) << "创建128字节cache失败";
  EXPECT_STREQ(cache128->name, "test128") << "cache名称不正确";
  EXPECT_EQ(cache128->objectSize, 128) << "cache对象大小不正确";
  PrintCacheInfo(cache128, "128字节cache");

  std::cout << "\n创建1024字节cache..." << std::endl;
  kmem_cache_t* cache1024 =
      kmem_cache_create("test1024", 1024, nullptr, nullptr);
  ASSERT_NE(cache1024, nullptr) << "创建1024字节cache失败";
  EXPECT_STREQ(cache1024->name, "test1024") << "cache名称不正确";
  EXPECT_EQ(cache1024->objectSize, 1024) << "cache对象大小不正确";
  PrintCacheInfo(cache1024, "1024字节cache");

  std::cout << "\n打印所有cache信息..." << std::endl;
  kmem_cache_allInfo();

  // 测试销毁cache
  std::cout << "\n销毁cache..." << std::endl;
  kmem_cache_destroy(cache64);
  kmem_cache_destroy(cache128);
  kmem_cache_destroy(cache1024);

  std::cout << "✓ cache创建和销毁测试完成" << std::endl;
}

/**
 * @brief 测试cache的参数验证
 */
TEST_F(SlabTest, CacheParameterValidation) {
  std::cout << "\n=== CacheParameterValidation 测试开始 ===" << std::endl;

  // 测试无效参数
  std::cout << "\n测试空名称..." << std::endl;
  kmem_cache_t* cache = kmem_cache_create(nullptr, 64, nullptr, nullptr);
  EXPECT_EQ(cache, nullptr) << "空名称应该返回nullptr";

  std::cout << "\n测试空字符串名称..." << std::endl;
  cache = kmem_cache_create("", 64, nullptr, nullptr);
  EXPECT_EQ(cache, nullptr) << "空字符串名称应该返回nullptr";

  std::cout << "\n测试零大小..." << std::endl;
  cache = kmem_cache_create("test", 0, nullptr, nullptr);
  EXPECT_EQ(cache, nullptr) << "零大小应该返回nullptr";

  std::cout << "\n测试负数大小..." << std::endl;
  cache = kmem_cache_create("test", static_cast<size_t>(-1), nullptr, nullptr);
  EXPECT_EQ(cache, nullptr) << "负数大小应该返回nullptr";

  std::cout << "\n测试与cache_cache同名..." << std::endl;
  cache = kmem_cache_create("kmem_cache", 64, nullptr, nullptr);
  EXPECT_EQ(cache, nullptr) << "与cache_cache同名应该返回nullptr";

  std::cout << "✓ 参数验证测试完成" << std::endl;
}

/**
 * @brief 测试对象的基本分配和释放
 */
TEST_F(SlabTest, BasicObjectAllocFree) {
  std::cout << "\n=== BasicObjectAllocFree 测试开始 ===" << std::endl;

  // 创建测试cache
  std::cout << "\n创建测试cache..." << std::endl;
  kmem_cache_t* cache = kmem_cache_create("alloc_test", 256, nullptr, nullptr);
  ASSERT_NE(cache, nullptr) << "创建测试cache失败";
  PrintCacheInfo(cache, "初始状态");

  // 分配多个对象
  std::vector<void*> objects;
  const int num_objects = 5;

  std::cout << "\n分配 " << num_objects << " 个对象..." << std::endl;
  for (int i = 0; i < num_objects; ++i) {
    void* obj = kmem_cache_alloc(cache);
    ASSERT_NE(obj, nullptr) << "分配第 " << i << " 个对象失败";
    EXPECT_TRUE(
        ValidateObjectAddress(obj, 256, ("对象" + std::to_string(i)).c_str()));

    // 填充数据并验证
    uint8_t pattern = static_cast<uint8_t>(0x10 + i);
    FillObjectData(obj, 256, pattern);
    EXPECT_TRUE(VerifyObjectData(obj, 256, pattern)) << "对象数据验证失败";

    objects.push_back(obj);
    std::cout << "✓ 对象 " << i << " 分配成功: " << obj << std::endl;
  }

  PrintCacheInfo(cache, "分配后状态");

  // 验证所有对象的数据完整性
  std::cout << "\n验证对象数据完整性..." << std::endl;
  for (int i = 0; i < num_objects; ++i) {
    uint8_t pattern = static_cast<uint8_t>(0x10 + i);
    EXPECT_TRUE(VerifyObjectData(objects[i], 256, pattern))
        << "对象 " << i << " 数据完整性验证失败";
  }
  std::cout << "✓ 所有对象数据完整性验证通过" << std::endl;

  // 释放对象
  std::cout << "\n释放对象..." << std::endl;
  for (int i = 0; i < num_objects; ++i) {
    kmem_cache_free(cache, objects[i]);
    std::cout << "✓ 对象 " << i << " 释放成功" << std::endl;
  }

  PrintCacheInfo(cache, "释放后状态");

  // 清理
  kmem_cache_destroy(cache);
  std::cout << "✓ 对象分配释放测试完成" << std::endl;
}

/**
 * @brief 测试构造函数和析构函数
 */
TEST_F(SlabTest, ConstructorDestructor) {
  std::cout << "\n=== ConstructorDestructor 测试开始 ===" << std::endl;

  // 构造函数：设置魔数
  static int ctor_calls = 0;
  static int dtor_calls = 0;

  auto ctor = [](void* obj) {
    ctor_calls++;
    *static_cast<uint32_t*>(obj) = 0xDEADBEEF;  // 设置魔数
    std::cout << "构造函数调用 #" << ctor_calls << ", 对象: " << obj
              << std::endl;
  };

  // 析构函数：清除魔数
  auto dtor = [](void* obj) {
    dtor_calls++;
    uint32_t magic = *static_cast<uint32_t*>(obj);
    EXPECT_EQ(magic, 0xDEADBEEF) << "析构时魔数不正确";
    *static_cast<uint32_t*>(obj) = 0x00000000;  // 清除魔数
    std::cout << "析构函数调用 #" << dtor_calls << ", 对象: " << obj
              << std::endl;
  };

  // 重置计数器
  ctor_calls = 0;
  dtor_calls = 0;

  std::cout << "\n创建带构造/析构函数的cache..." << std::endl;
  kmem_cache_t* cache =
      kmem_cache_create("ctor_test", sizeof(uint32_t), ctor, dtor);
  ASSERT_NE(cache, nullptr) << "创建cache失败";

  // 分配对象（应该调用构造函数）
  std::cout << "\n分配对象..." << std::endl;
  void* obj1 = kmem_cache_alloc(cache);
  ASSERT_NE(obj1, nullptr) << "分配对象失败";

  // 验证构造函数是否被调用
  uint32_t magic = *static_cast<uint32_t*>(obj1);
  EXPECT_EQ(magic, 0xDEADBEEF) << "构造函数未正确设置魔数";
  std::cout << "✓ 对象魔数验证通过: 0x" << std::hex << magic << std::dec
            << std::endl;

  // 分配更多对象
  void* obj2 = kmem_cache_alloc(cache);
  void* obj3 = kmem_cache_alloc(cache);
  ASSERT_NE(obj2, nullptr) << "分配第二个对象失败";
  ASSERT_NE(obj3, nullptr) << "分配第三个对象失败";

  std::cout << "构造函数调用次数: " << ctor_calls << std::endl;
  EXPECT_GT(ctor_calls, 0) << "构造函数应该被调用";

  // 释放对象（应该调用析构函数）
  std::cout << "\n释放对象..." << std::endl;
  kmem_cache_free(cache, obj1);
  kmem_cache_free(cache, obj2);
  kmem_cache_free(cache, obj3);

  std::cout << "析构函数调用次数: " << dtor_calls << std::endl;
  EXPECT_EQ(dtor_calls, 3) << "析构函数应该被调用3次";

  // 清理
  kmem_cache_destroy(cache);
  std::cout << "✓ 构造/析构函数测试完成" << std::endl;
}

/**
 * @brief 测试kmalloc和kfree
 */
TEST_F(SlabTest, KmallocKfree) {
  std::cout << "\n=== KmallocKfree 测试开始 ===" << std::endl;

  // 测试不同大小的分配
  std::vector<std::pair<void*, size_t>> allocations;
  std::vector<size_t> sizes = {32, 64, 128, 256, 512, 1024, 2048, 4096};

  std::cout << "\n分配不同大小的内存块..." << std::endl;
  for (size_t size : sizes) {
    void* ptr = kmalloc(size);
    ASSERT_NE(ptr, nullptr) << "kmalloc(" << size << ") 失败";
    EXPECT_TRUE(ValidateObjectAddress(
        ptr, size, ("kmalloc(" + std::to_string(size) + ")").c_str()));

    // 填充数据
    uint8_t pattern = static_cast<uint8_t>(size & 0xFF);
    FillObjectData(ptr, size, pattern);

    allocations.emplace_back(ptr, size);
    std::cout << "✓ kmalloc(" << size << ") = " << ptr << std::endl;
  }

  std::cout << "\n验证内存数据完整性..." << std::endl;
  for (const auto& [ptr, size] : allocations) {
    uint8_t pattern = static_cast<uint8_t>(size & 0xFF);
    EXPECT_TRUE(VerifyObjectData(ptr, size, pattern))
        << "kmalloc(" << size << ") 数据完整性验证失败";
  }
  std::cout << "✓ 所有内存块数据完整性验证通过" << std::endl;

  std::cout << "\n释放内存块..." << std::endl;
  for (const auto& [ptr, size] : allocations) {
    kfree(ptr);
    std::cout << "✓ kfree(" << size << " bytes) 完成" << std::endl;
  }

  // 测试边界条件
  std::cout << "\n测试边界条件..." << std::endl;

  // 测试最小大小以下
  void* ptr = kmalloc(16);  // 小于最小支持大小32
  EXPECT_EQ(ptr, nullptr) << "小于最小大小的分配应该失败";

  // 测试最大大小以上
  ptr = kmalloc(200000);  // 大于最大支持大小131072
  EXPECT_EQ(ptr, nullptr) << "大于最大大小的分配应该失败";

  // 测试kfree nullptr
  kfree(nullptr);  // 应该安全处理
  std::cout << "✓ kfree(nullptr) 安全处理" << std::endl;

  std::cout << "✓ kmalloc/kfree测试完成" << std::endl;
}

/**
 * @brief 测试cache收缩功能
 */
TEST_F(SlabTest, CacheShrink) {
  std::cout << "\n=== CacheShrink 测试开始 ===" << std::endl;

  std::cout << "\n创建测试cache..." << std::endl;
  kmem_cache_t* cache = kmem_cache_create("shrink_test", 128, nullptr, nullptr);
  ASSERT_NE(cache, nullptr) << "创建cache失败";
  PrintCacheInfo(cache, "初始状态");

  // 分配大量对象以创建多个slab
  std::vector<void*> objects;
  const int num_objects = 20;

  std::cout << "\n分配 " << num_objects << " 个对象..." << std::endl;
  for (int i = 0; i < num_objects; ++i) {
    void* obj = kmem_cache_alloc(cache);
    ASSERT_NE(obj, nullptr) << "分配对象失败";
    objects.push_back(obj);
  }
  PrintCacheInfo(cache, "分配后状态");

  // 释放所有对象
  std::cout << "\n释放所有对象..." << std::endl;
  for (void* obj : objects) {
    kmem_cache_free(cache, obj);
  }
  PrintCacheInfo(cache, "释放后状态");

  // 收缩cache
  std::cout << "\n收缩cache..." << std::endl;
  int blocks_freed = kmem_cache_shrink(cache);
  std::cout << "收缩释放了 " << blocks_freed << " 个内存块" << std::endl;
  EXPECT_GT(blocks_freed, 0) << "应该释放一些内存块";
  PrintCacheInfo(cache, "收缩后状态");

  // 清理
  kmem_cache_destroy(cache);
  std::cout << "✓ cache收缩测试完成" << std::endl;
}

/**
 * @brief 测试错误处理
 */
TEST_F(SlabTest, ErrorHandling) {
  std::cout << "\n=== ErrorHandling 测试开始 ===" << std::endl;

  std::cout << "\n创建测试cache..." << std::endl;
  kmem_cache_t* cache = kmem_cache_create("error_test", 64, nullptr, nullptr);
  ASSERT_NE(cache, nullptr) << "创建cache失败";

  // 测试释放nullptr
  std::cout << "\n测试释放nullptr..." << std::endl;
  kmem_cache_free(cache, nullptr);  // 应该安全处理
  int error_code = kmem_cache_error(cache);
  std::cout << "错误码: " << error_code << std::endl;

  // 测试释放无效指针
  std::cout << "\n测试释放无效指针..." << std::endl;
  void* invalid_ptr = reinterpret_cast<void*>(0x12345678);
  kmem_cache_free(cache, invalid_ptr);
  error_code = kmem_cache_error(cache);
  std::cout << "错误码: " << error_code << std::endl;

  // 测试给nullptr cache分配对象
  std::cout << "\n测试从nullptr cache分配..." << std::endl;
  void* obj = kmem_cache_alloc(nullptr);
  EXPECT_EQ(obj, nullptr) << "从nullptr cache分配应该失败";

  // 测试错误信息打印
  std::cout << "\n测试错误信息打印..." << std::endl;
  kmem_cache_error(cache);
  kmem_cache_error(nullptr);

  // 清理
  kmem_cache_destroy(cache);
  std::cout << "✓ 错误处理测试完成" << std::endl;
}

/**
 * @brief 压力测试 - 大量分配释放操作
 */
TEST_F(SlabTest, StressTest) {
  std::cout << "\n=== StressTest 测试开始 ===" << std::endl;

  std::cout << "\n创建多个不同大小的cache..." << std::endl;
  std::vector<kmem_cache_t*> caches;
  std::vector<size_t> sizes = {32, 64, 128, 256, 512};

  for (size_t size : sizes) {
    std::string name = "stress_" + std::to_string(size);
    kmem_cache_t* cache =
        kmem_cache_create(name.c_str(), size, nullptr, nullptr);
    ASSERT_NE(cache, nullptr) << "创建cache失败: " << size;
    caches.push_back(cache);
  }

  // 随机分配和释放
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> cache_dist(0, caches.size() - 1);
  std::uniform_int_distribution<> action_dist(0, 1);  // 0=分配, 1=释放

  std::vector<std::vector<void*>> allocated_objects(caches.size());
  const int operations = 1000;

  std::cout << "\n执行 " << operations << " 次随机操作..." << std::endl;
  for (int i = 0; i < operations; ++i) {
    int cache_idx = cache_dist(gen);
    int action = action_dist(gen);

    if (action == 0 || allocated_objects[cache_idx].empty()) {
      // 分配操作
      void* obj = kmem_cache_alloc(caches[cache_idx]);
      if (obj != nullptr) {
        allocated_objects[cache_idx].push_back(obj);
        // 填充数据
        uint8_t pattern = static_cast<uint8_t>(i & 0xFF);
        FillObjectData(obj, sizes[cache_idx], pattern);
      }
    } else {
      // 释放操作
      if (!allocated_objects[cache_idx].empty()) {
        size_t obj_idx = gen() % allocated_objects[cache_idx].size();
        void* obj = allocated_objects[cache_idx][obj_idx];
        kmem_cache_free(caches[cache_idx], obj);
        allocated_objects[cache_idx].erase(
            allocated_objects[cache_idx].begin() + obj_idx);
      }
    }

    if ((i + 1) % 100 == 0) {
      std::cout << "完成 " << (i + 1) << " 次操作" << std::endl;
    }
  }

  // 释放剩余对象
  std::cout << "\n释放剩余对象..." << std::endl;
  for (size_t i = 0; i < caches.size(); ++i) {
    for (void* obj : allocated_objects[i]) {
      kmem_cache_free(caches[i], obj);
    }
    allocated_objects[i].clear();
  }

  // 清理cache
  std::cout << "\n清理cache..." << std::endl;
  for (kmem_cache_t* cache : caches) {
    kmem_cache_destroy(cache);
  }

  std::cout << "✓ 压力测试完成" << std::endl;
}

/**
 * @brief 测试相同参数cache的重用
 */
TEST_F(SlabTest, CacheReuse) {
  std::cout << "\n=== CacheReuse 测试开始 ===" << std::endl;

  // 创建第一个cache
  std::cout << "\n创建第一个cache..." << std::endl;
  kmem_cache_t* cache1 = kmem_cache_create("reuse_test", 128, nullptr, nullptr);
  ASSERT_NE(cache1, nullptr) << "创建第一个cache失败";

  // 尝试创建相同名称和大小的cache（应该返回同一个）
  std::cout << "\n创建相同参数的cache..." << std::endl;
  kmem_cache_t* cache2 = kmem_cache_create("reuse_test", 128, nullptr, nullptr);
  ASSERT_NE(cache2, nullptr) << "创建第二个cache失败";

  // 应该是同一个cache对象
  EXPECT_EQ(cache1, cache2) << "相同参数的cache应该重用同一个对象";
  std::cout << "✓ cache重用验证通过: " << cache1 << " == " << cache2
            << std::endl;

  // 创建不同大小的cache（应该是新的）
  std::cout << "\n创建不同大小的cache..." << std::endl;
  kmem_cache_t* cache3 = kmem_cache_create("reuse_test", 256, nullptr, nullptr);
  ASSERT_NE(cache3, nullptr) << "创建不同大小cache失败";
  EXPECT_NE(cache1, cache3) << "不同大小的cache应该是不同对象";
  std::cout << "✓ 不同参数cache验证通过: " << cache1 << " != " << cache3
            << std::endl;

  // 清理（注意：由于cache1和cache2是同一个，只需销毁一次）
  kmem_cache_destroy(cache1);  // 这会销毁cache1/cache2
  kmem_cache_destroy(cache3);

  std::cout << "✓ cache重用测试完成" << std::endl;
}

}  // namespace bmalloc
