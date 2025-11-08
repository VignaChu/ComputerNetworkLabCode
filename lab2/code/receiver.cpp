// receiver.cpp  ——  RDT Receiver (UDP + SR + SACK + 模拟丢包)
// 编译：cl /std:c++17 /EHsc receiver.cpp ws2_32.lib

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstdint>
#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <random>
#include <ctime>

#include "rdt.hpp"

using std::cout;
using std::endl;
using std::string;

// ---------- 配置 ----------
namespace cfg {
constexpr const char* OUTPUT_FILE     = "output.txt";
constexpr double      PACKET_LOSS_RATE= 0.10;          // 10% 丢包
constexpr uint32_t    RECV_WIN_PKTS   = INITIAL_WINDOW_SIZE;
}

// ---------- 日志 ----------
inline void logInfo(const string& s) { cout << "[RECV] " << s << endl; }

// ---------- RAII 锁 ----------
struct Lock {
    CRITICAL_SECTION& cs_;
    explicit Lock(CRITICAL_SECTION& cs) : cs_(cs) { EnterCriticalSection(&cs_); }
    ~Lock() { LeaveCriticalSection(&cs_); }
};

// ---------- 全局状态 ----------
namespace receiver {
SOCKET sock;
sockaddr_in peerAddr{};
int addrLen = sizeof(peerAddr);

CRITICAL_SECTION csSR;

// 接收窗口
uint32_t baseSeq = 0;                       // 期望序号
uint32_t winSize = cfg::RECV_WIN_PKTS * MSS;// 字节数

// 乱序缓存 <seqNum, RdtPacket>
std::map<uint32_t, RdtPacket> buf;

// 文件
std::ofstream out;
} // namespace receiver

// ---------- 工具 ----------
inline void sendPkt(const RdtPacket& p) {
    sendto(receiver::sock, reinterpret_cast<const char*>(&p), sizeof(p), 0,
           reinterpret_cast<sockaddr*>(&receiver::peerAddr), receiver::addrLen);
}

inline RdtPacket makeAck(uint32_t ack, uint32_t win) {
    RdtPacket pkt{};
    pkt.type    = static_cast<uint8_t>(PacketType::ACK);
    pkt.ack_num = ack;
    pkt.win_size= win;
    RdtProtocolHelper::setChecksum(pkt);
    return pkt;
}

inline RdtPacket makeSetupAck(uint32_t ack, uint32_t win) {
    RdtPacket pkt{};
    pkt.type    = static_cast<uint8_t>(PacketType::SETUP_ACK);
    pkt.ack_num = ack;
    pkt.win_size= win;
    RdtProtocolHelper::setChecksum(pkt);
    return pkt;
}

inline RdtPacket makeFinAck(uint32_t ack) {
    RdtPacket pkt{};
    pkt.type    = static_cast<uint8_t>(PacketType::FIN_ACK);
    pkt.ack_num = ack;
    RdtProtocolHelper::setChecksum(pkt);
    return pkt;
}

// ---------- 模拟丢包 ----------
bool shouldDrop() {
    static std::mt19937 rng(static_cast<unsigned>(std::time(nullptr)));
    static std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng) < cfg::PACKET_LOSS_RATE;
}

// ---------- 主函数 ----------
int main() {
    // 设置控制台输出编码为 UTF-8
    SetConsoleOutputCP(CP_UTF8);
    
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;
    InitializeCriticalSection(&receiver::csSR);

    receiver::sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receiver::sock == INVALID_SOCKET) return 1;

    sockaddr_in local{};
    local.sin_family      = AF_INET;
    local.sin_port        = htons(RDT_PORT);
    local.sin_addr.s_addr = INADDR_ANY;
    if (bind(receiver::sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        closesocket(receiver::sock); WSACleanup(); return 1;
    }

    receiver::out.open(cfg::OUTPUT_FILE, std::ios::binary | std::ios::trunc);
    if (!receiver::out) return 1;

    logInfo("Receiver ready on port " + std::to_string(RDT_PORT));

    char buf[sizeof(RdtPacket)];
    while (true) {
        int n = recvfrom(receiver::sock, buf, sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&receiver::peerAddr), &receiver::addrLen);
        if (n <= 0) continue;

        RdtPacket pkt;
        memcpy(&pkt, buf, sizeof(pkt));

        if (shouldDrop() && pkt.type == static_cast<uint8_t>(PacketType::DATA)) {
            logInfo("Dropped DATA seq=" + std::to_string(pkt.seq_num));
            continue;
        }

        if (!RdtProtocolHelper::isChecksumValid(pkt)) {
            logInfo("Bad checksum, drop seq=" + std::to_string(pkt.seq_num));
            continue;
        }

        Lock l(receiver::csSR);

        if (pkt.type == static_cast<uint8_t>(PacketType::SETUP)) {
            sendPkt(makeSetupAck(pkt.seq_num + 1, receiver::winSize));
            logInfo("SETUP received -> sent SETUP_ACK");
        }
        else if (pkt.type == static_cast<uint8_t>(PacketType::DATA)) {
            uint32_t seq = pkt.seq_num;
            uint32_t end = seq + pkt.data_len;

            // 窗口外 → 直接重发当前 ACK
            if (seq < receiver::baseSeq || seq >= receiver::baseSeq + receiver::winSize) {
                sendPkt(makeAck(receiver::baseSeq, receiver::winSize));
                continue;
            }

            // 重复或乱序 → 缓存
            if (seq != receiver::baseSeq) {
                receiver::buf[seq] = pkt;
                logInfo("Buffered out-of-order seq=" + std::to_string(seq));
            } else {
                // 顺序交付
                receiver::out.write(pkt.payload, pkt.data_len);
                receiver::baseSeq = end;
                // 连续交付缓存
                while (receiver::buf.count(receiver::baseSeq)) {
                    RdtPacket& p = receiver::buf.at(receiver::baseSeq);
                    receiver::out.write(p.payload, p.data_len);
                    receiver::baseSeq += p.data_len;
                    receiver::buf.erase(receiver::baseSeq - p.data_len);
                }
            }
            sendPkt(makeAck(receiver::baseSeq, receiver::winSize));
        }
        else if (pkt.type == static_cast<uint8_t>(PacketType::FIN)) {
            sendPkt(makeFinAck(pkt.seq_num + 1));
            logInfo("FIN received -> sent FIN_ACK. Transfer complete.");
            break;
        }
    }

    receiver::out.close();
    DeleteCriticalSection(&receiver::csSR);
    closesocket(receiver::sock);
    WSACleanup();
    return 0;
}