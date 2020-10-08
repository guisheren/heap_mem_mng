#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>

#include "heap_mem_mng.h"

#define HEAP_FILE_LEN	64

//内存块当前状态
typedef enum
{
	HEAP_S_FREE = 1,	
	HEAP_S_USED = 2
}HEAP_STATUS;

//内存块节点信息
typedef struct _HEAP_CHUNK{
	struct _HEAP_CHUNK *pre_chunk;
	struct _HEAP_CHUNK *next_chunk;
	HEAP_STATUS mem_flag;
	size_t size;
	char file_name[HEAP_FILE_LEN];
	size_t line_num;
}HEAP_CHUNK;

//内存管理句柄
typedef struct _HEAP_CTRL{
	char *p_buf;				//heap内存首地址
	size_t buf_size;			//heap内存大小
	char *align_buf;			//对齐的内存首地址,可能等于p_buf
	size_t valid_size;			//对齐后的内存大小
	pthread_mutex_t lock;		//多线程并行访问锁
	HEAP_CHUNK *heap_mng_entry; //实际内存链表入口
	size_t heap_alloc_cnt;		//内存申请次数统计
	char file_name[HEAP_FILE_LEN];
	size_t line_num;
}HEAP_CTRL;

#define HEAP_ALLOC_MIN_SIZE sizeof(long int)
#define ALIGN_UP_VAL(val, align) (((val) + (align) - 1) & (~((align)-1)))
#define ALIGN_DOWN_VAL(val, align) ((val) & (~((align)-1)))

#define HEAP_DBG(fmt, args...)		do{\
			printf("<DBG>[%s][%u]:", __FILE__, __LINE__);printf(fmt, ##args);	\
		}while(0)

#define HEAP_ERR(fmt, args...)		do{\
			printf("<ERR>[%s][%d]:", __FILE__, __LINE__);printf(fmt, ##args);	\
		}while(0)

static void 		_heap_lock(void);
static void 		_heap_unlock(void);
static HEAP_CHUNK*	_heap_new_chunk(void *buf, size_t size);
static void 		_heap_resize_chunk(HEAP_CHUNK *p_chunk, size_t size);
static void 		_heap_insert_chunk(HEAP_CHUNK *p_chunk, HEAP_CHUNK *p_next_chunk);
static void 		_heap_merger_chunk(HEAP_CHUNK *p_chunk, HEAP_CHUNK *p_next_chunk);
static void*		_heap_chunk_to_pointer(HEAP_CHUNK *p_chunk);
static HEAP_CHUNK* 	_heap_pointer_to_chunk(void *buf);


#define HEAP_MNG_LOCK(val)		pthread_mutex_lock(&val)
#define HEAP_MNG_UNLOCK(val)	pthread_mutex_unlock(&val)

//static api
static HEAP_CHUNK* _heap_new_chunk(void *buf, size_t size)
{
	HEAP_CHUNK *p_chunk = (HEAP_CHUNK*)buf;
	p_chunk->pre_chunk = p_chunk;
	p_chunk->next_chunk = p_chunk;
	p_chunk->mem_flag = HEAP_S_FREE;
	p_chunk->size = size;

	return p_chunk;
}

static void _heap_resize_chunk(HEAP_CHUNK *p_chunk, size_t size)
{
	p_chunk->size = size;
}

static void _heap_insert_chunk(HEAP_CHUNK *p_chunk, HEAP_CHUNK *p_next_chunk)
{
	HEAP_CHUNK *p_cur_next_chunk = p_chunk->next_chunk;
	p_cur_next_chunk->pre_chunk = p_next_chunk;
	p_next_chunk->next_chunk = p_chunk->next_chunk;
	p_chunk->next_chunk = p_next_chunk;
	p_next_chunk->pre_chunk = p_chunk;
	
	return;
}

static void _heap_merger_chunk(HEAP_CHUNK *p_chunk, HEAP_CHUNK *p_next_chunk)
{	
	if(p_next_chunk == p_chunk)
	{
		return ;
	}

	//不相邻不合并
	if(p_chunk->next_chunk != p_next_chunk)
	{
		return ;
	}

	HEAP_CHUNK *p_next_next_chunk = p_next_chunk->next_chunk;

	p_chunk->size += p_next_chunk->size;
	p_chunk->next_chunk = p_next_chunk->next_chunk;

	if(p_next_next_chunk != p_chunk)
	{
		p_next_next_chunk->pre_chunk = p_chunk;
	}

	p_next_chunk->pre_chunk = NULL;
	p_next_chunk->next_chunk = NULL;
	p_next_chunk->size = 0;
	p_next_chunk->mem_flag = HEAP_S_FREE;

	return ;
}

static void* _heap_chunk_to_pointer(HEAP_CHUNK *p_chunk)
{
	return ((char*)p_chunk +  sizeof(HEAP_CHUNK));
}

static HEAP_CHUNK* _heap_pointer_to_chunk(void *buf)
{
	return (HEAP_CHUNK*)((char*)buf- sizeof(HEAP_CHUNK));
}

void* heap_malloc(void *heap_ctrl, size_t size, const char *file_name, size_t line_num)
{
	HEAP_CTRL *p_heap_ctrl = (HEAP_CTRL *)heap_ctrl;
	if(NULL == heap_ctrl)
	{
		HEAP_ERR("error heap_ctrl %p\n", p_heap_ctrl);
		return NULL;
	}

	HEAP_CHUNK *p_chunk = p_heap_ctrl->heap_mng_entry;
	HEAP_CHUNK *p_heap_alloc_chunk = NULL;
	HEAP_CHUNK *p_free_chunk = NULL;
	void *p_buf = NULL;

	size = ALIGN_UP_VAL(size, sizeof(long int));
	HEAP_DBG("heap_alloc size = %u\n", size);
	
	if(size < HEAP_ALLOC_MIN_SIZE)
	{
		HEAP_ERR("size %u error,must be align %d\n", size, sizeof(long int));
		return NULL;
	}

	if(NULL == p_chunk)
	{
		HEAP_ERR("g_heap_entry is NULL, please init heap\n");
		return NULL;
	}

	HEAP_MNG_LOCK(p_heap_ctrl->lock);
	if(p_heap_ctrl->heap_alloc_cnt == INT_MAX)
	{
		HEAP_MNG_UNLOCK(p_heap_ctrl->lock);
		HEAP_ERR("g_heap_count %u over than INT_MAX\n", p_heap_ctrl->heap_alloc_cnt);
		return NULL;
	}

	do{
		if(p_chunk->mem_flag == HEAP_S_FREE)
		{
			if(p_chunk->size >= sizeof(HEAP_CHUNK) + size + sizeof(HEAP_CHUNK) + HEAP_ALLOC_MIN_SIZE)
			{
				size_t heap_alloc_size = sizeof(HEAP_CHUNK) + size;
				p_heap_alloc_chunk = p_chunk;
				memset(p_heap_alloc_chunk->file_name, 0, sizeof(p_heap_alloc_chunk->file_name));
				memcpy(p_heap_alloc_chunk->file_name, file_name, strlen(file_name));
				p_heap_alloc_chunk->line_num = line_num;
				p_free_chunk = _heap_new_chunk((char*)p_chunk + heap_alloc_size, p_chunk->size - heap_alloc_size);
				_heap_resize_chunk(p_heap_alloc_chunk, heap_alloc_size);
				_heap_insert_chunk(p_heap_alloc_chunk, p_free_chunk);
				p_heap_alloc_chunk->mem_flag = HEAP_S_USED;
				p_heap_ctrl->heap_alloc_cnt++;
				p_buf = _heap_chunk_to_pointer(p_heap_alloc_chunk);
			}
		}
		p_chunk = (HEAP_CHUNK*)p_chunk->next_chunk;

	}while(NULL == p_buf && (p_chunk != p_heap_ctrl->heap_mng_entry));

	HEAP_MNG_UNLOCK(p_heap_ctrl->lock);

	return p_buf;
}

size_t _heap_get_free(void *heap_ctrl)
{
	HEAP_CTRL *p_heap_ctrl = (HEAP_CTRL *)heap_ctrl;
	if(NULL == p_heap_ctrl)
	{
		HEAP_ERR("error heap_ctrl %p\n", p_heap_ctrl);
		return 0;
	}

	HEAP_CHUNK *p_chunk = p_heap_ctrl->heap_mng_entry;
	int i = 0;
	size_t free_size = 0;

	if(NULL == p_chunk)
	{
		HEAP_ERR("g_heap_entry is NULL, please init heap\n");
		return 0;
	}

	HEAP_MNG_LOCK(p_heap_ctrl->lock);

	do{
		if(p_chunk->mem_flag == HEAP_S_FREE)
		{
			free_size += (p_chunk->size - sizeof(HEAP_CHUNK));
		}
		i++;
		p_chunk = (HEAP_CHUNK*)p_chunk->next_chunk;

	}while(p_chunk != NULL && p_chunk != p_heap_ctrl->heap_mng_entry);

	HEAP_MNG_UNLOCK(p_heap_ctrl->lock);

	HEAP_DBG("heap free size %u\n", free_size);
	return free_size;
}

void _heap_dump(void * heap_ctrl)
{
	HEAP_CTRL *p_heap_ctrl = (HEAP_CTRL *)heap_ctrl;
	if(NULL == heap_ctrl)
	{
		HEAP_ERR("error heap_ctrl %p\n", p_heap_ctrl);
		return;
	}

	HEAP_CHUNK *p_chunk = p_heap_ctrl->heap_mng_entry;
	int i = 0;

	if(NULL == p_chunk)
	{
		HEAP_ERR("g_heap_entry is NULL, please init heap\n");
		return ;
	}

	HEAP_MNG_LOCK(p_heap_ctrl->lock);

	do{
		if(HEAP_S_USED == p_chunk->mem_flag)
			HEAP_DBG("idx[%d] file %s line %u USED size %u\n", i, p_chunk->file_name, p_chunk->line_num, p_chunk->size);
		else
			HEAP_DBG("idx[%d] FREE size %u\n", i, p_chunk->size);
		//HEAP_DBG("next chunk %p\n", p_chunk->next_chunk);
		//HEAP_DBG("pre  chunk %p\n", p_chunk->pre_chunk);
		i++;
		p_chunk = (HEAP_CHUNK*)p_chunk->next_chunk;

	}while(p_chunk != NULL && p_chunk != p_heap_ctrl->heap_mng_entry);
	HEAP_MNG_UNLOCK(p_heap_ctrl->lock);

	HEAP_DBG("\n");
	return ;

}

int heap_init(void **heap_ctrl, size_t size, const char *file_name, size_t line_num)
{
	if(heap_ctrl == NULL)
	{
		HEAP_ERR("heap_init failed\n");
		return HEAP_RET_FAILD;
	}

	HEAP_CTRL *p_heap_ctrl = malloc(sizeof(HEAP_CTRL)); 
	if(NULL == p_heap_ctrl)
	{
		HEAP_ERR("malloc failed! heap_init failed\n");
		*heap_ctrl = NULL;
		return HEAP_RET_FAILD;
	}
	memset(p_heap_ctrl, 0, sizeof(p_heap_ctrl));

	size = ALIGN_UP_VAL(size, sizeof(long int));
	p_heap_ctrl->p_buf = malloc(size);
	p_heap_ctrl->buf_size = size;
	if(NULL == p_heap_ctrl->p_buf)
	{
		HEAP_ERR("malloc heap buf failed\n");
		free(p_heap_ctrl);
		*heap_ctrl = NULL;
		return HEAP_RET_FAILD;
	}
	memset(p_heap_ctrl->p_buf, 0, size);

	p_heap_ctrl->align_buf = (void*)ALIGN_UP_VAL((unsigned long)p_heap_ctrl->p_buf, sizeof(long int));
	p_heap_ctrl->valid_size = p_heap_ctrl->buf_size - ((char*)p_heap_ctrl->align_buf - (char*)p_heap_ctrl->p_buf);
	p_heap_ctrl->valid_size = ALIGN_DOWN_VAL(p_heap_ctrl->valid_size, sizeof(long int));
	p_heap_ctrl->heap_mng_entry = _heap_new_chunk(p_heap_ctrl->align_buf, p_heap_ctrl->valid_size);
	p_heap_ctrl->heap_alloc_cnt = 0;
	pthread_mutex_init(&p_heap_ctrl->lock, NULL);
	memcpy(p_heap_ctrl->file_name, file_name, strlen(file_name)+1);
	p_heap_ctrl->line_num = line_num;

	_heap_dump(p_heap_ctrl);
	_heap_get_free(p_heap_ctrl);

	*heap_ctrl = p_heap_ctrl;

	return HEAP_RET_OK;
}

void heap_deinit(void *heap_ctrl)
{
	HEAP_DBG("heap_deinit\n");

	HEAP_CTRL *p_heap_ctrl = (HEAP_CTRL *)heap_ctrl;
	if(NULL == heap_ctrl)
	{
		HEAP_ERR("error heap_ctrl %p\n", p_heap_ctrl);
		return;
	}

	_heap_dump(p_heap_ctrl);
	_heap_get_free(p_heap_ctrl);

	HEAP_MNG_LOCK(p_heap_ctrl->lock);

	if(p_heap_ctrl->p_buf) free(p_heap_ctrl->p_buf);
	p_heap_ctrl->p_buf = NULL;
	p_heap_ctrl->buf_size = 0;
	p_heap_ctrl->heap_alloc_cnt = 0;
	p_heap_ctrl->heap_mng_entry = NULL;
	HEAP_MNG_UNLOCK(p_heap_ctrl->lock);
	pthread_mutex_destroy(&p_heap_ctrl->lock);
	free(p_heap_ctrl);

}

void heap_free(void *heap_ctrl, void *p_buf)
{
	HEAP_CTRL *p_heap_ctrl = (HEAP_CTRL *)heap_ctrl;
	if(NULL == heap_ctrl)
	{
		HEAP_ERR("error heap_ctrl %p\n", p_heap_ctrl);
		return ;
	}

	HEAP_CHUNK * p_chunk = NULL;
	size_t old_size = 0;

	HEAP_DBG("free addr %p\n", (unsigned long)p_buf);

	if(NULL == p_buf)
	{
		HEAP_ERR("free params NULL\n");
		return ;
	}
	HEAP_MNG_LOCK(p_heap_ctrl->lock);
	p_chunk = _heap_pointer_to_chunk(p_buf);
	if(p_chunk == 0)
	{
		HEAP_MNG_UNLOCK(p_heap_ctrl->lock);
		HEAP_ERR("p_chunk NULL\n");
		return ;
	}

	old_size = p_chunk->size - sizeof(HEAP_CHUNK);

	HEAP_DBG("old_size = %u\n", old_size);

	p_chunk->mem_flag = HEAP_S_FREE;
	{
		HEAP_CHUNK * p_pre_chunk = (HEAP_CHUNK *)(p_chunk->pre_chunk);
		if(p_pre_chunk < p_chunk && p_pre_chunk->mem_flag == p_chunk->mem_flag)
		{
			_heap_merger_chunk(p_pre_chunk, p_chunk);
			p_chunk = p_pre_chunk;
			p_heap_ctrl->heap_alloc_cnt--;
		}
	}
	{
		HEAP_CHUNK * p_next_chunk = (HEAP_CHUNK *)(p_chunk->next_chunk);
		if(p_next_chunk > p_chunk && p_next_chunk->mem_flag == p_chunk->mem_flag)
		{
			_heap_merger_chunk(p_chunk, p_next_chunk);
			p_heap_ctrl->heap_alloc_cnt--;
		}
	}
	HEAP_MNG_UNLOCK(p_heap_ctrl->lock);

	return ;
}

void *heap_alloc(void *heap_ctrl, unsigned int count, size_t size, const char *file_name, size_t line_num)
{
	size_t all_size = count * size;
	void *p_buf = heap_malloc(heap_ctrl, all_size, file_name, line_num);
	if(NULL == p_buf)
	{
		HEAP_ERR("heap_alloc mem bull count %u size %u\n", count, size);
		return NULL;
	}

	memset(p_buf, 0, all_size);

	return p_buf;
}

void *heap_realloc(void *heap_ctrl, void *p_buf, size_t size, const char *file_name, size_t line_num)
{
	HEAP_CTRL *p_heap_ctrl = (HEAP_CTRL *)heap_ctrl;
	if(NULL == p_heap_ctrl)
	{
		HEAP_ERR("error heap_ctrl %p\n", p_heap_ctrl);
		return NULL;
	}

	void* p_new_buf = NULL;
	HEAP_CHUNK *p_chunk = NULL;
	size_t old_size = 0;

	if(NULL == p_buf)
	{
		HEAP_ERR("input param null\n");
		return NULL;
	}

	HEAP_MNG_LOCK(p_heap_ctrl->lock);
	p_chunk = _heap_pointer_to_chunk(p_buf);
	if(NULL != p_chunk)
	{
		old_size = p_chunk->size - sizeof(HEAP_CHUNK);
	}
	HEAP_MNG_UNLOCK(p_heap_ctrl->lock);

	if(p_chunk == 0)
	{
		HEAP_ERR("p_chunk NULL\n");
		return NULL;
	}

	p_new_buf = heap_malloc(heap_ctrl, size, file_name, line_num);

	if(p_new_buf)
	{
		if(old_size > size)
		{
			memcpy(p_new_buf, p_buf, old_size);
		}
		else
		{
			memcpy(p_new_buf, p_buf, size);
		}
		heap_free(heap_ctrl, p_buf);
	}

	return p_new_buf;
}

