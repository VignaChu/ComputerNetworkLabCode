// sender.cpp -- RDT Sender (UDP + TCP-Reno + SR + SACK)
// g++ -std=c++17 -O2 -Wall -Wextra -o sender.exe sender.cpp -lws2_32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0600  // 为了使用 inet_pton
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <cstdint>
#include <iostream>
#include <fstream>
#include <map>
#include <chrono>
#include <iomanip>
#include <thread>
#include "rdt.hpp"

// 定义Reno状态枚举
enum RenoState { 
    RENO_SLOW_START,
    RENO_CA,
    RENO_FAST_RECOVERY
};

using std::cout;
using std::endl;
using std::cerr;
using std::string;
using clock_type = std::chrono::steady_clock;
using ms = std::chrono::milliseconds;

// ---------- 配置 ----------
namespace cfg {
    constexpr const char* SERVER_IP = "127.0.0.1";
    constexpr uint16_t SERVER_PORT = 6000;
    constexpr const char* INPUT_FILE = "input.txt";
    constexpr uint32_t SND_BUF_SZ = 4 * 1024 * 1024;
    constexpr uint32_t RCV_BUF_SZ = 4 * 1024 * 1024;
}

// ---------- 日志 ----------
inline void logInfo(const string& s) { cout << "[SENDER] " << s << endl; }

// ---------- RAII 锁 ----------
struct Lock {
    CRITICAL_SECTION& cs_;
    explicit Lock(CRITICAL_SECTION& cs) : cs_(cs) { EnterCriticalSection(&cs_); }
    ~Lock() { LeaveCriticalSection(&cs_); }
};

// ---------- 全局状态 ----------
namespace sender {
    SOCKET sock;
    sockaddr_in srvAddr{};
    int addrLen = sizeof(sockaddr_in);
    CRITICAL_SECTION csReno;
    CRITICAL_SECTION csSR;
    
    // 文件
    uint32_t fileSize = 0;
    std::ifstream file;
    
    // RENO
    double cwnd = 1.0;
    double ssthresh = 64.0;
    RenoState renoState = RENO_SLOW_START;
    int dupAck = 0;
    
    // SR
    uint32_t baseSeq = 0;
    uint32_t nextSeq = 0;
    uint32_t peerWin = 0;
    
    struct Unacked {
        RdtPacket pkt;
        clock_type::time_point ts;
    };
    std::map<uint32_t, Unacked> winMap;
    
    // 计时
    clock_type::time_point t0;
}

// ---------- 工具 ----------
inline void sendPkt(const RdtPacket& p) {
    sendto(sender::sock, reinterpret_cast<const char*>(&p), sizeof(p), 0, 
           reinterpret_cast<sockaddr*>(&sender::srvAddr), sender::addrLen);
}

inline RdtPacket makeDataPkt(uint32_t seq, uint16_t len) {
    RdtPacket p{};
    p.type = static_cast<uint8_t>(PacketType::DATA);
    p.seq_num = seq;
    p.data_len = len;
    p.win_size = cfg::RCV_BUF_SZ;
    RdtProtocolHelper::setChecksum(p);
    return p;
}

inline RdtPacket makeFinPkt(uint32_t seq) {
    RdtPacket p{};
    p.type = static_cast<uint8_t>(PacketType::FIN);
    p.seq_num = seq;
    RdtProtocolHelper::setChecksum(p);
    return p;
}

// ---------- RENO ----------
void renoTimeout() {
    Lock l(sender::csReno);
    sender::ssthresh = std::max(sender::cwnd / 2.0, 2.0);
    sender::cwnd = 1.0;
    sender::renoState = RENO_SLOW_START;
    sender::dupAck = 0;
    logInfo("Timeout → cwnd=" + std::to_string(sender::cwnd) + " ssthresh=" + std::to_string(sender::ssthresh));
    
    auto it = sender::winMap.find(sender::baseSeq);
    if (it != sender::winMap.end()) {
        sendPkt(it->second.pkt);
        it->second.ts = clock_type::now();
    }
}

void renoNewAck(uint32_t newBase) {
    Lock l(sender::csReno);
    uint32_t acked = newBase - sender::baseSeq;
    int segs = static_cast<int>(acked / MSS);
    
    for (uint32_t s = sender::baseSeq; s < newBase; s += MSS) {
        sender::winMap.erase(s);
    }
    
    sender::baseSeq = newBase;
    
    switch (sender::renoState) {
        case RENO_SLOW_START:
            sender::cwnd += segs;
            if (sender::cwnd >= sender::ssthresh) {
                sender::renoState = RENO_CA;
            }
            break;
        case RENO_CA:
            sender::cwnd += static_cast<double>(segs) / sender::cwnd;
            break;
        case RENO_FAST_RECOVERY:
            sender::cwnd = sender::ssthresh;
            sender::renoState = RENO_CA;
            break;
    }
    
    sender::dupAck = 0;
    logInfo("NewACK -> cwnd=" + std::to_string(sender::cwnd) + " base=" + std::to_string(sender::baseSeq));
}

void renoDupAck() {
    Lock l(sender::csReno);
    ++sender::dupAck;
    
    if (sender::renoState == RENO_SLOW_START || sender::renoState == RENO_CA) {
        if (sender::dupAck == 3) {
            sender::ssthresh = std::max(sender::cwnd / 2.0, 2.0);
            sender::cwnd = sender::ssthresh + 3;
            sender::renoState = RENO_FAST_RECOVERY;
            logInfo("FastRetransmit → cwnd=" + std::to_string(sender::cwnd));
            
            auto it = sender::winMap.find(sender::baseSeq);
            if (it != sender::winMap.end()) {
                sendPkt(it->second.pkt);
                it->second.ts = clock_type::now();
            }
        }
    } else if (sender::renoState == RENO_FAST_RECOVERY) {
        sender::cwnd += 1.0;
    }
}

// ---------- 接收线程 ----------
DWORD WINAPI recvThread(LPVOID) {
    char buf[sizeof(RdtPacket)];
    while (true) {
        int n = recvfrom(sender::sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n <= 0) continue;
        
        RdtPacket pkt;
        memcpy(&pkt, buf, sizeof(pkt));
        
        if (!RdtProtocolHelper::isChecksumValid(pkt)) continue;
        
        if (pkt.type == static_cast<uint8_t>(PacketType::ACK)) {
            Lock lr(sender::csReno), ls(sender::csSR);
            sender::peerWin = pkt.win_size;
            
            if (pkt.ack_num > sender::baseSeq) {
                renoNewAck(pkt.ack_num);
            } else if (pkt.ack_num == sender::baseSeq && sender::nextSeq > sender::baseSeq) {
                renoDupAck();
            }
        } else if (pkt.type == static_cast<uint8_t>(PacketType::FIN_ACK)) {
            logInfo("Received FIN_ACK");
            break;
        }
    }
    return 0;
}

// ---------- 主函数 ----------
int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;
    
    InitializeCriticalSection(&sender::csReno);
    InitializeCriticalSection(&sender::csSR);
    
    sender::sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender::sock == INVALID_SOCKET) return 1;
    
    int opt = cfg::SND_BUF_SZ;
    setsockopt(sender::sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&opt), sizeof(opt));
    opt = cfg::RCV_BUF_SZ;
    setsockopt(sender::sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&opt), sizeof(opt));
    
    sender::srvAddr.sin_family = AF_INET;
    sender::srvAddr.sin_port = htons(cfg::SERVER_PORT);
    
    // 修复1: 使用正确的 inet_pton 函数（小写）
    if (inet_pton(AF_INET, cfg::SERVER_IP, &sender::srvAddr.sin_addr) != 1) {
        cerr << "Failed to convert IP address" << endl;
        return 1;
    }
    
    // handshake
    RdtPacket setup{};
    setup.type = static_cast<uint8_t>(PacketType::SETUP);
    setup.win_size = cfg::RCV_BUF_SZ;
    RdtProtocolHelper::setChecksum(setup);
    sendPkt(setup);
    
    RdtPacket setupAck{};
    if (recvfrom(sender::sock, reinterpret_cast<char*>(&setupAck), sizeof(setupAck), 0, 
                reinterpret_cast<sockaddr*>(&sender::srvAddr), &sender::addrLen) <= 0 ||
        setupAck.type != static_cast<uint8_t>(PacketType::SETUP_ACK)) {
        cerr << "Handshake failed\n";
        return 1;
    }
    
    sender::peerWin = setupAck.win_size;
    logInfo("Handshake done, peerWin=" + std::to_string(sender::peerWin));
    
    // read file
    sender::file.open(cfg::INPUT_FILE, std::ios::binary | std::ios::ate);
    if (!sender::file) return 1;
    sender::fileSize = static_cast<uint32_t>(sender::file.tellg());
    sender::file.seekg(0);
    
    // start recv thread
    sender::t0 = clock_type::now();
    HANDLE hRecv = CreateThread(nullptr, 0, recvThread, nullptr, 0, nullptr);
    
    // main send loop
    while (sender::baseSeq < sender::fileSize || !sender::winMap.empty()) {
        // timeout check
        {
            Lock l(sender::csSR);
            auto it = sender::winMap.find(sender::baseSeq);
            if (it != sender::winMap.end() && 
                std::chrono::duration_cast<ms>(clock_type::now() - it->second.ts).count() > TIMEOUT_MS) {
                renoTimeout();
            }
        }
        
        // send window
        uint32_t cwndBytes = static_cast<uint32_t>(sender::cwnd * MSS);
        uint32_t winBytes = std::min(cwndBytes, sender::peerWin);
        uint32_t inFlight = sender::nextSeq - sender::baseSeq;
        uint32_t canSend = (winBytes > inFlight) ? winBytes - inFlight : 0;
        
        while (canSend >= MSS && sender::nextSeq < sender::fileSize) {
            RdtPacket pkt = makeDataPkt(sender::nextSeq, MSS);
            sender::file.read(pkt.payload, MSS);
            pkt.data_len = static_cast<uint16_t>(sender::file.gcount());
            RdtProtocolHelper::setChecksum(pkt);
            
            {
                Lock l(sender::csSR);
                sender::winMap[sender::nextSeq] = {pkt, clock_type::now()};
                sendPkt(pkt);
                sender::nextSeq += pkt.data_len;
                canSend -= pkt.data_len;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // fin
    sendPkt(makeFinPkt(sender::nextSeq));
    WaitForSingleObject(hRecv, 2000);
    
    // result
    auto dur = std::chrono::duration_cast<ms>(clock_type::now() - sender::t0).count();
    double thr = static_cast<double>(sender::fileSize * 8) / dur * 1000 / (1024 * 1024);
    
    cout << "\n========== Result ==========\n";
    cout << "FileSize : " << sender::fileSize << " bytes\n";
    cout << "Time : " << dur << " ms\n";
    cout << "Throughput: " << std::fixed << std::setprecision(3) << thr << " Mbps\n";
    
    // cleanup
    CloseHandle(hRecv);
    DeleteCriticalSection(&sender::csReno);
    DeleteCriticalSection(&sender::csSR);
    closesocket(sender::sock);
    WSACleanup();
    
    return 0;
}