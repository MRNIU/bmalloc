/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_DOUBLY_LINKED_LIST_HPP_
#define BMALLOC_SRC_INCLUDE_DOUBLY_LINKED_LIST_HPP_

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>
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

    /**
     * @brief 节点原地构造函数
     * @tparam Args 构造参数类型
     * @param args 构造参数
     */
    template <typename... Args>
    explicit Node(Args&&... args)
        : data(std::forward<Args>(args)...), next(nullptr), prev(nullptr) {}
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
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

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
    const DoublyLinkedList*
        container_;  ///< 指向容器的指针，用于处理end()迭代器

   public:
    /**
     * @brief 默认构造函数
     */
    iterator() : current_(nullptr), container_(nullptr) {}

    /**
     * @brief 迭代器构造函数
     * @param node 指向的节点
     * @param container 指向容器的指针
     */
    explicit iterator(Node* node, const DoublyLinkedList* container = nullptr)
        : current_(node), container_(container) {}

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
      } else if (container_ && container_->tail_) {
        // 如果当前是nullptr（end迭代器），移动到尾节点
        current_ = container_->tail_;
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

    /**
     * @brief 获取容器指针（内部使用）
     * @return 容器指针
     */
    const DoublyLinkedList* get_container() const { return container_; }

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
    const DoublyLinkedList*
        container_;  ///< 指向容器的指针，用于处理end()迭代器

   public:
    /**
     * @brief 默认构造函数
     */
    const_iterator() : current_(nullptr), container_(nullptr) {}

    /**
     * @brief 常量迭代器构造函数
     * @param node 指向的节点
     * @param container 指向容器的指针
     */
    explicit const_iterator(const Node* node,
                            const DoublyLinkedList* container = nullptr)
        : current_(node), container_(container) {}

    /**
     * @brief 从普通迭代器构造常量迭代器
     * @param it 普通迭代器
     */
    const_iterator(const iterator& it)
        : current_(it.get_node()), container_(it.get_container()) {}

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
      if (current_) {
        current_ = current_->prev;
      } else if (container_ && container_->tail_) {
        // 如果当前是nullptr（end迭代器），移动到尾节点
        current_ = container_->tail_;
      }
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
   * @brief 初始化列表构造函数
   * @param ilist 初始化列表
   *
   * @details 使用初始化列表中的元素创建链表
   */
  DoublyLinkedList(std::initializer_list<T> ilist)
      : head_(nullptr), tail_(nullptr), size_(0) {
    for (const auto& item : ilist) {
      push_back(item);
    }
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

  /**
   * @brief 初始化列表赋值操作符
   * @param ilist 初始化列表
   * @return 当前链表的引用
   */
  DoublyLinkedList& operator=(std::initializer_list<T> ilist) {
    clear();
    for (const auto& item : ilist) {
      push_back(item);
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

  /**
   * @brief 在指定位置插入元素
   * @param pos 插入位置的迭代器
   * @param value 要插入的元素值
   * @return 指向插入元素的迭代器
   *
   * @details 时间复杂度: O(1)
   */
  iterator insert(iterator pos, const T& value) {
    if (pos == begin()) {
      push_front(value);
      return begin();
    } else if (pos == end()) {
      push_back(value);
      return iterator(tail_, this);
    } else {
      Node* new_node = new Node(value);
      Node* pos_node = pos.get_node();

      new_node->next = pos_node;
      new_node->prev = pos_node->prev;
      pos_node->prev->next = new_node;
      pos_node->prev = new_node;

      ++size_;
      return iterator(new_node, this);
    }
  }

  /**
   * @brief 在指定位置插入元素（移动语义）
   * @param pos 插入位置的迭代器
   * @param value 要插入的元素值（右值引用）
   * @return 指向插入元素的迭代器
   *
   * @details 时间复杂度: O(1)
   */
  iterator insert(iterator pos, T&& value) {
    if (pos == begin()) {
      push_front(std::move(value));
      return begin();
    } else if (pos == end()) {
      push_back(std::move(value));
      return iterator(tail_, this);
    } else {
      Node* new_node = new Node(std::move(value));
      Node* pos_node = pos.get_node();

      new_node->next = pos_node;
      new_node->prev = pos_node->prev;
      pos_node->prev->next = new_node;
      pos_node->prev = new_node;

      ++size_;
      return iterator(new_node, this);
    }
  }

  /**
   * @brief 在指定位置插入多个相同元素
   * @param pos 插入位置的迭代器
   * @param count 插入元素的数量
   * @param value 要插入的元素值
   * @return 指向第一个插入元素的迭代器
   *
   * @details 时间复杂度: O(count)
   */
  iterator insert(iterator pos, size_type count, const T& value) {
    if (count == 0) return pos;

    iterator result = insert(pos, value);
    for (size_type i = 1; i < count; ++i) {
      insert(pos, value);
    }
    return result;
  }

  /**
   * @brief 在指定位置插入范围内的元素
   * @tparam InputIt 输入迭代器类型
   * @param pos 插入位置的迭代器
   * @param first 范围起始迭代器
   * @param last 范围结束迭代器
   * @return 指向第一个插入元素的迭代器
   *
   * @details 时间复杂度: O(n)，其中n是插入的元素数量
   */
  template <typename InputIt>
  typename std::enable_if<!std::is_integral<InputIt>::value, iterator>::type
  insert(iterator pos, InputIt first, InputIt last) {
    if (first == last) return pos;

    iterator result = insert(pos, *first);
    ++first;
    while (first != last) {
      insert(pos, *first);
      ++first;
    }
    return result;
  }

  /**
   * @brief 在指定位置插入初始化列表中的元素
   * @param pos 插入位置的迭代器
   * @param ilist 初始化列表
   * @return 指向第一个插入元素的迭代器
   *
   * @details 时间复杂度: O(n)，其中n是插入的元素数量
   */
  iterator insert(iterator pos, std::initializer_list<T> ilist) {
    return insert(pos, ilist.begin(), ilist.end());
  }

  /**
   * @brief 在链表头部原地构造元素
   * @tparam Args 构造参数类型
   * @param args 构造参数
   * @return 对新插入元素的引用
   */
  template <typename... Args>
  reference emplace_front(Args&&... args) {
    Node* new_node = new Node(std::forward<Args>(args)...);

    if (empty()) {
      head_ = tail_ = new_node;
    } else {
      new_node->next = head_;
      head_->prev = new_node;
      head_ = new_node;
    }

    ++size_;
    return new_node->data;
  }

  /**
   * @brief 在链表尾部原地构造元素
   * @tparam Args 构造参数类型
   * @param args 构造参数
   * @return 对新插入元素的引用
   */
  template <typename... Args>
  reference emplace_back(Args&&... args) {
    Node* new_node = new Node(std::forward<Args>(args)...);

    if (empty()) {
      head_ = tail_ = new_node;
    } else {
      tail_->next = new_node;
      new_node->prev = tail_;
      tail_ = new_node;
    }

    ++size_;
    return new_node->data;
  }

  /**
   * @brief 在指定位置原地构造元素
   * @tparam Args 构造参数类型
   * @param pos 插入位置的迭代器
   * @param args 构造参数
   * @return 指向新插入元素的迭代器
   */
  template <typename... Args>
  iterator emplace(iterator pos, Args&&... args) {
    if (pos == begin()) {
      emplace_front(std::forward<Args>(args)...);
      return begin();
    } else if (pos == end()) {
      emplace_back(std::forward<Args>(args)...);
      return iterator(tail_, this);
    } else {
      Node* new_node = new Node(std::forward<Args>(args)...);
      Node* pos_node = pos.get_node();

      new_node->next = pos_node;
      new_node->prev = pos_node->prev;
      pos_node->prev->next = new_node;
      pos_node->prev = new_node;

      ++size_;
      return iterator(new_node, this);
    }
  }
  /// @}

  /// @name 迭代器
  /// @{

  /**
   * @brief 获取指向第一个元素的迭代器
   * @return 指向第一个元素的迭代器
   */
  iterator begin() { return iterator(head_, this); }

  /**
   * @brief 获取指向第一个元素的常量迭代器
   * @return 指向第一个元素的常量迭代器
   */
  const_iterator begin() const { return const_iterator(head_, this); }

  /**
   * @brief 获取指向第一个元素的常量迭代器
   * @return 指向第一个元素的常量迭代器
   */
  const_iterator cbegin() const { return const_iterator(head_, this); }

  /**
   * @brief 获取指向尾后位置的迭代器
   * @return 指向尾后位置的迭代器
   */
  iterator end() { return iterator(nullptr, this); }

  /**
   * @brief 获取指向尾后位置的常量迭代器
   * @return 指向尾后位置的常量迭代器
   */
  const_iterator end() const { return const_iterator(nullptr, this); }

  /**
   * @brief 获取指向尾后位置的常量迭代器
   * @return 指向尾后位置的常量迭代器
   */
  const_iterator cend() const { return const_iterator(nullptr, this); }

  /**
   * @brief 获取指向最后一个元素的反向迭代器
   * @return 指向最后一个元素的反向迭代器
   */
  reverse_iterator rbegin() { return reverse_iterator(end()); }

  /**
   * @brief 获取指向最后一个元素的常量反向迭代器
   * @return 指向最后一个元素的常量反向迭代器
   */
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }

  /**
   * @brief 获取指向最后一个元素的常量反向迭代器
   * @return 指向最后一个元素的常量反向迭代器
   */
  const_reverse_iterator crbegin() const {
    return const_reverse_iterator(cend());
  }

  /**
   * @brief 获取指向首前位置的反向迭代器
   * @return 指向首前位置的反向迭代器
   */
  reverse_iterator rend() { return reverse_iterator(begin()); }

  /**
   * @brief 获取指向首前位置的常量反向迭代器
   * @return 指向首前位置的常量反向迭代器
   */
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  /**
   * @brief 获取指向首前位置的常量反向迭代器
   * @return 指向首前位置的常量反向迭代器
   */
  const_reverse_iterator crend() const {
    return const_reverse_iterator(cbegin());
  }
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

    return iterator(next_node, this);
  }

  /**
   * @brief 删除指定范围的元素
   * @param first 范围起始迭代器
   * @param last 范围结束迭代器
   * @return 指向被删除范围之后元素的迭代器
   *
   * @details 时间复杂度: O(n)，其中n是删除的元素数量
   */
  iterator erase(iterator first, iterator last) {
    while (first != last) {
      first = erase(first);
    }
    return last;
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
