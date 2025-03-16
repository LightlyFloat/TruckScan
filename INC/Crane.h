#pragma once
#include <time.h>
#include "snap7.h"
#include "DBConnetPool.h"


class Crane
{
public:
	
	Crane(const char* Scan, const char* RFID, const char* PLC, const char* Forklift, int ScanType, unsigned char No, int Range, CDBConnetPool& DBPool);
	~Crane();
	int Init();
	int ReadPLC();
	void ForkliftPause();// 铲车暂停函数声明
	int Scan();
	int Identify();
	int Calculate();
	int WritePLC();
	
private:

	/**************����Ϊ��ʼ��ʱ��Ҫ����ı���*********************************/
	double dTruckHead = 0.0;//������ͷλ��
	double dTruckTail = 0.0;//������βλ��
	int iCountY = 0;


	/***************����Ϊ��PLC��ȡ����ʱ��ʼ���ı�****************************/
	
	unsigned char CraneState = 0;//ж����״̬��0������ҵ��1��ж����2��װ��
	unsigned char HopPos = 0;//�϶�λ��
	unsigned char GrabNum = 0;//�ѷ�������


	unsigned char HearBeat = 0;
	unsigned char Fault = 1;//����
	unsigned char UnloadPermit = 0;//��������ж��
	unsigned char CarMoveCmd = 0;//�����ƶ�ָ��

	int GrabTotal = 0;

	int iState = -1;//��ǰɨ���״̬��0����������1����ʼ����ɣ�2����ȡPLC��ɣ�3��ɨ����ɣ�4��ʶ����ɣ�5��������ɣ�6��д��PLC���


	/***************����Ϊ�Ӳ���RFID��ȡ����ʱ����****************************/
	unsigned char Pause = 0;//��ͣ��0��������1����ͣ

	/****************�ɿ���ɨ����������************************/
	unsigned char TruckHave = 0;//��ǰж�����Ƿ��п�����0���޳���1���г�

	/****************����һ�γ�ͷ��β��λ�þ���***********************************/
	double dLastHead = 0.0;//�ϴο�����ͷλ��
	double dLastTail = 0.0;//�ϴο�����βλ��
	time_t lastTime;
			
		
	/*******���µı���Ϊ������ʱ�͸�ֵ�Ժ󲻻��ٱ��**********/
	unsigned char CraneNo = 0;//ж������
	int iScanType = 1;//ɨ���ǵ����͡�1��SICKLMS511��2��XXWXTL302
	int iRange = 500;//�жϿ���ͣ����Χ��
	char ipRFID[18]{ 0 };//RFIDʶ����IP��ַ��
	char ipPLC[18]{ 0 };//PLC������IP��ַ��
	char ipForklift[18]{ 0 };//����RFIDʶ����IP��ַ��

	
	/****************��RFID������********************************************/
	unsigned char truckRFID[20]{ 0 };//RFID��ǩ��

	/****************�ɲ���RFID������********************************************/
	unsigned char forkliftRFID[20]{ 0 };//����RFID��ǩ��

	/****************�����ݿ��л��**********************************************/
	int truckLocation[9]{ 0 };



	/****************PLC������**********************************************/
	TS7Client plc;

	/****************ɨ��ͷ���������**********************************************/
	SOCKET client;
	sockaddr_in serverAddr;//ɨ����IP��ַ��


	/****************���ݿ������**********************************************/
	CDBConnetPool& DB;
	

	int GetSICKLMS511();
	int GetXXWXTL302();
	int ConnectScan();
};


