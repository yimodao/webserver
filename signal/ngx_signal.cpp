#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_global.h"


static void ngx_process_get_status(void);
static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext); 
typedef struct{
    int          signo;
    const char*  signame;
    void (*handler)(int signo, siginfo_t *siginfo, void *ucontext);

}ngx_signal_t;


ngx_signal_t  signals[] = {
    // signo      signame             handler
    { SIGHUP,    "SIGHUP",           ngx_signal_handler },        //终端断开信号，对于守护进程常用于reload重载配置文件通知--标识1
    { SIGINT,    "SIGINT",           ngx_signal_handler },        //标识2   
	{ SIGTERM,   "SIGTERM",          ngx_signal_handler },        //标识15
    { SIGCHLD,   "SIGCHLD",          ngx_signal_handler },        //子进程退出时，父进程会收到这个信号--标识17
    { SIGQUIT,   "SIGQUIT",          ngx_signal_handler },        //标识3
    { SIGIO,     "SIGIO",            ngx_signal_handler },        //指示一个异步I/O事件【通用异步I/O信号】
    { SIGSYS,    "SIGSYS, SIG_IGN",  NULL            },        //我们想忽略这个信号，SIGSYS表示收到了一个无效系统调用，如果我们不忽略，进程会被操作系统杀死，--标识31
                                                                  //所以我们把handler设置为NULL，代表 我要求忽略这个信号，请求操作系统不要执行缺省的该信号处理动作（杀掉我）
    //...日后根据需要再继续增加
    { 0,         NULL,             NULL           }         //信号对应的数字至少是1，所以可以用0作为一个特殊标记
};
int ngx_init_signals(){
    ngx_signal_t* sig;
    struct sigaction sa;
    for(sig=signals;sig->signo!=0;sig++){
        memset(&sa,0,sizeof(struct sigaction));
        if(sig->handler){
            sa.sa_sigaction=sig->handler;
            sa.sa_flags=SA_SIGINFO;
        }
        else{
            sa.sa_handler=SIG_IGN;
        }
        sigemptyset(&sa.sa_mask); 
        if (sigaction(sig->signo, &sa, NULL) == -1) //参数1：要操作的信号
                                                     //参数2：主要就是那个信号处理函数以及执行信号处理函数时候要屏蔽的信号等等内容
                                                      //参数3：返回以往的对信号的处理方式【跟sigprocmask()函数边的第三个参数是的】，跟参数2同一个类型，我们这里不需要这个东西，所以直接设置为NULL；
        {   
            ngx_log_error_core(NGX_LOG_EMERG,errno,"sigaction(%s) failed",sig->signame); //显示到日志文件中去的 
            return -1; //有失败就直接返回
        }	
        else
        {            
            //ngx_log_error_core(NGX_LOG_EMERG,errno,"sigaction(%s) succed!",sig->signame);     //成功不用写日志 
            //ngx_log_stderr(0,"sigaction(%s) succed!",sig->signame); //直接往屏幕上打印看看 ，不需要时可以去掉
        }
    }
    return 0;
}


void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext){
    ngx_signal_t* sig;
    char* action;
    for(sig=signals;sig->signo!=0;sig++){
        if(sig->signo==signo){
            break;
        }
    }
    action =(char*)"";
    if(ngx_process==NGX_PROCESS_MASTER){
        switch(signo){
            case SIGCHLD:
                ngx_reap=1;
                break;
            default:
                break;
        }
    }
    else if(ngx_process==NGX_PROCESS_WORKER){
    }
    else{
    }
    //这里记录一些日志信息
    if(siginfo && siginfo->si_pid)  //si_pid = sending process ID【发送该信号的进程id】
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received from %P%s", signo, sig->signame, siginfo->si_pid, action); 
    }
    else
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received %s",signo, sig->signame, action);//没有发送该信号的进程id，所以不显示发送该信号的进程id
    }
    if (signo == SIGCHLD) 
    {
        ngx_process_get_status(); //获取子进程的结束状态
    } 
    return;
}

static void ngx_process_get_status(){
    int one=0;
    pid_t pid;
    int status;
    int err;
    for( ; ;){
        pid=waitpid(-1,&status,WNOHANG);
        if(pid==0){
            return;
        }
        if(pid==-1){
            err=errno;
            if(err==EINTR){
                continue;
            }
            if(err==ECHILD&&one){
                return;
            }
            if(err == ECHILD){
                ngx_log_error_core(NGX_LOG_INFO,err,"waitpid() failed!");
                return;
            }
            ngx_log_error_core(NGX_LOG_INFO,err,"waitpid() failed!");
            return;
        }
        one=1;
        if(WTERMSIG(status)){
            ngx_log_error_core(NGX_LOG_ALERT,0,"pid = %P exited on signal %d!",pid,WTERMSIG(status));
        }
        else{
            ngx_log_error_core(NGX_LOG_NOTICE,0,"pid = %P exited with code %d!",pid,WEXITSTATUS(status));
        }
    }
    return;
}

