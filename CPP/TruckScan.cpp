// TruckScan.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//



#include <windows.h>
#include <process.h>
#include <iostream>
#include <thread>//包含线程库
#include "easylogging++.h"//包含Easylogging++库
#include "Crane.h"//包含卸船机类



INITIALIZE_EASYLOGGINGPP//初始化Easylogging++

using namespace std;//使用标准命名空间
using namespace el;//使用Easylogging++命名空间




int main()//主函数
{
    char c_exe_path[MAX_PATH]{ 0 };//定义一个字符数组，用于存储程序路径
    GetModuleFileNameA(nullptr, c_exe_path, sizeof(c_exe_path));//获取程序路径

    // 获取程序路径并配置日志
    string str_exe_path = c_exe_path;//将字符数组转换为字符串
    string str_path = str_exe_path.substr(0, str_exe_path.find_last_of('\\'));//获取程序路径
    Configurations conf(str_path + "\\my_log.conf");//配置日志文件
    Loggers::reconfigureAllLoggers(conf);//重新配置所有日志记录器

    string str_Mutex = str_exe_path;//定义一个字符串，用于存储程序路径

    // 处理程序路径
    size_t i_POs = str_Mutex.find(':');//查找冒号
    if (i_POs != string::npos)//如果找到冒号
        str_Mutex.replace(i_POs, 1, "");
    i_POs = str_Mutex.find('\\');//查找反斜杠
    while (i_POs != string::npos)//如果找到反斜杠
    {
        str_Mutex.replace(i_POs, 1, "");//替换反斜杠
        i_POs = str_Mutex.find('\\');//查找反斜杠
    }
    i_POs = str_Mutex.find('.');//查找点
    if (i_POs != string::npos)//如果找到点
        str_Mutex.replace(i_POs, 1, "");//替换点


    wchar_t wc_Mutex[MAX_PATH]{ L"Global\\" };//定义一个宽字符数组，用于存储互斥体名称
    swprintf_s(wc_Mutex + 7, MAX_PATH - 7, L"%S", str_Mutex.c_str());//将字符串转换为宽字符串
    // 创建互斥体
    HANDLE hMutex = CreateMutexW(NULL, false, wc_Mutex);//创建一个互斥体

    // 检查互斥体是否已存在或拒绝访问
    if (ERROR_ALREADY_EXISTS == GetLastError() || ERROR_ACCESS_DENIED == GetLastError())//如果互斥体已存在或拒绝访问
    {
        LOG(ERROR) << "TruckScan is Running !";//日志记录
        if (hMutex)
            CloseHandle(hMutex);
        return -1;
    }
    

    LOG(INFO) << "程序开始运行！运行路径："<< str_path << "，程序路径：" << str_exe_path;//日志记录


    WSADATA wsData;//定义一个WSADATA结构体，用于存储WinSocket数据
    if (WSAStartup(MAKEWORD(2, 2), &wsData) != 0)//初始化WinSocket
    {
        LOG(ERROR) << "初始化WinSocket失败!";//日志记录
        return -1;
    }



    
    string connstr = "Provider=SQLOLEDB;Server=192.168.100.250;Initial Catalog=XCJ; uid=sa; pwd=Sa123;Pooling=true;Max Pool Size =1024;";//定义一个字符串，用于存储数据库连接字符串
    CDBConnetPool DBPool(10, (_bstr_t)(connstr.c_str()));//创建一个数据库连接池
    // 1. 读取配置文件路径
    string str_Config = str_path + "\\Config.ini";//定义一个字符串，用于存储配置文件路径
    // 2. 获取卸船机数量
    int CraneNum = GetPrivateProfileIntA("PUBLIC", "CraneNum", 4, str_Config.c_str());//获取卸船机数量
    // 3. 循环读取每台卸船机的配置

    // 为每台卸船机创建一个控制线程
    for (int crane = 1; crane <= CraneNum; ++crane)//在 for 循环中定义并使用 crane 变量
    {
        // crane 从 1 开始，到 CraneNum 结束
        // 定义变量存储配置信息
        char SCANIP[18] = { 0 };//定义一个长度为18的字符数组，用于存储扫描仪IP地址
        char RFIDIP[18] = { 0 };//定义一个长度为18的字符数组，用于存储RFID地址
        char PLCIP[18] = { 0 };//定义一个长度为18的字符数组，用于存储PLCIP地址
        char Forklift[18] = { 0 };//定义一个长度为18的字符数组，用于存储Forklift地址

        // 构建每台卸船机的配置节名称（如 "Crane1", "Crane2" 等）
        string CraneName = "Crane" + to_string(crane);//定义一个字符串，用于存储卸船机名称

        // 读取各项配置
        GetPrivateProfileStringA(CraneName.c_str(), "PLCIP", "", PLCIP, 18, str_Config.c_str());//获取PLC地址
        GetPrivateProfileStringA(CraneName.c_str(), "RFIDIP", "", RFIDIP, 18, str_Config.c_str());//获取RFID地址    
        GetPrivateProfileStringA(CraneName.c_str(), "SCANIP", "", SCANIP, 18, str_Config.c_str());//获取扫描仪IP地址    
        GetPrivateProfileStringA(CraneName.c_str(), "ForkliftIP", "", Forklift, 18, str_Config.c_str());//获取铲车IP地址  
        
        // 在 [Crane1] 节中
        // 2. 找到 SCANTYPE 这一项
        // 3. 读取数值（如 2）
        // 4. 存储到整数变量 SCANTYPE 中
        int SCANTYPE = GetPrivateProfileIntA(CraneName.c_str(), "SCANTYPE", 1, str_Config.c_str());//获取扫描仪类型 
        
        unsigned char no = GetPrivateProfileIntA(CraneName.c_str(), "CraneNo", 5, str_Config.c_str());//获取卸船机编号  
        int Range = GetPrivateProfileIntA(CraneName.c_str(), "Range", 500, str_Config.c_str());//获取卡车停车范围   

        LOG(INFO) << "卸船机号："<< no <<"，扫描仪类型：" << SCANTYPE  << "，卡车停车范围："<< Range << "，PLC地址：" //日志记录
            << PLCIP << "，卡车RFID地址：" << RFIDIP << "，卡车扫描仪地址：" << SCANIP << "，铲车扫描仪地址：" << Forklift  ;//日志记录

        Crane c(SCANIP, RFIDIP, PLCIP, Forklift, SCANTYPE, no, Range, DBPool);//创建一个卸船机对象
        thread t(BeginCrane, &c);//创建一个线程，用于执行卸船机任务
        t.detach();//分离线程
    }
    while (1)//无限循环
    {
        Sleep(10000000);//休眠10000000毫秒
    }
    WSACleanup();//清理WinSocket
    return 0;//返回0
}



void BeginCrane(Crane* c)//执行卸船机任务
// 函数定义：接收一个 Crane 类型的指针作为参数
// c 指向要控制的卸船机对象
{
    for (;;)  
// 无限循环，让卸船机持续工作
// ;; 表示没有初始条件、循环条件和增量
    {
    Sleep(200);  
    // 休眠200毫秒
    // 控制循环执行的频率，避免太频繁

    int iRes = c->Init();  
    // 调用卸船机的初始化方法
    // 返回值存在 iRes 中表示是否成功
    // 0 表示成功，其他值表示失败

    if (iRes == 0)  
        iRes = c->ReadPLC();  
        // 如果初始化成功（iRes == 0）
        // 就读取 PLC 中的数据
        // 可能包括位置、状态等信息

    c->ForkliftPause();  
    // 执行铲车暂停操作
    // 这个操作不检查返回值

    if (iRes > 0)  
        iRes = c->Scan();  
        // 如果前面操作成功（iRes > 0）
        // 执行扫描操作

    if (iRes > 0)  
        iRes = c->Identify();  
        // 如果扫描成功
        // 执行识别操作

    if (iRes > 0)  
        iRes = c->Calculate();  
        // 如果识别成功
        // 执行计算操作

    iRes = c->WritePLC();  
    // 最后将结果写回 PLC
    // 不管前面是否成功

    }
}




// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
