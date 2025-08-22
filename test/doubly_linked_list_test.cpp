/**
 * Copyright The bmalloc Contributors
 * @file doubly_linked_list_test.cpp
 * @brief DoublyLinkedList的Google Test测试用例
 */

#include "doubly_linked_list.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace bmalloc;

// 测试夹具
class DoublyLinkedListTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 测试前的初始化
  }

  void TearDown() override {
    // 测试后的清理
  }

  DoublyLinkedList<int> int_list;
  DoublyLinkedList<std::string> string_list;
};

// 基本构造和析构测试
TEST_F(DoublyLinkedListTest, DefaultConstructor) {
  EXPECT_TRUE(int_list.empty());
  EXPECT_EQ(int_list.size(), 0);
  EXPECT_EQ(int_list.begin(), int_list.end());
}

// 测试 push_back 和 push_front
TEST_F(DoublyLinkedListTest, PushOperations) {
  // 测试 push_back
  int_list.push_back(1);
  EXPECT_FALSE(int_list.empty());
  EXPECT_EQ(int_list.size(), 1);
  EXPECT_EQ(int_list.front(), 1);
  EXPECT_EQ(int_list.back(), 1);

  int_list.push_back(2);
  EXPECT_EQ(int_list.size(), 2);
  EXPECT_EQ(int_list.front(), 1);
  EXPECT_EQ(int_list.back(), 2);

  int_list.push_back(3);
  EXPECT_EQ(int_list.size(), 3);
  EXPECT_EQ(int_list.front(), 1);
  EXPECT_EQ(int_list.back(), 3);

  // 测试 push_front
  int_list.push_front(0);
  EXPECT_EQ(int_list.size(), 4);
  EXPECT_EQ(int_list.front(), 0);
  EXPECT_EQ(int_list.back(), 3);

  int_list.push_front(-1);
  EXPECT_EQ(int_list.size(), 5);
  EXPECT_EQ(int_list.front(), -1);
  EXPECT_EQ(int_list.back(), 3);
}

// 测试移动语义的 push 操作
TEST_F(DoublyLinkedListTest, PushMoveOperations) {
  std::string s1 = "hello";
  std::string s2 = "world";

  string_list.push_back(std::move(s1));
  EXPECT_EQ(string_list.size(), 1);
  EXPECT_EQ(string_list.front(), "hello");

  string_list.push_front(std::move(s2));
  EXPECT_EQ(string_list.size(), 2);
  EXPECT_EQ(string_list.front(), "world");
  EXPECT_EQ(string_list.back(), "hello");
}

// 测试 pop 操作
TEST_F(DoublyLinkedListTest, PopOperations) {
  // 先添加一些元素
  int_list.push_back(1);
  int_list.push_back(2);
  int_list.push_back(3);
  EXPECT_EQ(int_list.size(), 3);

  // 测试 pop_back
  int_list.pop_back();
  EXPECT_EQ(int_list.size(), 2);
  EXPECT_EQ(int_list.back(), 2);

  // 测试 pop_front
  int_list.pop_front();
  EXPECT_EQ(int_list.size(), 1);
  EXPECT_EQ(int_list.front(), 2);
  EXPECT_EQ(int_list.back(), 2);

  // 删除最后一个元素
  int_list.pop_back();
  EXPECT_TRUE(int_list.empty());
  EXPECT_EQ(int_list.size(), 0);

  // 测试空列表的 pop 操作（应该不会崩溃）
  int_list.pop_front();
  int_list.pop_back();
  EXPECT_TRUE(int_list.empty());
}

// 测试迭代器
TEST_F(DoublyLinkedListTest, Iterators) {
  // 添加元素
  for (int i = 1; i <= 5; ++i) {
    int_list.push_back(i);
  }

  // 测试正向迭代
  int expected = 1;
  for (auto it = int_list.begin(); it != int_list.end(); ++it) {
    EXPECT_EQ(*it, expected++);
  }

  // 测试范围for循环
  expected = 1;
  for (const auto& value : int_list) {
    EXPECT_EQ(value, expected++);
  }

  // 测试常量迭代器
  const auto& const_list = int_list;
  expected = 1;
  for (auto it = const_list.begin(); it != const_list.end(); ++it) {
    EXPECT_EQ(*it, expected++);
  }

  // 测试迭代器递减
  auto it = int_list.begin();
  ++it;
  ++it;
  ++it;
  ++it;  // 移动到最后一个元素
  EXPECT_EQ(*it, 5);
  --it;
  EXPECT_EQ(*it, 4);
}

// 测试迭代器类型特征
TEST_F(DoublyLinkedListTest, IteratorTraits) {
  using iterator = DoublyLinkedList<int>::iterator;
  using const_iterator = DoublyLinkedList<int>::const_iterator;

  // 检查迭代器类别
  static_assert(std::is_same_v<iterator::iterator_category,
                               std::bidirectional_iterator_tag>);
  static_assert(std::is_same_v<const_iterator::iterator_category,
                               std::bidirectional_iterator_tag>);

  // 检查值类型
  static_assert(std::is_same_v<iterator::value_type, int>);
  static_assert(std::is_same_v<const_iterator::value_type, int>);
}

// 测试迭代器无效访问
TEST_F(DoublyLinkedListTest, InvalidIteratorAccess) {
  auto it = int_list.begin();  // 空列表的迭代器
  EXPECT_THROW(*it, std::runtime_error);
  EXPECT_THROW(it.operator->(), std::runtime_error);

  auto const_it = int_list.cbegin();
  EXPECT_THROW(*const_it, std::runtime_error);
  EXPECT_THROW(const_it.operator->(), std::runtime_error);
}

// 测试迭代器比较
TEST_F(DoublyLinkedListTest, IteratorComparison) {
  int_list.push_back(1);
  int_list.push_back(2);

  auto it1 = int_list.begin();
  auto it2 = int_list.begin();
  auto it3 = ++int_list.begin();

  // 相等测试
  EXPECT_TRUE(it1 == it2);
  EXPECT_FALSE(it1 != it2);

  // 不等测试
  EXPECT_FALSE(it1 == it3);
  EXPECT_TRUE(it1 != it3);

  // 与const_iterator的比较
  auto const_it = int_list.cbegin();
  EXPECT_TRUE(it1 == const_it);
  EXPECT_FALSE(it1 != const_it);
}

// 测试复制构造函数
TEST_F(DoublyLinkedListTest, CopyConstructor) {
  // 添加元素到原始列表
  for (int i = 1; i <= 3; ++i) {
    int_list.push_back(i);
  }

  // 复制构造
  DoublyLinkedList<int> copied_list(int_list);

  EXPECT_EQ(copied_list.size(), int_list.size());
  EXPECT_EQ(copied_list.front(), int_list.front());
  EXPECT_EQ(copied_list.back(), int_list.back());

  // 验证两个列表内容相同但独立
  auto it1 = int_list.begin();
  auto it2 = copied_list.begin();
  while (it1 != int_list.end() && it2 != copied_list.end()) {
    EXPECT_EQ(*it1, *it2);
    ++it1;
    ++it2;
  }

  // 修改原始列表不应影响复制的列表
  int_list.push_back(4);
  EXPECT_NE(copied_list.size(), int_list.size());
}

// 测试移动构造函数
TEST_F(DoublyLinkedListTest, MoveConstructor) {
  // 添加元素
  for (int i = 1; i <= 3; ++i) {
    int_list.push_back(i);
  }

  size_t original_size = int_list.size();
  int original_front = int_list.front();
  int original_back = int_list.back();

  // 移动构造
  DoublyLinkedList<int> moved_list(std::move(int_list));

  EXPECT_EQ(moved_list.size(), original_size);
  EXPECT_EQ(moved_list.front(), original_front);
  EXPECT_EQ(moved_list.back(), original_back);

  // 原始列表应该为空
  EXPECT_TRUE(int_list.empty());
  EXPECT_EQ(int_list.size(), 0);
}

// 测试复制赋值操作符
TEST_F(DoublyLinkedListTest, CopyAssignment) {
  // 添加元素到原始列表
  for (int i = 1; i <= 3; ++i) {
    int_list.push_back(i);
  }

  DoublyLinkedList<int> assigned_list;
  assigned_list.push_back(99);  // 先添加一个元素

  // 复制赋值
  assigned_list = int_list;

  EXPECT_EQ(assigned_list.size(), int_list.size());
  EXPECT_EQ(assigned_list.front(), int_list.front());
  EXPECT_EQ(assigned_list.back(), int_list.back());

  // 验证内容相同
  auto it1 = int_list.begin();
  auto it2 = assigned_list.begin();
  while (it1 != int_list.end() && it2 != assigned_list.end()) {
    EXPECT_EQ(*it1, *it2);
    ++it1;
    ++it2;
  }

  // 测试自赋值
  assigned_list = assigned_list;
  EXPECT_EQ(assigned_list.size(), 3);
}

// 测试移动赋值操作符
TEST_F(DoublyLinkedListTest, MoveAssignment) {
  // 添加元素
  for (int i = 1; i <= 3; ++i) {
    int_list.push_back(i);
  }

  size_t original_size = int_list.size();
  int original_front = int_list.front();
  int original_back = int_list.back();

  DoublyLinkedList<int> assigned_list;
  assigned_list.push_back(99);  // 先添加一个元素

  // 移动赋值
  assigned_list = std::move(int_list);

  EXPECT_EQ(assigned_list.size(), original_size);
  EXPECT_EQ(assigned_list.front(), original_front);
  EXPECT_EQ(assigned_list.back(), original_back);

  // 原始列表应该为空
  EXPECT_TRUE(int_list.empty());
  EXPECT_EQ(int_list.size(), 0);
}

// 测试 clear 操作
TEST_F(DoublyLinkedListTest, Clear) {
  // 添加元素
  for (int i = 1; i <= 5; ++i) {
    int_list.push_back(i);
  }
  EXPECT_FALSE(int_list.empty());

  // 清空
  int_list.clear();
  EXPECT_TRUE(int_list.empty());
  EXPECT_EQ(int_list.size(), 0);
  EXPECT_EQ(int_list.begin(), int_list.end());
}

// 测试 find 操作
TEST_F(DoublyLinkedListTest, FindOperations) {
  // 添加元素
  for (int i = 1; i <= 5; ++i) {
    int_list.push_back(i);
  }

  // 测试找到元素
  auto it = int_list.find(3);
  EXPECT_NE(it, int_list.end());
  EXPECT_EQ(*it, 3);

  // 测试找不到元素
  auto not_found = int_list.find(10);
  EXPECT_EQ(not_found, int_list.end());

  // 测试常量版本
  const auto& const_list = int_list;
  auto const_it = const_list.find(2);
  EXPECT_NE(const_it, const_list.end());
  EXPECT_EQ(*const_it, 2);
}

// 测试 contains 操作
TEST_F(DoublyLinkedListTest, Contains) {
  // 添加元素
  for (int i = 1; i <= 5; ++i) {
    int_list.push_back(i);
  }

  EXPECT_TRUE(int_list.contains(1));
  EXPECT_TRUE(int_list.contains(3));
  EXPECT_TRUE(int_list.contains(5));
  EXPECT_FALSE(int_list.contains(0));
  EXPECT_FALSE(int_list.contains(10));
  EXPECT_FALSE(int_list.contains(-1));
}

// 测试 remove 操作
TEST_F(DoublyLinkedListTest, Remove) {
  // 添加元素，包括重复元素
  int_list.push_back(1);
  int_list.push_back(2);
  int_list.push_back(3);
  int_list.push_back(2);
  int_list.push_back(4);
  int_list.push_back(2);

  EXPECT_EQ(int_list.size(), 6);

  // 移除所有的2
  size_t removed = int_list.remove(2);
  EXPECT_EQ(removed, 3);
  EXPECT_EQ(int_list.size(), 3);

  // 验证2已被完全移除
  EXPECT_FALSE(int_list.contains(2));

  // 验证其他元素仍然存在
  EXPECT_TRUE(int_list.contains(1));
  EXPECT_TRUE(int_list.contains(3));
  EXPECT_TRUE(int_list.contains(4));

  // 移除不存在的元素
  size_t not_removed = int_list.remove(10);
  EXPECT_EQ(not_removed, 0);
  EXPECT_EQ(int_list.size(), 3);
}

// 测试 erase 操作
TEST_F(DoublyLinkedListTest, Erase) {
  // 添加元素
  for (int i = 1; i <= 5; ++i) {
    int_list.push_back(i);
  }

  // 删除第二个元素
  auto it = int_list.begin();
  ++it;  // 指向第二个元素 (2)
  auto next_it = int_list.erase(it);

  EXPECT_EQ(int_list.size(), 4);
  EXPECT_EQ(*next_it, 3);  // 应该指向被删除元素的下一个

  // 验证元素顺序
  std::vector<int> expected = {1, 3, 4, 5};
  std::vector<int> actual;
  for (const auto& value : int_list) {
    actual.push_back(value);
  }
  EXPECT_EQ(actual, expected);
}

// 测试 Reverse 操作
TEST_F(DoublyLinkedListTest, Reverse) {
  // 测试空列表反转
  int_list.reverse();
  EXPECT_TRUE(int_list.empty());

  // 测试单元素列表反转
  int_list.push_back(1);
  int_list.reverse();
  EXPECT_EQ(int_list.size(), 1);
  EXPECT_EQ(int_list.front(), 1);

  // 测试多元素列表反转
  int_list.clear();
  for (int i = 1; i <= 5; ++i) {
    int_list.push_back(i);
  }

  int_list.reverse();

  // 验证反转后的顺序
  std::vector<int> expected = {5, 4, 3, 2, 1};
  std::vector<int> actual;
  for (const auto& value : int_list) {
    actual.push_back(value);
  }

  EXPECT_EQ(actual, expected);
  EXPECT_EQ(int_list.front(), 5);
  EXPECT_EQ(int_list.back(), 1);
}

// 测试 max_size
TEST_F(DoublyLinkedListTest, MaxSize) {
  EXPECT_GT(int_list.max_size(), 0);
  EXPECT_GT(int_list.max_size(), 1000);  // 应该是一个很大的数
}

// 测试STL算法兼容性
TEST_F(DoublyLinkedListTest, STLAlgorithmCompatibility) {
  // 添加元素
  for (int i = 1; i <= 5; ++i) {
    int_list.push_back(i);
  }

  // 测试 std::find
  auto it = std::find(int_list.begin(), int_list.end(), 3);
  EXPECT_NE(it, int_list.end());
  EXPECT_EQ(*it, 3);

  // 测试 std::count
  int_list.push_back(3);  // 添加重复元素
  auto count = std::count(int_list.begin(), int_list.end(), 3);
  EXPECT_EQ(count, 2);

  // 测试 std::distance
  auto distance = std::distance(int_list.begin(), int_list.end());
  EXPECT_EQ(distance, static_cast<std::ptrdiff_t>(int_list.size()));
}

// 性能测试（简单版本）
TEST_F(DoublyLinkedListTest, PerformanceTest) {
  const size_t large_size = 10000;

  // 测试大量插入操作
  auto start = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < large_size; ++i) {
    int_list.push_back(static_cast<int>(i));
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  EXPECT_EQ(int_list.size(), large_size);
  EXPECT_LT(duration.count(), 1000);  // 应该在1秒内完成

  // 测试遍历
  start = std::chrono::high_resolution_clock::now();

  size_t count = 0;
  for (const auto& value : int_list) {
    count++;
    (void)value;  // 避免未使用变量警告
  }

  end = std::chrono::high_resolution_clock::now();
  duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  EXPECT_EQ(count, large_size);
  EXPECT_LT(duration.count(), 100);  // 遍历应该很快
}

// 测试异常安全性
TEST_F(DoublyLinkedListTest, ExceptionSafety) {
  auto it = int_list.begin();
  EXPECT_THROW(*it, std::runtime_error);
  EXPECT_THROW(it.operator->(), std::runtime_error);

  auto const_it = int_list.cbegin();
  EXPECT_THROW(*const_it, std::runtime_error);
  EXPECT_THROW(const_it.operator->(), std::runtime_error);
}

// 测试字符串类型
TEST_F(DoublyLinkedListTest, StringType) {
  string_list.push_back("hello");
  string_list.push_back("world");
  string_list.push_front("hi");

  EXPECT_EQ(string_list.size(), 3);
  EXPECT_EQ(string_list.front(), "hi");
  EXPECT_EQ(string_list.back(), "world");

  // 测试查找
  auto it = string_list.find("world");
  EXPECT_NE(it, string_list.end());
  EXPECT_EQ(*it, "world");

  // 测试包含
  EXPECT_TRUE(string_list.contains("hello"));
  EXPECT_FALSE(string_list.contains("goodbye"));
}
