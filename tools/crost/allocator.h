#pragma once

#include <stdint.h>

typedef struct Allocator
{
    void* (*allocate)(struct Allocator* allocator, uint64_t size);
    void* (*reallocate)(struct Allocator* allocator, void* oldPtr, uint64_t newSize);
    void (*free)(struct Allocator* allocator, void* ptr);

    void* data;
} Allocator;
