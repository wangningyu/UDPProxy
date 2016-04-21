#ifndef __OPINI__H__
#define __OPINI__H__

#include <afxwin.h>

#pragma once

class COPini  
{
public:
	COPini();
	~COPini(); 

    static DWORD ReadString (char *section, char * key,  char *stringtoread,  char * filename);
    static BOOL WriteString(LPCTSTR section, LPCTSTR key,char* stringtoadd, char *filename);
}; 

#endif	// end of define __OPINI__H__
