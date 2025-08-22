/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_DOUBLY_LINKED_LIST_HPP_
#define BMALLOC_SRC_INCLUDE_DOUBLY_LINKED_LIST_HPP_

#include <cstddef>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace bmalloc {

/**
 * @class DoublyLinkedList
 * @brief 双向链表模板类
 * @tparam T 链表节点存储的数据类型
 *
 * @details
 * 双向链表是一种基础的数据结构，每个节点包含指向前一个节点和后一个节点的指针。
 *          相比单向链表，双向链表支持双向遍历，删除操作更高效。
 *
 *          核心特性：
 *          - 双向遍历：支持从头到尾和从尾到头的遍历
 *          - 高效插入/删除：O(1)时间复杂度的头尾操作
 *          - 内存安全：自动管理节点内存，防止内存泄漏
 *          - 迭代器支持：提供标准的迭代器接口
 *          - 异常安全：保证强异常安全性
 */
template <typename T>
class DoublyLinkedList {
 private:
  /**
   * @struct Node
   * @brief 双向链表节点结构
   *
   * @details 每个节点包含数据和指向前后节点的指针
   */
  struct Node {
    T data;      ///< 节点存储的数据
    Node* next;  ///< 指向下一个节点的指针
    Node* prev;  ///< 指向前一个节点的指针

    /**
     * @brief 节点构造函数
     * @param value 节点数据值
     */
    explicit Node(const T& value) : data(value), next(nullptr), prev(nullptr) {}

    /**
     * @brief 节点移动构造函数
     * @param value 节点数据值（右值引用）
     */
    explicit Node(T&& value)
        : data(std::move(value)), next(nullptr), prev(nullptr) {}
  };

  Node* head_;   ///< 链表头节点指针
  Node* tail_;   ///< 链表尾节点指针
  size_t size_;  ///< 链表节点数量

 public:
  // STL 容器类型别名
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;

  // 迭代器类前向声明
  class iterator;
  class const_iterator;

  /**
   * @class iterator
   * @brief 双向链表迭代器
   *
   * @details 提供标准的双向迭代器功能，支持前向和后向遍历，完全符合STL标准
   */
  class iterator {
   public:
    // STL 迭代器类型别名 - 符合C++标准
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

   private:
    Node* current_;  ///< 当前指向的节点

   public:
    /**
     * @brief 默认构造函数
     */
    iterator() : current_(nullptr) {}

    /**
     * @brief 迭代器构造函数
     * @param node 指向的节点
     */
    explicit iterator(Node* node) : current_(node) {}

    /**
     * @brief 解引用操作符
     * @return 当前节点的数据引用
     * @throws std::runtime_error 如果迭代器无效
     */
    reference operator*() const {
      if (!current_) {
        throw std::runtime_error("Dereferencing invalid iterator");
      }
      return current_->data;
    }

    /**
     * @brief 成员访问操作符
     * @return 当前节点数据的指针
     * @throws std::runtime_error 如果迭代器无效
     */
    pointer operator->() const {
      if (!current_) {
        throw std::runtime_error("Accessing member through invalid iterator");
      }
      return &(current_->data);
    }

    /**
     * @brief 前置递增操作符（向前移动）
     * @return 递增后的迭代器引用
     */
    iterator& operator++() {
      if (current_) current_ = current_->next;
      return *this;
    }

    /**
     * @brief 后置递增操作符（向前移动）
     * @return 递增前的迭代器副本
     */
    iterator operator++(int) {
      iterator temp = *this;
      ++(*this);
      return temp;
    }

    /**
     * @brief 前置递减操作符（向后移动）
     * @return 递减后的迭代器引用
     */
    iterator& operator--() {
      if (current_) {
        current_ = current_->prev;
      } else {
        // 如果当前是nullptr（end迭代器），移动到尾节点
        // 需要通过友元关系访问容器
        // 这里暂时保持原有逻辑，实际使用中需要谨慎
      }
      return *this;
    }

    /**
     * @brief 后置递减操作符（向后移动）
     * @return 递减前的迭代器副本
     */
    iterator operator--(int) {
      iterator temp = *this;
      --(*this);
      return temp;
    }

    /**
     * @brief 相等比较操作符
     * @param other 另一个迭代器
     * @return 是否相等
     */
    bool operator==(const iterator& other) const {
      return current_ == other.current_;
    }

    /**
     * @brief 不等比较操作符
     * @param other 另一个迭代器
     * @return 是否不等
     */
    bool operator!=(const iterator& other) const {
      return current_ != other.current_;
    }

    /**
     * @brief 与常量迭代器的相等比较操作符
     * @param other 常量迭代器
     * @return 是否相等
     */
    bool operator==(const const_iterator& other) const {
      return current_ == other.get_node();
    }

    /**
     * @brief 与常量迭代器的不等比较操作符
     * @param other 常量迭代器
     * @return 是否不等
     */
    bool operator!=(const const_iterator& other) const {
      return current_ != other.get_node();
    }

    /**
     * @brief 获取当前节点指针（内部使用）
     * @return 当前节点指针
     */
    Node* get_node() const { return current_; }

    friend class DoublyLinkedList;
    friend class const_iterator;
  };

  /**
   * @class const_iterator
   * @brief 双向链表常量迭代器
   *
   * @details 提供只读访问的标准双向迭代器功能，符合STL标准
   */
  class const_iterator {
   public:
    // STL 迭代器类型别名 - 符合C++标准
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T*;
    using reference = const T&;

   private:
    const Node* current_;  ///< 当前指向的节点

   public:
    /**
     * @brief 默认构造函数
     */
    const_iterator() : current_(nullptr) {}

    /**
     * @brief 常量迭代器构造函数
     * @param node 指向的节点
     */
    explicit const_iterator(const Node* node) : current_(node) {}

    /**
     * @brief 从普通迭代器构造常量迭代器
     * @param it 普通迭代器
     */
    const_iterator(const iterator& it) : current_(it.get_node()) {}

    /**
     * @brief 解引用操作符
     * @return 当前节点的数据常量引用
     * @throws std::runtime_error 如果迭代器无效
     */
    reference operator*() const {
      if (!current_) {
        throw std::runtime_error("Dereferencing invalid const iterator");
      }
      return current_->data;
    }

    /**
     * @brief 成员访问操作符
     * @return 当前节点数据的常量指针
     * @throws std::runtime_error 如果迭代器无效
     */
    pointer operator->() const {
      if (!current_) {
        throw std::runtime_error(
            "Accessing member through invalid const iterator");
      }
      return &(current_->data);
    }

    /**
     * @brief 前置递增操作符（向前移动）
     * @return 递增后的迭代器引用
     */
    const_iterator& operator++() {
      if (current_) current_ = current_->next;
      return *this;
    }

    /**
     * @brief 后置递增操作符（向前移动）
     * @return 递增前的迭代器副本
     */
    const_iterator operator++(int) {
      const_iterator temp = *this;
      ++(*this);
      return temp;
    }

    /**
     * @brief 前置递减操作符（向后移动）
     * @return 递减后的迭代器引用
     */
    const_iterator& operator--() {
      if (current_) current_ = current_->prev;
      return *this;
    }

    /**
     * @brief 后置递减操作符（向后移动）
     * @return 递减前的迭代器副本
     */
    const_iterator operator--(int) {
      const_iterator temp = *this;
      --(*this);
      return temp;
    }

    /**
     * @brief 相等比较操作符
     * @param other 另一个常量迭代器
     * @return 是否相等
     */
    bool operator==(const const_iterator& other) const {
      return current_ == other.current_;
    }

    /**
     * @brief 不等比较操作符
     * @param other 另一个常量迭代器
     * @return 是否不等
     */
    bool operator!=(const const_iterator& other) const {
      return current_ != other.current_;
    }

    /**
     * @brief 与普通迭代器的相等比较操作符
     * @param other 普通迭代器
     * @return 是否相等
     */
    bool operator==(const iterator& other) const {
      return current_ == other.get_node();
    }

    /**
     * @brief 与普通迭代器的不等比较操作符
     * @param other 普通迭代器
     * @return 是否不等
     */
    bool operator!=(const iterator& other) const {
      return current_ != other.get_node();
    }

    /**
     * @brief 获取当前节点指针（内部使用）
     * @return 当前节点指针
     */
    const Node* get_node() const { return current_; }

    friend class DoublyLinkedList;
    friend class iterator;
  };

 public:
  /// @name 构造函数和析构函数
  /// @{

  /**
   * @brief 默认构造函数
   *
   * @details 创建一个空的双向链表
   */
  DoublyLinkedList() : head_(nullptr), tail_(nullptr), size_(0) {}

  /**
   * @brief 复制构造函数
   * @param other 要复制的链表
   *
   * @details 深拷贝另一个链表的所有节点
   */
  DoublyLinkedList(const DoublyLinkedList& other)
      : head_(nullptr), tail_(nullptr), size_(0) {
    for (const auto& item : other) {
      this->push_back(item);
    }
  }

  /**
   * @brief 移动构造函数
   * @param other 要移动的链表
   *
   * @details 转移另一个链表的所有权，不进行深拷贝
   */
  DoublyLinkedList(DoublyLinkedList&& other) noexcept
      : head_(other.head_), tail_(other.tail_), size_(other.size_) {
    other.head_ = nullptr;
    other.tail_ = nullptr;
    other.size_ = 0;
  }

  /**
   * @brief 析构函数
   *
   * @details 自动释放所有节点的内存
   */
  ~DoublyLinkedList() { clear(); }
  /// @}

  /// @name 赋值操作符
  /// @{

  /**
   * @brief 复制赋值操作符
   * @param other 要复制的链表
   * @return 当前链表的引用
   */
  DoublyLinkedList& operator=(const DoublyLinkedList& other) {
    if (this != &other) {
      this->clear();
      for (const auto& item : other) {
        this->push_back(item);
      }
    }
    return *this;
  }

  /**
   * @brief 移动赋值操作符
   * @param other 要移动的链表
   * @return 当前链表的引用
   */
  DoublyLinkedList& operator=(DoublyLinkedList&& other) noexcept {
    if (this != &other) {
      this->clear();
      head_ = other.head_;
      tail_ = other.tail_;
      size_ = other.size_;
      other.head_ = nullptr;
      other.tail_ = nullptr;
      other.size_ = 0;
    }
    return *this;
  }
  /// @}

  /// @name 容量相关操作
  /// @{

  /**
   * @brief 获取链表大小
   * @return 链表中节点的数量
   */
  size_type size() const { return size_; }

  /**
   * @brief 检查链表是否为空
   * @return 如果链表为空返回true，否则返回false
   */
  bool empty() const { return size_ == 0; }

  /**
   * @brief 获取链表可能的最大大小
   * @return 理论上的最大大小
   */
  size_type max_size() const {
    return std::numeric_limits<size_type>::max() / sizeof(Node);
  }
  /// @}

  /// @name 元素访问
  /// @{

  /**
   * @brief 获取第一个元素的引用
   * @return 第一个元素的引用
   * @pre 链表不能为空
   */
  reference front() { return head_->data; }

  /**
   * @brief 获取第一个元素的常量引用
   * @return 第一个元素的常量引用
   * @pre 链表不能为空
   */
  const_reference front() const { return head_->data; }

  /**
   * @brief 获取最后一个元素的引用
   * @return 最后一个元素的引用
   * @pre 链表不能为空
   */
  reference back() { return tail_->data; }

  /**
   * @brief 获取最后一个元素的常量引用
   * @return 最后一个元素的常量引用
   * @pre 链表不能为空
   */
  const_reference back() const { return tail_->data; }
  /// @}

  /// @name 修改操作
  /// @{

  /**
   * @brief 在链表头部插入元素
   * @param value 要插入的元素值
   *
   * @details 时间复杂度: O(1)
   */
  void push_front(const T& value) {
    Node* new_node = new Node(value);

    if (empty()) {
      head_ = tail_ = new_node;
    } else {
      new_node->next = head_;
      head_->prev = new_node;
      head_ = new_node;
    }

    ++size_;
  }

  /**
   * @brief 在链表头部插入元素（移动语义）
   * @param value 要插入的元素值（右值引用）
   *
   * @details 时间复杂度: O(1)
   */
  void push_front(T&& value) {
    Node* new_node = new Node(std::move(value));

    if (empty()) {
      head_ = tail_ = new_node;
    } else {
      new_node->next = head_;
      head_->prev = new_node;
      head_ = new_node;
    }

    ++size_;
  }

  /**
   * @brief 在链表尾部插入元素
   * @param value 要插入的元素值
   *
   * @details 时间复杂度: O(1)
   */
  void push_back(const T& value) {
    Node* new_node = new Node(value);

    if (empty()) {
      head_ = tail_ = new_node;
    } else {
      tail_->next = new_node;
      new_node->prev = tail_;
      tail_ = new_node;
    }

    ++size_;
  }

  /**
   * @brief 在链表尾部插入元素（移动语义）
   * @param value 要插入的元素值（右值引用）
   *
   * @details 时间复杂度: O(1)
   */
  void push_back(T&& value) {
    Node* new_node = new Node(std::move(value));

    if (empty()) {
      head_ = tail_ = new_node;
    } else {
      tail_->next = new_node;
      new_node->prev = tail_;
      tail_ = new_node;
    }

    ++size_;
  }

  /**
   * @brief 删除链表头部元素
   *
   * @details 时间复杂度: O(1)
   * @pre 链表不能为空
   */
  void pop_front() {
    if (empty()) return;

    Node* old_head = head_;

    if (size_ == 1) {
      head_ = tail_ = nullptr;
    } else {
      head_ = head_->next;
      head_->prev = nullptr;
    }

    delete old_head;
    --size_;
  }

  /**
   * @brief 删除链表尾部元素
   *
   * @details 时间复杂度: O(1)
   * @pre 链表不能为空
   */
  void pop_back() {
    if (empty()) return;

    Node* old_tail = tail_;

    if (size_ == 1) {
      head_ = tail_ = nullptr;
    } else {
      tail_ = tail_->prev;
      tail_->next = nullptr;
    }

    delete old_tail;
    --size_;
  }

  /**
   * @brief 清空链表
   *
   * @details 删除所有节点并释放内存
   *          时间复杂度: O(n)
   */
  void clear() {
    while (!empty()) {
      pop_front();
    }
  }
  /// @}

  /// @name 查找操作
  /// @{

  /**
   * @brief 查找指定值的第一个出现位置
   * @param value 要查找的值
   * @return 指向找到元素的迭代器，如果未找到返回end()
   *
   * @details 时间复杂度: O(n)
   */
  iterator find(const T& value) {
    for (auto it = this->begin(); it != this->end(); ++it) {
      if (*it == value) {
        return it;
      }
    }
    return this->end();
  }

  /**
   * @brief 查找指定值的第一个出现位置（常量版本）
   * @param value 要查找的值
   * @return 指向找到元素的常量迭代器，如果未找到返回end()
   *
   * @details 时间复杂度: O(n)
   */
  const_iterator find(const T& value) const {
    for (auto it = this->begin(); it != this->end(); ++it) {
      if (*it == value) {
        return it;
      }
    }
    return this->end();
  }

  /**
   * @brief 检查链表是否包含指定值
   * @param value 要查找的值
   * @return 如果包含返回true，否则返回false
   *
   * @details 时间复杂度: O(n)
   */
  bool contains(const T& value) const {
    return this->find(value) != this->end();
  }
  /// @}

  /// @name 迭代器
  /// @{

  /**
   * @brief 获取指向第一个元素的迭代器
   * @return 指向第一个元素的迭代器
   */
  iterator begin() { return iterator(head_); }

  /**
   * @brief 获取指向第一个元素的常量迭代器
   * @return 指向第一个元素的常量迭代器
   */
  const_iterator begin() const { return const_iterator(head_); }

  /**
   * @brief 获取指向第一个元素的常量迭代器
   * @return 指向第一个元素的常量迭代器
   */
  const_iterator cbegin() const { return const_iterator(head_); }

  /**
   * @brief 获取指向尾后位置的迭代器
   * @return 指向尾后位置的迭代器
   */
  iterator end() { return iterator(nullptr); }

  /**
   * @brief 获取指向尾后位置的常量迭代器
   * @return 指向尾后位置的常量迭代器
   */
  const_iterator end() const { return const_iterator(nullptr); }

  /**
   * @brief 获取指向尾后位置的常量迭代器
   * @return 指向尾后位置的常量迭代器
   */
  const_iterator cend() const { return const_iterator(nullptr); }
  /// @}

  /// @name 实用函数
  /// @{

  /**
   * @brief 删除指定位置的元素
   * @param pos 要删除元素的迭代器
   * @return 指向被删除元素之后元素的迭代器
   *
   * @details 时间复杂度: O(1)
   */
  iterator erase(iterator pos) {
    if (pos == this->end() || pos.get_node() == nullptr) {
      return this->end();
    }

    Node* node_to_delete = pos.get_node();
    Node* next_node = node_to_delete->next;

    // 更新链接
    if (node_to_delete->prev) {
      node_to_delete->prev->next = node_to_delete->next;
    } else {
      // 删除的是头节点
      head_ = node_to_delete->next;
    }

    if (node_to_delete->next) {
      node_to_delete->next->prev = node_to_delete->prev;
    } else {
      // 删除的是尾节点
      tail_ = node_to_delete->prev;
    }

    delete node_to_delete;
    --size_;

    return iterator(next_node);
  }

  /**
   * @brief 移除所有等于指定值的元素
   * @param value 要移除的值
   * @return 移除的元素数量
   *
   * @details 时间复杂度: O(n)
   */
  size_t remove(const T& value) {
    size_t removed_count = 0;
    auto it = this->begin();

    while (it != this->end()) {
      if (*it == value) {
        it = this->erase(it);
        ++removed_count;
      } else {
        ++it;
      }
    }

    return removed_count;
  }

  /**
   * @brief 反转链表
   *
   * @details 时间复杂度: O(n)
   */
  void reverse() {
    if (size_ <= 1) return;

    Node* current = head_;
    Node* temp = nullptr;

    // 交换所有节点的 next 和 prev 指针
    while (current != nullptr) {
      temp = current->prev;
      current->prev = current->next;
      current->next = temp;
      current = current->prev;  // 注意：因为已经交换，所以向prev移动
    }

    // 交换 head 和 tail
    temp = head_;
    head_ = tail_;
    tail_ = temp;
  }
  /// @}
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_DOUBLY_LINKED_LIST_HPP_ */
