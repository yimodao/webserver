#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_threadpool.h"
#include "ngx_c_memory.h"

pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER;  //#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) -1)
pthread_cond_t CThreadPool::m_pthreadCond = PTHREAD_COND_INITIALIZER;     //#define PTHREAD_COND_INITIALIZER ((pthread_cond_t) -1)
bool CThreadPool::m_shutdown=false;

CThreadPool::CThreadPool(){
    m_iRunningThreadNum = 0;  //正在运行的线程，开始给个0【注意这种写法：原子的对象给0也可以直接赋值，当整型变量来用】
    m_iLastEmgTime = 0;       //上次报告线程不够用了的时间；
    m_iRecvMsgQueueCount = 0; //收消息队列
}
void CThreadPool::clearMsgRecvQueue(){
    char * sTmpMempoint;
	CMemory *p_memory = CMemory::GetInstance();

	//尾声阶段，需要互斥？该退的都退出了，该停止的都停止了，应该不需要退出了
	while(!m_MsgRecvQueue.empty())
	{
		sTmpMempoint = m_MsgRecvQueue.front();		
		m_MsgRecvQueue.pop_front(); 
		p_memory->FreeMemory(sTmpMempoint);
	}	
}
CThreadPool::~CThreadPool(){
    clearMsgRecvQueue();
}
bool CThreadPool::Create(int threadNum){
    ThreadItem*pNew;
    int err;
    m_iThreadNum=threadNum;
    for(int i=0;i<m_iThreadNum;i++){
        pNew=new ThreadItem(this);
        m_threadVector.push_back(pNew);
        err=pthread_create(&(pNew->_Handle),nullptr,ThreadFunc,pNew);
        if(err != 0)
        {
            //创建线程有错
            ngx_log_stderr(err,"CThreadPool::Create()创建线程%d失败，返回的错误码为%d!",i,err);
            return false;
        }
        else
        {
            //创建线程成功
            //ngx_log_stderr(0,"CThreadPool::Create()创建线程%d成功,线程id=%d",pNew->_Handle);
        }        
    }
    std::vector<ThreadItem*>::iterator pos;
lblfor:
    for(pos=m_threadVector.begin();pos!=m_threadVector.end();++pos){
        if((*pos)->ifrunning == false){
            usleep(100 * 1000);
            goto lblfor;
        }
    }
    return true;
}

void CThreadPool::StopAll(){
    if(m_shutdown == true){
        return;
    }
    m_shutdown=true;
    int err=pthread_cond_broadcast(&m_pthreadCond);
    if(err != 0)
    {
        //这肯定是有问题，要打印紧急日志
        ngx_log_stderr(err,"CThreadPool::StopAll()中pthread_cond_broadcast()失败，返回的错误码为%d!",err);
        return;
    }
    std::vector<ThreadItem*>::iterator pos;
    for(pos=m_threadVector.begin();pos!=m_threadVector.end();++pos){
        pthread_join((*pos)->_Handle,nullptr);
    }
    pthread_mutex_destroy(&m_pthreadMutex);
    pthread_cond_destroy(&m_pthreadCond); 
    for(pos=m_threadVector.begin();pos!=m_threadVector.end();++pos){
        if(*pos)
            delete (*pos);
    }
    m_threadVector.clear();
    ngx_log_stderr(0,"CThreadPool::StopAll()成功返回，线程池中线程全部正常结束!");
    return; 
}

void* CThreadPool::ThreadFunc(void *threadData){
    ThreadItem* pThread=static_cast<ThreadItem*>(threadData);
    CThreadPool *pThreadPoolObj = pThread->_pThis;
    CMemory *p_memory = CMemory::GetInstance();	  
    int err;
    while(true){
        int err=pthread_mutex_lock(&m_pthreadMutex);
        if(err != 0) ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_mutex_lock()失败，返回的错误码为%d!",err);
        while((pThreadPoolObj->m_MsgRecvQueue.size()==0) && m_shutdown==false){
            if(pThread->ifrunning==false){
                pThread->ifrunning=true;
            }
            pthread_cond_wait(&m_pthreadCond,&m_pthreadMutex);

        }
        if(m_shutdown)
        {   
            pthread_mutex_unlock(&m_pthreadMutex); //解锁互斥量
            break;                     
        }
        char* jobbuf=(pThreadPoolObj->m_MsgRecvQueue).front();
        pThreadPoolObj->m_MsgRecvQueue.pop_front();
        --pThreadPoolObj->m_iRecvMsgQueueCount;
        err=pthread_mutex_unlock(&m_pthreadMutex);
        if(err != 0)  ngx_log_stderr(err,"CThreadPool::ThreadFunc()pthread_cond_wait()失败，返回的错误码为%d!",err);//有问题，要及时报告
        ++pThreadPoolObj->m_iRunningThreadNum;
        //处理业务
        ngx_log_stderr(0,"服务器处理一条消息!");
        g_socket.threadRecvProcFunc(jobbuf);
        //........................................
        p_memory->FreeMemory(jobbuf); 
        --pThreadPoolObj->m_iRunningThreadNum;
    }
    return (void*)0;
}
void CThreadPool::inMsgRecvQueueAndSignal(char *buf){
    int err=pthread_mutex_lock(&m_pthreadMutex);
    if(err != 0)
    {
        ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_lock()失败，返回的错误码为%d!",err);
    }
    m_MsgRecvQueue.push_back(buf);
    ++m_iRecvMsgQueueCount;
    err = pthread_mutex_unlock(&m_pthreadMutex);   
    if(err != 0)
    {
        ngx_log_stderr(err,"CThreadPool::inMsgRecvQueueAndSignal()pthread_mutex_unlock()失败，返回的错误码为%d!",err);
    } 	
    Call();
}
void CThreadPool::Call(){
    int err = pthread_cond_signal(&m_pthreadCond); //唤醒一个等待该条件的线程，也就是可以唤醒卡在pthread_cond_wait()的线程
    if(err != 0 )
    {
        //这是有问题啊，要打印日志啊
        ngx_log_stderr(err,"CThreadPool::Call()中pthread_cond_signal()失败，返回的错误码为%d!",err);
    }
    if(m_iThreadNum == m_iRunningThreadNum) //线程池中线程总量，跟当前正在干活的线程数量一样，说明所有线程都忙碌起来，线程不够用了
    {        
        //线程不够用了
        //ifallthreadbusy = true;
        time_t currtime = time(NULL);
        if(currtime - m_iLastEmgTime > 10) //最少间隔10秒钟才报一次线程池中线程不够用的问题；
        {
            //两次报告之间的间隔必须超过10秒，不然如果一直出现当前工作线程全忙，但频繁报告日志也够烦的
            m_iLastEmgTime = currtime;  //更新时间
            //写日志，通知这种紧急情况给用户，用户要考虑增加线程池中线程数量了
            ngx_log_stderr(0,"CThreadPool::Call()中发现线程池中当前空闲线程数量为0，要考虑扩容线程池了!");
        }
    }
    return;
}
