#pragma once

#include <string>

std::string get_current_time();

class Message {
public:
    Message();
    // 使用 Getter 避免直接访问私有成员
    Message(const std::string& t, const std::string& u, const std::string& m, const std::string& tm);

    // 修正：统一使用 from_json
    static Message from_json(const std::string& json);

    // Getter 方法
    const std::string& getType() const { return type; }
    const std::string& getUser() const { return user; }
    const std::string& getMsg() const { return msg; }
    const std::string& getTime() const { return time; }

    // 修正：统一使用 toString
    std::string toString() const;

private:
    std::string type;
    std::string user;
    std::string msg;
    std::string time;
    static std::string extract_value_robust(const std::string& json, const std::string& key);
};