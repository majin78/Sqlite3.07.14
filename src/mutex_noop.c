/*
** 2008 October 07
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
**
** This implementation in this file does not provide any mutual
** exclusion and is thus suitable for use only in applications
** that use SQLite in a single thread.  The routines defined
** here are place-holders.  Applications can substitute working
** mutex routines at start-time using the
**
**     sqlite3_config(SQLITE_CONFIG_MUTEX,...)
**
** interface.
**
** If compiled with SQLITE_DEBUG, then additional logic is inserted
** that does error checking on mutexes to make sure they are being
** called correctly.
*/
//此文件定义的互斥体只适用但进程调用
//通过如下方式适用 sqlite3_config(SQLITE_CONFIG_MUTEX,...)
//定义了SQLITE_DEBUG 则使用logic进行错误检查
#include "sqliteInt.h"

#ifndef SQLITE_MUTEX_OMIT//非调试的情况运行

#ifndef SQLITE_DEBUG
/*
** Stub routines for all mutex methods.
//所有互斥体实现的基本函数
**
** This routines provide no mutual exclusion or error checking.
*/
static int noopMutexInit(void){ return SQLITE_OK; }
static int noopMutexEnd(void){ return SQLITE_OK; }
static sqlite3_mutex *noopMutexAlloc(int id){ 
  UNUSED_PARAMETER(id);// 指针转化(void)(id)
  return (sqlite3_mutex*)8; //默认返回8
}
static void noopMutexFree(sqlite3_mutex *p){ UNUSED_PARAMETER(p); return; }
static void noopMutexEnter(sqlite3_mutex *p){ UNUSED_PARAMETER(p); return; }
static int noopMutexTry(sqlite3_mutex *p){
  UNUSED_PARAMETER(p);
  return SQLITE_OK;
}
static void noopMutexLeave(sqlite3_mutex *p){ UNUSED_PARAMETER(p); return; }

sqlite3_mutex_methods const *sqlite3NoopMutex(void){
  static const sqlite3_mutex_methods sMutex = {
    noopMutexInit,
    noopMutexEnd,
    noopMutexAlloc,
    noopMutexFree,
    noopMutexEnter,
    noopMutexTry,
    noopMutexLeave,

    0, // int (*xMutexHeld)(sqlite3_mutex *); 
    0,// int (*xMutexNotheld)(sqlite3_mutex *);
  };

  return &sMutex;//返回分配的互斥体
}
#endif /* !SQLITE_DEBUG */

#ifdef SQLITE_DEBUG
/*
** In this implementation, error checking is provided for testing
** and debugging purposes.  The mutexes still do not provide any
** mutual exclusion.
*/
//调试的情况
/*
** The mutex object
*/
typedef struct sqlite3_debug_mutex {  //调试专用互斥体
  int id;     /* The mutex type */
  int cnt;    /* Number of entries without a matching leave */
} sqlite3_debug_mutex;

/*
** The sqlite3_mutex_held() and sqlite3_mutex_notheld() routine are
** intended for use inside assert() statements.
*/
//debug 下检查是否（PENDING锁）多个进程处于中间状态
static int debugMutexHeld(sqlite3_mutex *pX){
  sqlite3_debug_mutex *p = (sqlite3_debug_mutex*)pX;
  return p==0 || p->cnt>0;//互斥体指针不为空 且进入的进程数量为1 才返回0  即只有一个处于转化状态（PENDING锁）
}
static int debugMutexNotheld(sqlite3_mutex *pX){
  sqlite3_debug_mutex *p = (sqlite3_debug_mutex*)pX;
  return p==0 || p->cnt==0;//互斥体指针不为空 且进入的进程数量大于1 才返回0 即非转化状态（其他锁）
}

/*
** Initialize and deinitialize the mutex subsystem.
*/
//DEBUG时初始和销毁互斥体系统，无意义
static int debugMutexInit(void){ return SQLITE_OK; }
static int debugMutexEnd(void){ return SQLITE_OK; }

/*
** The sqlite3_mutex_alloc() routine allocates a new
** mutex and returns a pointer to it.  If it returns NULL
** that means that a mutex could not be allocated. 
*/
//
static sqlite3_mutex *debugMutexAlloc(int id){
  static sqlite3_debug_mutex aStatic[6];//定义了静态的6个互斥体
  sqlite3_debug_mutex *pNew = 0;
  switch( id ){//id为互斥体类型
    case SQLITE_MUTEX_FAST://#define SQLITE_MUTEX_FAST             0
    case SQLITE_MUTEX_RECURSIVE: {//#define SQLITE_MUTEX_RECURSIVE        1
      pNew = sqlite3Malloc(sizeof(*pNew));//分配mem0.mutex 返回指针
      if( pNew ){
        pNew->id = id;
        pNew->cnt = 0;
      }
      break;
    }
    default: {
	/************************************************************************/
	/* 其他情况
// #define SQLITE_MUTEX_STATIC_MASTER    2
// #define SQLITE_MUTEX_STATIC_MEM       3  /* sqlite3_malloc() */
// #define SQLITE_MUTEX_STATIC_MEM2      4  /* NOT USED */
// #define SQLITE_MUTEX_STATIC_OPEN      4  /* sqlite3BtreeOpen() */
// #define SQLITE_MUTEX_STATIC_PRNG      5  /* sqlite3_random() */
// #define SQLITE_MUTEX_STATIC_LRU       6  /* lru page list */
// #define SQLITE_MUTEX_STATIC_LRU2      7  /* NOT USED */
// #define SQLITE_MUTEX_STATIC_PMEM      7  /* sqlite3PageMalloc() */                                                                     */
	/************************************************************************/
      assert( id-2 >= 0 );//debug情况下，检查错误
      assert( id-2 < (int)(sizeof(aStatic)/sizeof(aStatic[0])) );//debug情况下，检查错误
      pNew = &aStatic[id-2];
      pNew->id = id;
      break;
    }
  }
  return (sqlite3_mutex*)pNew;//返回互斥体指针
}

/*
** This routine deallocates a previously allocated mutex.
*/
static void debugMutexFree(sqlite3_mutex *pX){
  sqlite3_debug_mutex *p = (sqlite3_debug_mutex*)pX;
  //debug情况下，检查错误
  assert( p->cnt==0 );//进程数为零 报警
  assert( p->id==SQLITE_MUTEX_FAST || p->id==SQLITE_MUTEX_RECURSIVE );  //锁类型2以下也报警
  sqlite3_free(p);
}

/*
** The sqlite3_mutex_enter() and sqlite3_mutex_try() routines attempt
** to enter a mutex.  If another thread is already within the mutex,
** sqlite3_mutex_enter() will block and sqlite3_mutex_try() will return
** SQLITE_BUSY.  The sqlite3_mutex_try() interface returns SQLITE_OK
** upon successful entry.  Mutexes created using SQLITE_MUTEX_RECURSIVE can
** be entered multiple times by the same thread.  In such cases the,
** mutex must be exited an equal number of times before another thread
** can enter.  If the same thread tries to enter any other kind of mutex
** more than once, the behavior is undefined.
*/
//忙则返回SQLITE_BUSY 否则分配锁 未定义同一进程取得多种类型锁的情况
static void debugMutexEnter(sqlite3_mutex *pX){
  sqlite3_debug_mutex *p = (sqlite3_debug_mutex*)pX;
  assert( p->id==SQLITE_MUTEX_RECURSIVE || debugMutexNotheld(pX) );//检查是否多个处于（排斥锁）状态 是则报错
  p->cnt++;//进程进来一个 数量加1
}
static int debugMutexTry(sqlite3_mutex *pX){
  sqlite3_debug_mutex *p = (sqlite3_debug_mutex*)pX;
  assert( p->id==SQLITE_MUTEX_RECURSIVE || debugMutexNotheld(pX) );
  p->cnt++;
  return SQLITE_OK;
}

/*
** The sqlite3_mutex_leave() routine exits a mutex that was
** previously entered by the same thread.  The behavior
** is undefined if the mutex is not currently entered or
** is not currently allocated.  SQLite will never do either.
*/
static void debugMutexLeave(sqlite3_mutex *pX){
  sqlite3_debug_mutex *p = (sqlite3_debug_mutex*)pX;
  assert( debugMutexHeld(pX) );//是否pending锁状态，是报错
  p->cnt--;//进程进来一个 数量加1
  assert( p->id==SQLITE_MUTEX_RECURSIVE || debugMutexNotheld(pX) );//检查是否多个处于（排斥锁）状态 是则报错
}
//定义了sqlite3NoopMutex
sqlite3_mutex_methods const *sqlite3NoopMutex(void){
  static const sqlite3_mutex_methods sMutex = {
    debugMutexInit,
    debugMutexEnd,
    debugMutexAlloc,
    debugMutexFree,
    debugMutexEnter,
    debugMutexTry,
    debugMutexLeave,

    debugMutexHeld,
    debugMutexNotheld
  };

  return &sMutex;
}
#endif /* SQLITE_DEBUG */

/*
** If compiled with SQLITE_MUTEX_NOOP, then the no-op mutex implementation
** is used regardless of the run-time threadsafety setting.
*/
#ifdef SQLITE_MUTEX_NOOP
sqlite3_mutex_methods const *sqlite3DefaultMutex(void){
  return sqlite3NoopMutex();
}
#endif /* defined(SQLITE_MUTEX_NOOP) */
#endif /* !defined(SQLITE_MUTEX_OMIT) */
