/*
** 2007 August 14
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains the C functions that implement mutexes.
**这个文档包含互斥锁的实现
**
** This file contains code that is common across all mutex implementations.
**这个文档包含互斥锁的实现（拒绝百度翻译）
*/
#include "sqliteInt.h"

#if defined(SQLITE_DEBUG) && !defined(SQLITE_MUTEX_OMIT)
/*
** For debugging purposes, record when the mutex subsystem is initialized
** and uninitialized so that we can assert() if there is an attempt to
** allocate a mutex while the system is uninitialized.
**用于调试目的,记录时,互斥锁子系统初始化的和未初始化,
**以便我们可以断言()如果有试图分配一个互斥而系统未初始化。
**
**
*/
static SQLITE_WSD int mutexIsInit = 0;
#endif /* SQLITE_DEBUG */


#ifndef SQLITE_MUTEX_OMIT
/*
** Initialize the mutex system.初始化互斥系统
*/
int sqlite3MutexInit(void){ 
  int rc = SQLITE_OK;
  if( !sqlite3GlobalConfig.mutex.xMutexAlloc ){
    
    /* If the xMutexAlloc method has not been set, then the user did not
    ** install a mutex implementation via sqlite3_config() prior to 
    ** sqlite3_initialize() being called. This block copies pointers to
    ** the default implementation into the sqlite3GlobalConfig structure.
    **如果xMutexAlloc 函数没设置，
    **那么,用户没有安装一个互斥对象实现通过sqlite3_config () 
    **sqlite3_initialize()前被调用。
   ** 这段复制指针向sqlite3GlobalConfig默认实现结构。
    */
    	  //////////////////////////////////////////////////////////////////////////
	  /************************************************************************/
	  /* 我是华丽的分割线                                                                     */
	  /************************************************************************/
    	  //majin78 xMutexAlloc未设置，"sqliteInt.h" #ifdef SQLITE_OMIT_WSD  
// #define sqlite3GlobalConfig GLOBAL(struct Sqlite3Config, sqlite3Config) 宏定义 为Sqlite3Config结构体，后一参数为结构体size
	  //majin78 sqlite3GlobalConfig.mutex 属性为sqlite3_mem_methods 定义于sqlite3.h  typedef struct sqlite3_mutex_methods sqlite3_mutex_methods;
	  //struct sqlite3_mutex_methods 定义于sqlite3.h
	  
    sqlite3_mutex_methods const *pFrom;
    sqlite3_mutex_methods *pTo = &sqlite3GlobalConfig.mutex;

    if( sqlite3GlobalConfig.bCoreMutex ){//  int bCoreMutex;                   /* True to enable core mutexing */
      pFrom = sqlite3DefaultMutex();//#ifndef SQLITE_MUTEX_OMIT  sqlite3_mutex_methods const *sqlite3DefaultMutex(void);
    }else{
      pFrom = sqlite3NoopMutex();//  sqlite3_mutex_methods const *sqlite3NoopMutex(void);
    }
    memcpy(pTo, pFrom, offsetof(sqlite3_mutex_methods, xMutexAlloc));//offsetof该宏用于求结构体中一个成员在该结构体中的偏移量。
    memcpy(&pTo->xMutexFree, &pFrom->xMutexFree,
           sizeof(*pTo) - offsetof(sqlite3_mutex_methods, xMutexFree));
    pTo->xMutexAlloc = pFrom->xMutexAlloc;
  }
  rc = sqlite3GlobalConfig.mutex.xMutexInit();

#ifdef SQLITE_DEBUG
  GLOBAL(int, mutexIsInit) = 1;
#endif

  return rc;
}

/*
** Shutdown the mutex system. This call frees resources allocated by
** sqlite3MutexInit().
**关闭互斥系统。释放sqlite3MutexInit()分配的资源。
*/
int sqlite3MutexEnd(void){
  int rc = SQLITE_OK;
  if( sqlite3GlobalConfig.mutex.xMutexEnd ){
    rc = sqlite3GlobalConfig.mutex.xMutexEnd();
  }

#ifdef SQLITE_DEBUG
  GLOBAL(int, mutexIsInit) = 0;
#endif

  return rc;
}

/*
** Retrieve a pointer to a static mutex or allocate a new dynamic one.
**获取一个指向静态互斥锁或分配一个新的动态互斥锁。
*/
sqlite3_mutex *sqlite3_mutex_alloc(int id){
#ifndef SQLITE_OMIT_AUTOINIT
  if( sqlite3_initialize() ) return 0;
#endif
  return sqlite3GlobalConfig.mutex.xMutexAlloc(id);
}

sqlite3_mutex *sqlite3MutexAlloc(int id){
  if( !sqlite3GlobalConfig.bCoreMutex ){
    return 0;
  }
  assert( GLOBAL(int, mutexIsInit) );
  return sqlite3GlobalConfig.mutex.xMutexAlloc(id);
}

/*
** Free a dynamic mutex.释放一个动态互斥锁
*/
void sqlite3_mutex_free(sqlite3_mutex *p){
  if( p ){
    sqlite3GlobalConfig.mutex.xMutexFree(p);
  }
}

/*
** Obtain the mutex p. If some other thread already has the mutex, block
** until it can be obtained.
**获得互斥锁p。
**如果其他线程已经拥有互斥锁,
**那么就将其阻塞,直到它可以获得。
*/
void sqlite3_mutex_enter(sqlite3_mutex *p){
  if( p ){
    sqlite3GlobalConfig.mutex.xMutexEnter(p);
  }
}

/*
** Obtain the mutex p. If successful, return SQLITE_OK. Otherwise, if another
** thread holds the mutex and it cannot be obtained, return SQLITE_BUSY.
**获得互斥锁p。如果成功,返回SQLITE_OK。
**否则,如果另一个线程持有互斥锁,它不能得到,就返回SQLITE_BUSY。
*/
int sqlite3_mutex_try(sqlite3_mutex *p){
  int rc = SQLITE_OK;
  if( p ){
    return sqlite3GlobalConfig.mutex.xMutexTry(p);
  }
  return rc;
}

/*
** The sqlite3_mutex_leave() routine exits a mutex that was previously
** entered by the same thread.  The behavior is undefined if the mutex 
** is not currently entered. If a NULL pointer is passed as an argument
** this function is a no-op.
**sqlite3_mutex_leave() 程序退出之前由相同的线程输入的互斥对象。
**这个行为没被定义，如果该互斥锁不是当前输入的。
**如果一个空指针作为参数传递给该函数，那么就不做任何操作
**
*/
void sqlite3_mutex_leave(sqlite3_mutex *p){
  if( p ){
    sqlite3GlobalConfig.mutex.xMutexLeave(p);
  }
}

#ifndef NDEBUG
/*
** The sqlite3_mutex_held() and sqlite3_mutex_notheld() routine are
** intended for use inside assert() statements.
**sqlite3_mutex_held() 和sqlite3_mutex_notheld() 函数用于内部assert() 语句
**
*/
int sqlite3_mutex_held(sqlite3_mutex *p){
  return p==0 || sqlite3GlobalConfig.mutex.xMutexHeld(p);
}
int sqlite3_mutex_notheld(sqlite3_mutex *p){
  return p==0 || sqlite3GlobalConfig.mutex.xMutexNotheld(p);
}
#endif

#endif /* !defined(SQLITE_MUTEX_OMIT) */

