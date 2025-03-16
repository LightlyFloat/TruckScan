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
    cout << "开始新一轮的扫描" << endl;
    //CraneNo = 0;//卸船机号
    CraneState = 0;//卸船机状态：0、无作业；1、卸船；2、装船
    HopPos = 0;//料斗位置
    GrabNum = 0;//已放料数量

    HearBeat = 0;//心跳信号
    Fault = 1;//故障
    Pause = 0;//暂停
    TruckHave = 0;//当前卸船机是否有卡车。0、无车；1、有车
    UnloadPermit = 0;//卡车允许卸料
    CarMoveCmd = 0;//卡车移动指令
    GrabTotal = 0;//应放斗数
    dTruckHead = 0.0;
    dTruckTail = 0.0;
    iCountY = 0;
    iState = 1;
    return 0;
}

int Crane::ReadPLC() 
{
    unsigned char currData[21]{ 0 };//一级内的数据
    if(plc.Connected())
        plc.DBRead(1003, 0, 21, currData);
    else
    {        
        if (plc.ConnectTo(ipPLC, 0, 1) == 0)
            LOG(INFO) << "在读取PLC数据时，与PLC重新建立连接成功!" ;
        else 
            LOG(ERROR) << "在读取PLC数据时，与PLC通讯出现错误!";
        return -1;
    }

    //CraneNo = currData[0];//卸船机号
    CraneState = currData[1];//卸船机状态：0、无作业；1、卸船；2、装船
    HopPos = currData[2];//料斗位置
    GrabNum = currData[3];//已放料数量
    HearBeat = currData[9] == 255 ? 1 : currData[9] + 1;//心跳信号
    //Fault = currData[11];//故障
    Pause = currData[11];//暂停
    TruckHave = currData[12];//当前卸船机是否有卡车。0、无车；1、有车
    GrabTotal = currData[13];//应放斗数
    UnloadPermit = currData[14];//已放料数量
    CarMoveCmd = currData[15];//已放料数量
    
    iState = 2;

    if (currData[0] == 0 || currData[0] > 6 || currData[0] != CraneNo)
    {          
        LOG(ERROR) << "卸船机的编号不正确";
        closesocket(client);
        return -2;
    }          
    if (CraneState > 2)
    {
        LOG(ERROR) << "卸船机的状态不正确";
        closesocket(client);
        return -3;
    }
    if (HopPos > 3)
    {
        LOG(ERROR) << "卸船机的料斗位置不正确";
        closesocket(client);
        return -4;
    }
    if (HopPos < 2)
    {
        LOG(INFO) << "卸船机的料斗位置不在工作位";
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
                    LOG(INFO) << "卸船机范围内没有车辆。";
                    goto  CleanForklift;
                }
                else if (CardNum == 1)
                {
                    if (Totallen > 16 || Totallen < 1)
                    {
                        LOG(ERROR) << "铲车的标识不正确。";
                        goto  CleanForklift;
                    }
                    LOG(INFO) << "卸船机范围内有车辆。" << EPC;
                }
                else
                {
                    LOG(INFO) << "卸船机范围内有多辆车。";
                    goto  CleanForklift;
                }
            }
            else
            {
                LOG(ERROR) << "连接铲车RFID设备失败。";
                goto  CleanForklift;
            }


            for (int i = 0; i < Totallen; ++i)
            {
                if (forkliftRFID[i] != EPC[i])
                {
                    //铲车已经换车了
                    //从数据库中查询对应铲车的信息
                    char c_Buff[35]{ 0 };
                    for (int i_RFID = 0; i_RFID < Totallen; ++i_RFID)
                        sprintf_s(c_Buff + i_RFID * 2, sizeof(c_Buff) - i_RFID * 2, "%02X", EPC[i_RFID]);
                    int CraneID = (int)CraneNo;
                    LOG(INFO) << "卸船机号为：" << CraneID << "，铲车标识为：" << c_Buff;
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

                        cmmdmy1->CommandText = _bstr_t("CheckTruckNo");//存储过程名
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
                        LOG(ERROR) << "从数据库中读铲车信息出错:" << c_Buff;
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
            LOG(ERROR) << "无法打开RFID识别库。";
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
        LOG(ERROR) << "载入RFID出错。";
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
            LOG(ERROR) << "卡车扫描雷达类型错误!";
            return -19;
        }

        if (TruckHave == 1)
        {
            
            LOG(INFO) << "车头在：" << dTruckHead << "，车尾在：" << dTruckTail << "；上次车头在：" << dLastHead << "，上次车尾在：" << dLastTail ;
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
                            LOG(ERROR) << "没有识别到卡车的标识。";
                            if (GrabTotal <= 0)
                                iRes = -20;
                            goto  CleanRFID;
                        }
                        else if (CardNum == 1)
                        {
                            if (Totallen > 16 || Totallen < 1)
                            {
                                LOG(ERROR) << "卡车的标识不正确。";
                                if (GrabTotal <= 0)
                                    iRes = -21;
                                goto  CleanRFID;
                            }
                            LOG(INFO) << "识别到卡车身份。" << EPC;
                        }
                        else
                        {
                            LOG(ERROR) << "卡车的周围有其它的车辆。";
                            if (GrabTotal <= 0)
                                iRes = -22;
                            goto  CleanRFID;
                        }
                    }
                    else
                    {
                        LOG(ERROR) << "连接RFID设备失败。";
                        if (GrabTotal <= 0)
                            iRes = -23;
                        goto  CleanRFID;
                    }
                    
                    for (int i = 0; i < Totallen; ++i)
                    {
                        if (truckRFID[i] != EPC[i] || GrabTotal <= 0)
                        {
                            ::memset(truckLocation, 0, sizeof(truckLocation));
                            //卡车已经换车了
                            //从数据库中查询对应卡车的信息以及每一抓的位置信息，将位置信息写入到truckLocation中
                            char c_Buff[35]{ 0 };
                            for (int i_RFID = 0; i_RFID < Totallen; ++i_RFID)
                                sprintf_s(c_Buff + i_RFID * 2, sizeof(c_Buff) - i_RFID * 2, "%02X", EPC[i_RFID]);
                            int CraneID =(int) CraneNo;
                            LOG(INFO) <<"卸船机号为："<< CraneID << "，卡车标识为：" << c_Buff ;
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

                                cmmdmy1->CommandText = _bstr_t("CheckTruckNo");//存储过程名
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
                                LOG(ERROR) << "写入数据库出错:" << c_Buff;
                                if (GrabTotal <= 0)
                                    iRes = -26;
                                
                            }
                            DB.ReleaseCon(conn);
                            //将卡车的车号写入到数据库中

                            ::memset(truckRFID, 0, sizeof(truckRFID));
                            ::memcpy(truckRFID, EPC, Totallen);
                            break;
                        }
                    }
                }
                else
                {
                    LOG(ERROR) << "载入RFID识别库出错。";
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
                LOG(ERROR) << "载入RFID出错。";
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
                    ///更新数据库将此卸船机的卡车信息清空
                    
                    int CraneID = (int)CraneNo;
                    LOG(INFO) << "卸船机号为：" << CraneID << "，卡车已经离开。";
                    _ConnectionPtr conn = DB.GetCon();
                    try
                    {
                        _CommandPtr cmmdmy1;
                        cmmdmy1.CreateInstance(__uuidof(Command));;
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@CraneID"), adInteger, adParamInput, 4, CraneID));

                        cmmdmy1->CommandText = _bstr_t("CleanTruckNo");//存储过程名
                        cmmdmy1->ActiveConnection = conn;
                        cmmdmy1->CommandType = adCmdStoredProc;
                        cmmdmy1->Execute(NULL, NULL, adCmdStoredProc);
                        
                    }
                    catch (...)
                    {
                        LOG(ERROR) << "无卡车写入数据库出错。";
                        iRes = -26;

                    }
                    DB.ReleaseCon(conn);
                    ::memset(truckLocation, 0, sizeof(truckLocation));
                    ::memset(truckRFID, 0, sizeof(truckRFID));//清空卡车信息
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
                    LOG(ERROR) << "已放数量过大。";
                    return -31;
                }
            }
            else
            {
                LOG(ERROR) << "没有卡车装料的位置信息。";
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
        result[3] = TruckHave;//当前卸船机是否有卡车。0、无车；1、有车
        result[4] = GrabTotal;//应放斗数
        result[5] = UnloadPermit;//卡车允许卸料
        result[6] = CarMoveCmd;//卡车移动指令

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
                LOG(INFO) << "在写入PLC数据时，与PLC重新建立连接成功!";
            else
                LOG(ERROR) << "在写入PLC数据时，与PLC通讯出现错误!";
            return -41;
        }


        iState = 5;
        return 40;
    }
    else 
    {
        LOG(ERROR) << "系统有问题";
        return -42;
    }
}

int Crane::ConnectScan()
{
    int iRes = -1;

    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET)
    {
        LOG(ERROR) << "创建WinSocket失败!";
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
            LOG(ERROR) << "连接SICKLMS511扫描仪失败!";
            return iRes;
        }
    }
    else if (iScanType == 2)
    {
        if (connect(client, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            LOG(ERROR) << "连接兴玄物联XT-L302失败!";
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
                cout << "扫描仪成功已经切换到测量状态。" << endl;
            }
            else
            {
                LOG(ERROR) << "扫描仪切换到测量状态失败。";
                closesocket(client);
                return iRes;
            }
        }
        else
        {
            LOG(ERROR) << "兴玄物联XT-L302无法正常工作!";
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
    //  '''角度分辨率'''
    double ang = 0.0;
    double anglestep = 0.0;
    // '''数据总量'''
    int datanum = 0;
    
    if (send(client, c_Sendscan, 17, 0) < 0)
    {
        LOG(ERROR) << "向SICKLMS511发送查询指令失败!";
        closesocket(client);
        iRes = ConnectScan();
        goto CleanSICKLMS511;
    }
    i_RecLen = recv(client, c_RecBuff, MAX_BUFF_SIZE, 0);
    if (i_RecLen <= 0 || i_RecLen >= MAX_BUFF_SIZE - 1)
    { 
        LOG(ERROR) << "SICKLMS511返回的数据不正常!";
        Fault = 0;//有无故障。0：故障，1：正常
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
    //  '''角度分辨率'''
    ang = stoi(data[24], nullptr, 16);
    anglestep = ang / 10000.0;
    // '''数据总量'''
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

        if (y > 2000.0 + (HopPos - 1) * 7500.0 && y < 2000.0 + HopPos * 7500.0) //判断头尾
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
        //扫描仪发生了飘
        LOG(ERROR) << "扫描仪出现了数据误差!";
        iRes = -15;
        goto CleanSICKLMS511;

    }

    if (iCountY > 9)
    {
        LOG(ERROR) << "卡车周围有其它车辆!";
        iRes = -16;
        goto CleanSICKLMS511;
    }

    if (dTruckHead > 2000.0)//判断车辆是不是在扫描的范围内
    {
        if (dTruckTail > 29500.0)
        {
            LOG(ERROR) << "卡车还没有完全进入到扫描区域!";
            iRes = -17;
            goto CleanSICKLMS511;
        }

        if (dTruckTail - dTruckHead > 10000 && dTruckTail - dTruckHead < 18000)
            //判断为有车

            TruckHave = 1;
        else 
        {
            LOG(ERROR) << "卡车前后有其它车辆!";
            iRes = -18;
            goto CleanSICKLMS511;
        }

    }
    else
    {
        if (dTruckTail < 21000.0f)
        {
            //判断为无车
            TruckHave = 0;
            goto CleanSICKLMS511;
        }
        LOG(ERROR) << "卡车前后有其它车辆!";
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
        LOG(ERROR) << "向兴玄物联XT-L302发送查询指令失败!";
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
            LOG(ERROR) << "兴玄物联XT-L302返回的数据不正常!";
            Fault = 0;//有无故障。0：故障，1：正常
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

                    if (y > 2000.0 + (HopPos - 1) * 7500.0 && y < 2000.0 + HopPos * 7500.0) //判断头尾
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
        //扫描仪发生了飘
        LOG(ERROR) << "扫描仪出现了数据误差!";
        iRes = -15;
        goto ReturnXXWXTL302;

    }

    if (iCountY > 9)
    {
        LOG(ERROR) << "卡车周围有其它车辆!";
        iRes = -16;
        goto ReturnXXWXTL302;
    }


    if (dTruckHead > 2000.0)//判断车辆是不是在扫描的范围内
    {
        if (dTruckTail > 29500.0)
        {
            LOG(ERROR) << "卡车还没有完全进入到扫描区域!";
            iRes = -17;
            goto ReturnXXWXTL302;
        }

        if (dTruckTail - dTruckHead > 9000 && dTruckTail - dTruckHead < 18000)
            //判断为有车

            TruckHave = 1;
        else
        {
            LOG(ERROR) << "卡车前后有其它车辆!";
            iRes = -18;
            goto ReturnXXWXTL302;
        }

    }
    else
    {
        if (dTruckTail < 21000.0f)
        {
            //判断为无车
            TruckHave = 0;
            goto ReturnXXWXTL302;
        }
        LOG(ERROR) << "卡车前后有其它车辆!";
        iRes = -18;
    }

ReturnXXWXTL302:
    delete[] c_Data;
    return iRes;
}





