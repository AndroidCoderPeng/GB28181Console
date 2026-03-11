//
// Created by pengx on 2026/3/10.
//

#include "logger.hpp"
#include <iostream>

// ANSI 颜色码定义
#define COLOR_RESET   "\033[0m"
#define COLOR_DEBUG   "\033[32m"   // 绿色
#define COLOR_INFO    "\033[36m"   // 青色
#define COLOR_WARN    "\033[33m"   // 黄色
#define COLOR_ERROR   "\033[31m"   // 红色

// 获取 LogLevel 对应的颜色码
static const char* levelColor(const LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            return COLOR_DEBUG;
        case LogLevel::INFO:
            return COLOR_INFO;
        case LogLevel::WARN:
            return COLOR_WARN;
        case LogLevel::ERROR:
            return COLOR_ERROR;
        default:
            return COLOR_RESET;
    }
}

void Logger::print_border(const char* left, const char* fill, const char* right, const LogLevel level) const {
    std::string border;
    const char* color = levelColor(level);
    border += color;
    border += left;
    for (int i = 0; i < DEFAULT_WIDTH; ++i) {
        border += fill;
    }
    border += right;
    border += COLOR_RESET;
    log_raw(level, border.c_str());
}

void Logger::log_raw(const LogLevel level, const char* msg) const {
    std::string levelStr;
    const char* color = levelColor(level);
    switch (level) {
        case LogLevel::DEBUG:
            levelStr = "[DEBUG]";
            break;
        case LogLevel::INFO:
            levelStr = "[INFO ]";
            break;
        case LogLevel::WARN:
            levelStr = "[WARN ]";
            break;
        case LogLevel::ERROR:
            levelStr = "[ERROR]";
            break;
    }

    // 格式化 _tag_ptr 的宽度
    const std::string tag_str(_tag_ptr);
    if (tag_str.length() > TAG_MAX_WIDTH) {
        // 超过长度时截断并添加 "..."
        tag_str.substr(0, TAG_MAX_WIDTH - 3) + "...";
    }
    const auto tag = tag_str + std::string(TAG_MAX_WIDTH - tag_str.length(), ' ');
    std::cout << color << levelStr << COLOR_RESET << " [" << tag << "] " << msg << std::endl;
}

void Logger::print_line(const std::string& content, const LogLevel level) const {
    std::string line;
    const char* color = levelColor(level);

    line += color;
    line += V_LINE;
    line += " ";
    line += content;
    line += COLOR_RESET;

    log_raw(level, line.c_str());
}

void Logger::print_box(const LogLevel level, const char* content) const {
    print_border(TOP_LEFT, H_LINE, TOP_RIGHT, level);
    print_line(content, level);
    print_border(BOTTOM_LEFT, H_LINE, BOTTOM_RIGHT, level);
}

// 简单单行边框
void Logger::d(const char* msg) const {
    print_box(LogLevel::DEBUG, msg);
}

void Logger::i(const char* msg) const {
    print_box(LogLevel::INFO, msg);
}

void Logger::w(const char* msg) const {
    print_box(LogLevel::WARN, msg);
}

void Logger::e(const char* msg) const {
    print_box(LogLevel::ERROR, msg);
}

Logger::BoxBuilder& Logger::BoxBuilder::add(const std::string& line) {
    _lines.push_back(line);
    return *this;
}

Logger::BoxBuilder& Logger::BoxBuilder::addBlock(const std::string& content) {
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        _lines.push_back(line);
    }
    return *this;
}

void Logger::BoxBuilder::print() const {
    if (_lines.empty())
        return;

    // 上边框
    _logger.print_border(TOP_LEFT, H_LINE, TOP_RIGHT, _level);

    // 内容行
    const char* color = levelColor(_level);
    for (const auto& line : _lines) {
        std::string output = std::string(color) + V_LINE + " " + line + COLOR_RESET;
        _logger.log_raw(_level, output.c_str());
    }

    // 下边框
    _logger.print_border(BOTTOM_LEFT, H_LINE, BOTTOM_RIGHT, _level);
}

Logger::BoxBuilder Logger::box(LogLevel level) {
    return {*this, level};
}
