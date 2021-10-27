// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include "../types.h"
#include "d3d12.h"

namespace lgfx {

const size_t kMemoryPoolDefaultHeapSize = 268435456;
const size_t kMemoryPoolDefaultBlockSize = 1048576;

class MemoryHeap {

friend class MemoryPool;
friend class Texture;

public:

    MemoryHeap(Device* device, const uint64_t align, const MemoryType type, const MemoryFlags flags);
    MemoryHeap(const MemoryHeap&) = delete;
    MemoryHeap(MemoryHeap&&) = delete;

    MemoryHeap& operator=(const MemoryHeap&) = delete;
    MemoryHeap& operator=(MemoryHeap&&) = delete;

private:

    ComPtr<ID3D12Heap> heap_;

    size_t heap_size_;

    uint64_t block_count_;
    std::vector<uint8_t> blocks_;

    uint64_t offset_;
};

struct MemoryAllocInfo {

    MemoryHeap* heap;
    uint64_t offset;

    inline bool operator!() const {
        
        return !heap;
    }
};

class MemoryPool {

public:

    MemoryPool(Device* device, const size_t size, const uint64_t align, const MemoryType type, const MemoryFlags flags);
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;

    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    MemoryAllocInfo Allocate(const size_t size);
    void Deallocate(const MemoryAllocInfo& alloc_info, const size_t size);

    inline MemoryType GetType() const { return type_; }
    inline MemoryFlags GetFlags() const { return flags_; }

private:

    size_t AlignedBlockSize(const size_t size) const;

    MemoryType type_;
    MemoryFlags flags_;

    std::vector<std::unique_ptr<MemoryHeap>> heaps_;
};

}