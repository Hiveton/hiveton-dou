#ifndef HIVETON_NETWORK_MEM_H
#define HIVETON_NETWORK_MEM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *network_mem_malloc(uint32_t size);
void *network_mem_calloc(uint32_t count, uint32_t size);
void *network_mem_realloc(void *ptr, uint32_t newsize);
void network_mem_free(void *ptr);
char *network_mem_strdup(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* HIVETON_NETWORK_MEM_H */
