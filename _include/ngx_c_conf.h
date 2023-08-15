#ifndef __NGX_CONF_H__
#define __NGX_CONF_H__

#include <stdio.h>
#include <vector>
#include "ngx_global.h"

class CConfig{
private:
    CConfig();
public:
    ~CConfig();
private:
    static CConfig* m_instance;
public:
    static CConfig* GetInstance()
    {
        if(m_instance==nullptr){
            if(m_instance==nullptr){
                m_instance=new CConfig();
                printf("creat a new m_instance!\n");
                static CGarhuishou cl;
            }
        }
        return m_instance;
    }
    class CGarhuishou  
	{
	public:				
		~CGarhuishou()
		{
			if (CConfig::m_instance)
			{						
				delete CConfig::m_instance;				
				CConfig::m_instance = nullptr;				
			}
		}
	};

public:
    bool Load(const char*pconfName);
    const char* Getstring(const char*p_item);
    int GetIntDefault(const char*p_item,int def);
public:
    std::vector<LPCConfItem>m_ConfigItemList;
};

#endif