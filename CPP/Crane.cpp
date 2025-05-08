// --- Ԥ����ָ�� (Preprocessor Directives) ---

#define MAX_BUFF_SIZE 16384  // ������󻺳�����С
#define MAX_REC_NUM 10      // ��������¼��
#define _USE_MATH_DEFINES   // ���� <cmath> ����ѧ��������
#include <WS2tcpip.h>      // ����ͨ�����
#include <cmath>           // ��ѧ���������� C++ ��׼��ѧ��ͷ�ļ����ṩ��ѧ���㹦��
#include <vector>          // STL�����������ṩ��̬���鹦��
#include "Crane.h"         // ж������ͷ�ļ�
#include "easylogging++.h" // ��־ϵͳ


using namespace std;//�����ռ䣬��׼��
using namespace el;//�����ռ䣬��־ϵͳ
#pragma comment(lib,"ws2_32.lib")//���ӿ�

// �����˵�һ̨�µ� `Crane` (ж�������Ƴ���ʵ��) ������ʱ����Ҫ����Щ��ʼ�����������¸����豸�������ַ (IP ��ַ)����̨ж�����Լ��ı�š��õ�������ɨ���ǵȵȣ������Ժ� PLC��ɨ���ǽ�����һ�����ӡ�
Crane::Crane(const char* Scan, const char* RFID, const char* PLC, const char* Forklift,// ����: Scan, RFID, PLC, Forklift ��IP��ַ (C����ַ���), ɨ��������, ж�������, ������Χ, ���ݿ����ӳض�������
             int ScanType, unsigned char No, int Range, CDBConnetPool& DBPool) :DB(DBPool)
{
    // ��ʼ��IP��ַ (��¼�������豸�������ַ)
    strcpy_s(ipRFID, RFID);        // �������RFID��ȡ��IP��ַ���Ƶ���ĳ�Ա���� ipRFID ��
    strcpy_s(ipPLC, PLC);          // �������PLC������IP��ַ���Ƶ���ĳ�Ա���� ipPLC ��
    strcpy_s(ipForklift, Forklift); // ������Ĳ泵����豸IP��ַ���Ƶ���ĳ�Ա���� ipForklift ��

    iScanType = ScanType;          // �����õ����������͵�ɨ���ǣ��������ɨ�������͸�ֵ����Ա���� iScanType
    lastTime = time(NULL) - 5;     // ��ʼ��һ��ʱ�������ȡ��ǰʱ�������ȥ5�룬��ֵ����Ա���� lastTime
    CraneNo = No;                  // ������̨ж�����Լ��ı�ţ��������ж������Ÿ�ֵ����Ա���� CraneNo
    iRange = Range;                // ���¹�����Χ��ص����ã�������Ĺ�����Χ������ֵ����Ա���� iRange

    // ��ʼ�����ֻ����� (׼����һЩ�հ׵ġ���¼�������ڴ�ռ䡱)
    ::memset(truckRFID, 0, sizeof(truckRFID));       // ��ռ�¼����ID�ĵط�
    ::memset(truckLocation, 0, sizeof(truckLocation)); // ��ռ�¼����λ�õĵط�
    ::memset(forkliftRFID, 0, sizeof(forkliftRFID)); // ��ռ�¼�泵ID�ĵط�

    // ����ɨ������������ (����ɨ�������ͣ����úú�����˵�����ķ�ʽ)
    if (iScanType == 1) {  // ����� SICK LMS511 ɨ����
        serverAddr.sin_port = htons(2111); // ���ö˿ں�Ϊ 2111
    }
    else if (iScanType == 2) {  // ����� XXWXTL302 ɨ����
        serverAddr.sin_port = htons(65530); // ���ö˿ں�Ϊ 65530
    }

    // ����PLC��ɨ���� (���Ժ�PLC��ɨ���ǽ�����ϵ)
    plc.ConnectTo(ipPLC, 0, 1); // ��������PLC
    iState = ConnectScan();      // ���ñ���� ConnectScan ������������ɨ���ǣ�����¼��������״̬�������ص�����״̬��״̬���� `iState` �С�
}

//�����ж�������Ƴ���ʵ��������Ҫ��׼������ʱ����δ��븺����������
Crane::~Crane()
{
    if (plc.Connected())
        plc.Disconnect();    // ���������PLC������� plc ����� Disconnect �����Ͽ���PLC������
    closesocket(client);     // ���� closesocket �����ر���ɨ�������ӵ��׽��� (client ���׽��־��)
}

// ��ж������ʼ��һ�ֹ���ǰ�������й���״̬��־&״̬����(���統ǰ����ʲô��ץ�˶��ٴΡ���û�й��ϵ�)����س�ʼֵ
int Crane::Init()
{
    // ��ʼ������״̬���� (�����й���״̬��־����س�ʼֵ)
    CraneState = 0;    // ж����״̬������ 0 �������
    HopPos = 0;        // ץ��λ��
    GrabNum = 0;       // ��ץȡ��������
    HearBeat = 0;      // �����ź� (����ȷ���豸�Ƿ�����)
    Fault = 1;         // ����״̬ (����һ��ʼ���������⣬��Ҫ���)
    Pause = 0;         // ��ͣ״̬ (Ĭ�ϲ���ͣ)
    TruckHave = 0;     // Ĭ��û�п���
    UnloadPermit = 0;  // Ĭ�ϲ�����ж��
    CarMoveCmd = 0;    // Ĭ��û�г����ƶ�ָ��
    GrabTotal = 0;     // ��ץȡ��������
    dTruckHead = 0.0;  // ����ͷλ������
    dTruckTail = 0.0;  // ����βλ������
    iCountY = 0;       // һ������������
    iState = 1;        // �豸�ڲ�״̬��־
    return 0;          // ����0��ʾ��ʼ���ɹ�
}

//�� PLC ��ȡ���ݵĳ�Ա��������ȡ PLC ֪���Ĺ�������ж����������״̬��Ϣ������һЩ������飬ȷ����Ϣ�Ǻ���ġ�
int Crane::ReadPLC()// ����һ��int����ֵ (��ʾ��ȡ���״̬)
{
    unsigned char currData[21]{ 0 };  // ����һ���ֲ�unsigned char����currData����СΪ21�ֽڣ�����ʼ������Ԫ��Ϊ0���������������ʱ���������洢��PLC��ȡ��ԭʼ���ݡ�׼��һ�������ӡ�����װ��PLC����������

    // ��ȡPLC����
    if(plc.Connected()) { // ���plc �����PLC����
        plc.DBRead(1003, 0, 21, currData); // ��ȥ��PLC���ַ1003��ʼ��21���ֽ�����
    }
    else { // ���û����
        // ������������PLC
        if (plc.ConnectTo(ipPLC, 0, 1) == 0) // ConnectTo�ɹ�����0
            LOG(INFO) << "PLC�������ӳɹ�"; // ��¼��־�����ӳɹ�
        else
            LOG(ERROR) << "PLCͨ�Ŵ���"; // ��¼��־������ʧ��
        return -1; // ���ش������
    }

    //CraneNo = currData[0];//��??????
    // ����PLC����
    CraneState = currData[1];  // ж����״̬
    HopPos = currData[2];      // ץ��λ��
    GrabNum = currData[3];     // ��ץȡ����
    HearBeat = currData[9];    // �����ź�
    //Fault = currData[11];
    Pause = currData[11];      // ��ͣ״̬
    TruckHave = currData[12];  // �Ƿ��п���
    GrabTotal = currData[13];  // ��ץȡ����
    UnloadPermit = currData[14]; // ����ж��
    CarMoveCmd = currData[15];   // �����ƶ�ָ��
    
    iState = 2; // �����ڲ�״̬

    // ״̬��� (�Զ�����������һЩ�����Լ��)
    if (currData[0] == 0 || currData[0] > 6 || currData[0] != CraneNo) // ���PLC�����ж������� (currData[0]) �Ƿ���ȷ
    {
        LOG(ERROR) << "ж�����ı�Ų���ȷ";
        closesocket(client); // �����ˣ��رպ�ɨ���ǵ�����
        return -2; // ���ش������
    }          

    // ״̬���2�����ж����״̬
    if (CraneState > 2)
    {
        LOG(ERROR) << "ж������״̬����ȷ";
        closesocket(client);
        return -3;
    }

    // ״̬���3�����ץ��λ������
    if (HopPos > 3)
    {
        LOG(ERROR) << "ж������ץ��λ�ò���ȷ";
        closesocket(client);
        return -4;
    }

    // ״̬���4�����ץ��λ������
    if (HopPos < 2)
    {
        LOG(INFO) << "ж������ץ��λ�ò��ڹ���λ";
        closesocket(client);
        return -5;
    }

    return 1;  // ���м��ͨ��,���سɹ�����
}

// ���� Crane ���һ����Ա���������� ForkliftPause���������û�з���ֵ (void)
void Crane::ForkliftPause()
{
    // 1. ״̬��� (���ж���������ض�״̬���Ͳ���Ҫ���泵)
    // ֻ�е� �ڲ�״̬ iState �� 2 ���� ж����״̬ CraneState Ҳ�� 2 ��ʱ�򣬲ż�����顣
    if (iState != 2 || CraneState != 2)
    {
        Pause = 0; // ����Ҫ��ͣ
        return;    // ֱ�ӽ���������
    }

    // 2. ����RFID��ȡ����̬�� (׼���úͲ泵RFID��ȡ����˵�����Ĺ���)��һ���� UHFReader18.dll ���ļ���
    HINSTANCE UHFReaderDll = LoadLibrary(L"UHFReader18.dll"); // ���ع��߿�"UHFReader18.dll" 
    if (UHFReaderDll) // ������߿���سɹ�
    {
        // 3. ��ʼ������������RFID��ȡ��
        long frmcomportindex = 0;
        unsigned char* EPC = new unsigned char[5000] { 0 }; // ����һ����װ5000���ַ��ġ��װ塱(�ڴ�ռ�)������д�Ӳ泵������RFID��ǩ��Ϣ(EPC)�������ȰѰװ���ɾ�(��0)
        long Totallen = 0;  // �������������¼ʵ�ʴӲ泵RFID��������Ϣ�ж೤

        // 4. ��ȡ��̬�⺯��ָ��
        typedef long (WINAPI* Open)(int, const char*, unsigned char*, long*);
        Open OpenNetPort = (Open)GetProcAddress(UHFReaderDll, "OpenNetPort");
        
        typedef long (WINAPI* Inventory)(unsigned char*, unsigned char, unsigned char, 
                                       unsigned char, unsigned char*, long*, long*, long);
        Inventory Inventory_G2 = (Inventory)GetProcAddress(UHFReaderDll, "Inventory_G2");
        
        typedef long (WINAPI* Close)(long);
        Close CloseNetPort = (Close)GetProcAddress(UHFReaderDll, "CloseNetPort");

        // 5. ����RFID��ȡ��
        if (OpenNetPort)
        {
            unsigned char fComAdr = 0xFF;
            OpenNetPort(6000, ipRFID, &fComAdr, &frmcomportindex);
            if (frmcomportindex != 0)
            {
                // 6. ��ȡRFID��ǩ
                long CardNum = 0;
                Inventory_G2(&fComAdr, 0, 0, 0, EPC, &Totallen, &CardNum, frmcomportindex);
                
                // 7. �����ȡ���
                if (CardNum == 0)
                {
                    LOG(INFO) << "ж������Χû�в泵";
                    goto CleanForklift;
                }
                else if (CardNum == 1)
                {
                    if (Totallen > 16 || Totallen < 1)
                    {
                        LOG(ERROR) << "�泵�ı�ʶ����ȷ";
                        goto CleanForklift;
                    }
                    LOG(INFO) << "ж������Χ�в泵����ʶ��" << EPC;
                }
                else
                {
                    LOG(INFO) << "ж������Χ�ж����泵";
                    goto CleanForklift;
                }
            }
            else
            {
                LOG(ERROR) << "���Ӳ泵RFID�豸ʧ��";
                goto CleanForklift;
            }

            // 8. �Ƚ�RFID��ǩ�Ƿ����仯
            for (int i = 0; i < Totallen; ++i)
            {
                if (forkliftRFID[i] != EPC[i])
                {
                    // 9. ��ѯ���ݿ��ȡ�泵��Ϣ
                    char c_Buff[35]{ 0 };
                    for (int i_RFID = 0; i_RFID < Totallen; ++i_RFID)
                        sprintf_s(c_Buff + i_RFID * 2, sizeof(c_Buff) - i_RFID * 2, "%02X", EPC[i_RFID]);
                    
                    int CraneID = (int)CraneNo;
                    LOG(INFO) << "ж������ţ�" << CraneID << "���泵��ʶΪ��" << c_Buff;
                    
                    // 10. ���ݿ����
                    _ConnectionPtr conn = DB.GetCon();
                    try
                    {
                        // �����洢��������
                        _CommandPtr cmmdmy1;
                        cmmdmy1.CreateInstance(__uuidof(Command));

                        // ���ô洢���̲���
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

                        // ִ�д洢���̡����ô洢�������ƺ�ִ������
                        cmmdmy1->CommandText = _bstr_t("CheckTruckNo");
                        cmmdmy1->ActiveConnection = conn;
                        cmmdmy1->CommandType = adCmdStoredProc;
                        
                        // 11. ������
                        int iPause = cmmdmy1->Parameters->GetItem("@GrabTotal")->GetValue();
                        if (iPause == -1)
                        {
                            Pause = 1;  // ��Ҫ��ͣ
                            ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
                            ::memcpy(forkliftRFID, EPC, Totallen);
                        }
                        else
                        {
                            Pause = 0;  // ����Ҫ��ͣ
                            ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
                        }
                    }
                    catch (...)
                    {
                        LOG(ERROR) << "�����ݿ��ж�ȡ�泵��Ϣ����:" << c_Buff;
                    }
                    DB.ReleaseCon(conn);
                    break;
                }
            }
        }
        else
        {
            LOG(ERROR) << "�޷�����RFIDʶ����";
            Pause = 0;
            ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
        }
        
        // 12. ������Դ
        delete[] EPC;
        FreeLibrary(UHFReaderDll);
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
            LOG(ERROR) << "?????????????????!";
            return -19;
        }

        if (TruckHave == 1)
        {
            
            LOG(INFO) << "??????" << dTruckHead << "????��???" << dTruckTail << "????��?????" << dLastHead << "????��?��???" << dLastTail ;
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
                            LOG(ERROR) << "???????????????";
                            if (GrabTotal <= 0)
                                iRes = -20;
                            goto  CleanRFID;
                        }
                        else if (CardNum == 1)
                        {
                            if (Totallen > 16 || Totallen < 1)
                            {
                                LOG(ERROR) << "???????????????";
                                if (GrabTotal <= 0)
                                    iRes = -21;
                                goto  CleanRFID;
                            }
                            LOG(INFO) << "???????????" << EPC;
                        }
                        else
                        {
                            LOG(ERROR) << "????????��?????????????";
                            if (GrabTotal <= 0)
                                iRes = -22;
                            goto  CleanRFID;
                        }
                    }
                    else
                    {
                        LOG(ERROR) << "????RFID?��????";
                        if (GrabTotal <= 0)
                            iRes = -23;
                        goto  CleanRFID;
                    }
                    
                    for (int i = 0; i < Totallen; ++i)
                    {
                        if (truckRFID[i] != EPC[i] || GrabTotal <= 0)
                        {
                            ::memset(truckLocation, 0, sizeof(truckLocation));
                            //?????????????
                            //????????��??????????????????????��?????????��?????��??truckLocation??
                            char c_Buff[35]{ 0 };
                            for (int i_RFID = 0; i_RFID < Totallen; ++i_RFID)
                                sprintf_s(c_Buff + i_RFID * 2, sizeof(c_Buff) - i_RFID * 2, "%02X", EPC[i_RFID]);
                            int CraneID =(int) CraneNo;
                            LOG(INFO) <<"��?????????"<< CraneID << "????????????" << c_Buff ;
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

                                cmmdmy1->CommandText = _bstr_t("CheckTruckNo");//?��??????
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
                                LOG(ERROR) << "��??????????:" << c_Buff;
                                if (GrabTotal <= 0)
                                    iRes = -26;
                                
                            }
                            DB.ReleaseCon(conn);
                            //???????????��?????????

                            ::memset(truckRFID, 0, sizeof(truckRFID));
                            ::memcpy(truckRFID, EPC, Totallen);
                            break;
                        }
                    }
                }
                else
                {
                    LOG(ERROR) << "????RFID?????????";
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
                LOG(ERROR) << "????RFID??????";
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
                    ///???????????��???????????????
                    
                    int CraneID = (int)CraneNo;
                    LOG(INFO) << "��?????????" << CraneID << "?????????????";
                    _ConnectionPtr conn = DB.GetCon();
                    try
                    {
                        _CommandPtr cmmdmy1;
                        cmmdmy1.CreateInstance(__uuidof(Command));;
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@CraneID"), adInteger, adParamInput, 4, CraneID));

                        cmmdmy1->CommandText = _bstr_t("CleanTruckNo");//?��??????
                        cmmdmy1->ActiveConnection = conn;
                        cmmdmy1->CommandType = adCmdStoredProc;
                        cmmdmy1->Execute(NULL, NULL, adCmdStoredProc);
                        
                    }
                    catch (...)
                    {
                        LOG(ERROR) << "?????��????????????";
                        iRes = -26;

                    }
                    DB.ReleaseCon(conn);
                    ::memset(truckLocation, 0, sizeof(truckLocation));
                    ::memset(truckRFID, 0, sizeof(truckRFID));//?????????
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
                    LOG(ERROR) << "???????????";
                    return -31;
                }
            }
            else
            {
                LOG(ERROR) << "??��???????��???????";
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
        result[3] = TruckHave;//???��????????��?????0???????1???��?
        result[4] = GrabTotal;//??????
        result[5] = UnloadPermit;//????????��??
        result[6] = CarMoveCmd;//??????????

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
                LOG(INFO) << "??��??PLC?????????PLC?????????????!";
            else
                LOG(ERROR) << "??��??PLC?????????PLC?????????!";
            return -41;
        }


        iState = 5;
        return 40;
    }
    else 
    {
        LOG(ERROR) << "????????";
        return -42;
    }
}

int Crane::ConnectScan()
{
    int iRes = -1; // Ĭ�Ϸ���ʧ��

    // ����һ���������ӵġ������� (Socket)
    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) // �������ʧ��
    {
        LOG(ERROR) << "������������ʧ��!";
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

    // ����ɨ�������ͣ�ִ�в�ͬ�����Ӻͳ�ʼ������
    if (iScanType == 1) // SICK LMS511
    {
        // �������ӵ�֮ǰ���úõķ�������ַ�Ͷ˿�
        if (connect(client, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            LOG(ERROR) << "���� SICKLMS511 ɨ����ʧ��!";
            return iRes;
        }
    }
    else if (iScanType == 2) // XXWXTL302
    {
        // ��������
        if (connect(client, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            LOG(ERROR) << "���� XXWXTL302 ʧ��!";
            return iRes;
        }
        // ���ӳɹ�����Ҫ����һ���ض���ָ���������ĳ�ֹ���ģʽ
        char c_RecBuff[50]{ 0 }; // ׼�����ջظ��ġ����ӡ�
        unsigned char c_SendMode0[] = { 0xFE,0x00,0x00,0x01,0x00,0x78,0xF0 }; // Ҫ���͵�ָ������
        send(client, (const char*)c_SendMode0, sizeof(c_SendMode0), 0); // ����ָ��
        int i_RecLen = recv(client, c_RecBuff, sizeof(c_RecBuff), 0); // �ȴ������ջظ�

        // ���ظ��Ƿ����Ԥ��
        if (i_RecLen == 7 && c_RecBuff[1] == 1 && ...) // ����ظ���ȷ
        {
            if (c_RecBuff[4] == 0) // ���һظ��е�ĳ����־λ��0
            {
                cout << "ɨ���ǳɹ��л����������״̬��" << endl; // �ɹ���
            }
            else // �ظ��еı�־λ����
            {
                LOG(ERROR) << "ɨ�����л�״̬ʧ�ܡ�";
                closesocket(client); // �ر�����
                return iRes;
            }
        }
        else // �ظ������ݲ��Ի򳤶Ȳ���
        {
            LOG(ERROR) << "���ӵ� XT-L302 ����Ч��Ӧ!";
            closesocket(client); // �ر�����
            return iRes;
        }
    }
    else // δ֪��ɨ��������
        return iRes;

    return 0; // ���в��趼�ɹ�������0
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
    //  '''???????'''
    double ang = 0.0;
    double anglestep = 0.0;
    // '''????????'''
    int datanum = 0;
    
    if (send(client, c_Sendscan, 17, 0) < 0)
    {
        LOG(ERROR) << "??SICKLMS511????????????!";
        closesocket(client);
        iRes = ConnectScan();
        goto CleanSICKLMS511;
    }
    i_RecLen = recv(client, c_RecBuff, MAX_BUFF_SIZE, 0);
    if (i_RecLen <= 0 || i_RecLen >= MAX_BUFF_SIZE - 1)
    { 
        LOG(ERROR) << "SICKLMS511??????????????!";
        Fault = 0;//????????0???????1??????
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
    //  '''???????'''
    ang = stoi(data[24], nullptr, 16);
    anglestep = ang / 10000.0;
    // '''????????'''
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

        if (y > 2000.0 + (HopPos - 1) * 7500.0 && y < 2000.0 + HopPos * 7500.0) //?��??��
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
        //???????????
        LOG(ERROR) << "?????????????????!";
        iRes = -15;
        goto CleanSICKLMS511;

    }

    if (iCountY > 9)
    {
        LOG(ERROR) << "??????��??????????!";
        iRes = -16;
        goto CleanSICKLMS511;
    }

    if (dTruckHead > 2000.0)//?��????????????????��??
    {
        if (dTruckTail > 29500.0)
        {
            LOG(ERROR) << "???????????????????????!";
            iRes = -17;
            goto CleanSICKLMS511;
        }

        if (dTruckTail - dTruckHead > 10000 && dTruckTail - dTruckHead < 18000)
            //?��???��?

            TruckHave = 1;
        else 
        {
            LOG(ERROR) << "?????????????????!";
            iRes = -18;
            goto CleanSICKLMS511;
        }

    }
    else
    {
        if (dTruckTail < 21000.0f)
        {
            //?��?????
            TruckHave = 0;
            goto CleanSICKLMS511;
        }
        LOG(ERROR) << "?????????????????!";
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
        LOG(ERROR) << "??????????XT-L302????????????!";
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
            LOG(ERROR) << "????????XT-L302??????????????!";
            Fault = 0;//????????0???????1??????
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

                    if (y > 2000.0 + (HopPos - 1) * 7500.0 && y < 2000.0 + HopPos * 7500.0) //?��??��
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
        //???????????
        LOG(ERROR) << "?????????????????!";
        iRes = -15;
        goto ReturnXXWXTL302;

    }

    if (iCountY > 9)
    {
        LOG(ERROR) << "??????��??????????!";
        iRes = -16;
        goto ReturnXXWXTL302;
    }


    if (dTruckHead > 2000.0)//?��????????????????��??
    {
        if (dTruckTail > 29500.0)
        {
            LOG(ERROR) << "???????????????????????!";
            iRes = -17;
            goto ReturnXXWXTL302;
        }

        if (dTruckTail - dTruckHead > 9000 && dTruckTail - dTruckHead < 18000)
            //?��???��?

            TruckHave = 1;
        else
        {
            LOG(ERROR) << "?????????????????!";
            iRes = -18;
            goto ReturnXXWXTL302;
        }

    }
    else
    {
        if (dTruckTail < 21000.0f)
        {
            //?��?????
            TruckHave = 0;
            goto ReturnXXWXTL302;
        }
        LOG(ERROR) << "?????????????????!";
        iRes = -18;
    }

ReturnXXWXTL302:
    delete[] c_Data;
    return iRes;
}





