#include <iostream>
#include "DBConnetPool.h"
using namespace std;

CDBConnetPool::SConData::SConData(const TCHAR* connstr)
{
	if (FAILED(::CoInitialize(NULL)))
	{
		cout << "ConnInsql:ADO Init failed" << endl;
	}
	db_con.CreateInstance(__uuidof(Connection));
	try
	{
		db_con->Open((_bstr_t)connstr, "", "", adConnectUnspecified);
		flag = 0;

	}
	catch (_com_error *e)
	{
		cout << "SConData数据库连接错误";// << e->ErrorMessage() << e->Description();
	}
	catch (...)
	{
		cout << "连接池初始化数据库连接错误";
	}


}


////////////////////////////////////////////////////////////////
CDBConnetPool::CDBConnetPool(int max, TCHAR* connstr)
{
	this->connstr = connstr;
	if (max <= 0)
		m_max_size = 1;
	else
		m_max_size = max;

	m_pool = new SConData*[m_max_size];
	for (int i = 0; i < m_max_size; i++)
	{
		m_pool[i] = new SConData(connstr);
	}
}

CDBConnetPool::~CDBConnetPool(void)
{

	for (int i = 0; i < m_max_size; i++)
	{
		delete m_pool[i];
	}
	delete[] m_pool;

	if (connstr != NULL)
		delete[]connstr;
}

_ConnectionPtr CDBConnetPool::GetCon()
{
	lock.lock();
	int index = -1;
	int count = 0;
	for (int i = 0; i < m_max_size; i++)
	{
		if (m_pool[i]->flag == 0)
		{
			m_pool[i]->flag = 1;
			index = i;
			break;
		}
	}
	for (int i = 0; i < m_max_size; i++)
	{
		if (m_pool[i]->flag == 0)count++;
	}
	lock.unlock();
	//cout << "getconn" << index << " "<<",remain"<< count<<" "<<endl;
	if (index == -1)
	{
		_ConnectionPtr temp;
		temp = NULL;
		return temp;
	}
	return  m_pool[index]->db_con;

}

void CDBConnetPool::ReleaseCon(_ConnectionPtr con)
{
	lock.lock();
	int  count = 0;
	int index = 0;
	for (int i = 0; i < m_max_size; i++)
	{
		if (m_pool[i]->db_con == con)
		{
			m_pool[i]->flag = 0;
			index = i;
			break;
		}
	}
	for (int i = 0; i < m_max_size; i++)
	{
		if (m_pool[i]->flag == 0)count++;
	}
	lock.unlock();
	//cout << "ReleaseCon" << index << " " << ",remian" << count << " " << endl;
}
