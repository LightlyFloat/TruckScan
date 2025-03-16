#define MAX_BUFF_SIZE 16384
#define MAX_REC_NUM 10
#define _USE_MATH_DEFINES
#include <WS2tcpip.h>
#include <cmath>
#include <vector>
#include "Crane.h"
#include "easylogging++.h"


using namespace std;
using namespace el;
#pragma comment(lib,"ws2_32.lib")

Crane::Crane(const char* Scan, const char* RFID, const char* PLC, const char* Forklift, int ScanType, unsigned char No,int Range, CDBConnetPool& DBPool) :DB(DBPool)
{
    strcpy_s(ipRFID, RFID);
    strcpy_s(ipPLC, PLC);
    strcpy_s(ipForklift, Forklift);
    iScanType = ScanType;
    lastTime = time(NULL) - 5;
    CraneNo = No;
    iRange = Range;
    ::memset(truckRFID, 0, sizeof(truckRFID));
    ::memset(truckLocation, 0, sizeof(truckLocation));
    ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
    ::memset(&serverAddr, 0, sizeof(serverAddr));
    
    if (iScanType == 1)
    {
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(2111);
        inet_pton(AF_INET, Scan, &serverAddr.sin_addr);
    }
    else if (iScanType == 2)
    {
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(65530);
        inet_pton(AF_INET, Scan, &serverAddr.sin_addr);
    }    
    plc.ConnectTo(ipPLC, 0, 1);
    iState = ConnectScan();
}

Crane::~Crane() 
{
    if (plc.Connected())
        plc.Disconnect();
    closesocket(client);
}

int Crane::Init()
{
    cout << "��ʼ��һ�ֵ�ɨ��" << endl;
    //CraneNo = 0;//ж������
    CraneState = 0;//ж����״̬��0������ҵ��1��ж����2��װ��
    HopPos = 0;//�϶�λ��
    GrabNum = 0;//�ѷ�������

    HearBeat = 0;//�����ź�
    Fault = 1;//����
    Pause = 0;//��ͣ
    TruckHave = 0;//��ǰж�����Ƿ��п�����0���޳���1���г�
    UnloadPermit = 0;//��������ж��
    CarMoveCmd = 0;//�����ƶ�ָ��
    GrabTotal = 0;//Ӧ�Ŷ���
    dTruckHead = 0.0;
    dTruckTail = 0.0;
    iCountY = 0;
    iState = 1;
    return 0;
}

int Crane::ReadPLC() 
{
    unsigned char currData[21]{ 0 };//һ���ڵ�����
    if(plc.Connected())
        plc.DBRead(1003, 0, 21, currData);
    else
    {        
        if (plc.ConnectTo(ipPLC, 0, 1) == 0)
            LOG(INFO) << "�ڶ�ȡPLC����ʱ����PLC���½������ӳɹ�!" ;
        else 
            LOG(ERROR) << "�ڶ�ȡPLC����ʱ����PLCͨѶ���ִ���!";
        return -1;
    }

    //CraneNo = currData[0];//ж������
    CraneState = currData[1];//ж����״̬��0������ҵ��1��ж����2��װ��
    HopPos = currData[2];//�϶�λ��
    GrabNum = currData[3];//�ѷ�������
    HearBeat = currData[9] == 255 ? 1 : currData[9] + 1;//�����ź�
    //Fault = currData[11];//����
    Pause = currData[11];//��ͣ
    TruckHave = currData[12];//��ǰж�����Ƿ��п�����0���޳���1���г�
    GrabTotal = currData[13];//Ӧ�Ŷ���
    UnloadPermit = currData[14];//�ѷ�������
    CarMoveCmd = currData[15];//�ѷ�������
    
    iState = 2;

    if (currData[0] == 0 || currData[0] > 6 || currData[0] != CraneNo)
    {          
        LOG(ERROR) << "ж�����ı�Ų���ȷ";
        closesocket(client);
        return -2;
    }          
    if (CraneState > 2)
    {
        LOG(ERROR) << "ж������״̬����ȷ";
        closesocket(client);
        return -3;
    }
    if (HopPos > 3)
    {
        LOG(ERROR) << "ж�������϶�λ�ò���ȷ";
        closesocket(client);
        return -4;
    }
    if (HopPos < 2)
    {
        LOG(INFO) << "ж�������϶�λ�ò��ڹ���λ";
        closesocket(client);
        return -5;
    }
    return 1;
}

void Crane::ForkliftPause()
{
    if (iState != 2 || CraneState != 2)
    {
        Pause = 0;
        return;
    }

    HINSTANCE UHFReaderDll = LoadLibrary(L"UHFReader18.dll");
    if (UHFReaderDll)
    {
        long frmcomportindex = 0;
        unsigned char* EPC = new unsigned char[5000] { 0 };
        long Totallen = 0;
        typedef long (WINAPI* Open)(int, const char*, unsigned char*, long*);
        Open OpenNetPort = (Open)GetProcAddress(UHFReaderDll, "OpenNetPort");
        typedef long (WINAPI* Inventory)(unsigned char*, unsigned char, unsigned char, unsigned char, unsigned char*, long*, long*, long);
        Inventory Inventory_G2 = (Inventory)GetProcAddress(UHFReaderDll, "Inventory_G2");
        typedef long (WINAPI* Close)(long);
        Close CloseNetPort = (Close)GetProcAddress(UHFReaderDll, "CloseNetPort");

        if (OpenNetPort)
        {
            unsigned char fComAdr = 0xFF;
            OpenNetPort(6000, ipRFID, &fComAdr, &frmcomportindex);
            if (frmcomportindex != 0)
            {
                long CardNum = 0;
                Inventory_G2(&fComAdr, 0, 0, 0, EPC, &Totallen, &CardNum, frmcomportindex);
                if (CardNum == 0)
                {
                    LOG(INFO) << "ж������Χ��û�г�����";
                    goto  CleanForklift;
                }
                else if (CardNum == 1)
                {
                    if (Totallen > 16 || Totallen < 1)
                    {
                        LOG(ERROR) << "�����ı�ʶ����ȷ��";
                        goto  CleanForklift;
                    }
                    LOG(INFO) << "ж������Χ���г�����" << EPC;
                }
                else
                {
                    LOG(INFO) << "ж������Χ���ж�������";
                    goto  CleanForklift;
                }
            }
            else
            {
                LOG(ERROR) << "���Ӳ���RFID�豸ʧ�ܡ�";
                goto  CleanForklift;
            }


            for (int i = 0; i < Totallen; ++i)
            {
                if (forkliftRFID[i] != EPC[i])
                {
                    //�����Ѿ�������
                    //�����ݿ��в�ѯ��Ӧ��������Ϣ
                    char c_Buff[35]{ 0 };
                    for (int i_RFID = 0; i_RFID < Totallen; ++i_RFID)
                        sprintf_s(c_Buff + i_RFID * 2, sizeof(c_Buff) - i_RFID * 2, "%02X", EPC[i_RFID]);
                    int CraneID = (int)CraneNo;
                    LOG(INFO) << "ж������Ϊ��" << CraneID << "��������ʶΪ��" << c_Buff;
                    _ConnectionPtr conn = DB.GetCon();
                    try
                    {
                        _CommandPtr cmmdmy1;
                        cmmdmy1.CreateInstance(__uuidof(Command));;
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@CraneID"), adInteger, adParamInput, 4, CraneID));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@TruckID"), adVarChar, adParamInput, 30, c_Buff));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@GrabTotal"), adInteger, adParamOutput, 4));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location1"), adInteger, adParamOutput, 4));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location2"), adInteger, adParamOutput, 4));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location3"), adInteger, adParamOutput, 4));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location4"), adInteger, adParamOutput, 4));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location5"), adInteger, adParamOutput, 4));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location6"), adInteger, adParamOutput, 4));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location7"), adInteger, adParamOutput, 4));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location8"), adInteger, adParamOutput, 4));
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location9"), adInteger, adParamOutput, 4));
                        //cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@CoilID"), adVarChar, adParamOutput, 50));

                        cmmdmy1->CommandText = _bstr_t("CheckTruckNo");//�洢������
                        cmmdmy1->ActiveConnection = conn;
                        cmmdmy1->CommandType = adCmdStoredProc;
                        cmmdmy1->Execute(NULL, NULL, adCmdStoredProc);
                        int iPause = cmmdmy1->Parameters->GetItem("@GrabTotal")->GetValue();
                        if (iPause == -1)
                        {
                            Pause = 1;
                            ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
                            ::memcpy(forkliftRFID, EPC, Totallen);
                        }
                        else
                        {
                            Pause = 0;
                            ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
                        }

                    }
                    catch (...)
                    {
                        LOG(ERROR) << "�����ݿ��ж�������Ϣ����:" << c_Buff;
                        //Pause = 0;
                        //::memset(forkliftRFID, 0, sizeof(forkliftRFID));
                    }
                    DB.ReleaseCon(conn);
                    break;
                }
            }
            goto  CleanForklift;
        }
        else
        {
            LOG(ERROR) << "�޷���RFIDʶ��⡣";
            Pause = 0;
            ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
            goto  CleanForklift;
        }

    CleanForklift:
        delete[] EPC;
        if (CloseNetPort)
        {
            CloseNetPort(frmcomportindex);
        }
        FreeLibrary(UHFReaderDll);
    }
    else
    {
        LOG(ERROR) << "����RFID����";
        Pause = 0;
        ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
    }
}

int  Crane::Scan()
{
    if (iState > 1 && CraneState < 2)
    {
        int i_res = 0;
        if (iScanType == 1)
            i_res = GetSICKLMS511();
        else if (iScanType == 2)
            i_res = GetXXWXTL302();
        else
        {
            LOG(ERROR) << "����ɨ���״����ʹ���!";
            return -19;
        }

        if (TruckHave == 1)
        {
            
            LOG(INFO) << "��ͷ�ڣ�" << dTruckHead << "����β�ڣ�" << dTruckTail << "���ϴγ�ͷ�ڣ�" << dLastHead << "���ϴγ�β�ڣ�" << dLastTail ;
        }
        else
        {
            GrabTotal = 0;
            UnloadPermit = 0;
            CarMoveCmd = 0;
        }
        if (i_res >= 0)
            iState = 3;
        return i_res;
    }
    else
    {
        return -10;
    }
}

int  Crane::Identify()
{
    int iRes = 20;
    if (iState == 3)
    {
        if (TruckHave == 1)
        { 
            HINSTANCE UHFReaderDll = LoadLibrary(L"UHFReader18.dll");
            if (UHFReaderDll)
            {
                long frmcomportindex = 0;
                unsigned char* EPC = new unsigned char[5000] { 0 };
                long Totallen = 0;
                typedef long (WINAPI* Open)(int, const char*, unsigned char*, long*);
                Open OpenNetPort = (Open)GetProcAddress(UHFReaderDll, "OpenNetPort");
                typedef long (WINAPI* Inventory)(unsigned char*, unsigned char, unsigned char, unsigned char, unsigned char*, long*, long*, long);
                Inventory Inventory_G2 = (Inventory)GetProcAddress(UHFReaderDll, "Inventory_G2");
                typedef long (WINAPI* Close)(long);
                Close CloseNetPort = (Close)GetProcAddress(UHFReaderDll, "CloseNetPort");

                if (OpenNetPort)
                {
                    unsigned char fComAdr = 0xFF;
                    OpenNetPort(6000, ipRFID, &fComAdr, &frmcomportindex);
                    if (frmcomportindex != 0)
                    {
                        long CardNum = 0;
                        Inventory_G2(&fComAdr, 0, 0, 0, EPC, &Totallen, &CardNum, frmcomportindex);
                        if (CardNum == 0)
                        {
                            LOG(ERROR) << "û��ʶ�𵽿����ı�ʶ��";
                            if (GrabTotal <= 0)
                                iRes = -20;
                            goto  CleanRFID;
                        }
                        else if (CardNum == 1)
                        {
                            if (Totallen > 16 || Totallen < 1)
                            {
                                LOG(ERROR) << "�����ı�ʶ����ȷ��";
                                if (GrabTotal <= 0)
                                    iRes = -21;
                                goto  CleanRFID;
                            }
                            LOG(INFO) << "ʶ�𵽿�����ݡ�" << EPC;
                        }
                        else
                        {
                            LOG(ERROR) << "��������Χ�������ĳ�����";
                            if (GrabTotal <= 0)
                                iRes = -22;
                            goto  CleanRFID;
                        }
                    }
                    else
                    {
                        LOG(ERROR) << "����RFID�豸ʧ�ܡ�";
                        if (GrabTotal <= 0)
                            iRes = -23;
                        goto  CleanRFID;
                    }
                    
                    for (int i = 0; i < Totallen; ++i)
                    {
                        if (truckRFID[i] != EPC[i] || GrabTotal <= 0)
                        {
                            ::memset(truckLocation, 0, sizeof(truckLocation));
                            //�����Ѿ�������
                            //�����ݿ��в�ѯ��Ӧ��������Ϣ�Լ�ÿһץ��λ����Ϣ����λ����Ϣд�뵽truckLocation��
                            char c_Buff[35]{ 0 };
                            for (int i_RFID = 0; i_RFID < Totallen; ++i_RFID)
                                sprintf_s(c_Buff + i_RFID * 2, sizeof(c_Buff) - i_RFID * 2, "%02X", EPC[i_RFID]);
                            int CraneID =(int) CraneNo;
                            LOG(INFO) <<"ж������Ϊ��"<< CraneID << "��������ʶΪ��" << c_Buff ;
                            _ConnectionPtr conn = DB.GetCon();
                            try
                            {
                                _CommandPtr cmmdmy1;
                                cmmdmy1.CreateInstance(__uuidof(Command));;
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@CraneID"), adInteger, adParamInput, 4, CraneID));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@TruckID"), adVarChar, adParamInput, 30, c_Buff));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@GrabTotal"), adInteger, adParamOutput, 4));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location1"), adInteger, adParamOutput, 4));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location2"), adInteger, adParamOutput, 4));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location3"), adInteger, adParamOutput, 4));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location4"), adInteger, adParamOutput, 4));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location5"), adInteger, adParamOutput, 4));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location6"), adInteger, adParamOutput, 4));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location7"), adInteger, adParamOutput, 4));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location8"), adInteger, adParamOutput, 4));
                                cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@Location9"), adInteger, adParamOutput, 4));
                                //cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@CoilID"), adVarChar, adParamOutput, 50));

                                cmmdmy1->CommandText = _bstr_t("CheckTruckNo");//�洢������
                                cmmdmy1->ActiveConnection = conn;
                                cmmdmy1->CommandType = adCmdStoredProc;
                                cmmdmy1->Execute(NULL, NULL, adCmdStoredProc);
                                GrabTotal = cmmdmy1->Parameters->GetItem("@GrabTotal")->GetValue();
                                if (GrabTotal > 0)
                                {
                                    truckLocation[0] = cmmdmy1->Parameters->GetItem("@Location1")->GetValue();
                                    truckLocation[1] = cmmdmy1->Parameters->GetItem("@Location2")->GetValue();
                                    truckLocation[2] = cmmdmy1->Parameters->GetItem("@Location3")->GetValue();
                                    truckLocation[3] = cmmdmy1->Parameters->GetItem("@Location4")->GetValue();
                                    truckLocation[4] = cmmdmy1->Parameters->GetItem("@Location5")->GetValue();
                                    truckLocation[5] = cmmdmy1->Parameters->GetItem("@Location6")->GetValue();
                                    truckLocation[6] = cmmdmy1->Parameters->GetItem("@Location7")->GetValue();
                                    truckLocation[7] = cmmdmy1->Parameters->GetItem("@Location8")->GetValue();
                                    truckLocation[8] = cmmdmy1->Parameters->GetItem("@Location9")->GetValue();
                                }
                                else
                                {
                                    GrabTotal = 0;
                                    ::memset(truckLocation, 0, sizeof(truckLocation));
                                }
                            }
                            catch (...)
                            {
                                LOG(ERROR) << "д�����ݿ����:" << c_Buff;
                                if (GrabTotal <= 0)
                                    iRes = -26;
                                
                            }
                            DB.ReleaseCon(conn);
                            //�������ĳ���д�뵽���ݿ���

                            ::memset(truckRFID, 0, sizeof(truckRFID));
                            ::memcpy(truckRFID, EPC, Totallen);
                            break;
                        }
                    }
                }
                else
                {
                    LOG(ERROR) << "����RFIDʶ������";
                    if (GrabTotal <= 0)
                        iRes = -24;
                    goto  CleanRFID;
                }

            CleanRFID:
                delete[] EPC;
                if (CloseNetPort)
                {
                    CloseNetPort(frmcomportindex);
                }
                FreeLibrary(UHFReaderDll);
            }
            else
            {
                LOG(ERROR) << "����RFID����";
                if (GrabTotal <= 0)
                    iRes = -25;
            }
        }
        else if (TruckHave == 0)
        {
            for (int i = 0; i < sizeof(truckRFID); ++i)
            {
                if (truckRFID[i] > 0)
                {
                    ///�������ݿ⽫��ж�����Ŀ�����Ϣ���
                    
                    int CraneID = (int)CraneNo;
                    LOG(INFO) << "ж������Ϊ��" << CraneID << "�������Ѿ��뿪��";
                    _ConnectionPtr conn = DB.GetCon();
                    try
                    {
                        _CommandPtr cmmdmy1;
                        cmmdmy1.CreateInstance(__uuidof(Command));;
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@CraneID"), adInteger, adParamInput, 4, CraneID));

                        cmmdmy1->CommandText = _bstr_t("CleanTruckNo");//�洢������
                        cmmdmy1->ActiveConnection = conn;
                        cmmdmy1->CommandType = adCmdStoredProc;
                        cmmdmy1->Execute(NULL, NULL, adCmdStoredProc);
                        
                    }
                    catch (...)
                    {
                        LOG(ERROR) << "�޿���д�����ݿ����";
                        iRes = -26;

                    }
                    DB.ReleaseCon(conn);
                    ::memset(truckLocation, 0, sizeof(truckLocation));
                    ::memset(truckRFID, 0, sizeof(truckRFID));//��տ�����Ϣ
                    break;
                }
            }
            if (GrabTotal != 0)
                GrabTotal = 0;
        }
        if (iRes > 0)
            iState = 4;
    }
    return iRes;
}

int  Crane::Calculate()
{
    if (iState == 4)
    {
        if (TruckHave == 1)
        {
            if (GrabTotal > 0 && GrabTotal < 9)
            {
                if (GrabTotal == GrabNum)
                {
                    Fault = 1;
                    CarMoveCmd = 2;
                    UnloadPermit = 0;
                }
                else if (GrabNum < GrabTotal)
                {
                    if (dTruckTail < truckLocation[GrabNum] - iRange)
                    {
                        CarMoveCmd = 3;
                        UnloadPermit = 0;
                    }
                    else if (dTruckTail > truckLocation[GrabNum] + iRange)
                    {
                        CarMoveCmd = 2;
                        UnloadPermit = 0;
                    }
                    else
                    {
                        CarMoveCmd = 1;
                        if (time(0) >= lastTime + 2)
                        {
                            if (abs(dTruckHead - dLastHead) < 500 && abs(dTruckTail - dLastTail) < 200)
                                UnloadPermit = 1;
                            else
                                UnloadPermit = 0;
                        }
                    }
                }
                else
                {
                    //Fault = 0;
                    UnloadPermit = 0;
                    CarMoveCmd = 0;
                    LOG(ERROR) << "�ѷ���������";
                    return -31;
                }
            }
            else
            {
                LOG(ERROR) << "û�п���װ�ϵ�λ����Ϣ��";
                return -32;
            }
        }
        else
        {
            GrabTotal = 0;
            UnloadPermit = 0;
            CarMoveCmd = 0;
        }
        iState = 5;
        return 30;
    }
    else
    {
        return -30;
    }
}

int Crane::WritePLC()
{
    time_t now = time(0);
    if (now >= lastTime + 2)
    {
        dLastHead = dTruckHead;
        dLastTail = dTruckTail;
        lastTime = now;
    }
    if (iState > 1) 
    { 
        unsigned char result[12]{ 0 };
        result[0] = HearBeat;
        result[1] = Fault;
        result[2] = Pause;
        result[3] = TruckHave;//��ǰж�����Ƿ��п�����0���޳���1���г�
        result[4] = GrabTotal;//Ӧ�Ŷ���
        result[5] = UnloadPermit;//��������ж��
        result[6] = CarMoveCmd;//�����ƶ�ָ��

        float f_temp = (float)dTruckTail;
        memcpy(result + 7, &f_temp, 4);
        unsigned char c_temp = 0;
        c_temp = result[7];
        result[7] = result[10];
        result[10] = c_temp;
        c_temp = result[8];
        result[8] = result[9];
        result[9] = c_temp;

        if (plc.Connected())
            plc.DBWrite(1003, 9, 12, result);
        else
        {
            if (plc.ConnectTo(ipPLC, 0, 1) == 0)
                LOG(INFO) << "��д��PLC����ʱ����PLC���½������ӳɹ�!";
            else
                LOG(ERROR) << "��д��PLC����ʱ����PLCͨѶ���ִ���!";
            return -41;
        }


        iState = 5;
        return 40;
    }
    else 
    {
        LOG(ERROR) << "ϵͳ������";
        return -42;
    }
}

int Crane::ConnectScan()
{
    int iRes = -1;

    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET)
    {
        LOG(ERROR) << "����WinSocketʧ��!";
        return iRes;
    }

    int i_param = 0;
    i_param = 1000;
    i_param = setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char*)&i_param, sizeof(i_param));
    i_param = 1000;
    i_param = setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&i_param, sizeof(i_param));
    linger lin_param;
    lin_param.l_onoff = 1;
    lin_param.l_linger = 0;
    i_param = setsockopt(client, SOL_SOCKET, SO_LINGER, (const char*)&lin_param, sizeof(lin_param));
    if (iScanType == 1)
    {
        if (connect(client, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            LOG(ERROR) << "����SICKLMS511ɨ����ʧ��!";
            return iRes;
        }
    }
    else if (iScanType == 2)
    {
        if (connect(client, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            LOG(ERROR) << "������������XT-L302ʧ��!";
            return iRes;
        }
        char c_RecBuff[50]{ 0 };
        unsigned char c_SendMode0[] = { 0xFE,0x00,0x00,0x01,0x00,0x78,0xF0 };
        send(client, (const char*)c_SendMode0, sizeof(c_SendMode0), 0);
        int i_RecLen = recv(client, c_RecBuff, sizeof(c_RecBuff), 0);
        if (i_RecLen == 7 && c_RecBuff[1] == 1 && c_RecBuff[2] == 0 && c_RecBuff[3] == 1)
        {
            if (c_RecBuff[4] == 0)
            {
                cout << "ɨ���ǳɹ��Ѿ��л�������״̬��" << endl;
            }
            else
            {
                LOG(ERROR) << "ɨ�����л�������״̬ʧ�ܡ�";
                closesocket(client);
                return iRes;
            }
        }
        else
        {
            LOG(ERROR) << "��������XT-L302�޷���������!";
            closesocket(client);
            return iRes;
        }
    }
    else
        return iRes;

    return 0;
}

int Crane::GetSICKLMS511()
{
    int iRes = 10;

    char c_Sendscan[17] = { 0x02, 0x73, 0x52, 0x4E, 0x20, 0x4C, 0x4D, 0x44, 0x73, 0x63, 0x61, 0x6E, 0x64, 0x61, 0x74, 0x61, 0x03 };
    char* c_RecBuff = new char[MAX_BUFF_SIZE] {0};
   
    int i_RecLen = 0;
    string st_data = "";
    size_t pos = 0;
    vector<string> data;
    int factors = 1;
    double startangle = 0.0;
    //  '''�Ƕȷֱ���'''
    double ang = 0.0;
    double anglestep = 0.0;
    // '''��������'''
    int datanum = 0;
    
    if (send(client, c_Sendscan, 17, 0) < 0)
    {
        LOG(ERROR) << "��SICKLMS511���Ͳ�ѯָ��ʧ��!";
        closesocket(client);
        iRes = ConnectScan();
        goto CleanSICKLMS511;
    }
    i_RecLen = recv(client, c_RecBuff, MAX_BUFF_SIZE, 0);
    if (i_RecLen <= 0 || i_RecLen >= MAX_BUFF_SIZE - 1)
    { 
        LOG(ERROR) << "SICKLMS511���ص����ݲ�����!";
        Fault = 0;//���޹��ϡ�0�����ϣ�1������
        goto CleanSICKLMS511;
    }

    c_RecBuff[i_RecLen] = ' ';
    c_RecBuff[i_RecLen + 1] = '\0';

    st_data = c_RecBuff;
    pos = st_data.find(' ');
    while (pos > 0 and pos < st_data.size())
    {
        string st_temp = st_data.substr(0, pos);
        data.push_back(st_temp);////??????
        st_data = st_data.substr(pos + 1, st_data.size());
        pos = st_data.find(' ');
    }

    if (data[21] == "40000000")
        factors = 2;

    startangle = stoi(data[23], nullptr, 16) / 10000.0;
    //  '''�Ƕȷֱ���'''
    ang = stoi(data[24], nullptr, 16);
    anglestep = ang / 10000.0;
    // '''��������'''
    datanum = stoi(data[25], nullptr, 16);

    for (int i = 0; i < datanum; ++i)
    {
        int dd = stoi(data[26 + i], nullptr, 16);
        if (dd <= 0)
            continue;
        double x = (double)dd * factors * cos((startangle + i * anglestep) / 180.0 * M_PI);
        double y = (double)dd * factors * sin((startangle + i * anglestep) / 180.0 * M_PI);


        if (x > 10000.0 || x < -20000.0)
            continue;
        else
            x = 20000.0 + x;

        if (y < 2000.0 + (HopPos - 1) * 7500.0 && y > 500.0)
        {
            ++iCountY;
        }

        if (y > 2000.0 + (HopPos - 1) * 7500.0 && y < 2000.0 + HopPos * 7500.0) //�ж�ͷβ
        {
            if (abs(dTruckHead - 0.0) > 0.1)
            {
                if (dTruckHead > x)
                {
                    dTruckHead = x;
                }
            }
            else
            {
                dTruckHead = x;
            }

            if (abs(dTruckTail - 0.0) > 0.1)
            {
                if (dTruckTail < x)
                {
                    dTruckTail = x;
                }
            }
            else
            {
                dTruckTail = x;
            }
        }
    }

    if (abs(dTruckTail - 0.0) < 0.1 && abs(dTruckHead - 0.0) < 0.1 && dLastHead > 3000.0 && dLastTail > 3000.0)
    {
        //ɨ���Ƿ�����Ʈ
        LOG(ERROR) << "ɨ���ǳ������������!";
        iRes = -15;
        goto CleanSICKLMS511;

    }

    if (iCountY > 9)
    {
        LOG(ERROR) << "������Χ����������!";
        iRes = -16;
        goto CleanSICKLMS511;
    }

    if (dTruckHead > 2000.0)//�жϳ����ǲ�����ɨ��ķ�Χ��
    {
        if (dTruckTail > 29500.0)
        {
            LOG(ERROR) << "������û����ȫ���뵽ɨ������!";
            iRes = -17;
            goto CleanSICKLMS511;
        }

        if (dTruckTail - dTruckHead > 10000 && dTruckTail - dTruckHead < 18000)
            //�ж�Ϊ�г�

            TruckHave = 1;
        else 
        {
            LOG(ERROR) << "����ǰ������������!";
            iRes = -18;
            goto CleanSICKLMS511;
        }

    }
    else
    {
        if (dTruckTail < 21000.0f)
        {
            //�ж�Ϊ�޳�
            TruckHave = 0;
            goto CleanSICKLMS511;
        }
        LOG(ERROR) << "����ǰ������������!";
        iRes = -18;
    }

CleanSICKLMS511:    
    delete[] c_RecBuff;
    return iRes;
}

int Crane::GetXXWXTL302()
{
    int iRes = 10;
    int i_RecLen = 1;
    
    unsigned char c_SendApply[] = { 0xFE,0x00,0x15,0x01,0x00,0x78,0xF0 };
    
   
   
    unsigned char* c_Data = new unsigned char[MAX_BUFF_SIZE] {0};
    unsigned short startangle = 0;
    unsigned short  dataNum = 0;
    i_RecLen = send(client, (const char*)c_SendApply, sizeof(c_SendApply), 0);
    if (i_RecLen < 0)
    {
        LOG(ERROR) << "����������XT-L302���Ͳ�ѯָ��ʧ��!";
        closesocket(client);
        iRes = ConnectScan();
        goto ReturnXXWXTL302;
    }
    for (;;)
    {
        i_RecLen = 0;
        i_RecLen = recv(client, (char*)(c_Data), MAX_BUFF_SIZE, 0);

        if (i_RecLen <= 1 || i_RecLen >= MAX_BUFF_SIZE - 1)
        {
            LOG(ERROR) << "��������XT-L302���ص����ݲ�����!";
            Fault = 0;//���޹��ϡ�0�����ϣ�1������
            closesocket(client);
            iRes = ConnectScan();
            goto ReturnXXWXTL302;
        }
        if (i_RecLen <= 20)
        {
            continue;
        }
        break;
    }
   

    for (int i = 0; i < i_RecLen; ++i )
    { 
        if (*(c_Data + i) == 0XFE && *(c_Data + i + 1) == 0X02 && *(c_Data + i + 2) == 0X06)
        {
            ::memcpy((char*)&startangle, c_Data + i + 5, 2);
            ::memcpy((char*)&dataNum, c_Data + i + 7, 2);
            
            for (int j = 0; j < dataNum; ++j)
            {
                int angle = startangle + j - 225;
                if (angle >= 825)
                    break;
                else if (angle >= 75 && angle < 825)
                {

                    unsigned short dd = 0;
                    ::memcpy((char*)&dd, c_Data + i + 9 + j * 2, 2);
                    if (dd == 0)
                        continue;

                    double redian = angle / 900.0 * M_PI;

                    double x = dd * cos(redian);
                    double y = dd * sin(redian);

                    if (x > 10000.0 || x < -20000.0)
                        continue;
                    else
                        x = 20000.0 + x;

                    if (y < 2000.0 + (HopPos - 1) * 7500.0 && y > 500.0)
                    {
                        ++iCountY;
                    }

                    if (y > 2000.0 + (HopPos - 1) * 7500.0 && y < 2000.0 + HopPos * 7500.0) //�ж�ͷβ
                    {
                        if (abs(dTruckHead - 0.0) > 0.1)
                        {
                            if (dTruckHead > x)
                            {
                                dTruckHead = x;
                            }
                        }
                        else
                        {
                            dTruckHead = x;
                        }

                        if (abs(dTruckTail - 0.0) > 0.1)
                        {
                            if (dTruckTail < x)
                            {
                                dTruckTail = x;
                            }
                        }
                        else
                        {
                            dTruckTail = x;
                        }

                    }
                }
            }
            break;
        }
    }
    
    if (abs(dTruckTail - 0.0) < 0.1 && abs(dTruckHead - 0.0) < 0.1 && dLastHead > 3000.0 && dLastTail > 3000.0)
    {
        //ɨ���Ƿ�����Ʈ
        LOG(ERROR) << "ɨ���ǳ������������!";
        iRes = -15;
        goto ReturnXXWXTL302;

    }

    if (iCountY > 9)
    {
        LOG(ERROR) << "������Χ����������!";
        iRes = -16;
        goto ReturnXXWXTL302;
    }


    if (dTruckHead > 2000.0)//�жϳ����ǲ�����ɨ��ķ�Χ��
    {
        if (dTruckTail > 29500.0)
        {
            LOG(ERROR) << "������û����ȫ���뵽ɨ������!";
            iRes = -17;
            goto ReturnXXWXTL302;
        }

        if (dTruckTail - dTruckHead > 9000 && dTruckTail - dTruckHead < 18000)
            //�ж�Ϊ�г�

            TruckHave = 1;
        else
        {
            LOG(ERROR) << "����ǰ������������!";
            iRes = -18;
            goto ReturnXXWXTL302;
        }

    }
    else
    {
        if (dTruckTail < 21000.0f)
        {
            //�ж�Ϊ�޳�
            TruckHave = 0;
            goto ReturnXXWXTL302;
        }
        LOG(ERROR) << "����ǰ������������!";
        iRes = -18;
    }

ReturnXXWXTL302:
    delete[] c_Data;
    return iRes;
}





