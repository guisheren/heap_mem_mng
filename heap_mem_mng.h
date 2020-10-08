
#ifndef  _HEAP_MEM_MNG_H_
#define  _HEAP_MEM_MNG_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <unistd.h>
#include "heap_mem_mng.h"

#define HEAP_RET_OK (0)
#define HEAP_RET_FAILD (-1)

//获取当前堆内存剩余内存大小
size_t _heap_get_free(void *heap_ctrl);

//dump当前内存信息
void  _heap_dump(void * heap_ctrl);

//初始化堆内存管理句柄,并申请堆内存
int   heap_init(void **heap_ctrl, size_t size, const char *file_name, size_t line_num);

//反初始化堆内存
void  heap_deinit(void *heap_ctrl);

//释放内存
void heap_free(void *heap_ctrl, void *p_buf);

//申请内存
void* heap_malloc(void *heap_ctrl, size_t size, const char *file_name, size_t line_num);

//按照个数和大小申请内存
void *heap_alloc(void *heap_ctrl, unsigned int count, size_t size, const char *file_name, size_t line_num);

//申请内存
void *heap_realloc(void *heap_ctrl, void *p_buf, size_t size, const char *file_name, size_t line_num);

#ifdef __cplusplus
}
#endif

#endif

