#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS 
#define _CRT_SECURE_NO_WARNINGS        

#include <iostream>
#include <vector>
#include <winsock2.h>  
#include <windows.h> 

#pragma comment(lib, "ws2_32.lib")

#include "message.h"

using std::string;
using std::cout;
using std::endl;
using std::cerr;
using std::vector;

#define PORT 8080
#define BUFFER_SIZE 4096

vector<SOCKET> clients; // 存储所有客户端的套接字
vector<string> usernames; // 存储所有客户端的用户名
HANDLE clients_mutex;

void broadcast(const string& msg) { // 广播消息，将消息发送到所有客户端
    WaitForSingleObject(clients_mutex, INFINITE);
    for (size_t i = 0; i < clients.size(); ++i) {
        send(clients[i], msg.c_str(), msg.length(), 0);
    }
    ReleaseMutex(clients_mutex);
}

DWORD WINAPI handle_client(LPVOID lpParam) { // 定义单线程执行逻辑，处理单个客户端请求
    SOCKET client_sock = (SOCKET)lpParam;  // 获取传入的客户端套接字
    char buffer[BUFFER_SIZE];
    string username; 
    
    // 接收登录消息,并通过Message::from_json解析成Message对象
    int bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0); // 持续接受消息
    if (bytes <= 0) {
        closesocket(client_sock);
        return 0;
    }
    buffer[bytes] = '\0';
    Message login_msg = Message::from_json(string(buffer));
    username = login_msg.getUser(); // 通过get获取用户昵称

    // 添加到客户端列表
    WaitForSingleObject(clients_mutex, INFINITE);
    clients.push_back(client_sock);
    usernames.push_back(username);
    ReleaseMutex(clients_mutex);

    // 显示新用户加入消息
    std::string time_str = get_current_time();
    Message join_msg("system", "", username + " joined.", time_str);
    // 广播到全部客户端
    broadcast(join_msg.toString());

    // 主循环，处理用户聊天消息
    while (1) {
        bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0); // 持续接受消息
        if (bytes <= 0) break;

        buffer[bytes] = '\0';
        Message chat_msg = Message::from_json(string(buffer)); // 利用工厂方法解析json消息
        // 解析消息并广播
        if (chat_msg.getType() == "chat") {
            cout << "[" << chat_msg.getTime() << "] " << chat_msg.getUser() << ": " << chat_msg.getMsg() << endl;
            broadcast(chat_msg.toString());
        }
        // 当接受客户端退出时结束管理它的线程
        else if (chat_msg.getType() == "logout") {
            break;
        }
    }

    // 客户端断联处理
    WaitForSingleObject(clients_mutex, INFINITE);
    for (size_t i = 0; i < clients.size(); ++i) {
        if (clients[i] == client_sock) {
            clients.erase(clients.begin() + i);
            usernames.erase(usernames.begin() + i);
            break;
        }
    }
    ReleaseMutex(clients_mutex);
    // 广播用户离开消息
    time_str = get_current_time(); // 更新时间
    Message leave_msg("system", "", username + " left.", time_str);
    broadcast(leave_msg.toString());

    cout << username << " disconnected." << endl;
    closesocket(client_sock);
    return 0;
}

int main() {
    // 初始化Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        cerr << "WSAStartup failed." << endl;
        return 1;
    }
    // 创建互斥锁，保护客户端列表
    clients_mutex = CreateMutex(NULL, FALSE, NULL);
    if (!clients_mutex) {
        cerr << "CreateMutex failed." << endl;
        WSACleanup();
        return 1;
    }
    // 创建监听套接字
    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        cerr << "Socket creation failed." << endl;
        CloseHandle(clients_mutex); 
        WSACleanup();
        return 1;
    }
    // 绑定和监听
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cerr << "Bind failed." << endl;
        closesocket(server_sock);
        CloseHandle(clients_mutex);
        WSACleanup();
        return 1;
    }
    // 开始监听，最多五个排队
    if (listen(server_sock, 5) == SOCKET_ERROR) {
        cerr << "Listen failed." << endl;
        closesocket(server_sock);
        CloseHandle(clients_mutex);
        WSACleanup();
        return 1;
    }

    cout << "=== Chat Server Running on port " << PORT << " ===" << endl;
    // 主循环，接受客户端连接
    while (1) {
        sockaddr_in client_addr;
        int len = sizeof(client_addr);
        // 堵塞等待新链接
        SOCKET client_sock = accept(server_sock, (SOCKADDR*)&client_addr, &len);
        if (client_sock == INVALID_SOCKET) continue;

        HANDLE hThread = CreateThread(NULL, 0, handle_client, (LPVOID)client_sock, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        }
        else {
            cerr << "Failed to create thread." << endl;
            closesocket(client_sock);
        }
    }

    closesocket(server_sock);
    CloseHandle(clients_mutex);
    WSACleanup();
    return 0;
}