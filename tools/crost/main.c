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

int ByteBuffer_Append(ByteBuffer* buffer, uint8_t* bytes, uint64_t byteCount)
{
    const uint64_t initialCapacity = (byteCount < 64) ? 64 : byteCount;
    static const uint64_t capacityFactor = 2;

    if (!buffer->data)
    {
        buffer->data = buffer->allocator->allocate(buffer->allocator, initialCapacity);
        if (!buffer->data)
            return 1;
        buffer->capacity = initialCapacity;
    }

    if (buffer->len + byteCount > buffer->capacity)
    {
        const uint64_t minimumFactor = (2 * buffer->len + byteCount - 1) / buffer->len;
        const uint64_t factor = (capacityFactor < minimumFactor) ? minimumFactor : capacityFactor;
        uint8_t* newData = (uint8_t*)buffer->allocator->reallocate(buffer->allocator, buffer->data, buffer->capacity * factor);
        if (!newData)
            return 1;
        buffer->data = newData;
        buffer->capacity *= factor;
    }

    memcpy(buffer->data + buffer->len, bytes, byteCount);
    buffer->len += byteCount;
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

typedef struct
{
    const uint8_t* data;
    uint64_t len;
    uint64_t byteIndex;
    uint8_t bitIndex; // 0..7
} BitReader;

static void BitReader_Initialize(BitReader* reader, const uint8_t* data, uint64_t len)
{
    reader->data = data;
    reader->len = len;
    reader->byteIndex = 0;
    reader->bitIndex = 0;
}

/** Returns -1 when there are not any more bits */
static int BitReader_ReadBit(BitReader* reader)
{
    if (reader->byteIndex >= reader->len) return -1;

    int bit = (reader->data[reader->byteIndex] >> (7 - reader->bitIndex)) & 1;

    reader->bitIndex++;
    if (reader->bitIndex == 8)
    {
        reader->bitIndex = 0;
        reader->byteIndex++;
    }

    return bit;
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

static HuffmanNode* getHuffmanCodes(const uint64_t* frequencies, uint16_t frequencyCountAbove0, HuffmanNode** root, HuffmanCode* out, Allocator* allocator)
{
    if (frequencyCountAbove0 <= 0)
        return NULL;

    uint64_t usedNodes = 0;
    HuffmanNode* nodes = allocator->allocate(allocator, ((2 * frequencyCountAbove0) - 1) * sizeof(HuffmanNode) + frequencyCountAbove0 * sizeof(HuffmanNode*)); // 2k-1 nodes + k nodes for the heap
    if (!nodes)
        return NULL;

    if (frequencyCountAbove0 != 1)
    {
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

        *root = heap[0];

        uint8_t path[32] = {0};
        build_codes(*root, out, path, 0);
    }
    else
    {
        HuffmanNode* leaf = &nodes[usedNodes++];
        for (uint16_t i = 0; i < 256; i++)
        {
            if (frequencies[i] != 0)
            {
                leaf->frequency = frequencies[i];
                leaf->byteValue = (uint8_t)i;
                leaf->isLeaf = 1;
                leaf->left = NULL;
                leaf->right = NULL;

                out[i].length = 1;
                memset(out[i].bits, 0, sizeof(out[i].bits));
                break;
            }
        }
        *root = leaf;
    }

    return nodes;
}

static int writeU64(ByteBuffer* buffer, uint64_t value)
{
    uint8_t bytes[8];
    for (uint8_t i = 0; i < 8; i++)
        bytes[i] = (uint8_t)(value >> (8 * i));
    return ByteBuffer_Append(buffer, bytes, sizeof(bytes) / sizeof(bytes[0]));
}

static int writeU16(ByteBuffer* buffer, uint16_t value)
{
    uint8_t bytes[2] = { (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    return ByteBuffer_Append(buffer, bytes, sizeof(bytes) / sizeof(bytes[0]));
}

static uint64_t readU64(const uint8_t* p)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++)
        value |= ((uint64_t)p[i]) << (8 * i);
    return value;
}

static uint16_t readU16(const uint8_t* p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/** You have to call the free function of this allocator passed as argument on the data */
uint8_t* compress(Allocator* allocator, const uint8_t* input, uint64_t inputLen, uint64_t* outputLen)
{
    if (inputLen <= 0) return NULL;

    uint16_t frequencyCountAbove0 = 0;
    uint64_t frequencies[256] = {0};
    for (uint64_t i = 0; i < inputLen; i++)
    {
        if (frequencies[input[i]]++ == 0) frequencyCountAbove0++;
    }

    HuffmanCode codes[256];
    HuffmanNode* root;
    HuffmanNode* nodes = getHuffmanCodes(frequencies, frequencyCountAbove0, &root, codes, allocator);
    if (!nodes)
        return NULL;

    allocator->free(allocator, nodes);

    ByteBuffer buffer;
    ByteBuffer_Initialize(&buffer, allocator);

    /* Header
        typedef struct FrequencyHeader
        {
            uint8_t symbol;
            uint64_t frequency;
        } FrequencyHeader;

        uint64_t originalLength;
        uint16_t symbolCount; // Count of symbols with a frequency above 0
        FrequencyHeader frequencies[symbolCount];
    */

    if (writeU64(&buffer, inputLen) != 0)
    {
        ByteBuffer_Destroy(&buffer);
        return NULL;
    }
    if (writeU16(&buffer, frequencyCountAbove0) != 0)
    {
        ByteBuffer_Destroy(&buffer);
        return NULL;
    }
    for (uint16_t i = 0; i < 256; i++)
    {
        if (frequencies[i] <= 0) continue;

        uint8_t symbol = (uint8_t)i;
        if (ByteBuffer_PushByte(&buffer, symbol) != 0 || writeU64(&buffer, frequencies[i]) != 0)
        {
            ByteBuffer_Destroy(&buffer);
            return NULL;
        }
    }

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
    /* Header
        typedef struct FrequencyHeader
        {
            uint8_t symbol;
            uint64_t frequency;
        } FrequencyHeader;

        uint64_t originalLength;
        uint16_t symbolCount; // Count of symbols with a frequency above 0
        FrequencyHeader frequencies[symbolCount];
    */

    if (inputLen < 10) return NULL;

    const uint8_t* cursor = input;

    const uint64_t originalLength = readU64(cursor);
    cursor += 8;

    const uint16_t frequencyCountAbove0 = readU16(cursor);
    cursor += 2;

    if (inputLen < 10 + frequencyCountAbove0 * 9) return NULL;

    uint64_t frequencies[256] = {0};
    for (uint16_t i = 0; i < frequencyCountAbove0; i++)
    {
        uint8_t symbol = *cursor;
        cursor += 1;

        frequencies[symbol] = readU64(cursor);
        cursor += 8;
    }

    HuffmanCode codes[256];
    HuffmanNode* root;
    HuffmanNode* nodes = getHuffmanCodes(frequencies, frequencyCountAbove0, &root, codes, allocator);
    if (!nodes)
        return NULL;

    BitReader reader;
    BitReader_Initialize(&reader, cursor, inputLen - (10 + frequencyCountAbove0 * 9));

    uint8_t* output = allocator->allocate(allocator, originalLength);
    if (!output)
    {
        allocator->free(allocator, nodes);
        return NULL;
    }

    for (uint64_t produced = 0; produced < originalLength; produced++)
    {
        HuffmanNode* node = root;

        while (!node->isLeaf)
        {
            int bit = BitReader_ReadBit(&reader);
            if (bit < 0)
            {
                allocator->free(allocator, output);
                allocator->free(allocator, nodes);
                return NULL;
            }
            node = bit ? node->right : node->left;
        }

        output[produced] = node->byteValue;
    }

    allocator->free(allocator, nodes);
    *outputLen = originalLength;
    return output;
}

int main(int argc, const char* argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s compress/decompress <file> <output>\n", argv[0]);
        return 1;
    }

    Allocator allocator = getAllocator();

    const char* operation = argv[1];

    int doCompression;
    if (strcmp(operation, "compress") == 0)
        doCompression = 1;
    else if (strcmp(operation, "decompress") == 0)
        doCompression = 0;
    else
    {
        fprintf(stderr, "Usage: %s compress/decompress <file> <output>\n", argv[0]);
        return 1;
    }

    const char* filename = argv[2];
    const char* output = argv[3];

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
    uint8_t* compressed = doCompression
        ? compress(&allocator, buffer, (uint64_t)bytesRead, &outputLen)
        : decompress(&allocator, buffer, (uint64_t)bytesRead, &outputLen);
    if (!compressed)
    {
        fputs(doCompression ? "Error: Compressing failed\n" : "Error: Decompressing failed\n", stderr);
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
