#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// 一个点击动作。
//
// type:
//   "mouse"    鼠标 —— button 取 left/right/middle/x1/x2
//   "keyboard" 键盘 —— button 存键名，例如 "F6" / "A" / "Enter"
//   "combo"    组合键 —— keys 存按顺序按下的键名
//
// mode:
//   "click" 单击
//   "hold"  长按，holdDuration 秒；**holdDuration <= 0 表示一直按，直到停止**
struct ClickAction {
    QString type = QStringLiteral("mouse");
    QString button = QStringLiteral("left");
    QString mode = QStringLiteral("click");
    double holdDuration = 0.05;
    double delayAfter = 0.001;
    QStringList keys;

    // 该动作是否为"一直按"（需要调用方记住并跳过后续轮次）。
    bool isHoldForever() const;
};

// 一个配置（一套连点方案）。
struct Profile {
    QString name = QStringLiteral("默认配置");
    QList<ClickAction> actions;
    int interval = 1;      // 毫秒，0 = 最快
    int repeat = 0;        // 0 = 无限
    int loopDelay = 0;     // 每轮额外的延迟，毫秒
    QString startHotkey = QStringLiteral("F6");
    QString stopHotkey = QStringLiteral("F7");
    bool enabled = true;
    QString targetMode = QStringLiteral("foreground"); // foreground / background
    QString targetTitle;   // 目标窗口标题（持久化，HWND 不持久化）
    // 目标进程的可执行文件名，例如 "chrome.exe"。
    //
    // 为什么要连进程名一起存：窗口标题是会变的（浏览器切页面、
    // 记事本打开别的文件都会改标题），只按标题恢复经常找不回来；
    // 而进程名在程序重新启动后依然一致。恢复顺序见
    // MainWindow::selectWindowByHint()。
    QString targetExe;
    double clickX = 0.5;   // 相对客户区的比例 0..1
    double clickY = 0.5;
};

// 全局设置。
struct AppSettings {
    bool minimizeToTray = false;
    bool startMinimized = false;
    bool highPrecision = true;
    QString theme = QStringLiteral("auto"); // dark / light / auto
};

// 整份配置。
struct AppConfig {
    QList<Profile> profiles;
    int activeProfile = 0;
    AppSettings settings;
};

// 配置读写。文件位置：<数据目录>/config/config.json
//
// 铁律：**target_hwnd 绝不落盘**。HWND 每次开机都会变，写进配置后下次启动
// 会拿到指向别的窗口（或已失效）的陈旧句柄。持久化只用 targetTitle，
// 启动时按标题重新查找。
class ConfigManager {
public:
    ConfigManager();

    // 从磁盘读取；文件缺失或损坏时回落到默认配置。
    void load();

    // 写回磁盘。内部会先剔除运行时字段。
    bool save();

    // 重置为默认配置（不自动保存）。
    void reset();

    AppConfig &config() { return m_config; }
    const AppConfig &config() const { return m_config; }

    QList<Profile> &profiles() { return m_config.profiles; }
    const QList<Profile> &profiles() const { return m_config.profiles; }

    // 当前激活的配置。越界时返回第一个，空列表时返回一个静态默认值。
    Profile &activeProfile();
    const Profile &activeProfile() const;

    void setActiveProfile(int index);

    AppSettings &settings() { return m_config.settings; }
    const AppSettings &settings() const { return m_config.settings; }

    static QString configDir();
    static QString configFile();

    // 默认配置结构（单个"默认配置"、一个左键单击动作）。
    static AppConfig makeDefault();

private:
    AppConfig m_config;
};
