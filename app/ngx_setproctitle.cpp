#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "ngx_func.h"
#include "ngx_global.h"

void ngx_init_setproctitle(){
    gp_envmem=new char[g_envneedmem];
    memset(gp_envmem,0,g_envneedmem);
    char*tmp=gp_envmem;
    for(int i=0;environ[i];i++){
        size_t add_size=strlen(environ[i])+1;
        strcpy(tmp,environ[i]);
        environ[i]=tmp;
        tmp+=add_size;
    }
    return;
}

void ngx_setproctitle(const char *title){
    size_t need_size=strlen(title);
    size_t have_size=g_argvneedmem+g_envneedmem;
    if(have_size<=need_size){
        return;
    }
    g_os_argv[1]=nullptr;
    char*tmp=g_os_argv[0];
    strcpy(tmp,title);
    tmp+=need_size;
    memset(tmp,0,have_size-need_size);
    return;
}