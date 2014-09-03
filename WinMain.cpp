#define WIN32_LEAN_AND_MEAN
#pragma warning(disable:4786)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winsock2.h>
#include <winuser.h>
#include <shellapi.h>
#include <vector>
#include "Serial.h"

using namespace std;

#pragma comment(lib, "ws2_32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	ID_UNIQUE_APP				TEXT("FlexAide")
#define MUTEX_OPEN					1
#define MUTEX_CLOSE					0
#define WM_SOCKET_SERVER		WM_USER + 1
#define	SERVER_PORT					843
#define BUFFER_SIZE					1024
#define SOCKET_VERSION			0x0202
#define SERVER_SOCKET_ERROR 1
#define SERVER_SOCKET_OK		0
#define MAX_CLIENTS					10
#define CMD_POLICY					"<policy-file-request/>"
#define CMD_TEST						"TT"
#define CMD_EXIT						"ET"
#define CMD_OPENCOM					"OC"
#define CMD_CLOSECOM				"CC"
#define CMD_EXECUTE					"EX"
#define TIMER_ID						99
#define SERIAL_MIN					1
#define SERIAL_MAX					6
#define SERIAL_ERR					"ERR"
#define SERIAL_OK						"OK"
#define SWF_PLAYER					"ShockwaveFlash"
#define FLEX_DOMAIN_POLICY	"<cross-domain-policy><allow-access-from domain=\"*\" to-ports=\"*\"/></cross-domain-policy>\0"
#define ProcessBasicInformation 0

typedef struct
{
	DWORD ExitStatus;
	DWORD PebBaseAddress;
	DWORD AffinityMask;
	DWORD BasePriority;
	ULONG UniqueProcessId;
	ULONG InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION;
typedef LONG (WINAPI *PROCNTQSIP)(HANDLE,UINT,PVOID,ULONG,PULONG);

typedef struct tagWNDINFO
{
	HWND hWnd;
	DWORD dwProcessId;
} WNDINFO, *LPWNDINFO;


DWORD GetParentProcessID(DWORD dwId);
HWND GetProcessWindow(DWORD dwProcessId);
void MaximizeParentWindow();
void GetPathInfo();

HWND AllocateHWND(HINSTANCE hInstance, WNDPROC proc);
void DeallocateHWND(HWND hWnd);
LRESULT CALLBACK NoopWindowProc(HWND, UINT, WPARAM, LPARAM);
BOOL SetMutex(LPCSTR, BOOL);
LRESULT StartServer();
DWORD WINAPI ServerThread(LPVOID lpParameter);
void OnServerSocket(WPARAM wParam, LPARAM lParam);
void OnRead(SOCKET client);
void OnClose(SOCKET client);
void OnConnect(SOCKET client);
void OnAccept(SOCKET client);
void Broadcast(const char* msg);
void _stdcall ReadCOM(HWND, UINT, UINT, DWORD);

PROCNTQSIP				NtQueryInformationProcess;
HINSTANCE					hAppInstance;
TCHAR             szName[MAX_PATH], szPath[MAX_PATH], szParentPath[MAX_PATH];
HWND							wNoop;
SOCKET						sServer;
HANDLE						tServer;
vector<SOCKET>		sClients;
DWORD							dwBSMRecipients = BSM_APPLICATIONS, dwMessageId;
WNDCLASS					wcNoop = {0, NoopWindowProc, 0, 0, 0, 0, 0, 0, NULL, TEXT("NoopWindow")};
CSerial						com;

BOOL WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	hAppInstance = hInstance;
	// use mutex to restrict only one instance can be run
	dwMessageId = RegisterWindowMessage(ID_UNIQUE_APP);
	MaximizeParentWindow();
	GetPathInfo();
	if (!SetMutex(ID_UNIQUE_APP, MUTEX_OPEN)) return FALSE;
	wNoop = AllocateHWND(hInstance, (WNDPROC) NoopWindowProc);
	if (StartServer() == SERVER_SOCKET_ERROR) return FALSE;

	MSG		msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return TRUE;
	/*
  while (GetMessage(&msg, NULL, 0, 0))
  {
		if(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
  return msg.wParam;
	*/
}

void MaximizeParentWindow()
{
	NtQueryInformationProcess = (PROCNTQSIP)GetProcAddress(GetModuleHandle("ntdll"), "NtQueryInformationProcess");
	if (!NtQueryInformationProcess) return;
	DWORD dwPid = GetCurrentProcessId();
  DWORD dwParentPid = GetParentProcessID(dwPid);
	HWND hParentWnd = GetProcessWindow(dwParentPid);
	if (hParentWnd) ShowWindow(hParentWnd, SW_SHOWMAXIMIZED);
}

void GetPathInfo()
{
	if (!GetModuleFileName(NULL, szName, MAX_PATH)) return;
	int i, k;
	i=k=strlen(szName);
	szPath[0]=szParentPath[0]=0;
	while (k >= 0)
	{
		if (szName[--k] != '\\') continue;
		if (szPath[0] == 0) strncpy(szPath, szName, k);
		else if (szParentPath[0] == 0) 
		{
			strncpy(szParentPath, szName, k);
			break;
		}
	}
}

BOOL CALLBACK EnumProc(HWND hWnd, LPARAM lParam)
{
	DWORD dwProcessId;
	GetWindowThreadProcessId(hWnd, &dwProcessId);
	LPWNDINFO pInfo = (LPWNDINFO) lParam;
	char name[256];
	memset(name, 0, 256);
	GetClassName(hWnd, name, 256);
	if(dwProcessId == pInfo->dwProcessId && strstr(name, SWF_PLAYER) == name)
	{
		pInfo->hWnd = hWnd;
		return FALSE;
	}
	return TRUE;
}

HWND GetProcessWindow(DWORD dwProcessId)
{
	WNDINFO wi;
	wi.dwProcessId = dwProcessId;
	wi.hWnd = 0;
	EnumWindows(EnumProc, (LPARAM)&wi);
	return wi.hWnd;
}

DWORD GetParentProcessID(DWORD dwId)
{
	HRESULT	hr;
	DWORD		dwParentPid = (DWORD)-1;
	HANDLE	hProcess;
	PROCESS_BASIC_INFORMATION pbi;

	// Get process handle
	hProcess = OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,dwId);
	if (!hProcess) return (DWORD)-1;

	// Retrieve information
	hr = NtQueryInformationProcess(hProcess, ProcessBasicInformation, (PVOID)&pbi, sizeof(PROCESS_BASIC_INFORMATION), NULL);

	// Copy parent Id on success
	if (SUCCEEDED(hr)) dwParentPid = pbi.InheritedFromUniqueProcessId;
	CloseHandle (hProcess);
	return dwParentPid;
}

HWND AllocateHWND(HINSTANCE hInstance, WNDPROC proc)
{
	HWND			hWnd;
	WNDCLASS	tWndClass;
	HRESULT		hr;
	wcNoop.hInstance = hInstance;
	wcNoop.lpfnWndProc = NoopWindowProc;
	hr = GetClassInfo(hInstance, wcNoop.lpszClassName, &tWndClass);
	if (hr == 0 || tWndClass.lpfnWndProc != DefWindowProc)
	{
		if (hr != 0) UnregisterClass(wcNoop.lpszClassName, hInstance);
		RegisterClass(&wcNoop);
	}
	hWnd = CreateWindowEx(WS_EX_TOOLWINDOW, wcNoop.lpszClassName, TEXT(""), WS_POPUP, 0, 0, 0, 0, 0, 0, hInstance, NULL);
	if (proc) SetWindowLong(hWnd, GWL_WNDPROC, (long) proc);
	return hWnd;
}

void DeallocateHWND(HWND hWnd)
{
	DestroyWindow(hWnd);
}

LRESULT CALLBACK NoopWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
		case WM_DESTROY:
			CloseHandle(tServer);
			WSACleanup();
			DeallocateHWND(wNoop);
			SetMutex(ID_UNIQUE_APP, MUTEX_CLOSE);
			PostQuitMessage (0);
			return 0;
		case WM_SOCKET_SERVER:
			OnServerSocket(wParam, lParam);
			break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam) ;
}

BOOL SetMutex(LPCSTR InstanceName, BOOL mOperate)
{
	HANDLE hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, InstanceName);

	switch(mOperate)          
	{
		case MUTEX_OPEN:
			if(hMutex == NULL)    
			{
				hMutex = CreateMutex(NULL, FALSE, InstanceName);
				if( (hMutex == NULL) || (GetLastError() == ERROR_ALREADY_EXISTS)) return FALSE;
				else return TRUE;
			}
			else
			{
				// BroadcastSystemMessage(BSF_IGNORECURRENTTASK || BSF_POSTMESSAGE, &dwBSMRecipients, dwMessageId, 0, 0);
				return FALSE;
			}
			break;
		case MUTEX_CLOSE:
			ReleaseMutex(hMutex);
			CloseHandle(hMutex);
			break;
	}
	return TRUE;
}

LRESULT StartServer()
{
	WSADATA	wsaData;
	int err = WSAStartup(SOCKET_VERSION, &wsaData);

	if (err != 0 || wsaData.wVersion != SOCKET_VERSION)
	{
		MessageBox(NULL, "Winsock is not Available!\nProgram will Terminate!", "WinSock Error", MB_ICONERROR);
		WSACleanup();
		return SERVER_SOCKET_ERROR;
	}

	sServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(sServer == INVALID_SOCKET)
	{
		MessageBox(NULL, "Can NOT create server socket!", "WinSock Error", MB_ICONERROR);
		WSACleanup();
    return SERVER_SOCKET_ERROR;
	}

	SOCKADDR_IN service;
	service.sin_family = AF_INET;
	// service.sin_addr.s_addr = INADDR_ANY;
	service.sin_addr.s_addr = inet_addr("127.0.0.1");
	service.sin_port = htons(SERVER_PORT);

	if (bind(sServer, (SOCKADDR*)&service, sizeof(SOCKADDR)) == SOCKET_ERROR)
	{
		MessageBox(NULL, "Failed to bind server socket!", "WinSock Error", MB_ICONERROR);
		WSACleanup();
		return SERVER_SOCKET_ERROR;
	}

	if (listen(sServer, 2) == SOCKET_ERROR)
	{
		MessageBox(NULL, "Failed to listen on server socket!", "WinSock Error", MB_ICONERROR);
		WSACleanup();
		return SERVER_SOCKET_ERROR;
	}
	tServer=CreateThread(NULL, 0, ServerThread, 0, 0, NULL);
	// ServerThread(0);
	return SERVER_SOCKET_OK;
}

DWORD WINAPI ServerThread(LPVOID lpParameter)
{
	SOCKADDR_IN cin;
	SOCKET client;
	int len=sizeof(SOCKADDR);
	int err;

	while (true)
	{
		client=accept(sServer, (SOCKADDR*)&cin, &len);
		// 注册异步事件, 自定义消息为WM_SOCKET_SERVER, 注册异步事件后, sServer的listen队列限制不起作用。
		err=WSAAsyncSelect(client, wNoop, WM_SOCKET_SERVER, FD_ACCEPT | FD_READ | FD_CONNECT | FD_CLOSE);
		if(err == SOCKET_ERROR)
		{
			MessageBox(NULL, "Failed to register async server socket!", "WinSock Error", MB_ICONERROR);
			break;
		}
		sClients.push_back(client);
	}
	return 0;
}

void OnServerSocket(WPARAM wParam, LPARAM lParam)
{
	SOCKET client = (SOCKET) wParam; // 取得当前SOCKET
	int iEvent = WSAGETSELECTEVENT(lParam);
	switch(iEvent)
	{
		case FD_READ:
			OnRead(client);
			break;
		case FD_CLOSE:
			OnClose(client);
			break;
		case FD_CONNECT:
			OnConnect(client);
			break;
		case FD_ACCEPT:
			OnAccept(client);
			break;
		default:
			break;
	}
}

/**
<policy-file-request/>: Send flex policy
TT: Test
ET: Exit
OCx: Open COMx for reading
CC: Close COM
**/
void OnRead(SOCKET client)
{
	char buf[BUFFER_SIZE];
	memset(buf, 0, BUFFER_SIZE);
	recv(client, buf, BUFFER_SIZE, 0);
	// MessageBox(NULL, buf, "Socket Receive", MB_ICONINFORMATION);
	if (strstr(buf, CMD_POLICY) == buf)
	{
		sprintf(buf, FLEX_DOMAIN_POLICY);
		send(client, buf, strlen(buf) + 1, 0);
	}
	else if (strstr(buf, CMD_TEST) == buf)
	{
		sprintf(buf, "Welcome to Flex-Aide!\0");
		send(client, buf, strlen(buf) + 1, 0);
	}
	else if (strstr(buf, CMD_EXIT) == buf)
	{
		PostQuitMessage(0);
	}
	else if (strstr(buf, CMD_OPENCOM) == buf)
	{
		int port = (buf[2] == 0) ? 1 : buf[2] - '0';
		if (port < SERIAL_MIN || port > SERIAL_MAX) port = SERIAL_MIN;
		if (!com.Open(port, 57600))
		{
			send(client, SERIAL_ERR, strlen(SERIAL_ERR) + 1, 0);
		}
		else
		{
			send(client, SERIAL_OK, strlen(SERIAL_OK) + 1, 0);
		}
		SetTimer(wNoop, TIMER_ID, 1000, ReadCOM);
	}
	else if (strstr(buf, CMD_CLOSECOM) == buf)
	{
		com.Close();
		KillTimer(wNoop, TIMER_ID);
	}
	else if (strstr(buf, CMD_EXECUTE) == buf)
	{
		UINT uCmdShow = (buf[2] == 'H') ? SW_HIDE : SW_SHOWNORMAL;
		char cmd[MAX_PATH], fcmd[MAX_PATH], ncmd[MAX_PATH];
		memset(cmd, 0, MAX_PATH);
		memset(fcmd, 0, MAX_PATH);
		memset(ncmd, 0, MAX_PATH);
		strncpy(cmd, buf + 3, strlen(buf) - 3);
		LPCSTR lpCmdLine;
		if (strstr(cmd, "..\\") == cmd)
		{
			strncpy(ncmd, cmd + 3, strlen(cmd) - 3);
			sprintf(fcmd, "%s\\%s", szParentPath, ncmd);
			lpCmdLine = fcmd;
		}
		else
		{
			lpCmdLine = cmd;
		}
		// WinExec(lpCmdLine, uCmdShow);
		SHELLEXECUTEINFO exec = {0};
		exec.cbSize = sizeof(SHELLEXECUTEINFO);
		exec.fMask = SEE_MASK_NOCLOSEPROCESS;
		exec.hwnd = NULL;
		exec.lpVerb = NULL;
		exec.lpFile = lpCmdLine;
		exec.lpParameters = "";
		exec.lpDirectory = NULL;
		exec.nShow = uCmdShow;
		exec.hInstApp = NULL;
		ShellExecuteEx(&exec);
		// WaitForSingleObject(exec.hProcess,INFINITE);
	}
	else
	{
		send(client, buf, strlen(buf) + 1, 0);
	}
	// MessageBox(NULL, buf, "Socket Send", MB_ICONINFORMATION);
}

void OnClose(SOCKET client)
{
	closesocket(client);
	for(vector<SOCKET>::iterator it=sClients.begin(); it!=sClients.end(); )
	{
		if(*it == client)
		{
			it = sClients.erase(it);
		}
    else
		{
			++it;
		}
	}
	// MessageBox(NULL, "Socket Closed!", "Socket", MB_ICONINFORMATION);
}

void OnConnect(SOCKET client)
{
	// not triggered!!!
	// MessageBox(NULL, "Socket Connected!", "Socket", MB_ICONINFORMATION);
}

void OnAccept(SOCKET client)
{
	// not triggered!!!
	// MessageBox(NULL, "Socket Accepted!", "Socket", MB_ICONINFORMATION);
}

void Broadcast(const char* msg)
{
	for (int i = 0; i < sClients.size(); i++)
	{
		send(sClients[i], msg, strlen(msg) + 1, 0);
	}
}

void _stdcall ReadCOM(HWND hWnd, UINT message, UINT iTimerID, DWORD dwTime)
{
	unsigned char s[128];
	char r[32];
	memset(s, 0, sizeof(s));
	memset(r, 0, sizeof(r));
	int len=com.ReadData(s, sizeof(s));
	if (len >= 32)
	{
		// MessageBox(NULL, r, "Read COM", MB_ICONINFORMATION);
		sprintf(r, "%1X%02X-%02X%02X-%02X%02X#%02X%02X%02X%02X\0",
			s[28], s[29],
			s[30], s[31],
			s[14], s[15],
			s[10], s[11], s[12], s[13]);
		Broadcast(r);
	}
}