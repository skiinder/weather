#if !defined(__MEM_CHUNK_H__)
#define __MEM_CHUNK_H__

typedef struct MemChunkStruct
{
    char *ptr;
    int size;
} Memchunk;

int memchunk_init(Memchunk *chunk);

int memchunk_append(Memchunk *chunk, void *data, int size);

void memchunk_free(Memchunk *chunk);

#endif // __MEM_CHUNK_H__
