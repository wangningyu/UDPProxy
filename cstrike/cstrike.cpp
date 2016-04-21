// ctrike.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"

//////////////////////////////////////////////////////////////////////////
BOOL				g_bRunProxy = TRUE;						// 代理服务器是否运行	
USHORT				g_nProxyPort = {0x00};					// 代理服务器端口
char				g_szRealIP[MAX_IP_LEN] = {{0x00}};		// 真实服务器IP
USHORT				g_nRealPort = {0x00};					// 真实服务器端口
HANDLE				g_hConsole = INVALID_HANDLE_VALUE;		// 控制台句柄
HANDLE				g_hActive	 = INVALID_HANDLE_VALUE;	// 心跳线程句柄

//////////////////////////////////////////////////////////////////////////
char				g_szLanIP[MAX_PATH] = {0x00};			// 本机地址
char				g_szLanMask[MAX_PATH] = {0x00};			// 子网掩码
char				g_szLanBroadcast[MAX_PATH] = {0x00};	// 广播地址
char				g_szLanGateway[MAX_PATH] = {0x00};		// 网关地址

//////////////////////////////////////////////////////////////////////////
DWORD				g_nTimeMaxOfflineTime	= 200;			// 离线时间 单位(秒)
DWORD				g_nTimeUISleep			= 2;			// 刷新休眠 单位(秒)
DWORD				g_nTimeActiveSleep		= 100;			// 心跳休眠 单位(秒)
USHORT				g_nBindPort  = PROXY_BASE_PORT;			// 本地端口号

ClientWhiteList		g_WhiteList;							// 白名单IP链表
GateWayClientList	g_ClientList;							// 代理客户端链表
SOCKET				g_sProxyServer = INVALID_SOCKET;		// 代理服务器Soscket句柄
DWORD				g_nProxyTID	= 0x00;						// 代理服务器线程ID
HANDLE				g_hProxyRun	= INVALID_HANDLE_VALUE;		// 代理服务器线程句柄
CRITICAL_SECTION 	g_criLockGateWayList;					// 客户端链表锁
CRITICAL_SECTION 	g_criLockWhiteIPList;					// 白名单IP锁

int MTGetTransSize(DWORD nSize,char *pszTemp,int nLen)
{
	int		nRet = 0;
	if(nSize < 1024*1024)
		nRet = sprintf(pszTemp,"%.2fKB",(float)nSize / 1024.0);
	else if(nSize >= 1024*1024 && (float)nSize < 1024.0*1024.0*1024.0)
		nRet = sprintf(pszTemp,"%.2fMB",(float)nSize / 1024 / 1024);
	else if(nSize >= 1024*1024*1024)
		nRet = sprintf(pszTemp,"%.2fGB",(float)nSize / 1024.0 / 1024.0 / 1024.0);
	return nRet;
}

/************************************************************************
* 函数作用：读取上一次的列宽
* 参数说明：无
* 返 回 值：无
* 备注信息：无
************************************************************************/
void MTInitCFG()
{
	char	szDir[MAX_PATH]		= {0x00};
	char	szCFGPath[MAX_PATH] = {0x00};
	char	szTemp[MAX_PATH]	= {0x00};
	char	szValue[MAX_PATH]	= {0x00};
	char	szPort[MAX_PATH]	= {0x00};
	int		nLength				= 0;
	DWORD  i = 0;

	nLength = GetModuleFileName(NULL,szDir,MAX_PATH);
	while(TRUE)
	{
		if(szDir[nLength--]=='\\')
			break;
	}
	szDir[++nLength]=   0x0;

	MTLoadWhiteList();
	wsprintf(szCFGPath,"%s\\%s",szDir,ITEM_FILE_CFG_NAME);

	// Sleep UI
	COPini::ReadString(ITEM_ADVANCED_NODE, ITEM_SLEEP_UI, szTemp, szCFGPath);
	g_nTimeUISleep = (USHORT)atoi(szTemp);
	memset(szTemp,0x00,MAX_PATH);
	itoa((int)g_nTimeUISleep,szTemp,10);
	COPini::WriteString(ITEM_ADVANCED_NODE, ITEM_SLEEP_UI, szTemp, szCFGPath);

	// Sleep Active
	COPini::ReadString(ITEM_ADVANCED_NODE, ITEM_SLEEP_ACTIVE, szTemp, szCFGPath);
	g_nTimeActiveSleep = (USHORT)atoi(szTemp);
	memset(szTemp,0x00,MAX_PATH);
	itoa((int)g_nTimeActiveSleep,szTemp,10);
	COPini::WriteString(ITEM_ADVANCED_NODE, ITEM_SLEEP_ACTIVE, szTemp, szCFGPath);

	// Sleep Max Online
	COPini::ReadString(ITEM_ADVANCED_NODE, ITEM_MAX_ONLINE_SEC, szTemp, szCFGPath);
	g_nTimeMaxOfflineTime = (USHORT)atoi(szTemp);
	memset(szTemp,0x00,MAX_PATH);
	itoa((int)g_nTimeMaxOfflineTime,szTemp,10);
	COPini::WriteString(ITEM_ADVANCED_NODE, ITEM_MAX_ONLINE_SEC, szTemp, szCFGPath);

	//////////////////////////////////////////////////////////////////////////
	// Valve port
	COPini::ReadString(ITEM_VALVE_NODE, ITEM_LISTEN_PORT, szPort, szCFGPath);
	g_nProxyPort = (USHORT)atoi(szPort);
	memset(szPort,0x00,MAX_PATH);
	itoa((int)g_nProxyPort,szPort,10);
	COPini::WriteString(ITEM_VALVE_NODE, ITEM_LISTEN_PORT, szPort, szCFGPath);

	// Proxy ip
	COPini::ReadString(ITEM_VALVE_NODE, ITEM_REAL_IP, g_szRealIP, szCFGPath);
	COPini::WriteString(ITEM_VALVE_NODE, ITEM_REAL_IP, g_szRealIP, szCFGPath);

	// Proxy port
	COPini::ReadString(ITEM_VALVE_NODE, ITEM_REAL_PORT, szPort, szCFGPath);
	g_nRealPort = (USHORT)atoi(szPort);
	memset(szPort,0x00,MAX_PATH);
	itoa((int)g_nRealPort,szPort,10);
	COPini::WriteString(ITEM_VALVE_NODE, ITEM_REAL_PORT, szPort, szCFGPath);
}

void MTLockGateStart()
{
	EnterCriticalSection(&g_criLockGateWayList);
}

void MTLockGateStop()
{
	LeaveCriticalSection(&g_criLockGateWayList);
}

void MTLockWhiteStart()
{
	EnterCriticalSection(&g_criLockWhiteIPList);
}

void MTLockWhiteStop()
{
	LeaveCriticalSection(&g_criLockWhiteIPList);
}

BOOL MTInWhiteList(ULONG nAddr)
{
	if(g_WhiteList.size() == 0)
		return FALSE;

	MTLockWhiteStart();
	if(g_WhiteList.find(nAddr) != g_WhiteList.end())
	{
		MTLockWhiteStop();
		return TRUE;
	}

	MTLockWhiteStop();
	return FALSE;
}

BOOL MTLoadWhiteList()
{
	FILE	*f = NULL;
	int		nLength = 0;
	char	szDir[MAX_PATH]  = {0x00};
	char	szPath[MAX_PATH] = {0x00};
	char	szIP[MAX_PATH] = {0x00};

	nLength = GetModuleFileName(NULL,szDir,MAX_PATH);
	while(TRUE)
	{
		if(szDir[nLength--]=='\\')
			break;
	}
	szDir[++nLength]=   0x0;

	printf("正在加载白名单列表\n");
	MTLockWhiteStart();
	g_WhiteList.clear();
	wsprintf(szPath,"%s\\%s",szDir,ITEM_FILE_IP_NAME);

	f = fopen(szPath,"rt");
	if(!f)
		return FALSE;

	while(fgets(szIP,sizeof(szIP),f))
	{
		unsigned long adr = inet_addr(szIP);
		g_WhiteList[adr] = 1;
	}

	fclose(f);
	MTLockWhiteStop();
	printf("读取白名单列表成功，共计 %u 个\n",g_WhiteList.size());
	return g_WhiteList.size();
}

/************************************************************************/
/* 函数说明：初始化代理服务器
/* 参    数：无
/* 返 回 值：无
/* 备注信息：无
/************************************************************************/
void MTInitValveServer()
{
	g_sProxyServer = INVALID_SOCKET;
	g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	InitializeCriticalSection(&g_criLockGateWayList);
	InitializeCriticalSection(&g_criLockWhiteIPList);
	MTInitCFG();
	g_hProxyRun = CreateThread(NULL,NULL,(LPTHREAD_START_ROUTINE)ThreadListen,NULL,NULL,&g_nProxyTID);
	g_hActive    = CreateThread(NULL,NULL,(LPTHREAD_START_ROUTINE)MTThreadProxyActive,NULL,NULL,NULL);
}

/************************************************************************/
/* 函数说明：关闭代理服务器
/* 参    数：无
/* 返 回 值：无
/* 备注信息：无
/************************************************************************/
void MTShutDownValveServer()
{
	g_bRunProxy = FALSE;

	if(g_hActive != INVALID_HANDLE_VALUE)
	{
		TerminateThread(g_hActive,0);
		g_hActive = INVALID_HANDLE_VALUE;
	}

	
	if(g_hProxyRun != INVALID_HANDLE_VALUE)
	{
		TerminateThread(g_hProxyRun,0);
		g_hProxyRun = INVALID_HANDLE_VALUE;
	}

	while(g_ClientList.size())
	{
		GATEWAYCLIENT	*pClient = g_ClientList[0];
		if(pClient)
			MTProxyRemoveClient(pClient);
	}
	g_ClientList.clear();
	DeleteCriticalSection(&g_criLockGateWayList);
	DeleteCriticalSection(&g_criLockWhiteIPList);
}

/************************************************************************/
/* 函数说明：设置套接字延时
/* 参    数：s					源套按字引用
/*			 nTime				时间
/*			 bRecv				是否是接收漠视
/* 返 回 值：成功返回TRUE，否则返回FALSE
/* 备注信息：无
/************************************************************************/
int MTSetSocketTimeout(SOCKET &s, int nTime, BOOL bRecv)
{
	int nRet = 0;
	nRet = setsockopt(s, SOL_SOCKET,bRecv ? SO_RCVTIMEO : SO_SNDTIMEO, (char*)&nTime, sizeof(nTime));
	return nRet;
}

/************************************************************************/
/* 函数说明：代理接口 —— 心跳包检查
/* 函数参数：无
/* 返 回 值：无
/* 备注信息：每60秒一次,如果无任何数据则试为掉线
/************************************************************************/
BOOL MTProxyCheckActive()
{
	GATEWAYCLIENT		*pClient = NULL;
	GateWayClientPtr	iter;
	DWORD				nTickNow = 0;
	char				szErrorMsg[MAX_PATH] = {0x00};
	DWORD				nSubRecv = 0;
	DWORD				nSubSend = 0;

	if(g_ClientList.size() == 0)
		return TRUE;

	MTLockGateStart();
	for(iter=g_ClientList.begin(); iter!=g_ClientList.end();)
	{
		pClient = *iter;
		if( pClient)
		{
			nTickNow = GetTickCount();
			nSubSend = (DWORD)abs(long(nTickNow - pClient->m_nTickSend));
			nSubRecv = (DWORD)abs(long(nTickNow - pClient->m_nTickRecv));
			if( nSubSend >= (g_nTimeMaxOfflineTime*1000) || 
				nSubRecv >= (g_nTimeMaxOfflineTime*1000))
			{
				wsprintf(szErrorMsg,"客户端 %s:%u (%u) 断开网络,释放内存.\n",	\
					pClient->m_szSrcIP,pClient->m_nSrcPort,g_nProxyPort);
				printf(szErrorMsg);

				if(pClient->m_hThreadRecv != INVALID_HANDLE_VALUE)
				{
					TerminateThread(pClient->m_hThreadRecv,0);
					pClient->m_hThreadRecv = INVALID_HANDLE_VALUE;
					pClient->m_nRecvTID    = 0;
				}

				if(pClient->m_pszRecvData != NULL)
				{
					GlobalFree(pClient->m_pszRecvData);
					pClient->m_pszRecvData = NULL;
				}

				delete pClient;
				pClient = NULL;

				iter = g_ClientList.erase(iter);
			}
			else
			{
				iter++;
			}
		}
	}

	MTLockGateStop();
	return TRUE;
}

GATEWAYCLIENT*	MTProxyFindClientByFrom(SOCKADDR_IN *from)
{
	GateWayClientPtr	iter;
	BOOL				bFound = FALSE;
	GATEWAYCLIENT		*pTemp = NULL;

	MTLockGateStart();
	for(iter=g_ClientList.begin(); iter!=g_ClientList.end();)
	{
		pTemp = *iter;
		if( pTemp->m_addrfrom.sin_addr.S_un.S_addr == from->sin_addr.S_un.S_addr && 
			pTemp->m_addrfrom.sin_port == from->sin_port)
		{
			MTLockGateStop();
			return pTemp;
		}
		
		iter++;
	}
	MTLockGateStop();
	return NULL;
}

BOOL MTProxyRemoveClient(GATEWAYCLIENT *pClient/* = NULL*/)
{
	GateWayClientPtr	iter;
	BOOL				bFound = FALSE;
	GATEWAYCLIENT		*pTemp = NULL;
	DWORD				nIndex = 0;

	if(pClient == NULL)
		return FALSE;

	MTLockGateStart();
	for(iter=g_ClientList.begin(); iter!=g_ClientList.end();)
	{
		pTemp = *iter;
		if( pTemp == pClient)
		{
			iter = g_ClientList.erase(iter);
		}
		else
		{
			iter++;
		}
	}
	
	TerminateThread(pClient->m_hThreadRecv,0);
	pClient->m_hThreadRecv = INVALID_HANDLE_VALUE;
	pClient->m_nRecvTID = 0;

	if(pClient->m_pszRecvData)
	{
		GlobalFree(pClient->m_pszRecvData);
		pClient->m_pszRecvData = NULL;
	}
	
	delete pClient;
	pClient = NULL;
	MTLockGateStop();

	return bFound;
}

// 将真实主机转发给代理客户端
int MTProxyRecvData(GATEWAYCLIENT *pClient,char *pszData,int nLen)
{
	int				nRet = 0;
	int				fromlength  = sizeof(SOCKADDR);

	if(pClient != NULL)
	{
		pClient->m_nTickRecv = GetTickCount();
		pClient->m_nSizeRecv += nLen;
		nRet = sendto(g_sProxyServer,pszData,nLen,0,(SOCKADDR*)&pClient->m_addrfrom,fromlength);
	}

	return nRet;
}

// 将客户端数据转发给真实主机
int	MTProxySendData(GATEWAYCLIENT *pClient,char *pszData,int nLen)
{
	int				nRet = 0;
	int				fromlength  = sizeof(SOCKADDR);
	
	if(pClient != NULL)
	{
		pClient->m_nTickSend = GetTickCount();
		pClient->m_nSizeSend += nLen;
		nRet = sendto(pClient->m_socketProxy,pszData,nLen,0,(SOCKADDR*)&pClient->m_addrto,fromlength);
	}

	return nRet;
}

extern DWORD WINAPI MTProxyThreadRecv(LPVOID lParam);

// 添加客户端到链表
GATEWAYCLIENT* MTProxyNewClient(SOCKADDR_IN *from,char *pszData,int nLen)
{
	WSADATA	wsdata;
	if(WSAStartup(MAKEWORD(2,2),&wsdata) != 0)
	{
		return FALSE;
	}

	int				nRet = 0;
	char			*pszSrcIP = NULL;
	USHORT			nSrcPort  = 0;
	GATEWAYCLIENT	*pClient = NULL;

	pClient = MTProxyFindClientByFrom(from);
	if(pClient)
	{
		MTProxySendData(pClient,pszData,nLen);
		return pClient;
	}

	pClient = new GATEWAYCLIENT;
	pszSrcIP = inet_ntoa(from->sin_addr);
	nSrcPort = htons(from->sin_port);
	pClient->m_socketProxy  = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);

	// 设置真实主机服务器地址  
    pClient->m_addrto.sin_family = AF_INET;  
    pClient->m_addrto.sin_addr.S_un.S_addr = inet_addr(g_szRealIP);  
    pClient->m_addrto.sin_port = htons(g_nRealPort);
	memcpy(&pClient->m_addrfrom,from,sizeof(SOCKADDR_IN));

	pClient->m_pszRecvData = (char *)GlobalAlloc(GMEM_FIXED,PACKET_TRANS_LEN);

	//
	// 这里BIND一下是为了固定端口号，实际上也可以不固定，系统随机分配
	// bind...		
	//
	SOCKADDR_IN addrClient;
	addrClient.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	addrClient.sin_family = AF_INET;
	
	//
	// 自动寻找空闲端口号
	//
	while (TRUE)
	{
		if(g_nBindPort >= 65530)
			g_nBindPort = PROXY_BASE_PORT;

		addrClient.sin_port = htons(g_nBindPort);
		pClient->m_nBindPort = g_nBindPort;
		nRet = bind(pClient->m_socketProxy,(SOCKADDR *)&addrClient,sizeof(SOCKADDR));
		if(nRet == SOCKET_ERROR)
		{
			g_nBindPort++;
			continue;
		}

		break;
	}
	g_nBindPort++;

	MTLockGateStart();
	g_ClientList.push_back(pClient);
	MTLockGateStop();

	// 转发这一包,再启动接受线程
	pClient->m_hThreadRecv = CreateThread(NULL,NULL,(LPTHREAD_START_ROUTINE)MTProxyThreadRecv,pClient,NULL,&pClient->m_nRecvTID);
	nRet = MTProxySendData(pClient,pszData,nLen);

	return pClient;
}

// 为每一个代理客户端创建一个线程
DWORD WINAPI MTProxyThreadRecv(LPVOID lParam)
{
	GATEWAYCLIENT	*pClient = (GATEWAYCLIENT *)lParam;
	if(pClient == NULL)
		return FALSE;
	
	int			nLen,fromlength;
	SOCKADDR_IN from;
	char		szData[PACKET_TRANS_LEN] = {0x00};
	while(true)
	{
		memset(szData,0x00,PACKET_TRANS_LEN);
		fromlength = sizeof(SOCKADDR_IN);
		nLen = recvfrom(pClient->m_socketProxy,szData,PACKET_TRANS_LEN,0,(sockaddr *)&from,(int FAR *)&fromlength);
		if(nLen > 0)
			MTProxyRecvData(pClient,szData,nLen);
		else
			MTProxyRemoveClient(pClient);
	}
	
	return TRUE;
}

//
// 代理服务器侦听线程
//
DWORD WINAPI ThreadListen(LPVOID lParam)
{
	DWORD	nIndex = 0;
	int		nRet = 0;
	char	*pszData = NULL;
	WSADATA	wsdata;

	if(WSAStartup(MAKEWORD(2,2),&wsdata) != 0)
	{
		printf("WSAStartup failed : %d\n",WSAGetLastError());
		return FALSE;
	}

	int nRecvOpt = 0;
	int nRecvBuf = PACKET_TRANS_LEN;
	int optval = 1;

	g_sProxyServer = //WSASocket(AF_INET,SOCK_DGRAM,0,NULL,0,WSA_FLAG_OVERLAPPED);;
		socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
	if(g_sProxyServer == INVALID_SOCKET)
	{
		printf("create socket failed : %d\n",WSAGetLastError());
		return FALSE;
	}

	setsockopt(g_sProxyServer,SOL_SOCKET,SO_BROADCAST,(char FAR *)&optval,sizeof(optval));

	// 接收和发送缓冲区大小
	nRecvOpt = sizeof(nRecvBuf);
    getsockopt(g_sProxyServer,SOL_SOCKET,SO_RCVBUF,(CHAR *)&nRecvBuf,&nRecvOpt);
    setsockopt(g_sProxyServer,SOL_SOCKET,SO_RCVBUF,(char*)&nRecvBuf,nRecvOpt);
	setsockopt(g_sProxyServer,SOL_SOCKET,SO_SNDBUF,(char*)&nRecvBuf,nRecvOpt);

	SOCKADDR_IN addrClient;
	addrClient.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	addrClient.sin_family = AF_INET;
	addrClient.sin_port = htons(g_nProxyPort);
	nRet = bind(g_sProxyServer,(SOCKADDR *)&addrClient,sizeof(SOCKADDR));
	if(nRet == SOCKET_ERROR)
	{
		printf("bind [%d] failed : %d\n",g_nProxyPort,WSAGetLastError());
		return FALSE;
	}
	
	GATEWAYCLIENT	*pClient = NULL;
	SOCKADDR_IN		from;
	int				nLen,fromlength;

	printf("游戏端口 %u 初始化成功.\n",g_nProxyPort);
	pszData = (char *)GlobalAlloc(GMEM_FIXED,PACKET_TRANS_LEN);
	while(true)
	{
		memset(pszData,0x00,PACKET_TRANS_LEN);
		memset(&from,0x00,sizeof(SOCKADDR_IN));
		fromlength = sizeof(SOCKADDR_IN);
		nLen = recvfrom(g_sProxyServer,pszData,PACKET_TRANS_LEN,0,(sockaddr *)&from,(int FAR *)&fromlength);

		// Check from ip 
		// ...
		// Check data packet

		if(nLen > 0)
		{
			// check header
			if(pszData[0] != 0x59 || pszData[1] != 0x40)
				continue;

			// check command
			if(pszData[2] == 0x10 || pszData[2] == 0x20 || pszData[2] == 0x30 || pszData[2] == 0x60)
			{
				// check ip in white list
				if(!MTInWhiteList(from.sin_addr.S_un.S_addr))
				{
					printf("未知IP访问 %s:%u\n",inet_ntoa(from.sin_addr),htons(from.sin_port));
					continue;
				}

				pClient = MTProxyFindClientByFrom(&from);
				if(pClient == NULL)
				{
					pClient = MTProxyNewClient(&from,pszData,nLen);
					continue;
				}
				else
				{
					MTProxySendData(pClient,pszData,nLen);
					continue;
				}
			}
		}
		else
		{
			pClient = MTProxyFindClientByFrom(&from);
			if(pClient != NULL)
				MTProxyRemoveClient(pClient);
		}
	}
}

BOOL MTGetLanInfo(char *pszIP,char *pszMask,char *pszBroadcast,char *pszGateway)
{
	in_addr				broadcast;
	PIP_ADAPTER_INFO	pAdapterInfo;
	PIP_ADAPTER_INFO	pAdapter = NULL;
	DWORD				dwRetVal = 0;
	ULONG				ulOutBufLen = 0;
	char				*pszTemp = NULL;
	static	BOOL		bLoad = FALSE;

	if(bLoad || strlen(g_szLanIP))
		return TRUE;

	//得到结构体大小,用于GetAdaptersInfo参数
	pAdapterInfo = ( IP_ADAPTER_INFO *) malloc( sizeof( IP_ADAPTER_INFO ) );
	ulOutBufLen = sizeof(IP_ADAPTER_INFO); 

	// 第一次调用GetAdapterInfo获取ulOutBufLen大小 
	//调用GetAdaptersInfo函数,填充pIpAdapterInfo指针变量;其中ulOutBufLen参数既是一个输入量也是一个输出量
	if (GetAdaptersInfo( pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) 
	{ 
		free(pAdapterInfo);
		pAdapterInfo = (IP_ADAPTER_INFO *) malloc (ulOutBufLen); 
	}

	if ((dwRetVal = GetAdaptersInfo( pAdapterInfo, &ulOutBufLen)) == NO_ERROR)
	{ 
		pAdapter = pAdapterInfo; 
		while (pAdapter)    //有多个网卡
		{ 
			wsprintf(pszIP,"%s", pAdapter->IpAddressList.IpAddress.String);
			wsprintf(pszMask,"%s",pAdapter->IpAddressList.IpMask.String);
			wsprintf(pszGateway,"%s",pAdapter->GatewayList.IpAddress.String);

			broadcast.S_un.S_addr = (inet_addr(pszIP) & inet_addr(pszMask))| (inet_addr(pszGateway));
			pszTemp = inet_ntoa(broadcast);
			if(pszTemp)
				wsprintf(pszBroadcast,pszTemp,strlen(pszTemp));

			pAdapter = pAdapter->Next; 
			if(StrStrI(pszIP,"0.0.0.0"))
				continue;

			memset(g_szLanIP,0x00,MAX_PATH);
			memset(g_szLanMask,0x00,MAX_PATH);
			memset(g_szLanGateway,0x00,MAX_PATH);
			memset(g_szLanBroadcast,0x00,MAX_PATH);
			memcpy(g_szLanIP,pszIP,strlen(pszIP));
			memcpy(g_szLanMask,pszMask,strlen(pszMask));
			memcpy(g_szLanGateway,pszGateway,strlen(pszGateway));
			memcpy(g_szLanBroadcast,pszBroadcast,strlen(pszBroadcast));
		} 
	} 

	if (pAdapterInfo != NULL)
	{
		free(pAdapterInfo);
		pAdapterInfo = NULL;
	}

	bLoad = TRUE;
	return TRUE;
}

void MTUIRefresh(BOOL bShowAll)
{
	GateWayClientPtr	iter;
	BOOL				bFound = FALSE;
	GATEWAYCLIENT		*pTemp = NULL;
	char				*pszSrcIP = NULL;
	USHORT				nSrcPort = 0;
	char				szTitle[MAX_PATH] = {0x00};
	DWORD				nOnline = 0;

	if (g_hConsole == INVALID_HANDLE_VALUE)
		return;

	DWORD	nIndex = 0;
	DWORD	nPing  = 0;
	char	szPing[MAX_PATH] = {0x00};
	DWORD	nActive = 0;
	char	szActive[MAX_PATH] = {0x00};

	char	szSend[MAX_PATH] = {0x00};
	char	szRecv[MAX_PATH] = {0x00};

	MTPrintHelp();
	if(bShowAll)
		printf("当前玩家列表:\n");
	
	MTLockGateStart();
	for(iter=g_ClientList.begin(); iter!=g_ClientList.end();)
	{
		pTemp = *iter;
		if(bShowAll)
		{
			nIndex++;
			pszSrcIP = inet_ntoa(pTemp->m_addrfrom.sin_addr);
			nSrcPort = htons(pTemp->m_addrfrom.sin_port);

			nPing = abs(long(pTemp->m_nTickSend - pTemp->m_nTickRecv));
			wsprintf(szPing,"%ums",nPing);

			nPing = pTemp->m_nTickRecv > pTemp->m_nTickSend ? pTemp->m_nTickRecv : pTemp->m_nTickSend;
			nActive = abs(long(GetTickCount() - nPing));
			if(nActive < 1000 * 60)
				sprintf(szActive,"%u秒",nActive / 1000);
			else
				sprintf(szActive,"%.2f分",(float)nActive / 1000.0 / 60.0);

			MTGetTransSize(pTemp->m_nSizeSend,szSend,MAX_PATH);
			MTGetTransSize(pTemp->m_nSizeRecv,szRecv,MAX_PATH);

			printf("%02u %-05u [%-05u] %-15s:%-05u %-8s %-8s %-5s (%-7s)\n",	\
				nIndex,	\
				g_nRealPort,\
				pTemp->m_nBindPort,\
				pszSrcIP,	\
				nSrcPort,	\
				szSend,		\
				szRecv,		\
				szPing,szActive);
		}

		nOnline++;
		iter++;
	}
	MTLockGateStop();

	wsprintf(szTitle,"UDP代理精灵 V1.0     (当前在线: %u)",nOnline);
	SetConsoleTitle(szTitle);
}

/************************************************************************/
/* 函数说明：代理接口 —— 心跳包检查
/* 函数参数：无
/* 返 回 值：无
/* 备注信息：如果无任何数据则试为掉线
/************************************************************************/
DWORD WINAPI MTThreadProxyActive(LPVOID lParam)
{
	while(g_bRunProxy)
	{
		MTProxyCheckActive();
		Sleep(g_nTimeActiveSleep * 1000);
	}

	g_hActive = INVALID_HANDLE_VALUE;
	return TRUE;
}

/************************************************************************/
/* 函数说明：代理接口 —— 刷新控制台
/* 函数参数：无
/* 返 回 值：无
/* 备注信息：请注意频率不要太快防止与Find死锁
/************************************************************************/
DWORD WINAPI ThreadRefresh(LPVOID)
{
	while(g_bRunProxy)
	{
		MTUIRefresh(TRUE);
		Sleep(2000);
	}

	return TRUE;
}

void MTPrintHelp()
{
	system("cls");
	printf("*************************************************\n");
	printf("请输入数字; \n");
	printf("  0.退出程序\n");
	printf("  1.清除屏幕\n");
	printf("  2.在线列表\n");
	printf("  3.加载配制\n");
	printf("  4.显示帮助\n");
}

int _tmain(int argc, _TCHAR* argv[])
{
	char		szIPLan[MAX_PATH] = {0x00};
	char		szIPMask[MAX_PATH] = {0x00};
	char		szIPGateway[MAX_PATH] = {0x00};
	char		szIPBoard[MAX_PATH] = {0x00};
	int			nRet = 0;
	HANDLE		hThread = INVALID_HANDLE_VALUE;

	MTUIRefresh(FALSE);
	MTInitValveServer();
	MTGetLanInfo(szIPLan,szIPMask,szIPBoard,szIPGateway);
	hThread = CreateThread(NULL,NULL,(LPTHREAD_START_ROUTINE)ThreadRefresh,NULL,NULL,NULL);
	CloseHandle(hThread);

	printf("本机IP  : %s\n",g_szLanIP);
	printf("本机掩码: %s\n",g_szLanMask);
	printf("本机网关: %s\n",g_szLanGateway);
	printf("广播地址: %s\n*",g_szLanBroadcast);

	while(g_bRunProxy)
	{
		cin>>nRet;
		if(nRet == 0)
		{
			MTShutDownValveServer();
			break;
		}
		else if(nRet == 1)
			system("cls");
		else if(nRet == 2)
			MTUIRefresh(TRUE);
		else if(nRet == 3)
			MTInitCFG();
		else
			MTPrintHelp();
			
	}

	return 0;
}

