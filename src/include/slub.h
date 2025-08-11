/**
 * Copyright The bmalloc Contributors
 */

#ifndef BMALLOC_SRC_INCLUDE_SLUB_H_
#define BMALLOC_SRC_INCLUDE_SLUB_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include "allocator_base.h"

namespace bmalloc {

class Slub : public AllocatorBase {
 public:
  /// @name 构造/析构函数
  /// @{
  Slub() = default;
  Slub(const Slub&) = delete;
  Slub(Slub&&) = default;
  auto operator=(const Slub&) -> Slub& = delete;
  auto operator=(Slub&&) -> Slub& = default;
  ~Slub() override = default;
  /// @}

  [[nodiscard]] auto Alloc(size_t order) -> void* override;

  void Free(void* addr, size_t order) override;

 protected:
};

}  // namespace bmalloc

#endif /* BMALLOC_SRC_INCLUDE_SLUB_H_ */
