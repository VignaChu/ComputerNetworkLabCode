#define WIN32_LEAN_AND_MEAN // 取消 Windows.h 中不必要的部分，提高编译速率，减小可执行文件大小
#define _WINSOCK_DEPRECATED_NO_WARNINGS // 取消 Winsock 函数弃用警告
#define _CRT_SECURE_NO_WARNINGS // 取消 'localtime' 警告

#include <iostream>
#include <string>
#include <winsock2.h>  // 使用 Winsock2 库进行网络编程,必须放在windows.h前面，不然会报300个错，我也不知道为什么
#include <windows.h> // 使用 Windows API 进行多线程处理

#pragma comment(lib, "ws2_32.lib") // 链接 Winsock2 库

#include "message.h"

using std::string;
using std::cout;
using std::cin;
using std::endl;
using std::cerr;

#define PORT 8080 // 定义端口号
#define SERVER_IP "127.0.0.1" // 定义地址为本机
#define BUFFER_SIZE 4096 // 定义缓冲区大小

SOCKET client_sock;  // 客户端套接字
string username;  

// 接收消息的线程，处理异步接收消息 
DWORD WINAPI receive_thread(LPVOID lpParam) {
    char buffer[BUFFER_SIZE];
    while (1) {
        int bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);  // 一个字节一个字节持续从socket接收数据存到buffer缓存中
        if (bytes <= 0) {
            // 返回值小于0表明接收错误或断开连接，设置输出文字颜色为红色
            SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY);
            cout << "\r" << std::string(80, ' ') << "\r"; // 清除当前输入行防止消息覆盖
            cout << "[SYSTEM] Server disconnected." << endl;

            // 恢复默认颜色
            SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 7);

            break;
        }
        buffer[bytes] = '\0';

        Message msg = Message::from_json(string(buffer));

        // 清除当前输入行
        cout << "\r" << std::string(80, ' ') << "\r";

        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);  // 获取控制台句柄便于修改颜色

        if (msg.getType() == "system") {
            // 系统消息：亮绿色
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            cout << "[SYSTEM] " << msg.getMsg() << endl;
        }
        else if (msg.getType() == "error") {
            // 错误消息：亮红色
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
            cout << "[ERROR] " << msg.getMsg() << endl;
        }
        else {
            // 普通消息：灰白色
            SetConsoleTextAttribute(hConsole, 7);  
            cout << "[" << msg.getUser() << "]: " << msg.getMsg() << endl;
        }

        // 恢复默认颜色
        SetConsoleTextAttribute(hConsole, 7);

        // 重新显示输入提示
        cout << "[" << username << "]: ";
        cout.flush();
    }

    ExitProcess(0);
    return 0;
}


int main() {
    WSADATA wsa;  // 初始化Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cerr << "WSAStartup failed." << endl;
        return 1;
    }


    // 创建套接字
    client_sock = socket(AF_INET, SOCK_STREAM, 0); // IPV4,TCP
    if (client_sock == INVALID_SOCKET) {
        cerr << "Socket creation failed." << endl;
        WSACleanup();
        return 1;
    }

    // 定义IPv4 地址结构体
    sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // 开始聊天客户端，注册用户昵称
    cout << "=== Chat Client ===" << endl;
    cout << "Enter username: ";
    std::getline(cin, username);

    // 尝试连接到服务器
    if (connect(client_sock, (SOCKADDR*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        cerr << "Connect failed. Please ensure the server is running." << endl;
        closesocket(client_sock); 
        WSACleanup();
        return 1;
    }

    // 发送登录消息
    string time_str = get_current_time();
    Message login_msg("login", username, "", time_str); // 创建登录消息对象
    string login_data = login_msg.toString(); // 将消息转换为json格式的字符串
    send(client_sock, login_data.c_str(), login_data.length(), 0); // 发送到服务端

    cout << "Connected! Type 'quit' to exit." << endl;

    // 启动接收消息的线程
    HANDLE hThread = CreateThread(NULL, 0, receive_thread, NULL, 0, NULL);
    if (!hThread) {
        cerr << "Failed to create receiver thread." << endl;
        closesocket(client_sock);
        WSACleanup();
        return 1;
    }
    CloseHandle(hThread); // 释放句柄，防止资源泄漏

    // 主循环发送消息
    cout << "[" << username << "]: "; // 显示当前用户昵称
    cout.flush();

    while (1) {
        string input;
        std::getline(cin, input);

        // 当输入为quit时，发送登出消息并退出
        if (input == "quit") {
            Message logout_msg("logout", username, "", get_current_time());
            send(client_sock, logout_msg.toString().c_str(), logout_msg.toString().length(), 0);
            break;
        }

        // 其他情况则发送到服务端
        if (!input.empty()) {
            Message chat_msg("chat", username, input, get_current_time());
            send(client_sock, chat_msg.toString().c_str(), chat_msg.toString().length(), 0);
        }

        // 重新恢复状态等待下一条信息的发送
        cout << "[" << username << "]: ";
        cout.flush();
    }

    closesocket(client_sock);
    WSACleanup();
    return 0;
}