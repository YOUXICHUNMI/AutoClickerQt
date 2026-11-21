#pragma once

#include <QString>
#include <QtMessageHandler>

// 日志：按日期分文件写到 <dataDir>/log/app_YYYY-MM-DD.log，同时输出到控制台。
//
// 与 Python 版 utils/logger.py 对齐：
//   - 每行格式：[时间] [级别-7] [模块-14] 消息
//   - 首次调用时安装 Qt 消息处理器，统一接管 qDebug/qWarning/qCritical
//   - 提供 installGlobalHandler() 用于顶层异常兜底
namespace Logger {

// 安装日志系统。可重复调用，内部只安装一次。
void init();

// 关闭日志文件（退出前调用）。
void shutdown();

// 日志文件绝对路径（调试用）。
QString logFilePath();

// 安装全局异常兜底（std::terminate / 未捕获异常）。
void installGlobalHandler();

} // namespace Logger
