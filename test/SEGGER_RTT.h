#ifndef SEGGER_RTT_H
#define SEGGER_RTT_H
#include <stddef.h>
static inline int SEGGER_RTT_Write(unsigned int BufferIndex, const void* pBuffer, unsigned int NumBytes) {
    (void)BufferIndex;
    (void)pBuffer;
    (void)NumBytes;
    return 0;
}
#endif
