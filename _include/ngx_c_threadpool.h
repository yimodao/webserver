#ifndef __NGX_C_THREADPOOL_H__
#define __NGX_C_THREADPOOL_H__

#include <vector>
#include <list>
#include <atomic>
#include <pthread.h>

class CThreadPool
{
public:
    CThreadPool();
    ~CThreadPool();
public:
    bool Create(int threadNum);
    void StopAll();
    void inMsgRecvQueueAndSignal(char *buf);
    void clearMsgRecvQueue();
    void Call();
    static void* ThreadFunc(void *threadData);
private:
    struct ThreadItem   
    {
        pthread_t   _Handle;                        //线程句柄
        CThreadPool *_pThis;                        //记录线程池的指针	
        bool        ifrunning;                      //标记是否正式启动起来，启动起来后，才允许调用StopAll()来释放

        //构造函数
        ThreadItem(CThreadPool *pthis):_pThis(pthis),ifrunning(false){}                             
        //析构函数
        ~ThreadItem(){}        
    };
private:
    static pthread_mutex_t     m_pthreadMutex;      //线程同步互斥量/也叫线程同步锁
    static pthread_cond_t      m_pthreadCond;       //线程同步条件变量
    static bool                m_shutdown;          


    int                        m_iThreadNum;        //要创建的线程数量
    std::vector<ThreadItem*>   m_threadVector;      //线程vector
    std::atomic<int>           m_iRunningThreadNum; //运行线程数
    time_t                     m_iLastEmgTime;      //上次线程不够用的时刻


    //接收消息队列相关
    std::list<char *>          m_MsgRecvQueue;      //接收数据消息队列 
	int                        m_iRecvMsgQueueCount;//收消息队列大小
};

#endif