#define ELPP_DEFAULT_LOG_FILE "logs/TruckScan.log"
#define ELPP_LOGGING_FLAGS_FROM_ARG
#define ELPP_STL_LOGGING
#define ELPP_THREAD_SAFE
#define ELPP_NO_DEFAULT_LOG_FILE
#define _USE_MATH_DEFINES

// Windows相关头文件
#include <windows.h>
#include <tchar.h>

// COM相关头文件
#include <atlbase.h>
#include <atlcom.h>
#include <comdef.h>

// 导入ADO库
#import "msado15.dll" no_namespace rename("EOF", "adoEOF")

// 标准库头文件
#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include <stdexcept>
#include <memory>

// 项目头文件
#include "easylogging++.h"
#include "Crane.h"
#include "DBConnetPool.h"
#include "snap7.h"

// 初始化日志库
INITIALIZE_EASYLOGGINGPP

// 使用命名空间
using namespace std;

// 日志相关定义
#define LOG(LEVEL) ELPP_WRITE_LOG(el::base::Writer, LEVEL, el::base::DispatchAction::NormalLog)
#define LOG_INFO LOG(el::Level::Info)
#define LOG_DEBUG LOG(el::Level::Debug)
#define LOG_ERROR LOG(el::Level::Error)

// 全局日志配置
namespace {
    el::Configurations defaultConf;
    
    void initializeLogging() {
        defaultConf.setToDefault();
        // 设置日志格式
        defaultConf.set(el::Level::Global,
                       el::ConfigurationType::Format,
                       "%datetime %level [%func] %msg");
        // 设置日志文件
        defaultConf.set(el::Level::Global,
                       el::ConfigurationType::Filename,
                       "logs/TruckScan.log");
        // 应用配置
        el::Loggers::reconfigureLogger("default", defaultConf);
    }
}

// 数据库连接池测试
void TestDBConnetPool() {
    LOG(INFO) << "开始测试数据库连接池...";
    
    try {
        LOG(DEBUG) << "正在创建数据库连接池实例...";
        TCHAR* connstr = (TCHAR*)"Provider=SQLOLEDB;Server=192.168.100.250;Initial Catalog=XCJ; uid=sa; pwd=Sa123;";
        CDBConnetPool pool(5, connstr);
        LOG(DEBUG) << "连接池创建成功，初始连接数: 5";
        
        LOG(DEBUG) << "尝试获取数据库连接...";
        _ConnectionPtr conn = pool.GetCon();
        if(conn == nullptr) {
            LOG(ERROR) << "获取数据库连接失败";
            throw std::runtime_error("获取数据库连接失败");
        }
        LOG(DEBUG) << "成功获取数据库连接";
        
        LOG(DEBUG) << "尝试释放数据库连接...";
        pool.ReleaseCon(conn);
        LOG(DEBUG) << "成功释放数据库连接";
        
        LOG(INFO) << "数据库连接池测试完成";
    }
    catch(const std::exception& e) {
        LOG(ERROR) << "数据库测试失败: " << e.what();
        cerr << "数据库测试失败: " << e.what() << endl;
        throw;
    }
}

// PLC通信测试
void TestPLCCommunication() {
    LOG(INFO) << "开始测试PLC通信...";
    
    try {
        TS7Client plc;
        const char* plcIP = "192.168.100.1";
        LOG(DEBUG) << "尝试连接PLC，IP地址: " << plcIP;
        
        int result = plc.ConnectTo(plcIP, 0, 1);
        if(result != 0) {
            LOG(ERROR) << "PLC连接失败，错误代码: " << result;
            throw std::runtime_error("PLC连接失败");
        }
        LOG(DEBUG) << "PLC连接成功";
        
        LOG(DEBUG) << "尝试读取PLC数据...";
        unsigned char buffer[1024];
        result = plc.DBRead(1, 0, sizeof(buffer), &buffer);
        if(result != 0) {
            LOG(ERROR) << "读取PLC数据失败，错误代码: " << result;
            throw std::runtime_error("读取PLC数据失败");
        }
        LOG(DEBUG) << "成功读取PLC数据，数据大小: " << sizeof(buffer) << "字节";
        
        LOG(DEBUG) << "正在断开PLC连接...";
        plc.Disconnect();
        LOG(DEBUG) << "PLC连接已断开";
        
        LOG(INFO) << "PLC通信测试完成";
    }
    catch(const std::exception& e) {
        LOG(ERROR) << "PLC测试失败: " << e.what();
        cerr << "PLC测试失败: " << e.what() << endl;
        throw;
    }
}

// 卸船机控制测试
void TestCraneControl() {
    LOG(INFO) << "开始测试卸船机控制...";
    
    try {
        LOG(DEBUG) << "创建数据库连接池...";
        TCHAR* connstr = (TCHAR*)"Provider=SQLOLEDB;Server=192.168.100.250;Initial Catalog=XCJ; uid=sa; pwd=Sa123;";
        CDBConnetPool pool(5, connstr);
        LOG(DEBUG) << "数据库连接池创建成功";
        
        LOG(DEBUG) << "创建卸船机实例...";
        Crane crane("192.168.100.2", "192.168.100.3", "192.168.100.1", "192.168.100.4", 1, 1, 500, pool);
        LOG(DEBUG) << "卸船机实例创建成功";
        
        LOG(DEBUG) << "初始化卸船机...";
        int result = crane.Init();
        if(result != 0) {
            LOG(ERROR) << "卸船机初始化失败，错误代码: " << result;
            throw std::runtime_error("卸船机初始化失败");
        }
        LOG(DEBUG) << "卸船机初始化成功";
        
        LOG(DEBUG) << "读取PLC数据...";
        result = crane.ReadPLC();
        if(result < 0) {
            LOG(ERROR) << "读取PLC失败，错误代码: " << result;
            throw std::runtime_error("读取PLC失败");
        }
        LOG(DEBUG) << "PLC数据读取成功";
        
        LOG(DEBUG) << "开始扫描...";
        result = crane.Scan();
        if(result < 0) {
            LOG(ERROR) << "扫描功能失败，错误代码: " << result;
            throw std::runtime_error("扫描功能失败");
        }
        LOG(DEBUG) << "扫描完成";
        
        LOG(INFO) << "卸船机控制测试完成";
    }
    catch(const std::exception& e) {
        LOG(ERROR) << "卸船机测试失败: " << e.what();
        cerr << "卸船机测试失败: " << e.what() << endl;
        throw;
    }
}

// 主程序测试
void TestMainProgram() {
    LOG(INFO) << "开始测试主程序功能...";
    
    try {
        LOG(DEBUG) << "获取程序路径...";
        char c_exe_path[MAX_PATH]{ 0 };
        if(GetModuleFileNameA(nullptr, c_exe_path, sizeof(c_exe_path)) == 0) {
            LOG(ERROR) << "获取程序路径失败，错误码: " << GetLastError();
            throw std::runtime_error("获取程序路径失败");
        }
        LOG(DEBUG) << "程序路径: " << c_exe_path;
        
        LOG(INFO) << "测试日志记录";
        LOG(DEBUG) << "主程序功能测试完成";
    }
    catch(const std::exception& e) {
        LOG(ERROR) << "主程序测试失败: " << e.what();
        cerr << "主程序测试失败: " << e.what() << endl;
        throw;
    }
}

// 主测试函数
int main() {
    // 初始化日志系统
    initializeLogging();
    
    try {
        cout << "开始单元测试..." << endl;
        LOG(INFO) << "开始执行单元测试";
        
        // 执行各层级测试
        TestDBConnetPool();
        TestPLCCommunication();
        TestCraneControl();
        TestMainProgram();
        
        LOG(INFO) << "所有测试完成";
        cout << "所有测试完成" << endl;
        return 0;
    }
    catch (const std::exception& e) {
        LOG(ERROR) << "测试过程中发生错误: " << e.what();
        cerr << "测试过程中发生错误: " << e.what() << endl;
        return -1;
    }
} 