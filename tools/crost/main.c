#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

#include "allocator.h"

static void* alloc_allocate(Allocator* allocator, uint64_t size)
{
    (void)allocator;
    return malloc((size_t)size);
}

static void* alloc_reallocate(Allocator* allocator, void* oldPtr, uint64_t newSize)
{
    (void)allocator;
    return realloc(oldPtr, (size_t)newSize);
}

static void alloc_free(Allocator* allocator, void* ptr)
{
    (void)allocator;
    free(ptr);
}

static Allocator getAllocator(void)
{
    Allocator allocator;
    allocator.allocate = alloc_allocate;
    allocator.reallocate = alloc_reallocate;
    allocator.free = alloc_free;
    allocator.data = NULL;
    return allocator;
}

typedef struct ByteBuffer
{
    Allocator* allocator;
    uint8_t* data;
    uint64_t len;
    uint64_t capacity;
} ByteBuffer;

void ByteBuffer_Initialize(ByteBuffer* buffer, Allocator* allocator)
{
    buffer->allocator = allocator;
    buffer->data = NULL;
    buffer->len = 0;
    buffer->capacity = 0;
}

void ByteBuffer_Destroy(ByteBuffer* buffer)
{
    if (buffer->data && buffer->capacity)
        buffer->allocator->free(buffer->allocator, buffer->data);

    buffer->data = NULL;
    buffer->capacity = 0;
    buffer->len = 0;
}

int ByteBuffer_PushByte(ByteBuffer* buffer, uint8_t byte)
{
    static const uint64_t initialCapacity = 64;
    static const uint64_t capacityFactor = 2;

    if (!buffer->data)
    {
        buffer->data = buffer->allocator->allocate(buffer->allocator, initialCapacity);
        if (!buffer->data)
            return 1;
        buffer->capacity = initialCapacity;
    }

    if (buffer->len + 1 > buffer->capacity)
    {
        uint8_t* newData = (uint8_t*)buffer->allocator->reallocate(buffer->allocator, buffer->data, buffer->capacity * capacityFactor);
        if (!newData)
            return 1;
        buffer->data = newData;
        buffer->capacity *= capacityFactor;
    }

    buffer->data[buffer->len++] = byte;
    return 0;
}

typedef struct BitWriter
{
    ByteBuffer* buffer;
    uint8_t current;
    uint8_t bitCount;
} BitWriter;

void BitWriter_Initialize(BitWriter* writer, ByteBuffer* buffer)
{
    writer->buffer = buffer;
    writer->current = 0;
    writer->bitCount = 0;
}

int BitWriter_WriteBit(BitWriter* writer, int bit)
{
    if (bit)
        writer->current |= (1 << (7 - writer->bitCount));

    writer->bitCount++;

    if (writer->bitCount == 8)
    {
        if (ByteBuffer_PushByte(writer->buffer, writer->current) != 0)
            return 1;

        writer->current = 0;
        writer->bitCount = 0;
    }

    return 0;
}

int BitWriter_Flush(BitWriter* writer)
{
    if (writer->bitCount > 0)
    {
        if (ByteBuffer_PushByte(writer->buffer, writer->current) != 0)
            return 1;

        writer->current = 0;
        writer->bitCount = 0;
    }

    return 0;
}


typedef struct HuffmanNode
{
    uint64_t frequency;

    struct HuffmanNode* left;
    struct HuffmanNode* right;

    unsigned char isLeaf;

    uint8_t byteValue;
} HuffmanNode;

void heap_push(HuffmanNode** heap, uint64_t* heapSize, HuffmanNode* node)
{
    uint64_t index = (*heapSize)++;
    heap[index] = node;

    while (index > 0)
    {
        uint64_t parent = (index - 1) / 2;
        if (heap[parent]->frequency <= heap[index]->frequency) break;

        HuffmanNode* tmp = heap[parent];
        heap[parent] = heap[index];
        heap[index] = tmp;
        index = parent;
    }
}

HuffmanNode* heap_pop(HuffmanNode** heap, uint64_t* heapSize)
{
    HuffmanNode* min = heap[0];

    (*heapSize)--;
    heap[0] = heap[*heapSize];

    uint64_t index = 0;
    while (1)
    {
        uint64_t left = 2 * index + 1;
        uint64_t right = 2 * index + 2;
        uint64_t smallest = index;

        if (left < *heapSize && heap[left]->frequency < heap[smallest]->frequency)
            smallest = left;
        if (right < *heapSize && heap[right]->frequency < heap[smallest]->frequency)
            smallest = right;

        if (smallest == index) break;

        HuffmanNode* tmp = heap[smallest];
        heap[smallest] = heap[index];
        heap[index] = tmp;
        index = smallest;
    }

    return min;
}

typedef struct HuffmanCode
{
    uint8_t bits[32];
    uint8_t length;
} HuffmanCode;

void set_bit(uint8_t* path, uint16_t index, int value)
{
    uint16_t byteIndex = index / 8;
    uint8_t bitOffset = index % 8;

    if (value)
        path[byteIndex] |= (1 << (7 - bitOffset));
    else
        path[byteIndex] &= ~(1 << (7 - bitOffset));
}

int getBit(const uint8_t* path, uint16_t index)
{
    uint16_t byteIndex = index / 8;
    uint8_t bitOffset = index % 8;
    return (path[byteIndex] >> (7 - bitOffset)) & 1;
}

void build_codes(HuffmanNode* node, HuffmanCode* codes, uint8_t* path, uint16_t depth)
{
    if (node->isLeaf)
    {
        HuffmanCode* code = &codes[node->byteValue];
        code->length = (uint8_t)depth;
        memcpy(code->bits, path, 32);
        return;
    }

    set_bit(path, depth, 0);
    build_codes(node->left, codes, path, depth + 1);

    set_bit(path, depth, 1);
    build_codes(node->right, codes, path, depth + 1);
}

/** You have to call the free function of this allocator passed as argument on the data */
uint8_t* compress(Allocator* allocator, const uint8_t* input, uint64_t inputLen, uint64_t* outputLen)
{
    if (inputLen <= 0) return NULL;

    uint64_t frequencyCountAbove0 = 0;
    uint64_t frequencies[256] = {0};
    for (uint64_t i = 0; i < inputLen; i++)
    {
        if (frequencies[input[i]]++ == 0) frequencyCountAbove0++;
    }

    HuffmanCode codes[256];

    if (frequencyCountAbove0 != 1)
    {
        uint64_t usedNodes = 0;
        HuffmanNode* nodes = allocator->allocate(allocator, ((2 * frequencyCountAbove0) - 1) * sizeof(HuffmanNode) + frequencyCountAbove0 * sizeof(HuffmanNode*)); // 2k-1 nodes + k nodes for the heap
        if (!nodes)
            return NULL;

        uint64_t heapSize = 0;
        HuffmanNode** heap = (HuffmanNode**)(nodes + (2 * frequencyCountAbove0) - 1);

        for (uint16_t i = 0; i < 256; i++)
        {
            if (frequencies[i] == 0) continue;

            HuffmanNode* leaf = &nodes[usedNodes++];
            leaf->frequency = frequencies[i];
            leaf->byteValue = (uint8_t)i;
            leaf->isLeaf = 1;
            leaf->left = NULL;
            leaf->right = NULL;

            heap_push(heap, &heapSize, leaf);
        }

        while (heapSize > 1)
        {
            HuffmanNode* a = heap_pop(heap, &heapSize);
            HuffmanNode* b = heap_pop(heap, &heapSize);

            HuffmanNode* merged = &nodes[usedNodes++];
            merged->frequency = a->frequency + b->frequency;
            merged->isLeaf = 0;
            merged->left = a;
            merged->right = b;

            heap_push(heap, &heapSize, merged);
        }

        HuffmanNode* root = heap[0];

        uint8_t path[32] = {0};
        build_codes(root, codes, path, 0);

        allocator->free(allocator, nodes);
    }
    else
    {
        for (uint16_t i = 0; i < 256; i++)
        {
            if (frequencies[i] != 0)
            {
                codes[i].length = 1;
                memset(codes[i].bits, 0, sizeof(codes[i].bits));
                break;
            }
        }
    }

    ByteBuffer buffer;
    ByteBuffer_Initialize(&buffer, allocator);

    BitWriter writer;
    BitWriter_Initialize(&writer, &buffer);

    for (uint64_t i = 0; i < inputLen; i++)
    {
        HuffmanCode* code = &codes[input[i]];

        for (uint8_t b = 0; b < code->length; b++)
        {
            int bit = getBit(code->bits, b);
            if (BitWriter_WriteBit(&writer, bit) != 0)
            {
                ByteBuffer_Destroy(&buffer);
                return NULL;
            }
        }
    }

    if (BitWriter_Flush(&writer) != 0)
    {
        ByteBuffer_Destroy(&buffer);
        return NULL;
    }

    // TODO: Not fully clean, but works for now
    *outputLen = buffer.len;
    return buffer.data;
}

/** You have to call the free function of this allocator passed as argument on the data */
uint8_t* decompress(Allocator* allocator, const uint8_t* input, uint64_t inputLen, uint64_t* outputLen)
{
    (void)allocator;
    (void)input;
    (void)inputLen;
    (void)outputLen;
    return NULL;
}

int main(int argc, const char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <file> <output>\n", argv[0]);
        return 1;
    }

    Allocator allocator = getAllocator();

    const char* filename = argv[1];
    const char* output = argv[2];

#ifdef _WIN32
    FILE* file;
    errno_t fileResult = fopen_s(&file, filename, "rb");
    if (fileResult != 0)
#else
    FILE* file = fopen(filename, "rb");
    if (!file)
#endif
    {
        fprintf(stderr, "Error: Could not open %s\n", filename);
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (fileSize < 0)
    {
        fprintf(stderr, "Error: Could not determine file size for %s\n", filename);
        fclose(file);
        return 1;
    }

    uint8_t* buffer = allocator.allocate(&allocator, (size_t)fileSize);
    if (!buffer)
    {
        fputs("Error: Could not allocate buffer\n", stderr);
        fclose(file);
        return 1;
    }

    size_t bytesRead = fread(buffer, 1, (size_t)fileSize, file);
    fclose(file);

    if (bytesRead != (size_t)fileSize)
    {
        fprintf(stderr, "Error: Could not read entire file %s\n", filename);
        allocator.free(&allocator, buffer);
        return 1;
    }


    uint64_t outputLen;
    uint8_t* compressed = compress(&allocator, buffer, (uint64_t)bytesRead, &outputLen);
    if (!compressed)
    {
        fputs("Error: Compressing failed\n", stderr);
        allocator.free(&allocator, buffer);
        return 1;
    }

    allocator.free(&allocator, buffer);

#ifdef _WIN32
    FILE* outputFile;
    fileResult = fopen_s(&outputFile, output, "w+b");
    if (fileResult != 0)
#else
    FILE* outputFile = fopen(output, "w+b");
    if (!outputFile)
#endif
    {
        fprintf(stderr, "Error: Could not open %s\n", filename);
        return 1;
    }

    size_t bytesWritten = fwrite(compressed, 1, (size_t)outputLen, outputFile);
    fclose(outputFile);

    if (bytesWritten != outputLen)
    {
        fprintf(stderr, "Error: Could not write to %s\n", output);
        allocator.free(&allocator, compressed);
        return 1;
    }

    allocator.free(&allocator, compressed);

    return 0;
}
