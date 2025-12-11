
// Copyright (c) 2024 Lei Meng. All rights reserved.

#include <iostream>
#include <string>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string call_claude(const std::string& api_key, const json& request_body) {
    auto response = cpr::Post(
        cpr::Url{"https://api.anthropic.com/v1/messages"},
        cpr::Header{
            {"x-api-key", api_key},
            {"anthropic-version", "2023-06-01"},
            {"content-type", "application/json"}
        },
        cpr::Body{request_body.dump()}
    );

    if (response.status_code != 200) {
        std::cerr << "API 错误: " << response.status_code << std::endl;
        std::cerr << response.text << std::endl;
        return "";
    }

    return response.text;
}

int main() {
    const char* api_key = std::getenv("ANTHROPIC_API_KEY");
    if (!api_key) {
        std::cerr << "请设置 ANTHROPIC_API_KEY 环境变量或通过命令行参数提供" << std::endl;
        return 1;
    }

    // 构造请求
    json request = {
        {"model", "claude-sonnet-4-20250514"},
        {"max_tokens", 1024},
        {"messages", json::array({
            {{"role", "user"}, {"content", "Hello, Claude!"}}
        })}
    };

    // 调用 API
    std::string response_str = call_claude(api_key, request);
    json response = json::parse(response_str);

    // 输出结果
    //std::cout << "Claude: " << response["content"][0]["text"] << std::endl;
    std::cout << response.dump(4);

    return 0;
}

