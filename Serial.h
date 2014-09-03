// Serial.h: define COM serial port operate class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(SERIAL__INCLUDED_)
#define SERIAL__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <windows.h>

class CSerial
{
private:
	BOOL m_bOpened;
	HANDLE m_hComDev;
	OVERLAPPED m_OverlappedRead;
	OVERLAPPED m_OverlappedWrite;
public:
	CSerial(){};
	virtual ~CSerial(){};
public:
	BOOL Open(int nPort, int nBaud);
	BOOL Close();
	int InBufferCount();
	DWORD ReadData(void *buffer, DWORD dwBytesRead);
	DWORD SendData(const char *buffer, DWORD dwBytesWritten);
};

#endif // !defined(SERIAL__INCLUDED_)