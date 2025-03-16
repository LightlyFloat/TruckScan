#pragma once
#include <Windows.h> 

#import "C:\\Program Files\\Common Files\\System\\ado\\msado28.tlb" no_namespace  rename("EOF", "adoEOF"),rename("BOF","adoBOF")








class CDBConnetPool
{
public:
	CDBConnetPool(int max, TCHAR* ini_file = NULL);
	~CDBConnetPool(void);

private:
	class SConData
	{
	public:
		_ConnectionPtr db_con;
		volatile int flag;
		SConData(const TCHAR* ini_file);
	};

	SConData** m_pool;
	int m_max_size;
	TCHAR* connstr;
	lockBase lock;
public:
	static CDBConnetPool* s_pool;

	_ConnectionPtr GetCon();
	void ReleaseCon(_ConnectionPtr con);
};





class lockBase
{
protected:
	CRITICAL_SECTION cs;
public:
	lockBase() {
		::InitializeCriticalSection(&cs);
	}
	void lock() {
		::EnterCriticalSection(&cs);
	}
	void unlock() {
		::LeaveCriticalSection(&cs);
	}
	~lockBase() {
		::DeleteCriticalSection(&cs);
	}
};


























