#pragma once

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <cstdint>
#include <winsock2.h>
#include <windows.h>

// ======================= 常量定义 =======================
#define MSS 1024                // 最大报文段长度
#define RDT_PORT 6000           // 传输端口
#define TIMEOUT_MS 500          // 重传超时时间（毫秒）
#define INITIAL_WINDOW_SIZE 4   // 初始窗口大小（报文段数量）

// ======================= 报文类型定义 =======================
enum class PacketType : std::uint8_t {
    SETUP = 0,      // 连接建立请求
    SETUP_ACK = 1,  // 连接建立确认
    DATA = 2,       // 数据传输报文
    ACK = 3,        // 确认报文（累计ACK + SACK Mask）
    FIN = 4,        // 连接终止请求
    FIN_ACK = 5     // 连接终止确认
};

// ======================= RDT 报文结构 =======================
class RdtPacket {
public:
    std::uint8_t type;       // 报文类型
    std::uint16_t checksum;  // 校验和
    std::uint16_t data_len;  // 数据长度
    std::uint32_t seq_num;   // 序列号
    std::uint32_t ack_num;   // 确认号
    std::uint32_t win_size;  // 窗口大小
    std::uint32_t sack_mask; // SACK 位图
    char payload[MSS];       // 数据负载
};

// ======================= 工具类 =======================
class RdtProtocolHelper {
public:
    static std::uint16_t calculateChecksum(const char* buf, int len) {
        std::uint32_t sum = 0;
        const std::uint16_t* ptr = reinterpret_cast<const std::uint16_t*>(buf);
        while (len > 1) {
            sum += *ptr++;
            len -= 2;
        }
        if (len == 1) {
            sum += *reinterpret_cast<const std::uint8_t*>(ptr);
        }
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        return static_cast<std::uint16_t>(~sum);
    }

    static bool isChecksumValid(const RdtPacket& packet) {
        RdtPacket temp = packet;
        temp.checksum = 0;
        std::uint16_t calculated = calculateChecksum(
            reinterpret_cast<const char*>(&temp), sizeof(RdtPacket));
        return packet.checksum == calculated;
    }

    static void setChecksum(RdtPacket& packet) {
        packet.checksum = 0;
        packet.checksum = calculateChecksum(
            reinterpret_cast<const char*>(&packet), sizeof(RdtPacket));
    }
};