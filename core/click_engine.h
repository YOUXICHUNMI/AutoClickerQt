#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>

#include "utils/config_manager.h"

class QThread;

// 连点引擎：Win32 输入模拟 + 循环调度。
//
// 两种模式：
//   foreground —— SendInput 合成到系统输入队列，全局生效，会抢焦点
//   background —— PostMessage 直接投给目标 HWND，不抢焦点，可最小化
//
// 后台模式的两个关键点（原版踩坑换来的，改不得）：
//   1. 点击前必须补一条 WM_MOUSEMOVE，否则网页/游戏 UI 的 hover 状态不更新，
//      会"点了没反应"
//   2. 键盘只发 KEYDOWN/KEYUP，**绝不补 WM_CHAR** —— 目标消息循环的
//      TranslateMessage 会自动派生 WM_CHAR，手动再补一条会让字符翻倍
namespace ClickEngineApi {

// 窗口枚举：返回 (hwnd, 标题, 进程名)
struct WindowInfo {
    quintptr hwnd = 0;
    QString title;
    QString procName;
};

// 进程分组：某个 (pid, 进程名) 下的所有窗口
struct ProcessGroup {
    quint32 pid = 0;
    QString name;
    QList<QPair<quintptr, QString>> windows; // (hwnd, title)
};

// 枚举所有带标题的顶层窗口。
QList<WindowInfo> enumWindowsDetail();

// 按 (pid, 进程名) 分组返回进程列表，有窗口的排在前面。
QList<ProcessGroup> getProcessWindowList();

// 按标题查找窗口：先精确匹配，再包含匹配。找不到返回 0。
quintptr findWindowByTitle(const QString &title);

QString windowTitle(quintptr hwnd);
bool isWindow(quintptr hwnd);
bool isMinimized(quintptr hwnd);
void restoreWindow(quintptr hwnd);

// 客户区尺寸。客户区为 0（最小化）时用窗口正常位置估算。
QPair<int, int> clientSize(quintptr hwnd);

// 光标下窗口及其客户区坐标。无窗口时返回 (0, 0, 0)。
struct CursorTarget {
    quintptr hwnd = 0;
    int x = 0;
    int y = 0;
};
CursorTarget windowUnderCursor();

bool isMouseButtonDown(int vk = 0x01 /*VK_LBUTTON*/);

// 键名 → 虚拟键码。找不到返回 0。
int keyNameToVk(const QString &name);

// 虚拟键码 → 显示名。找不到返回空串。
QString vkToKeyName(int vk);

// 全部支持的键名 → vk 映射（供 UI 校验用）。
const QHash<QString, int> &vkMap();

} // namespace ClickEngineApi

// 连点引擎本体。跑在独立线程里，通过回调把进度通知给 UI。
class ClickEngine {
public:
    // index >= 0：即将执行第 index 个动作；index < 0：循环结束
    using ActionCallback = std::function<void(int)>;

    ClickEngine();
    ~ClickEngine();

    ClickEngine(const ClickEngine &) = delete;
    ClickEngine &operator=(const ClickEngine &) = delete;

    bool isRunning() const { return m_running.load(); }

    void setActionCallback(ActionCallback cb) { m_actionCallback = std::move(cb); };

    // 启动。profile 会被拷贝一份，调用方之后改原对象不影响运行。
    // targetHwnd：UI 已经解析好的目标窗口句柄。传 0 则内部按 targetTitle 查找。
    //   为什么需要这个参数：UI 侧可能刚刷新过窗口列表、持有更新的句柄，
    //   而按标题查找在多窗口同名时会拿错 —— 优先信 UI 给的。
    // 返回 false 表示未能启动（平台不支持 / 无动作 / 目标窗口找不到）。
    bool start(const Profile &profile, QString *errorOut = nullptr,
               quintptr targetHwnd = 0);

    // 请求停止（异步，线程会在下一个检查点退出）。
    void stop();

    // 等待线程真正结束（析构与退出时调用）。
    void waitForStop(int timeoutMs = 3000);

private:
    void runLoop();
    bool executeAction(const ClickAction &action);
    void releaseHeldKeys();
    void doMouse(const QString &button, bool down);
    void doKeyboard(int vk, bool down);
    void preciseSleep(double seconds);

    void postMouse(quintptr hwnd, const QString &button, bool down, double relX, double relY);
    void postKeyboard(quintptr hwnd, int vk, bool down);
    void sendMouse(const QString &button, bool down);
    void sendKeyboard(int vk, bool down);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};

    // 引擎线程用 QThread 而不是 std::thread。
    //
    // 为什么（实测踩过）：用 std::thread 时，程序退出会在日志里留下
    //   QThreadStorage: entry 0 destroyed before end of thread 0x...
    //   QThreadStorage: entry 1 destroyed before end of thread 0x...
    // std::thread 创建的线程不是 Qt 的线程，Qt 的线程局部存储（QThreadStorage，
    // QThreadData 内部就在用）没有机会在这个线程真正结束前清理。改用 QThread
    // （QThread::create 工厂，不需要额外 moc 子类）后由 Qt 自己管线程生命周期，
    // 线程局部存储会被正确析构，警告消失。
    QThread *m_thread = nullptr;
    ActionCallback m_actionCallback;

    Profile m_profile;
    quintptr m_targetHwnd = 0;
    bool m_background = false;
    double m_clickX = 0.5;
    double m_clickY = 0.5;

    // "一直按"状态下按住不放的键，停止时需要全部松开
    QStringList m_heldButtons; // 鼠标键名：left/right/middle/x1/x2
    QVector<int> m_heldKeys;   // 键盘虚拟键码
};
