#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   //uintptr_t
#include <stdarg.h>   //va_start....
#include <unistd.h>   //STDERR_FILENO等
#include <sys/time.h> //gettimeofday
#include <time.h>     //localtime_r
#include <fcntl.h>    //open
#include <errno.h>    //errno
// #include <sys/socket.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_memory.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"

ssize_t CSocket::recvproc(lpngx_connection_t c, char *buff, ssize_t buflen)
{
    ssize_t n;
    n = recv(c->fd, buff, buflen, 0);
    if (n == 0)
    {
        ngx_log_stderr(0, "连接被客户端正常关闭[四次挥手关闭]!");
        inRecyConnectQueue(c);
        return -1;
    }
    if (n < 0)
    {
        ngx_log_stderr(0, "连接被客户端非正常关闭!");
        // EAGAIN和EWOULDBLOCK[【这个应该常用在hp上】应该是一样的值，表示没收到数据，一般来讲，在ET模式下会出现这个错误，因为ET模式下是不停的recv肯定有一个时刻收到这个errno，但LT模式下一般是来事件才收，所以不该出现这个返回值
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // 我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            ngx_log_stderr(errno, "CSocket::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立，出乎我意料！"); // epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1;                                                                                               // 不当做错误处理，只是简单返回
        }
        if (errno == EINTR) // 这个不算错误，是我参考官方nginx，官方nginx这个就不算错误；
        {
            // 我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            ngx_log_stderr(errno, "CSocket::recvproc()中errno == EINTR成立，出乎我意料！"); // epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1;                                                                      // 不当做错误处理，只是简单返回
        }
        if (errno == ECONNRESET) // #define ECONNRESET 104 /* Connection reset by peer */
        {
        }
        else
        {
            // 能走到这里的，都表示错误，我打印一下日志，希望知道一下是啥错误，我准备打印到屏幕上
            ngx_log_stderr(errno, "CSocekt::recvproc()中发生错误，我打印出来看看是啥错误！"); // 正式运营时可以考虑这些日志打印去掉
        }
        if(close(c->fd) == -1)
        {
            ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::recvproc()中close_2(%d)失败!",c->fd);  
        }
        inRecyConnectQueue(c);
        return -1;
    }
    return n;
}

// 来数据时候的处理，当连接上有数据来的时候，本函数会被ngx_epoll_process_events()所调用  ,官方的类似函数为ngx_http_wait_request_handler();
void CSocket::ngx_wait_request_handler(lpngx_connection_t c)
{
    ssize_t reco = recvproc(c, c->precvbuf, c->irecvlen);
    if (reco <= 0)
    {
        return;
    }
    if (c->curStat == _PKG_HD_INIT)
    {
        if (reco == c->irecvlen)
        {
            c->curStat = m_iLenPkgHeader;
            ngx_wait_request_handler_proc_p1(c);
        }
        else
        {
            c->curStat = _PKG_HD_RECVING;
            c->precvbuf = c->precvbuf + reco;
            c->irecvlen = c->irecvlen - reco;
        }
    }
    else if (c->curStat == _PKG_HD_RECVING)
    {
        if (c->irecvlen == reco)
        {
            ngx_wait_request_handler_proc_p1(c); // 那就调用专门针对包头处理完整的函数去处理把。
        }
        else
        {
            c->precvbuf = c->precvbuf + reco;
            c->irecvlen = c->irecvlen - reco;
        }
    }
    else if (c->curStat == _PKG_BD_INIT)
    {
        if (reco == c->irecvlen)
        {
            ngx_wait_request_handler_proc_plast(c);
        }
        else
        {
            c->curStat = _PKG_BD_RECVING;
            c->precvbuf = c->precvbuf + reco;
            c->irecvlen = c->irecvlen - reco;
        }
    }
    else if (c->curStat == _PKG_BD_RECVING)
    {
        if (c->irecvlen == reco)
        {
            ngx_wait_request_handler_proc_plast(c);
        }
        else
        {
            c->precvbuf = c->precvbuf + reco;
            c->irecvlen = c->irecvlen - reco;
        }
    }
    return;
}

void CSocket::ngx_wait_request_handler_proc_p1(lpngx_connection_t c)
{
    CMemory *p_memory = CMemory::GetInstance();
    LPCOMM_PKG_HEADER pPkgHeader;
    pPkgHeader = (LPCOMM_PKG_HEADER)c->dataHeadInfo;
    unsigned short e_pkgLen;
    e_pkgLen = ntohs(pPkgHeader->pkgLen);
    if (e_pkgLen < m_iLenPkgHeader || e_pkgLen > (_PKG_MAX_LENGTH - 1000))
    {
        c->curStat = _PKG_HD_INIT;
        c->precvbuf = c->dataHeadInfo;
        c->irecvlen = m_iLenPkgHeader;
    }
    else
    {
        char *pTmpBuffer = (char *)p_memory->AllocMemory(e_pkgLen + m_iLenMsgHeader, false);
        c->precvMemPointer = pTmpBuffer;
        // 填写消息头
        LPSTRUC_MSG_HEADER ptmpMsgHeader = (LPSTRUC_MSG_HEADER)pTmpBuffer;
        ptmpMsgHeader->pConn = c;
        ptmpMsgHeader->iCurrsequence = c->iCurrsequence;
        // 填写包头
        pTmpBuffer += m_iLenMsgHeader;
        memcpy(pTmpBuffer, pPkgHeader, m_iLenPkgHeader);
        // 填写包体
        if (e_pkgLen == m_iLenPkgHeader)
        {
            ngx_wait_request_handler_proc_plast(c);
        }
        else
        {
            c->curStat = _PKG_BD_INIT;
            c->precvbuf = pTmpBuffer + m_iLenPkgHeader;
            c->irecvlen = e_pkgLen - m_iLenPkgHeader;
        }
    }
    return;
}

void CSocket::ngx_wait_request_handler_proc_plast(lpngx_connection_t c)
{
    // 把这段内存放到消息队列中来；
    g_threadpool.inMsgRecvQueueAndSignal(c->precvMemPointer);
    c->precvMemPointer = nullptr;
    c->curStat = _PKG_HD_INIT;     // 收包状态机的状态恢复为原始态，为收下一个包做准备
    c->precvbuf = c->dataHeadInfo; // 设置好收包的位置
    c->irecvlen = m_iLenPkgHeader; // 设置好要接收数据的大小
    return;
}

ssize_t CSocket::sendproc(lpngx_connection_t c,char *buff,ssize_t size)  //ssize_t是有符号整型，在32位机器上等同与int，在64位机器上等同与long int，size_t就是无符号型的ssize_t
{
    //这里参考借鉴了官方nginx函数ngx_unix_send()的写法
    ssize_t   n;

    for ( ;; )
    {
        n = send(c->fd, buff, size, 0); //send()系统函数， 最后一个参数flag，一般为0； 
        if(n > 0) //成功发送了一些数据
        {        
            //发送成功一些数据，但发送了多少，我们这里不关心，也不需要再次send
            //这里有两种情况
            //(1) n == size也就是想发送多少都发送成功了，这表示完全发完毕了
            //(2) n < size 没发送完毕，那肯定是发送缓冲区满了，所以也不必要重试发送，直接返回吧
            return n; //返回本次发送的字节数
        }

        if(n == 0)
        {
            //send()返回0？ 一般recv()返回0表示断开,send()返回0，我这里就直接返回0吧【让调用者处理】；我个人认为send()返回0，要么你发送的字节是0，要么对端可能断开。
            //网上找资料：send=0表示超时，对方主动关闭了连接过程
            //我们写代码要遵循一个原则，连接断开，我们并不在send动作里处理诸如关闭socket这种动作，集中到recv那里处理，否则send,recv都处理都处理连接断开关闭socket则会乱套
            //连接断开epoll会通知并且 recvproc()里会处理，不在这里处理
            return 0;
        }

        if(errno == EAGAIN)  //这东西应该等于EWOULDBLOCK
        {
            //内核缓冲区满，这个不算错误
            return -1;  //表示发送缓冲区满了
        }

        if(errno == EINTR) 
        {
            //这个应该也不算错误 ，收到某个信号导致send产生这个错误？
            //参考官方的写法，打印个日志，其他啥也没干，那就是等下次for循环重新send试一次了
            ngx_log_stderr(errno,"CSocekt::sendproc()中send()失败.");  //打印个日志看看啥时候出这个错误
            //其他不需要做什么，等下次for循环吧            
        }
        else
        {
            //走到这里表示是其他错误码，都表示错误，错误我也不断开socket，我也依然等待recv()来统一处理断开，因为我是多线程，send()也处理断开，recv()也处理断开，很难处理好
            return -2;    
        }
    } //end for
}
void CSocket::ngx_write_request_handler(lpngx_connection_t pConn){
    ssize_t n;
    CMemory *p_memory = CMemory::GetInstance();
    n=sendproc(pConn,pConn->psendbuf,(ssize_t)(pConn->isendlen));
    if(n > 0){
        if(n == pConn->isendlen){
            if(ngx_epoll_oper_event(pConn->fd,EPOLL_CTL_MOD,EPOLLOUT,1,pConn)== -1){
                ngx_log_stderr(errno,"CSocekt::ngx_write_request_handler()中ngx_epoll_oper_event()失败。");
            }
            ngx_log_stderr(0,"CSocekt::ngx_write_request_handler()中数据发送完毕，很好。");
        }
        else{
            pConn->psendbuf+=n;
            pConn->isendlen-=n;
            return;
        }
    }
    else if(n == -1){
        ngx_log_stderr(errno,"CSocekt::ngx_write_request_handler()时if(sendsize == -1)成立，这很怪异。");
        return;
    }
    if(sem_post(&m_semEventSendQueue)==-1)       
        ngx_log_stderr(0,"CSocekt::ngx_write_request_handler()中sem_post(&m_semEventSendQueue)失败.");
    p_memory->FreeMemory(pConn->psendMemPointer);
    pConn->psendMemPointer=nullptr;
    --pConn->iThrowsendCount;
    return;
} 

void CSocket::threadRecvProcFunc(char *pMsgBuf){
    return;
}
