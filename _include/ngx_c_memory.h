#ifndef __NGX_C_MEMORY_H__
#define __NGX_C_MEMORY_H__

#include <stddef.h>

class CMemory
{
private:
    CMemory(){};
public:
    static CMemory* GetInstance(){
        if(m_instance==nullptr){
            if(m_instance==nullptr){
                m_instance=new CMemory();
                static CGarhuishou c1;
            }
        }
        return m_instance;
    }
    class CGarhuishou{
    public:
        ~CGarhuishou(){
            if(CMemory::m_instance){
                delete m_instance;
                m_instance=nullptr;
            }
        }
    };
    void* AllocMemory(int memsize,bool ifmemset);
    void FreeMemory(void* point);
private:
    static CMemory* m_instance;

};
#endif