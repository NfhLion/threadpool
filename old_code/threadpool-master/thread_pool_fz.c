#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

//任务函数指针
typedef void (*JOB_CALLBACK)(void *);

//任务队列结点
typedef struct NJOB {
	struct NJOB *next;
	JOB_CALLBACK func;
	void *arg;
} nJob;

//执行队列结点
typedef struct NWORKER {
	struct NWORKER *active_next;
	pthread_t active_tid;
} nWorker;

//线程池结构体
typedef struct NTHREADPOOL {
	struct NTHREADPOOL *forw;
	struct NTHREADPOOL *back;
	pthread_mutex_t mtx;
	
	pthread_cond_t busycv;				//忙
	pthread_cond_t workcv;				//工作
	pthread_cond_t waitcv;				//等待
	
	nWorker *active;					//执行队列指针
	nJob *head;							//任务队列头
	nJob *tail;							//任务队列头
	pthread_attr_t attr;				//线程属性
	
	int flags;
	unsigned int linger;
	int minimum;
	int maximum;
	int nthreads;
	int idle;
 
} nThreadPool;

static void* ntyWorkerThread(void *arg);
#define NTY_POOL_WAIT   0x01
#define NTY_POOL_DESTROY  0x02

static pthread_mutex_t nty_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static sigset_t fillset;
nThreadPool *thread_pool = NULL;

//线程的创建
static int ntyWorkerCreate(nThreadPool *pool) 
{
	sigset_t oset;
	pthread_t thread_id;
	pthread_sigmask(SIG_SETMASK, &fillset, &oset);
	int error = pthread_create(&thread_id, &pool->attr, ntyWorkerThread, pool);
	pthread_sigmask(SIG_SETMASK, &oset, NULL);
	return error;
}

static void ntyWorkerCleanup(nThreadPool * pool) 
{

	--pool->nthreads;
	if (pool->flags & NTY_POOL_DESTROY) {
		if (pool->nthreads == 0) {
			pthread_cond_broadcast(&pool->busycv);
		}
	} else if (pool->head != NULL && pool->nthreads < pool->maximum && ntyWorkerCreate(pool) == 0) {
		pool->nthreads ++;
	}
	pthread_mutex_unlock(&pool->mtx);
 
}

static void ntyNotifyWaiters(nThreadPool *pool) 
{
 
	if (pool->head == NULL && pool->active == NULL) {
		pool->flags &= ~NTY_POOL_WAIT;
		pthread_cond_broadcast(&pool->waitcv);
	}
 
}

static void ntyJobCleanup(nThreadPool *pool) 
{
 
	pthread_t tid = pthread_self();
	nWorker *activep;
	nWorker **activepp;
 
	pthread_mutex_lock(&pool->mtx);
	for (activepp = &pool->active;(activep = *activepp) != NULL;activepp = &activep->active_next) {
		*activepp = activep->active_next;
		break;
	}
	if (pool->flags & NTY_POOL_WAIT) ntyNotifyWaiters(pool);
 
}

//线程处理函数
static void* ntyWorkerThread(void *arg) 
{
	nThreadPool *pool = (nThreadPool*)arg;
	nWorker active;
 
	int timeout;
	struct timespec ts;
	JOB_CALLBACK func;
 
	pthread_mutex_lock(&pool->mtx);
	pthread_cleanup_push(ntyWorkerCleanup, pool);
	active.active_tid = pthread_self();
	
	while (1) {
 
		pthread_sigmask(SIG_SETMASK, &fillset, NULL);
		pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  
		timeout = 0;
		pool->idle ++;
  
		if (pool->flags & NTY_POOL_WAIT) {
			ntyNotifyWaiters(pool);
		}
  
		while (pool->head == NULL && !(pool->flags & NTY_POOL_DESTROY)) {
			if (pool->nthreads <= pool->minimum) {
    
			pthread_cond_wait(&pool->workcv, &pool->mtx);
    
			} else {
   
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += pool->linger;
			if (pool->linger == 0 || pthread_cond_timedwait(&pool->workcv, &pool->mtx, &ts) == ETIMEDOUT) {
    
				timeout = 1;
				break;
				}
			}
		}
		pool->idle --;
  
		if (pool->flags & NTY_POOL_DESTROY) break;
  
		nJob *job = pool->head;  
		if (job != NULL) {
  
			timeout = 0;
			func = job->func;
   
			void *job_arg = job->arg;
			pool->head = job->next;
   
			if (job == pool->tail) {
				pool->tail == NULL;
			}
   
			active.active_next = pool->active;
			pool->active = &active;
   
			pthread_mutex_unlock(&pool->mtx);
			pthread_cleanup_push(ntyJobCleanup, pool);
   
			free(job);
			func(job_arg);
   
			pthread_cleanup_pop(1);
		}
  
		if (timeout && (pool->nthreads > pool->minimum)) {
			break;
		}
	}
	pthread_cleanup_pop(1);
 
	return NULL;
 
}

//设置线程属性
static void ntyCloneAttributes(pthread_attr_t *new_attr, pthread_attr_t *old_attr) 
{

	struct sched_param param;
	void *addr;
	size_t size;
	int value;
 
	pthread_attr_init(new_attr);		//对new_attr进行初始化
 
	if (old_attr != NULL) {
 
		pthread_attr_getstack(old_attr, &addr, &size);
		pthread_attr_setstack(new_attr, NULL, size);
  
		//用来得到和设置线程的作用域
		pthread_attr_getscope(old_attr, &value);
		pthread_attr_setscope(new_attr, value);
  
		//得到和设置线程的继承性
		pthread_attr_getinheritsched(old_attr, &value);
		pthread_attr_setinheritsched(new_attr, value);
  
		pthread_attr_getschedpolicy(old_attr, &value);
		pthread_attr_setschedpolicy(new_attr, value);
		
		//获得线程调度参数和设置线程调度参数
		pthread_attr_getschedparam(old_attr, &param);
		pthread_attr_setschedparam(new_attr, &param);
		
		//分别用来得到和设置线程栈末尾的警戒缓冲区大小
		pthread_attr_getguardsize(old_attr, &size);
		pthread_attr_setguardsize(new_attr, size);  
	}
	
	//设置线程分离状态
	pthread_attr_setdetachstate(new_attr, PTHREAD_CREATE_DETACHED); 
}

//线程池创建
nThreadPool *ntyThreadPoolCreate(int min_threads, int max_threads, int linger, pthread_attr_t *attr) 
{

	sigfillset(&fillset);
	if (min_threads > max_threads || max_threads < 1) {
		errno = EINVAL;			//EINVAL表示无效的参数，即为 invalid argument ，包括参数值、类型或数目无效等。
		return NULL;
	}
 
	nThreadPool *pool = (nThreadPool*)malloc(sizeof(nThreadPool));
	if (pool == NULL) {
		errno = ENOMEM;			//内存溢出
		return NULL;
	}
 
	//锁和条件变量的动态初始化
	pthread_mutex_init(&pool->mtx, NULL);
	pthread_cond_init(&pool->busycv, NULL);
	pthread_cond_init(&pool->workcv, NULL);
	pthread_cond_init(&pool->waitcv, NULL);
 
	pool->active = NULL;
	pool->head = NULL;
	pool->tail = NULL;
	pool->flags = 0;
	pool->linger = linger;
	pool->minimum = min_threads;
	pool->maximum = max_threads;
	pool->nthreads = 0;
	pool->idle = 0;
 
	ntyCloneAttributes(&pool->attr, attr);				//设置线程属性
	pthread_mutex_lock(&nty_pool_lock);
	
	//插入线程池操作，循环双向链表
	if (thread_pool == NULL) {
		pool->forw = pool;
		pool->back = pool;
		
		thread_pool = pool;
	
	} else {
	
		thread_pool->back->forw = pool;
		pool->forw = thread_pool;
		pool->back = thread_pool->back;
		thread_pool->back = pool;
	
	}
	
	pthread_mutex_unlock(&nty_pool_lock);
	return pool;
 
}

//向任务队列添加任务
int ntyThreadPoolQueue(nThreadPool *pool, JOB_CALLBACK func, void *arg) 
{

	nJob *job = (nJob*)malloc(sizeof(nJob));
	if (job == NULL) {
		errno = ENOMEM;
		return -1;
	}
	job->next = NULL;
	job->func = func;
	job->arg = arg;
	
	pthread_mutex_lock(&pool->mtx);
	if (pool->head == NULL) {
		pool->head = job;
	} else {
		pool->tail->next = job;
	}
	pool->tail = job;
	//pool->idle++;
	
	if (pool->idle > 0) {
		pthread_cond_signal(&pool->workcv);
	} else if (pool->nthreads < pool->maximum && ntyWorkerCreate(pool) == 0) {
		pool->nthreads ++;
	}
	
	pthread_mutex_unlock(&pool->mtx);
}

void nThreadPoolWait(nThreadPool *pool) 
{

	pthread_mutex_lock(&pool->mtx);
	pthread_cleanup_push(pthread_mutex_unlock, &pool->mtx);
	
	while (pool->head != NULL || pool->active != NULL) {
		pool->flags |= NTY_POOL_WAIT;
		pthread_cond_wait(&pool->waitcv, &pool->mtx);
	}
	
	pthread_cleanup_pop(1);
}

void nThreadPoolDestroy(nThreadPool *pool) 
{

	nWorker *activep;
	nJob *job;
	
	pthread_mutex_lock(&pool->mtx);
	pthread_cleanup_push(pthread_mutex_unlock, &pool->mtx);
	
	pool->flags |= NTY_POOL_DESTROY;
	pthread_cond_broadcast(&pool->workcv);
	
	for (activep = pool->active;activep != NULL;activep = activep->active_next) {
		pthread_cancel(activep->active_tid);
	}
	
	while (pool->nthreads != 0) {
		pthread_cond_wait(&pool->busycv, &pool->mtx);
	}
	
	pthread_cleanup_pop(1);
	pthread_mutex_lock(&nty_pool_lock);
	
	if (thread_pool == pool) {
		thread_pool = pool->forw;
	}
	
	if (thread_pool == pool) {
		thread_pool = NULL;
	} else {
		pool->back->forw = pool->forw;
		pool->forw->back = pool->back;
	}
	
	pthread_mutex_unlock(&nty_pool_lock);
	
	for (job = pool->head;job != NULL;job = pool->head) {
		pool->head = job->next;
		free(job);
	}
	pthread_attr_destroy(&pool->attr);
	free(pool);
 
}

/********************************* debug thread pool *********************************/

void king_counter(void *arg) 
{
	int index = *(int*)arg;
	printf("index : %d, selfid : %lu\n", index, pthread_self());
	
	free(arg);
	usleep(1);
}


#define KING_COUNTER_SIZE 1000


int main(int argc, char *argv[]) 
{

	nThreadPool *pool = ntyThreadPoolCreate(10, 20, 15, NULL);
	
	int i = 0;
	for (i = 0;i < KING_COUNTER_SIZE;i ++) {
	
		int *index = (int*)malloc(sizeof(int));
		
		memset(index, 0, sizeof(int));
		memcpy(index, &i, sizeof(int));
		
		ntyThreadPoolQueue(pool, king_counter, index);
	
	}
	
	return 0;
}