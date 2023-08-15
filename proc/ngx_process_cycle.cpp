#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"
#include "ngx_c_conf.h"

static void ngx_start_worker_processes(int threadnums);
static int ngx_spawn_process(int threadnums, const char *pprocname);
static void ngx_worker_process_cycle(int inum, const char *pprocname);
static void ngx_worker_process_init(int inum);

static u_char master_process[] = "master process";

void ngx_master_process_cycle()
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);  // 子进程状态改变
    sigaddset(&set, SIGALRM);  // 定时器超时
    sigaddset(&set, SIGIO);    // 异步I/O
    sigaddset(&set, SIGINT);   // 终端中断符
    sigaddset(&set, SIGHUP);   // 连接断开
    sigaddset(&set, SIGUSR1);  // 用户定义信号
    sigaddset(&set, SIGUSR2);  // 用户定义信号
    sigaddset(&set, SIGWINCH); // 终端窗口大小改变
    sigaddset(&set, SIGTERM);  // 终止
    sigaddset(&set, SIGQUIT);  // 终端退出符
    if (sigprocmask(SIG_BLOCK, &set, nullptr) == -1)
    {
        ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_master_process_cycle()中sigprocmask()失败!");
    }
    size_t size;
    int i;
    size = sizeof(master_process);
    size += g_argvneedmem;
    if (size < 1000)
    {
        char title[1000] = {0};
        strcpy(title, (const char *)master_process); //"master process"
        strcat(title, " ");                          // 跟一个空格分开一些，清晰    //"master process "
        for (i = 0; i < g_os_argc; i++)              //"master process ./nginx"
        {
            strcat(title, g_os_argv[i]);
        }                                                                                                   // end for
        ngx_setproctitle(title);                                                                            // 设置标题
        ngx_log_error_core(NGX_LOG_NOTICE, 0, "%s %P 【master进程】启动并开始运行......!", title, ngx_pid); // 设置标题时顺便记录下来进程名，进程id等信息到日志
    }
    CConfig *p_config = CConfig::GetInstance();
    int workprocess = p_config->GetIntDefault("WorkerProcesses", 1);
    ngx_start_worker_processes(workprocess);
    sigemptyset(&set);
    for (;;)
    {
        sigsuspend(&set);
        sleep(1);
    }
    return;
}

static void ngx_start_worker_processes(int threadnums)
{
    int i;
    for (i = 0; i < threadnums; i++) // master进程在走这个循环，来创建若干个子进程
    {
        ngx_spawn_process(i, "worker process");
    } // end for
    return;
}

static int ngx_spawn_process(int inum, const char *pprocname)
{
    pid_t pid;

    pid = fork(); // fork()系统调用产生子进程
    switch (pid)  // pid判断父子进程，分支处理
    {
    case -1: // 产生子进程失败
        ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_spawn_process()fork()产生子进程num=%d,procname=\"%s\"失败!", inum, pprocname);
        return -1;

    case 0:                                        // 子进程分支
        ngx_parent = ngx_pid;                      // 因为是子进程了，所有原来的pid变成了父pid
        ngx_pid = getpid();                        // 重新获取pid,即本子进程的pid
        ngx_worker_process_cycle(inum, pprocname); // 我希望所有worker子进程，在这个函数里不断循环着不出来，也就是说，子进程流程不往下边走;
        break;

    default: // 这个应该是父进程分支，直接break;，流程往switch之后走
        break;
    } // end switch

    // 父进程分支会走到这里，子进程流程不往下边走-------------------------
    // 若有需要，以后再扩展增加其他代码......
    return pid;
}

static void ngx_worker_process_cycle(int inum, const char *pprocname)
{
    // 设置一下变量
    ngx_process = NGX_PROCESS_WORKER; // 设置进程的类型，是worker进程

    // 重新为子进程设置进程名，不要与父进程重复------
    ngx_worker_process_init(inum);

    ngx_setproctitle(pprocname);                                                                            // 设置标题
    ngx_log_error_core(NGX_LOG_NOTICE, 0, "%s %P 【worker进程】启动并开始运行......!", pprocname, ngx_pid); // 设置标题时顺便记录下来进程名，进程id等信息到日志
    for (;;)
    {
        ngx_process_events_and_timers();
    }
    g_threadpool.StopAll(); 
    g_socket.Shutdown_subproc();
    return;
}

static void ngx_worker_process_init(int inum)
{
    sigset_t set; // 信号集

    sigemptyset(&set);                              // 清空信号集
    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1) // 原来是屏蔽那10个信号【防止fork()期间收到信号导致混乱】，现在不再屏蔽任何信号【接收任何信号】
    {
        ngx_log_error_core(NGX_LOG_ALERT, errno, "ngx_worker_process_init()中sigprocmask()失败!");
    }
    CConfig *p_config = CConfig::GetInstance();
    int tmpthreadnums = p_config->GetIntDefault("ProcMsgRecvWorkThreadCount",5); //处理接收到的消息的线程池中线程数量
    if(g_threadpool.Create(tmpthreadnums) == false)  //创建线程池中线程
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    sleep(1); //再休息1秒；
    if(g_socket.Initialize_subproc() == false) //初始化子进程需要具备的一些多线程能力相关的信息
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }
    g_socket.ngx_epoll_init();
    return;
}