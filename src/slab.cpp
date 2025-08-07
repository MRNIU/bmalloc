/**
 * @copyright Copyright The SimpleKernel Contributors
 */

#include "slab.h"

#include <cstdint>

#include "kernel_log.hpp"

Slab::Slab(const char* name, uint64_t start_addr, size_t bytes)
    : AllocatorBase(name, start_addr, bytes) {}

uint64_t Slab::Alloc(size_t bytes) { return 0; }

bool Slab::Alloc(uint64_t, size_t) { return true; }

void Slab::Free(uint64_t start_addr, size_t) {}

size_t Slab::GetUsedCount(void) const { return 0; }

size_t Slab::GetFreeCount(void) const { return 0; }
