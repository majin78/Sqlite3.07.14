/*
** 2008 October 7
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file contains code use to implement an in-memory rollback journal.
** The in-memory rollback journal is used to journal transactions for
** ":memory:" databases and when the journal_mode=MEMORY pragma is used.
*/
//此文件用于实现内存回滚日志
//journal_mode=MEMORY需开启
#include "sqliteInt.h"

/* Forward references to internal structures */
typedef struct MemJournal MemJournal;
typedef struct FilePoint FilePoint;
typedef struct FileChunk FileChunk;

/* Space to hold the rollback journal is allocated in increments of
** this many bytes.
**
** The size chosen is a little less than a power of two.  That way,
** the FileChunk object will have a size that almost exactly fills
** a power-of-two allocation.  This mimimizes wasted space in power-of-two
** memory allocators.
*/
//定义回滚的空间大小，最大1M
#define JOURNAL_CHUNKSIZE ((int)(1024-sizeof(FileChunk*)))

/* Macro to find the minimum of two numeric values.
*/
#ifndef MIN
# define MIN(x,y) ((x)<(y)?(x):(y))
#endif

/*
** The rollback journal is composed of a linked list of these structures.
*/
//文件块列表结构体
struct FileChunk {
  FileChunk *pNext;               /* Next chunk in the journal */
  u8 zChunk[JOURNAL_CHUNKSIZE];   /* Content of this chunk */
};

/*
** An instance of this object serves as a cursor into the rollback journal.
** The cursor can be either for reading or writing.
*/
//插入回滚日志的指针
struct FilePoint {
  sqlite3_int64 iOffset;          /* Offset from the beginning of the file */
  FileChunk *pChunk;              /* Specific chunk into which cursor points */
};

/*
** This subclass is a subclass of sqlite3_file.  Each open memory-journal
** is an instance of this class.
*/
// memory-journal 总结构体
struct MemJournal {
  sqlite3_io_methods *pMethod;    /* Parent class. MUST BE FIRST */
  FileChunk *pFirst;              /* Head of in-memory chunk-list */
  FilePoint endpoint;             /* Pointer to the end of the file */
  FilePoint readpoint;            /* Pointer to the end of the last xRead() */
};

/*
** Read data from the in-memory journal file.  This is the implementation
** of the sqlite3_vfs.xRead method.
*/
//从内存日志文件中读取数据
static int memjrnlRead(
  sqlite3_file *pJfd,    /* The journal file from which to read */ //读取文件的指针
  void *zBuf,            /* Put the results here */   //读取的结果
  int iAmt,              /* Number of bytes to read */  //读取的字节数
  sqlite_int64 iOfst     /* Begin reading at this offset */  //从该偏移量开始读取
){
  MemJournal *p = (MemJournal *)pJfd;
  u8 *zOut = zBuf;
  int nRead = iAmt;
  int iChunkOffset;
  FileChunk *pChunk;

  /* SQLite never tries to read past the end of a rollback journal file */
  assert( iOfst+iAmt<=p->endpoint.iOffset );//开始的偏移量加上读取的字节数 >文件结尾的偏移量 即溢出，则报警

  if( p->readpoint.iOffset!=iOfst || iOfst==0 ){//不是从文件头或者文件尾开始读
    sqlite3_int64 iOff = 0; //总共需读取的偏移量
    for(pChunk=p->pFirst; 
        ALWAYS(pChunk) && (iOff+JOURNAL_CHUNKSIZE)<=iOfst; //# define ALWAYS(X)      (X)  偏移量未读完则继续
        pChunk=pChunk->pNext
    ){
      iOff += JOURNAL_CHUNKSIZE;//总读取数累加
    }
  }else{
    pChunk = p->readpoint.pChunk;//设置为文件块的结尾
  }

  iChunkOffset = (int)(iOfst%JOURNAL_CHUNKSIZE);//定义iOfst长度
  do {
    int iSpace = JOURNAL_CHUNKSIZE - iChunkOffset;//iSpace为所剩空间
    int nCopy = MIN(nRead, (JOURNAL_CHUNKSIZE - iChunkOffset));//输出最小的长度块
    memcpy(zOut, &pChunk->zChunk[iChunkOffset], nCopy);
    zOut += nCopy;//输出长度加
    nRead -= iSpace;//剩余空间减
    iChunkOffset = 0;
  } while( nRead>=0 && (pChunk=pChunk->pNext)!=0 && nRead>0 );
  p->readpoint.iOffset = iOfst+iAmt;
  p->readpoint.pChunk = pChunk;

  return SQLITE_OK;
}

/*
** Write data to the file.
*/
//写数据到内存日志文件
static int memjrnlWrite(
  sqlite3_file *pJfd,    /* The journal file into which to write */
  const void *zBuf,      /* Take data to be written from here */
  int iAmt,              /* Number of bytes to write */
  sqlite_int64 iOfst     /* Begin writing at this offset into the file */
){
  MemJournal *p = (MemJournal *)pJfd;
  int nWrite = iAmt;
  u8 *zWrite = (u8 *)zBuf;

  /* An in-memory journal file should only ever be appended to. Random
  ** access writes are not required by sqlite.
  */
  //列表访问，不支持随机读写
  assert( iOfst==p->endpoint.iOffset );
  UNUSED_PARAMETER(iOfst);

  while( nWrite>0 ){
    FileChunk *pChunk = p->endpoint.pChunk;
    int iChunkOffset = (int)(p->endpoint.iOffset%JOURNAL_CHUNKSIZE);
    int iSpace = MIN(nWrite, JOURNAL_CHUNKSIZE - iChunkOffset);

    if( iChunkOffset==0 ){//偏移为0 需要新建一个文件块
      /* New chunk is required to extend the file. */
      FileChunk *pNew = sqlite3_malloc(sizeof(FileChunk));
      if( !pNew ){
        return SQLITE_IOERR_NOMEM;
      }
      pNew->pNext = 0;//新建块的指针初始化
      if( pChunk ){
        assert( p->pFirst ); //文件头指针为0
        pChunk->pNext = pNew;
      }else{
        assert( !p->pFirst );//文件头指针不为0
        p->pFirst = pNew;
      }
      p->endpoint.pChunk = pNew; 
    }

    memcpy(&p->endpoint.pChunk->zChunk[iChunkOffset], zWrite, iSpace);//复制一个文件块
    zWrite += iSpace;
    nWrite -= iSpace;
    p->endpoint.iOffset += iSpace;
  }

  return SQLITE_OK;
}

/*
** Truncate the file.
*/
//清空内存日志文件
static int memjrnlTruncate(sqlite3_file *pJfd, sqlite_int64 size){
  MemJournal *p = (MemJournal *)pJfd;
  FileChunk *pChunk;
  assert(size==0);
  UNUSED_PARAMETER(size);
  pChunk = p->pFirst;//找到文件块头指针
  while( pChunk ){
    FileChunk *pTmp = pChunk;
    pChunk = pChunk->pNext;//内存日志文件块指针下移
    sqlite3_free(pTmp); 
  }
  sqlite3MemJournalOpen(pJfd);
  return SQLITE_OK;
}

/*
** Close the file.
*/
//关闭内存日志文件
static int memjrnlClose(sqlite3_file *pJfd){
  memjrnlTruncate(pJfd, 0);//关闭前先清空内存日志文件
  return SQLITE_OK;
}


/*
** Sync the file.
**
** Syncing an in-memory journal is a no-op.  And, in fact, this routine
** is never called in a working implementation.  This implementation
** exists purely as a contingency, in case some malfunction in some other
** part of SQLite causes Sync to be called by mistake.
*/
//同步内存日志文件
//无操作符,一些错误导致的不同步的预防方案，极少使用
static int memjrnlSync(sqlite3_file *NotUsed, int NotUsed2){
  UNUSED_PARAMETER2(NotUsed, NotUsed2);//宏定义 (void)(NotUsed),(void)(NotUsed2)
  return SQLITE_OK;
}

/*
** Query the size of the file in bytes.
*/
//查询文件多少字节
static int memjrnlFileSize(sqlite3_file *pJfd, sqlite_int64 *pSize){//  typedef __int64 sqlite_int64;
  MemJournal *p = (MemJournal *)pJfd;
  *pSize = (sqlite_int64) p->endpoint.iOffset;//返回末尾的偏移即可
  return SQLITE_OK;
}

/*
** Table of methods for MemJournal sqlite3_file object.
*/
//内存日志文件MemJournal的操作定义
static const struct sqlite3_io_methods MemJournalMethods = {
  1,                /* iVersion */
  memjrnlClose,     /* xClose */
  memjrnlRead,      /* xRead */
  memjrnlWrite,     /* xWrite */
  memjrnlTruncate,  /* xTruncate */
  memjrnlSync,      /* xSync */
  memjrnlFileSize,  /* xFileSize */
  0,                /* xLock */
  0,                /* xUnlock */
  0,                /* xCheckReservedLock */
  0,                /* xFileControl */
  0,                /* xSectorSize */
  0,                /* xDeviceCharacteristics */
  0,                /* xShmMap */
  0,                /* xShmLock */
  0,                /* xShmBarrier */
  0                 /* xShmUnlock */
};

/* 
** Open a journal file.
*/
//打开内存日志文件
void sqlite3MemJournalOpen(sqlite3_file *pJfd){
  MemJournal *p = (MemJournal *)pJfd;
  assert( EIGHT_BYTE_ALIGNMENT(p) );//# define EIGHT_BYTE_ALIGNMENT(X)   ((((char*)(X) - (char*)0)&7)==0)//八位比特对齐
  memset(p, 0, sqlite3MemJournalSize());  //将p所指向的某一块内存中的所以字节的内容全部设置为0指定的ASCII值
  p->pMethod = (sqlite3_io_methods*)&MemJournalMethods;//设置内存日志文件MemJournal的操作集指针
}

/*
** Return true if the file-handle passed as an argument is 
** an in-memory journal 
*/
//返回是否为内存日志文件
int sqlite3IsMemJournal(sqlite3_file *pJfd){
  return pJfd->pMethods==&MemJournalMethods;
}

/* 
** Return the number of bytes required to store a MemJournal file descriptor.
*/
//返回内存日志文件的长度
int sqlite3MemJournalSize(void){
  return sizeof(MemJournal);
}
