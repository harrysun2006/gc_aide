#include <windows.h>
#include "Serial.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

BOOL CSerial::Open(int nPort, int nBaud)
{
	if(m_bOpened) return TRUE;
	char szPort[15];
	wsprintf(szPort, "COM%d", nPort);
	m_hComDev = CreateFile(szPort, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
	if(m_hComDev == NULL) return FALSE;

	memset(&m_OverlappedRead, 0, sizeof(OVERLAPPED));
	memset(&m_OverlappedWrite, 0, sizeof(OVERLAPPED));

	COMMTIMEOUTS commTimeOuts;
	commTimeOuts.ReadIntervalTimeout = 0xFFFFFFFF;
	commTimeOuts.ReadTotalTimeoutMultiplier = 0;
	commTimeOuts.ReadTotalTimeoutConstant = 0;
	commTimeOuts.WriteTotalTimeoutMultiplier = 0;
	commTimeOuts.WriteTotalTimeoutConstant = 5000;
	SetCommTimeouts(m_hComDev, &commTimeOuts);

	m_OverlappedRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_OverlappedWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	DCB dcb;
	dcb.DCBlength = sizeof(DCB);
	GetCommState(m_hComDev, &dcb);
	dcb.BaudRate = nBaud;
	dcb.ByteSize = 8;
	if(!SetCommState(m_hComDev, &dcb) || !SetupComm(m_hComDev, 10000, 10000) 
		||	m_OverlappedRead.hEvent == NULL || m_OverlappedWrite.hEvent == NULL)
	{
		DWORD dwError = GetLastError();
		if(m_OverlappedRead.hEvent != NULL) CloseHandle(m_OverlappedRead.hEvent);
		if(m_OverlappedWrite.hEvent != NULL) CloseHandle(m_OverlappedWrite.hEvent);
		CloseHandle(m_hComDev);
		return FALSE;
	}

	m_bOpened = TRUE;
	return m_bOpened;
}

BOOL CSerial::Close()
{
	m_bOpened=FALSE;
	CloseHandle(m_hComDev);
	return TRUE;
}

int CSerial::InBufferCount()
{
	if(!m_bOpened || m_hComDev == NULL) return 0;
	DWORD dwErrorFlags;
	COMSTAT ComStat;
	ClearCommError(m_hComDev, &dwErrorFlags, &ComStat);
	return (int)ComStat.cbInQue;
}

DWORD CSerial::ReadData(void *buffer, DWORD dwBytesRead)
{
	if(!m_bOpened || m_hComDev == NULL) return 0;
	BOOL bReadStatus;
	DWORD dwErrorFlags;
	COMSTAT ComStat;
	ClearCommError(m_hComDev, &dwErrorFlags, &ComStat);
	if(!ComStat.cbInQue) return 0;

	dwBytesRead = min(dwBytesRead,(DWORD)ComStat.cbInQue);
	bReadStatus = ReadFile(m_hComDev, buffer, dwBytesRead, &dwBytesRead, &m_OverlappedRead);
	if(!bReadStatus)
	{
		if(GetLastError() == ERROR_IO_PENDING)
		{
			WaitForSingleObject(m_OverlappedRead.hEvent, 2000);
			return dwBytesRead;
		}
		return 0;
	}
	return dwBytesRead;
}

DWORD CSerial::SendData(const char *buffer, DWORD dwBytesWritten)
{
	if( !m_bOpened || m_hComDev == NULL ) return( 0 );

	BOOL bWriteStat = WriteFile(m_hComDev, buffer, dwBytesWritten, &dwBytesWritten, &m_OverlappedWrite);
	if(!bWriteStat)
	{
		if (GetLastError() == ERROR_IO_PENDING) 
		{
			WaitForSingleObject(m_OverlappedWrite.hEvent, 1000);
			return dwBytesWritten;
		}
		return 0;
	}
	return dwBytesWritten;
}