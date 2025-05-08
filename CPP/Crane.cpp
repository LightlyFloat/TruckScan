// --- 预处理指令 (Preprocessor Directives) ---

#define MAX_BUFF_SIZE 16384  // 定义最大缓冲区大小
#define MAX_REC_NUM 10      // 定义最大记录数
#define _USE_MATH_DEFINES   // 启用 <cmath> 中数学常量定义
#include <WS2tcpip.h>      // 网络通信相关
#include <cmath>           // 数学函数，包含 C++ 标准数学库头文件，提供数学运算功能
#include <vector>          // STL向量容器，提供动态数组功能
#include "Crane.h"         // 卸船机类头文件
#include "easylogging++.h" // 日志系统


using namespace std;//命名空间，标准库
using namespace el;//命名空间，日志系统
#pragma comment(lib,"ws2_32.lib")//链接库

// 定义了当一台新的 `Crane` (卸船机控制程序实例) 被创建时，需要做哪些初始化工作。记下各种设备的网络地址 (IP 地址)、这台卸船机自己的编号、用的是哪种扫描仪等等，并尝试和 PLC、扫描仪建立第一次连接。
Crane::Crane(const char* Scan, const char* RFID, const char* PLC, const char* Forklift,// 参数: Scan, RFID, PLC, Forklift 的IP地址 (C风格字符串), 扫描仪类型, 卸船机编号, 工作范围, 数据库连接池对象引用
             int ScanType, unsigned char No, int Range, CDBConnetPool& DBPool) :DB(DBPool)
{
    // 初始化IP地址 (记录下其他设备的网络地址)
    strcpy_s(ipRFID, RFID);        // 将传入的RFID读取器IP地址复制到类的成员变量 ipRFID 中
    strcpy_s(ipPLC, PLC);          // 将传入的PLC控制器IP地址复制到类的成员变量 ipPLC 中
    strcpy_s(ipForklift, Forklift); // 将传入的叉车相关设备IP地址复制到类的成员变量 ipForklift 中

    iScanType = ScanType;          // 记下用的是哪种类型的扫描仪，将传入的扫描仪类型赋值给成员变量 iScanType
    lastTime = time(NULL) - 5;     // 初始化一个时间戳，获取当前时间戳，减去5秒，赋值给成员变量 lastTime
    CraneNo = No;                  // 记下这台卸船机自己的编号，将传入的卸船机编号赋值给成员变量 CraneNo
    iRange = Range;                // 记下工作范围相关的设置，将传入的工作范围参数赋值给成员变量 iRange

    // 初始化各种缓冲区 (准备好一些空白的“记录本”或“内存空间”)
    ::memset(truckRFID, 0, sizeof(truckRFID));       // 清空记录卡车ID的地方
    ::memset(truckLocation, 0, sizeof(truckLocation)); // 清空记录卡车位置的地方
    ::memset(forkliftRFID, 0, sizeof(forkliftRFID)); // 清空记录叉车ID的地方

    // 配置扫描仪网络连接 (根据扫描仪类型，设置好和它“说话”的方式)
    if (iScanType == 1) {  // 如果是 SICK LMS511 扫描仪
        serverAddr.sin_port = htons(2111); // 设置端口号为 2111
    }
    else if (iScanType == 2) {  // 如果是 XXWXTL302 扫描仪
        serverAddr.sin_port = htons(65530); // 设置端口号为 65530
    }

    // 连接PLC和扫描仪 (尝试和PLC、扫描仪建立联系)
    plc.ConnectTo(ipPLC, 0, 1); // 尝试连接PLC
    iState = ConnectScan();      // 调用本类的 ConnectScan 方法尝试连接扫描仪，并记录保存连接状态，将返回的连接状态在状态变量 `iState` 中。
}

//当这个卸船机控制程序实例不再需要，准备结束时，这段代码负责清理工作。
Crane::~Crane()
{
    if (plc.Connected())
        plc.Disconnect();    // 如果还连着PLC，则调用 plc 对象的 Disconnect 方法断开与PLC的连接
    closesocket(client);     // 调用 closesocket 函数关闭与扫描仪连接的套接字 (client 是套接字句柄)
}

// 在卸船机开始新一轮工作前，把所有工作状态标志&状态参数(比如当前在做什么、抓了多少次、有没有故障等)都设回初始值
int Crane::Init()
{
    // 初始化所有状态参数 (把所有工作状态标志都设回初始值)
    CraneState = 0;    // 卸船机状态：比如 0 代表空闲
    HopPos = 0;        // 抓斗位置
    GrabNum = 0;       // 已抓取次数清零
    HearBeat = 0;      // 心跳信号 (用来确认设备是否在线)
    Fault = 1;         // 故障状态 (假设一开始可能有问题，需要检查)
    Pause = 0;         // 暂停状态 (默认不暂停)
    TruckHave = 0;     // 默认没有卡车
    UnloadPermit = 0;  // 默认不允许卸货
    CarMoveCmd = 0;    // 默认没有车辆移动指令
    GrabTotal = 0;     // 总抓取次数清零
    dTruckHead = 0.0;  // 卡车头位置清零
    dTruckTail = 0.0;  // 卡车尾位置清零
    iCountY = 0;       // 一个计数器清零
    iState = 1;        // 设备内部状态标志
    return 0;          // 返回0表示初始化成功
}

//从 PLC 读取数据的成员函数。读取 PLC 知道的关于物理卸船机的最新状态信息，并做一些基本检查，确保信息是合理的。
int Crane::ReadPLC()// 返回一个int类型值 (表示读取结果状态)
{
    unsigned char currData[21]{ 0 };  // 定义一个局部unsigned char数组currData，大小为21字节，并初始化所有元素为0。这个数组用作临时缓冲区，存储从PLC读取的原始数据。准备一个“篮子”用来装从PLC读来的数据

    // 读取PLC数据
    if(plc.Connected()) { // 如果plc 对象和PLC连着
        plc.DBRead(1003, 0, 21, currData); // 就去读PLC里地址1003开始的21个字节数据
    }
    else { // 如果没连上
        // 尝试重新连接PLC
        if (plc.ConnectTo(ipPLC, 0, 1) == 0) // ConnectTo成功返回0
            LOG(INFO) << "PLC重新连接成功"; // 记录日志：连接成功
        else
            LOG(ERROR) << "PLC通信错误"; // 记录日志：连接失败
        return -1; // 返回错误代码
    }

    //CraneNo = currData[0];//ж??????
    // 解析PLC数据
    CraneState = currData[1];  // 卸船机状态
    HopPos = currData[2];      // 抓斗位置
    GrabNum = currData[3];     // 已抓取次数
    HearBeat = currData[9];    // 心跳信号
    //Fault = currData[11];
    Pause = currData[11];      // 暂停状态
    TruckHave = currData[12];  // 是否有卡车
    GrabTotal = currData[13];  // 总抓取次数
    UnloadPermit = currData[14]; // 允许卸货
    CarMoveCmd = currData[15];   // 车辆移动指令
    
    iState = 2; // 更新内部状态

    // 状态检查 (对读到的数据做一些合理性检查)
    if (currData[0] == 0 || currData[0] > 6 || currData[0] != CraneNo) // 检查PLC报告的卸船机编号 (currData[0]) 是否正确
    {
        LOG(ERROR) << "卸船机的编号不正确";
        closesocket(client); // 出错了，关闭和扫描仪的连接
        return -2; // 返回错误代码
    }          

    // 状态检查2：检查卸船机状态
    if (CraneState > 2)
    {
        LOG(ERROR) << "卸船机的状态不正确";
        closesocket(client);
        return -3;
    }

    // 状态检查3：检查抓斗位置上限
    if (HopPos > 3)
    {
        LOG(ERROR) << "卸船机的抓斗位置不正确";
        closesocket(client);
        return -4;
    }

    // 状态检查4：检查抓斗位置下限
    if (HopPos < 2)
    {
        LOG(INFO) << "卸船机的抓斗位置不在工作位";
        closesocket(client);
        return -5;
    }

    return 1;  // 所有检查通过,返回成功代码
}

// 定义 Crane 类的一个成员函数，名叫 ForkliftPause，这个函数没有返回值 (void)
void Crane::ForkliftPause()
{
    // 1. 状态检查 (如果卸船机不在特定状态，就不需要检查叉车)
    // 只有当 内部状态 iState 是 2 并且 卸船机状态 CraneState 也是 2 的时候，才继续检查。
    if (iState != 2 || CraneState != 2)
    {
        Pause = 0; // 不需要暂停
        return;    // 直接结束这个检查
    }

    // 2. 加载RFID读取器动态库 (准备好和叉车RFID读取器“说话”的工具)（一个叫 UHFReader18.dll 的文件）
    HINSTANCE UHFReaderDll = LoadLibrary(L"UHFReader18.dll"); // 加载工具库"UHFReader18.dll" 
    if (UHFReaderDll) // 如果工具库加载成功
    {
        // 3. 初始化变量，用于RFID读取器
        long frmcomportindex = 0;
        unsigned char* EPC = new unsigned char[5000] { 0 }; // 申请一块能装5000个字符的“白板”(内存空间)，用来写从叉车读到的RFID标签信息(EPC)，并且先把白板擦干净(填0)
        long Totallen = 0;  // 这个变量用来记录实际从叉车RFID读到的信息有多长

        // 4. 获取动态库函数指针
        typedef long (WINAPI* Open)(int, const char*, unsigned char*, long*);
        Open OpenNetPort = (Open)GetProcAddress(UHFReaderDll, "OpenNetPort");
        
        typedef long (WINAPI* Inventory)(unsigned char*, unsigned char, unsigned char, 
                                       unsigned char, unsigned char*, long*, long*, long);
        Inventory Inventory_G2 = (Inventory)GetProcAddress(UHFReaderDll, "Inventory_G2");
        
        typedef long (WINAPI* Close)(long);
        Close CloseNetPort = (Close)GetProcAddress(UHFReaderDll, "CloseNetPort");

        // 5. 连接RFID读取器
        if (OpenNetPort)
        {
            unsigned char fComAdr = 0xFF;
            OpenNetPort(6000, ipRFID, &fComAdr, &frmcomportindex);
            if (frmcomportindex != 0)
            {
                // 6. 读取RFID标签
                long CardNum = 0;
                Inventory_G2(&fComAdr, 0, 0, 0, EPC, &Totallen, &CardNum, frmcomportindex);
                
                // 7. 处理读取结果
                if (CardNum == 0)
                {
                    LOG(INFO) << "卸船机周围没有叉车";
                    goto CleanForklift;
                }
                else if (CardNum == 1)
                {
                    if (Totallen > 16 || Totallen < 1)
                    {
                        LOG(ERROR) << "叉车的标识不正确";
                        goto CleanForklift;
                    }
                    LOG(INFO) << "卸船机周围有叉车，标识：" << EPC;
                }
                else
                {
                    LOG(INFO) << "卸船机周围有多辆叉车";
                    goto CleanForklift;
                }
            }
            else
            {
                LOG(ERROR) << "连接叉车RFID设备失败";
                goto CleanForklift;
            }

            // 8. 比较RFID标签是否发生变化
            for (int i = 0; i < Totallen; ++i)
            {
                if (forkliftRFID[i] != EPC[i])
                {
                    // 9. 查询数据库获取叉车信息
                    char c_Buff[35]{ 0 };
                    for (int i_RFID = 0; i_RFID < Totallen; ++i_RFID)
                        sprintf_s(c_Buff + i_RFID * 2, sizeof(c_Buff) - i_RFID * 2, "%02X", EPC[i_RFID]);
                    
                    int CraneID = (int)CraneNo;
                    LOG(INFO) << "卸船机编号：" << CraneID << "，叉车标识为：" << c_Buff;
                    
                    // 10. 数据库操作
                    _ConnectionPtr conn = DB.GetCon();
                    try
                    {
                        // 创建存储过程命令
                        _CommandPtr cmmdmy1;
                        cmmdmy1.CreateInstance(__uuidof(Command));

                        // 设置存储过程参数
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

                        // 执行存储过程、设置存储过程名称和执行类型
                        cmmdmy1->CommandText = _bstr_t("CheckTruckNo");
                        cmmdmy1->ActiveConnection = conn;
                        cmmdmy1->CommandType = adCmdStoredProc;
                        
                        // 11. 处理结果
                        int iPause = cmmdmy1->Parameters->GetItem("@GrabTotal")->GetValue();
                        if (iPause == -1)
                        {
                            Pause = 1;  // 需要暂停
                            ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
                            ::memcpy(forkliftRFID, EPC, Totallen);
                        }
                        else
                        {
                            Pause = 0;  // 不需要暂停
                            ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
                        }
                    }
                    catch (...)
                    {
                        LOG(ERROR) << "从数据库中读取叉车信息出错:" << c_Buff;
                    }
                    DB.ReleaseCon(conn);
                    break;
                }
            }
        }
        else
        {
            LOG(ERROR) << "无法加载RFID识别器";
            Pause = 0;
            ::memset(forkliftRFID, 0, sizeof(forkliftRFID));
        }
        
        // 12. 清理资源
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
            
            LOG(INFO) << "??????" << dTruckHead << "????β???" << dTruckTail << "????γ?????" << dLastHead << "????γ?β???" << dLastTail ;
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
                            LOG(ERROR) << "????????Χ?????????????";
                            if (GrabTotal <= 0)
                                iRes = -22;
                            goto  CleanRFID;
                        }
                    }
                    else
                    {
                        LOG(ERROR) << "????RFID?豸????";
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
                            //????????в??????????????????????λ?????????λ?????д??truckLocation??
                            char c_Buff[35]{ 0 };
                            for (int i_RFID = 0; i_RFID < Totallen; ++i_RFID)
                                sprintf_s(c_Buff + i_RFID * 2, sizeof(c_Buff) - i_RFID * 2, "%02X", EPC[i_RFID]);
                            int CraneID =(int) CraneNo;
                            LOG(INFO) <<"ж?????????"<< CraneID << "????????????" << c_Buff ;
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

                                cmmdmy1->CommandText = _bstr_t("CheckTruckNo");//?洢??????
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
                                LOG(ERROR) << "д??????????:" << c_Buff;
                                if (GrabTotal <= 0)
                                    iRes = -26;
                                
                            }
                            DB.ReleaseCon(conn);
                            //???????????д?????????

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
                    ///???????????ж???????????????
                    
                    int CraneID = (int)CraneNo;
                    LOG(INFO) << "ж?????????" << CraneID << "?????????????";
                    _ConnectionPtr conn = DB.GetCon();
                    try
                    {
                        _CommandPtr cmmdmy1;
                        cmmdmy1.CreateInstance(__uuidof(Command));;
                        cmmdmy1->Parameters->Append(cmmdmy1->CreateParameter(_bstr_t("@CraneID"), adInteger, adParamInput, 4, CraneID));

                        cmmdmy1->CommandText = _bstr_t("CleanTruckNo");//?洢??????
                        cmmdmy1->ActiveConnection = conn;
                        cmmdmy1->CommandType = adCmdStoredProc;
                        cmmdmy1->Execute(NULL, NULL, adCmdStoredProc);
                        
                    }
                    catch (...)
                    {
                        LOG(ERROR) << "?????д????????????";
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
                LOG(ERROR) << "??п???????λ???????";
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
        result[3] = TruckHave;//???ж????????п?????0???????1???г?
        result[4] = GrabTotal;//??????
        result[5] = UnloadPermit;//????????ж??
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
                LOG(INFO) << "??д??PLC?????????PLC?????????????!";
            else
                LOG(ERROR) << "??д??PLC?????????PLC?????????!";
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
    int iRes = -1; // 默认返回失败

    // 创建一个网络连接的“插座” (Socket)
    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) // 如果创建失败
    {
        LOG(ERROR) << "创建网络连接失败!";
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

    // 根据扫描仪类型，执行不同的连接和初始化步骤
    if (iScanType == 1) // SICK LMS511
    {
        // 尝试连接到之前设置好的服务器地址和端口
        if (connect(client, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            LOG(ERROR) << "连接 SICKLMS511 扫描仪失败!";
            return iRes;
        }
    }
    else if (iScanType == 2) // XXWXTL302
    {
        // 尝试连接
        if (connect(client, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
        {
            LOG(ERROR) << "连接 XXWXTL302 失败!";
            return iRes;
        }
        // 连接成功后，需要发送一个特定的指令，让它进入某种工作模式
        char c_RecBuff[50]{ 0 }; // 准备接收回复的“篮子”
        unsigned char c_SendMode0[] = { 0xFE,0x00,0x00,0x01,0x00,0x78,0xF0 }; // 要发送的指令数据
        send(client, (const char*)c_SendMode0, sizeof(c_SendMode0), 0); // 发送指令
        int i_RecLen = recv(client, c_RecBuff, sizeof(c_RecBuff), 0); // 等待并接收回复

        // 检查回复是否符合预期
        if (i_RecLen == 7 && c_RecBuff[1] == 1 && ...) // 如果回复正确
        {
            if (c_RecBuff[4] == 0) // 并且回复中的某个标志位是0
            {
                cout << "扫描仪成功切换到连续输出状态。" << endl; // 成功了
            }
            else // 回复中的标志位不对
            {
                LOG(ERROR) << "扫描仪切换状态失败。";
                closesocket(client); // 关闭连接
                return iRes;
            }
        }
        else // 回复的数据不对或长度不对
        {
            LOG(ERROR) << "连接的 XT-L302 无有效响应!";
            closesocket(client); // 关闭连接
            return iRes;
        }
    }
    else // 未知的扫描仪类型
        return iRes;

    return 0; // 所有步骤都成功，返回0
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

        if (y > 2000.0 + (HopPos - 1) * 7500.0 && y < 2000.0 + HopPos * 7500.0) //?ж??β
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
        LOG(ERROR) << "??????Χ??????????!";
        iRes = -16;
        goto CleanSICKLMS511;
    }

    if (dTruckHead > 2000.0)//?ж????????????????Χ??
    {
        if (dTruckTail > 29500.0)
        {
            LOG(ERROR) << "???????????????????????!";
            iRes = -17;
            goto CleanSICKLMS511;
        }

        if (dTruckTail - dTruckHead > 10000 && dTruckTail - dTruckHead < 18000)
            //?ж???г?

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
            //?ж?????
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

                    if (y > 2000.0 + (HopPos - 1) * 7500.0 && y < 2000.0 + HopPos * 7500.0) //?ж??β
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
        LOG(ERROR) << "??????Χ??????????!";
        iRes = -16;
        goto ReturnXXWXTL302;
    }


    if (dTruckHead > 2000.0)//?ж????????????????Χ??
    {
        if (dTruckTail > 29500.0)
        {
            LOG(ERROR) << "???????????????????????!";
            iRes = -17;
            goto ReturnXXWXTL302;
        }

        if (dTruckTail - dTruckHead > 9000 && dTruckTail - dTruckHead < 18000)
            //?ж???г?

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
            //?ж?????
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





