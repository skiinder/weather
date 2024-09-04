#include "memchunk.h"
#include "lvgl.h"

int memchunk_init(Memchunk *chunk)
{
    if (chunk->ptr)
    {
        lv_mem_free(chunk->ptr);
    }
    chunk->ptr = lv_mem_alloc(1);
    if (chunk->ptr == NULL)
    {
        LV_LOG_ERROR("Failed to allocate memory for chunk %p\n", chunk);
        return -1;
    }
    chunk->ptr[0] = 0;
    chunk->size = 0;
    return 0;
}

int memchunk_append(Memchunk *chunk, void *data, int size)
{
    void *temp = lv_mem_realloc(chunk->ptr, chunk->size + size + 1);
    if (temp == NULL)
    {
        LV_LOG_ERROR("Failed to reallocate memory for chunk %p\n", chunk);
        return -1;
    }
    chunk->ptr = temp;
    memcpy(chunk->ptr + chunk->size, data, size);
    chunk->size += size;
    chunk->ptr[chunk->size] = 0;
    return 0;
}

void memchunk_free(Memchunk *chunk)
{
    if (chunk->ptr)
    {
        lv_mem_free(chunk->ptr);
        chunk->ptr = NULL;
    }
    chunk->size = 0;
}
