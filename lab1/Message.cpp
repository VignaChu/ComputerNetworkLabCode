// message.cpp

// 解决 'localtime' 警告
#define _CRT_SECURE_NO_WARNINGS 

#include "message.h"
#include <cstdio>
#include <cstring>
#include <ctime> // 引入 time.h
#include <string>

// 实现缺失的时间获取函数
std::string get_current_time() {
    char time_str[64];
    time_t now = time(0);
    struct tm* ltm = localtime(&now);

    // 格式化时间为 HH:MM:SS
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d",
        ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
    return std::string(time_str);
}

// 实现默认构造函数
Message::Message() : type(""), user(""), msg(""), time("") {}

// 实现带参构造函数
Message::Message(const std::string& t, const std::string& u, const std::string& m, const std::string& tm)
    : type(t), user(u), msg(m), time(tm) {
}

// from_json 实现（与您提供的文件内容一致）
Message Message::from_json(const std::string& json) {
    Message msg_obj;
    msg_obj.type = extract_value_robust(json, "type");
    msg_obj.user = extract_value_robust(json, "user");
    msg_obj.msg = extract_value_robust(json, "msg");
    msg_obj.time = extract_value_robust(json, "time");
    return msg_obj;
}

// toString 实现（与您提供的文件内容一致）
std::string Message::toString() const {
    const int MAX_BUFFER_SIZE = 5 * 1024;
    char* buffer = new char[MAX_BUFFER_SIZE];

    int required_size = snprintf(buffer, MAX_BUFFER_SIZE,
        "{\"type\":\"%s\",\"user\":\"%s\",\"msg\":\"%s\",\"time\":\"%s\"}",
        type.c_str(), user.c_str(), msg.c_str(), time.c_str());

    std::string result(buffer);
    delete[] buffer;
    return result;
}

// extract_value_robust 实现（与您提供的文件内容一致）
std::string Message::extract_value_robust(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":\"";
    size_t start_pos = json.find(pattern);
    if (start_pos == std::string::npos) {
        return "";
    }

    size_t value_start = start_pos + pattern.length();
    if (value_start >= json.length()) {
        return "";
    }

    size_t value_end = value_start;
    bool in_escape = false;

    for (; value_end < json.length(); ++value_end) {
        char current_char = json[value_end];
        if (in_escape) {
            in_escape = false;
        }
        else if (current_char == '\\') {
            in_escape = true;
        }
        else if (current_char == '\"') {
            break;
        }
    }

    if (value_end == json.length()) {
        return "";
    }

    size_t len = value_end - value_start;
    return json.substr(value_start, len);
}