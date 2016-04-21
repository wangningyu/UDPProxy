// OPini.cpp: implementation of the COPini class.

#include "StdAfx.h"
#include "OPini.h" 

COPini::COPini()
{
} 

COPini::~COPini()
{
}

BOOL COPini::WriteString(LPCTSTR section, LPCTSTR key, char *stringtoadd, char *filename)
{
    return ::WritePrivateProfileString(section,key,stringtoadd,filename);
} 

DWORD COPini::ReadString(char *section, char * key,  char stringtoread[],  char * filename)
{
    return ::GetPrivateProfileString(section, key,NULL,stringtoread,MAX_PATH,filename);
}