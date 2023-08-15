#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ngx_c_memory.h"


CMemory *CMemory::m_instance = nullptr;

void* CMemory::AllocMemory(int memsize,bool ifmemset){
    void* tmp=(void*) new char[memsize];
    if(ifmemset){
        memset(tmp,0,memsize);
    }
    return tmp;
}

void CMemory::FreeMemory(void* point){
    delete [] ((char*)point);
}