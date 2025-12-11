
// Copyright (c) 2024 Lei Meng. All rights reserved.

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <fstream>
#include <ctime>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ==================== 工具系统 ====================

class Tool {
public:
    std::string name;
    std::string description;
    json input_schema;
    std::function<std::string(const json&)> executor;

    json to_json() const
    {
        return {
            {"name", name},
            {"description", description},
            {"input_schema", input_schema}
        };
    }
};

class ToolRegistry {
private:
    std::map<std::string, Tool> tools;

public:
    void register_tool(const Tool& tool)
    {
        tools[tool.name] = tool;
        std::cout << "✓ 注册工具: " << tool.name << std::endl;
    }

    std::string execute(const std::string& tool_name, const json& params)
    {
        auto it = tools.find(tool_name);
        if (it != tools.end()) {
            try {
                return it->second.executor(params);
            } catch (const std::exception& e) {
                return std::string("Error: ") + e.what();
            }
        }
        return "Error: Tool '" + tool_name + "' not found";
    }

    json get_tool_definitions() const
    {
        json result = json::array();
        for (const auto& [name, tool] : tools) {
            result.push_back(tool.to_json());
        }
        return result;
    }

    bool has_tools() const
    {
        return !tools.empty();
    }
};

// ==================== Agent 核心 ====================

class Agent {
private:
    std::string api_key;
    std::string model;
    ToolRegistry tool_registry;
    json conversation_history;
    int max_iterations;

    json call_api(const json& messages, const json& tools)
    {
        json request = {
            {"model", model},
            {"max_tokens", 4096},
            {"messages", messages}
        };

        if (!tools.empty()) {
            request["tools"] = tools;
        }

        // 使用 CPR 发送请求
        auto response = cpr::Post(
            cpr::Url{"https://api.anthropic.com/v1/messages"},
            cpr::Header{
                {"x-api-key", api_key},
                {"anthropic-version", "2023-06-01"},
                {"content-type", "application/json"}
            },
            cpr::Body{request.dump()}
        );

        if (response.status_code != 200) {
            std::cerr << "❌ API 错误 " << response.status_code 
                      << ": " << response.text << std::endl;
            return json::object();
        }

        return json::parse(response.text);
    }

    bool process_response(const json& response, int iteration)
    {
        if (response.empty() || !response.contains("content")) {
            std::cerr << "❌ 无效的响应" << std::endl;
            return false;
        }

        json assistant_content = json::array();
        bool has_tool_use = false;

        for (const auto& block : response["content"]) {
            assistant_content.push_back(block);

            if (block["type"] == "text") {
                std::cout << "\n🤖 Claude: " << block["text"] << std::endl;
            }
            else if (block["type"] == "tool_use") {
                has_tool_use = true;
                std::string tool_name = block["name"];
                std::string tool_id = block["id"];
                json tool_input = block["input"];

                std::cout << "\n🔧 调用工具: " << tool_name << std::endl;
                std::cout << "   参数: " << tool_input.dump(2) << std::endl;

                // 执行工具
                std::string result = tool_registry.execute(tool_name, tool_input);
                std::cout << "   结果: " << result << std::endl;

                // 添加 assistant 消息（包含 tool_use）
                conversation_history.push_back({
                    {"role", "assistant"},
                    {"content", assistant_content}
                });

                // 添加工具结果
                conversation_history.push_back({
                    {"role", "user"},
                    {"content", json::array({
                        {
                            {"type", "tool_result"},
                            {"tool_use_id", tool_id},
                            {"content", result}
                        }
                    })}
                });

                // 继续下一轮（让 Claude 处理工具结果）
                if (iteration < max_iterations) {
                    return run_iteration(iteration + 1);
                } else {
                    std::cout << "\n⚠️  达到最大迭代次数 " << max_iterations << std::endl;
                    return false;
                }
            }
        }

        // 如果没有工具调用，添加普通响应
        if (!has_tool_use) {
            conversation_history.push_back({
                {"role", "assistant"},
                {"content", assistant_content}
            });
        }

        return true;
    }

    bool run_iteration(int iteration = 1)
    {
        std::cout << "\n--- 迭代 " << iteration << " ---" << std::endl;

        json tools = tool_registry.has_tools() 
            ? tool_registry.get_tool_definitions() 
            : json::array();

        json response = call_api(conversation_history, tools);

        if (response.empty()) {
            return false;
        }

        return process_response(response, iteration);
    }

public:
    Agent(const std::string& key, 
          const std::string& model_name = "claude-sonnet-4-20250514",
          int max_iter = 10) 
        : api_key(key), model(model_name), max_iterations(max_iter),
          conversation_history(json::array())
    {
        std::cout << "🚀 Agent 初始化完成 (model: " << model << ")" << std::endl;
    }

    void register_tool(const Tool& tool)
    {
        tool_registry.register_tool(tool);
    }

    void run(const std::string& user_input)
    {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "👤 用户: " << user_input << std::endl;

        conversation_history.push_back({
            {"role", "user"},
            {"content", user_input}
        });

        run_iteration(1);

        std::cout << std::string(60, '=') << "\n" << std::endl;
    }

    void reset()
    {
        conversation_history = json::array();
        std::cout << "🔄 对话历史已清空" << std::endl;
    }

    void print_history() const
    {
        std::cout << "\n📜 对话历史:" << std::endl;
        for (const auto& msg : conversation_history) {
            std::cout << "  " << msg["role"] << ": ";
            if (msg["content"].is_string()) {
                std::cout << msg["content"] << std::endl;
            } else if (msg["content"].is_array()) {
                for (const auto& block : msg["content"]) {
                    if (block["type"] == "text") {
                        std::cout << block["text"] << " ";
                    } else if (block["type"] == "tool_use") {
                        std::cout << "[Tool: " << block["name"] << "] ";
                    }
                }
                std::cout << std::endl;
            }
        }
    }
};

// ==================== 工具实现 ====================

// 获取当前时间
std::string get_current_time(const json& params)
{
    std::time_t now = std::time(nullptr);
    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return std::string(buf);
}

// 读取文件
std::string read_file(const json& params)
{
    std::string path = params["path"];
    std::ifstream file(path);

    if (!file.is_open()) {
        return "Error: 无法打开文件 '" + path + "'";
    }

    std::string content;
    std::string line;
    while (std::getline(file, line)) {
        content += line + "\n";
    }

    return content;
}

// 写入文件
std::string write_file(const json& params)
{
    std::string path = params["path"];
    std::string content = params["content"];

    std::ofstream file(path);
    if (!file.is_open()) {
        return "Error: 无法创建文件 '" + path + "'";
    }

    file << content;
    file.close();

    return "成功写入 " + std::to_string(content.length()) + " 字节到 '" + path + "'";
}

// 执行 shell 命令
std::string execute_command(const json& params)
{
    std::string command = params["command"];

    std::cout<< "cmd: " << command << std::endl;

    // 安全检查（简单示例）
    if (command.find("rm -rf") != std::string::npos ||
        command.find("mkfs") != std::string::npos) {
        return "Error: 危险命令被拒绝";
    }

    std::array<char, 128> buffer;
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return "Error: 无法执行命令";
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    int status = pclose(pipe);

    if (status != 0) {
        result += "\n[退出码: " + std::to_string(status) + "]";
    }

    return result;
}

// 简单计算器
std::string calculate(const json& params)
{
    std::string expression = params["expression"];

    // 这里应该用安全的表达式解析器
    // 作为演示，我们只支持简单的加减乘除
    std::cout << "   计算表达式: " << expression << std::endl;

    // 实际项目中应该使用 muParser 或类似库
    return "计算结果: 42 (演示用固定值)";
}

// 网络请求工具
std::string http_get(const json& params)
{
    std::string url = params["url"];

    auto response = cpr::Get(cpr::Url{url});

    if (response.status_code == 200) {
        // 限制返回长度
        std::string text = response.text;
        if (text.length() > 1000) {
            text = text.substr(0, 1000) + "...(已截断)";
        }
        return text;
    } else {
        return "Error: HTTP " + std::to_string(response.status_code);
    }
}

// ==================== 主程序 ====================

int main(int argc, char* argv[])
{
    // 从环境变量或命令行读取 API key
    std::string api_key;

    if (argc > 1) {
        api_key = argv[1];
    } else {
        const char* env_key = std::getenv("ANTHROPIC_API_KEY");
        if (env_key) {
            api_key = env_key;
        } else {
            std::cerr << "请设置 ANTHROPIC_API_KEY 环境变量或通过命令行参数提供" << std::endl;
            std::cerr << "用法: " << argv[0] << " <api-key>" << std::endl;
            return 1;
        }
    }

    // 创建 Agent
    Agent agent(api_key);

    // 注册工具
    agent.register_tool({
        "get_time",
        "获取当前系统时间",
        {
            {"type", "object"},
            {"properties", json::object()},
            {"required", json::array()}
        },
        get_current_time
    });

    agent.register_tool({
        "read_file",
        "读取文件内容",
        {
            {"type", "object"},
            {"properties", {
                {"path", {
                    {"type", "string"},
                    {"description", "文件路径"}
                }}
            }},
            {"required", json::array({"path"})}
        },
        read_file
    });

    agent.register_tool({
        "write_file",
        "写入内容到文件",
        {
            {"type", "object"},
            {"properties", {
                {"path", {
                    {"type", "string"},
                    {"description", "文件路径"}
                }},
                {"content", {
                    {"type", "string"},
                    {"description", "要写入的内容"}
                }}
            }},
            {"required", json::array({"path", "content"})}
        },
        write_file
    });

    agent.register_tool({
        "execute_command",
        "执行 shell 命令",
        {
            {"type", "object"},
            {"properties", {
                {"command", {
                    {"type", "string"},
                    {"description", "要执行的 shell 命令"}
                }}
            }},
            {"required", json::array({"command"})}
        },
        execute_command
    });

    agent.register_tool({
        "calculate",
        "计算数学表达式",
        {
            {"type", "object"},
            {"properties", {
                {"expression", {
                    {"type", "string"},
                    {"description", "数学表达式，如 '2+2' 或 '10*5'"}
                }}
            }},
            {"required", json::array({"expression"})}
        },
        calculate
    });

    agent.register_tool({
        "http_get",
        "发送 HTTP GET 请求",
        {
            {"type", "object"},
            {"properties", {
                {"url", {
                    {"type", "string"},
                    {"description", "目标 URL"}
                }}
            }},
            {"required", json::array({"url"})}
        },
        http_get
    });

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "AI Agent 已启动！输入 'quit' 退出，'reset' 清空对话" << std::endl;
    std::cout << std::string(60, '=') << "\n" << std::endl;

    // 交互式对话循环
    std::string input;
    while (true) {
        std::cout << "你: ";
        std::getline(std::cin, input);

        if (input == "quit" || input == "exit") {
            std::cout << "👋 再见！" << std::endl;
            break;
        }

        if (input == "reset") {
            agent.reset();
            continue;
        }

        if (input == "history") {
            agent.print_history();
            continue;
        }

        if (input.empty()) {
            continue;
        }

        agent.run(input);
    }

    return 0;
}
