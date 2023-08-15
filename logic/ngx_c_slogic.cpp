#include <string.h>  //memcpy

#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <pthread.h> 

#include "ngx_c_lockmutex.h"
#include "ngx_c_socket.h"
#include "ngx_c_slogic.h"
#include "ngx_func.h"
#include "ngx_comm.h"
#include "ngx_c_crc32.h"
#include "ngx_logiccomm.h"
#include "ngx_c_memory.h"

//定义成员函数指针
typedef bool (CLogicSocket::*handler)(  lpngx_connection_t pConn,      //连接池中连接的指针
                                        LPSTRUC_MSG_HEADER pMsgHeader,  //消息头指针
                                        char *pPkgBody,                 //包体指针
                                        unsigned short iBodyLength);    //包体长度

//用来保存 成员函数指针 的这么个数组
static const handler statusHandler[] = 
{
    //数组前5个元素，保留，以备将来增加一些基本服务器功能
    nullptr,                                                   //【0】：下标从0开始
    nullptr,                                                   //【1】：下标从0开始
    nullptr,                                                   //【2】：下标从0开始
    nullptr,                                                   //【3】：下标从0开始
    nullptr,                                                   //【4】：下标从0开始

    //开始处理具体的业务逻辑
    &CLogicSocket::_HandleRegister,                            //【5】：实现具体的注册功能
    &CLogicSocket::_HandleLogIn,                               //【6】：实现具体的登录功能
    //......其他待扩展，比如实现攻击功能，实现加血功能等等；


};
#define AUTH_TOTAL_COMMANDS sizeof(statusHandler)/sizeof(handler) //整个命令有多少个，编译时即可知道


CLogicSocket::CLogicSocket(){

}
CLogicSocket::~CLogicSocket(){

}
bool CLogicSocket::Initialize(){
    bool bParentInit=CSocket::Initialize();
    return bParentInit;
}

void CLogicSocket::threadRecvProcFunc(char *pMsgBuf){
    LPSTRUC_MSG_HEADER pMsgHeader=(LPSTRUC_MSG_HEADER)pMsgBuf;
    LPCOMM_PKG_HEADER pPkgHeader=(LPCOMM_PKG_HEADER)(pMsgBuf+m_iLenMsgHeader);
    void  *pPkgBody;
    unsigned short pkglen = ntohs(pPkgHeader->pkgLen);
    if(m_iLenPkgHeader == pkglen){
        if(pPkgHeader->crc32!=0){
            return;
        }
        pPkgBody=nullptr;
    }
    else{
        pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);		          //针对4字节的数据，网络序转主机序
		pPkgBody = (void *)(pMsgBuf+m_iLenMsgHeader+m_iLenPkgHeader); //跳过消息头 以及 包头 ，指向包体
        int calccrc = CCRC32::GetInstance()->Get_CRC((unsigned char *)pPkgBody,pkglen-m_iLenPkgHeader);
        if(calccrc!=pPkgHeader->crc32){
            ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中CRC错误，丢弃数据!");
            return;
        }
    }
    unsigned short imsgCode = ntohs(pPkgHeader->msgCode); //消息代码拿出来
    lpngx_connection_t p_Conn = pMsgHeader->pConn;        //消息头中藏着连接池中连接的指针
    if(p_Conn->iCurrsequence != pMsgHeader->iCurrsequence){
        return;
    }
    if(imsgCode >= AUTH_TOTAL_COMMANDS){
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码不对!",imsgCode); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return; 
    }
    if(statusHandler[imsgCode] == NULL) //这种用imsgCode的方式可以使查找要执行的成员函数效率特别高
    {
        ngx_log_stderr(0,"CLogicSocket::threadRecvProcFunc()中imsgCode=%d消息码找不到对应的处理函数!",imsgCode); //这种有恶意倾向或者错误倾向的包，希望打印出来看看是谁干的
        return;  //没有相关的处理函数
    }
    (this->*statusHandler[imsgCode])(p_Conn,pMsgHeader,(char*)pPkgBody,pkglen-m_iLenPkgHeader);
    return;
}

bool CLogicSocket::_HandleRegister(lpngx_connection_t pConn,LPSTRUC_MSG_HEADER pMsgHeader,char *pPkgBody,unsigned short iBodyLength){
    if(pPkgBody == nullptr){
        return false;
    }
    int iRecvLen = sizeof(STRUCT_REGISTER);
    if(iRecvLen!=iBodyLength){
        return false;
    }
    CLock(&(pConn->logicPorcMutex));
    LPSTRUCT_REGISTER p_RecvInfo = (LPSTRUCT_REGISTER)pPkgBody;
    LPCOMM_PKG_HEADER pPkgHeader;
    CMemory  *p_memory=CMemory::GetInstance();
    CCRC32   *p_crc32 = CCRC32::GetInstance();
    int iSendLen = 65000;//测试用
    char *p_sendbuf=(char*)p_memory->AllocMemory(m_iLenMsgHeader+m_iLenPkgHeader+iSendLen,false);
    //填充消息头
    memcpy(p_sendbuf,pMsgHeader,m_iLenMsgHeader);
    //填充包头
    pPkgHeader=(LPCOMM_PKG_HEADER)(p_sendbuf+m_iLenMsgHeader);
    pPkgHeader->msgCode=_CMD_REGISTER;
    pPkgHeader->msgCode=htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen=htons(m_iLenPkgHeader+iSendLen);
    //填充包体
    LPSTRUCT_REGISTER p_sendInfo = (LPSTRUCT_REGISTER)(p_sendbuf+m_iLenMsgHeader+m_iLenPkgHeader);
    //.........................

    pPkgHeader->crc32=p_crc32->Get_CRC((unsigned char*)p_sendInfo,iSendLen);
    pPkgHeader->crc32=htons(pPkgHeader->crc32);
    msgSend(p_sendbuf);
    return true;
}
bool CLogicSocket::_HandleLogIn(lpngx_connection_t pConn, LPSTRUC_MSG_HEADER pMsgHeader, char *pPkgBody, unsigned short iBodyLength){
    return true;
}


    