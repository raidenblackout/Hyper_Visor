

typedef unsigned __int64 size_t;

void* memset(void* dest, int value, size_t count);
void* memcpy(void* dest, const void* src, size_t count);

#pragma intrinsic(memset, memcpy)
#pragma function(memset, memcpy)

void* memset(void* dest, int value, size_t count)
{
    unsigned char* d = (unsigned char*)dest;
    for (size_t i = 0; i < count; ++i)
        d[i] = (unsigned char)value;
    return dest;
}

void* memcpy(void* dest, const void* src, size_t count)
{
    unsigned char*       d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < count; ++i)
        d[i] = s[i];
    return dest;
}
