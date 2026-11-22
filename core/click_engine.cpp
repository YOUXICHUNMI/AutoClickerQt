#include "click_engine.h"

#include <QDebug>
#include <QDeadlineTimer>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QThread>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <thread>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#  include <tlhelp32.h>
#endif

namespace {

#ifdef Q_OS_WIN

// ---- 常量 ----
constexpr DWORD kInputMouse = 0;
constexpr DWORD kInputKeyboard = 1;

constexpr DWORD kMouseEventLeftDown = 0x0002;
constexpr DWORD kMouseEventLeftUp = 0x0004;
constexpr DWORD kMouseEventRightDown = 0x0008;
constexpr DWORD kMouseEventRightUp = 0x0010;
constexpr DWORD kMouseEventMiddleDown = 0x0020;
constexpr DWORD kMouseEventMiddleUp = 0x0040;
constexpr DWORD kMouseEventXDown = 0x0080;
constexpr DWORD kMouseEventXUp = 0x0100;

constexpr DWORD kKeyEventKeyUp = 0x0002;

constexpr UINT kXButton1 = 0x0001;
constexpr UINT kXButton2 = 0x0002;

// PostMessage 相关
constexpr UINT kWM_MouseMove = 0x0200;
constexpr UINT kWM_LButtonDown = 0x0201;
constexpr UINT kWM_LButtonUp = 0x0202;
constexpr UINT kWM_RButtonDown = 0x0204;
constexpr UINT kWM_RButtonUp = 0x0205;
constexpr UINT kWM_MButtonDown = 0x0207;
constexpr UINT kWM_MButtonUp = 0x0208;
constexpr UINT kWM_XButtonDown = 0x020B;
constexpr UINT kWM_XButtonUp = 0x020C;
constexpr UINT kWM_KeyDown = 0x0100;
constexpr UINT kWM_KeyUp = 0x0101;

constexpr WPARAM kMK_LButton = 0x0001;
constexpr WPARAM kMK_RButton = 0x0002;
constexpr WPARAM kMK_MButton = 0x0010;
constexpr WPARAM kMK_XButton1 = 0x0020;
constexpr WPARAM kMK_XButton2 = 0x0040;

constexpr UINT kGA_Root = 2;
constexpr int kSW_ShowNoActivate = 4;

// ---- 发送结构 ----
struct MouseFlags {
    DWORD down;
    DWORD up;
    DWORD data; // XBUTTON1 / XBUTTON2，普通键为 0
};

MouseFlags mouseFlagsFor(const QString &button) {
    if (button == QLatin1String("right")) {
        return {kMouseEventRightDown, kMouseEventRightUp, 0};
    }
    if (button == QLatin1String("middle")) {
        return {kMouseEventMiddleDown, kMouseEventMiddleUp, 0};
    }
    if (button == QLatin1String("x1")) {
        return {kMouseEventXDown, kMouseEventXUp, kXButton1};
    }
    if (button == QLatin1String("x2")) {
        return {kMouseEventXDown, kMouseEventXUp, kXButton2};
    }
    return {kMouseEventLeftDown, kMouseEventLeftUp, 0};
}

// (DOWN 消息, UP 消息, MK 标志, XBUTTON 标志)
struct PostMouseMsg {
    UINT down;
    UINT up;
    WPARAM mk;
    UINT xbutton;
};

PostMouseMsg postMouseMsgFor(const QString &button) {
    if (button == QLatin1String("right")) {
        return {kWM_RButtonDown, kWM_RButtonUp, kMK_RButton, 0};
    }
    if (button == QLatin1String("middle")) {
        return {kWM_MButtonDown, kWM_MButtonUp, kMK_MButton, 0};
    }
    if (button == QLatin1String("x1")) {
        return {kWM_XButtonDown, kWM_XButtonUp, kMK_XButton1, kXButton1};
    }
    if (button == QLatin1String("x2")) {
        return {kWM_XButtonDown, kWM_XButtonUp, kMK_XButton2, kXButton2};
    }
    return {kWM_LButtonDown, kWM_LButtonUp, kMK_LButton, 0};
}

QString processNameOfWindow(HWND hwnd) {
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return QString();
    }
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        return QString();
    }
    wchar_t buf[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    QString name;
    if (::QueryFullProcessImageNameW(h, 0, buf, &size)) {
        name = QDir::fromNativeSeparators(QString::fromWCharArray(buf, static_cast<int>(size)));
        const int slash = name.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0) {
            name = name.mid(slash + 1);
        }
    }
    ::CloseHandle(h);
    return name;
}

#endif // Q_OS_WIN

// 键名 → 虚拟键码表。与 Python 版 VK_MAP 保持一致。
QHash<QString, int> buildVkMap() {
    QHash<QString, int> m;

    for (char c = 'A'; c <= 'Z'; ++c) {
        m.insert(QString(QChar(c)), static_cast<int>(c));
    }
    for (char c = '0'; c <= '9'; ++c) {
        m.insert(QString(QChar(c)), static_cast<int>(c));
    }
    for (int i = 1; i <= 16; ++i) {
        m.insert(QStringLiteral("F%1").arg(i), 0x70 + (i - 1));
    }

    m.insert(QStringLiteral("Shift"), 0x10);
    m.insert(QStringLiteral("shift"), 0x10);
    m.insert(QStringLiteral("Ctrl"), 0x11);
    m.insert(QStringLiteral("ctrl"), 0x11);
    m.insert(QStringLiteral("Alt"), 0x12);
    m.insert(QStringLiteral("alt"), 0x12);
    m.insert(QStringLiteral("Win"), 0x5B);
    m.insert(QStringLiteral("win"), 0x5B);
    m.insert(QStringLiteral("LShift"), 0xA0);
    m.insert(QStringLiteral("RShift"), 0xA1);
    m.insert(QStringLiteral("LCtrl"), 0xA2);
    m.insert(QStringLiteral("RCtrl"), 0xA3);
    m.insert(QStringLiteral("LAlt"), 0xA4);
    m.insert(QStringLiteral("RAlt"), 0xA5);
    m.insert(QStringLiteral("LWin"), 0x5B);
    m.insert(QStringLiteral("RWin"), 0x5C);

    m.insert(QStringLiteral("Space"), 0x20);
    m.insert(QStringLiteral("Enter"), 0x0D);
    m.insert(QStringLiteral("Return"), 0x0D);
    m.insert(QStringLiteral("Tab"), 0x09);
    m.insert(QStringLiteral("Escape"), 0x1B);
    m.insert(QStringLiteral("Esc"), 0x1B);
    m.insert(QStringLiteral("Backspace"), 0x08);
    m.insert(QStringLiteral("Back"), 0x08);
    m.insert(QStringLiteral("Delete"), 0x2E);
    m.insert(QStringLiteral("Del"), 0x2E);
    m.insert(QStringLiteral("Insert"), 0x2D);
    m.insert(QStringLiteral("Ins"), 0x2D);
    m.insert(QStringLiteral("Home"), 0x24);
    m.insert(QStringLiteral("End"), 0x23);
    m.insert(QStringLiteral("PageUp"), 0x21);
    m.insert(QStringLiteral("PgUp"), 0x21);
    m.insert(QStringLiteral("PageDown"), 0x22);
    m.insert(QStringLiteral("PgDn"), 0x22);

    m.insert(QStringLiteral("Up"), 0x26);
    m.insert(QStringLiteral("Down"), 0x28);
    m.insert(QStringLiteral("Left"), 0x25);
    m.insert(QStringLiteral("Right"), 0x27);

    for (int i = 0; i <= 9; ++i) {
        m.insert(QStringLiteral("Num%1").arg(i), 0x60 + i);
    }
    m.insert(QStringLiteral("NumLock"), 0x90);
    m.insert(QStringLiteral("ScrollLock"), 0x91);

    m.insert(QStringLiteral("Oem3"), 0xC0);
    m.insert(QStringLiteral("OemMinus"), 0xBD);
    m.insert(QStringLiteral("OemPlus"), 0xBB);
    m.insert(QStringLiteral("Oem5"), 0xDC);
    m.insert(QStringLiteral("Oem6"), 0xDD);
    m.insert(QStringLiteral("Oem4"), 0xDB);
    m.insert(QStringLiteral("Oem1"), 0xBA);
    m.insert(QStringLiteral("Oem7"), 0xDE);
    m.insert(QStringLiteral("OemComma"), 0xBC);
    m.insert(QStringLiteral("OemPeriod"), 0xBE);
    m.insert(QStringLiteral("Oem2"), 0xBF);

    m.insert(QStringLiteral("CapsLock"), 0x14);
    m.insert(QStringLiteral("PrintScreen"), 0x2C);
    m.insert(QStringLiteral("PrtSc"), 0x2C);
    m.insert(QStringLiteral("Pause"), 0x13);

    return m;
}

} // namespace

namespace ClickEngineApi {

const QHash<QString, int> &vkMap() {
    static const QHash<QString, int> m = buildVkMap();
    return m;
}

int keyNameToVk(const QString &name) {
    const auto &m = vkMap();

    auto it = m.constFind(name);
    if (it != m.constEnd()) {
        return it.value();
    }

    // 依次尝试首字母大写、全大写（对齐原版行为）
    QString cap = name;
    if (!cap.isEmpty()) {
        cap[0] = cap.at(0).toUpper();
    }
    it = m.constFind(cap);
    if (it != m.constEnd()) {
        return it.value();
    }

    it = m.constFind(name.toUpper());
    if (it != m.constEnd()) {
        return it.value();
    }

    return 0;
}

QString vkToKeyName(int vk) {
    static const QHash<int, QString> reverse = [] {
        QHash<int, QString> r;
        const auto &m = const_cast<const QHash<QString, int> &>(vkMap());
        // 只保留"规范名"（值最小的那个键名），避免 Ctrl/ctrl 之类的别名互相覆盖
        QStringList keys = m.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &k : keys) {
            const int v = m.value(k);
            if (!r.contains(v)) {
                r.insert(v, k);
            }
        }
        return r;
    }();

    return reverse.value(vk);
}

QList<WindowInfo> enumWindowsDetail() {
    QList<WindowInfo> results;
#ifdef Q_OS_WIN
    ::EnumWindows(
        [](HWND hwnd, LPARAM lparam) -> BOOL {
            auto *out = reinterpret_cast<QList<WindowInfo> *>(lparam);
            if (!::IsWindow(hwnd)) {
                return TRUE;
            }
            const int len = ::GetWindowTextLengthW(hwnd);
            if (len <= 0) {
                return TRUE;
            }
            wchar_t buf[512] = {0};
            const int got = ::GetWindowTextW(hwnd, buf, 512);
            if (got <= 0) {
                return TRUE;
            }
            const QString title = QString::fromWCharArray(buf, got).trimmed();
            if (title.isEmpty()) {
                return TRUE;
            }
            WindowInfo info;
            info.hwnd = reinterpret_cast<quintptr>(hwnd);
            info.title = title;
            info.procName = processNameOfWindow(hwnd);
            out->append(info);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&results));
#endif
    return results;
}

QList<ProcessGroup> getProcessWindowList() {
    // 关键：按 (pid, 进程名) 分组，不是按进程名。
    // 只按名字去重会让 Chrome / Edge / msedgewebview2 这类多进程应用
    // 只剩第一个 PID 的窗口，其余全丢。
    QList<WindowInfo> wins = enumWindowsDetail();

    struct Entry {
        quint32 pid;
        QString name;
        quintptr hwnd;
        QString title;
    };
    QList<Entry> entries;
    entries.reserve(wins.size());

    QList<QPair<quint32, QString>> order;
    QHash<QString, QList<QPair<quintptr, QString>>> grouped;

#ifdef Q_OS_WIN
    for (const WindowInfo &w : wins) {
        DWORD pid = 0;
        ::GetWindowThreadProcessId(reinterpret_cast<HWND>(w.hwnd), &pid);
        if (pid == 0) {
            continue;
        }
        const QString name = w.procName.isEmpty() ? QStringLiteral("(unknown)") : w.procName;
        const QPair<quint32, QString> key(static_cast<quint32>(pid), name);
        if (!grouped.contains(QStringLiteral("%1|%2").arg(pid).arg(name))) {
            grouped.insert(QStringLiteral("%1|%2").arg(pid).arg(name), {});
            order.append(key);
        }
        grouped[QStringLiteral("%1|%2").arg(pid).arg(name)].append({w.hwnd, w.title});
    }

    // 补上无窗口的进程：这部分仍按进程名去重，
    // 否则 Chrome 一家就会凭空多出几十条没用的噪音。
    {
        const HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            QSet<QString> seenNameless;
            if (::Process32FirstW(snap, &pe)) {
                do {
                    const QString name = QString::fromWCharArray(pe.szExeFile);
                    bool hasWindow = false;
                    for (const auto &k : order) {
                        if (k.first == static_cast<quint32>(pe.th32ProcessID)) {
                            hasWindow = true;
                            break;
                        }
                    }
                    if (hasWindow) {
                        continue;
                    }
                    if (seenNameless.contains(name)) {
                        continue;
                    }
                    seenNameless.insert(name);
                    order.append({static_cast<quint32>(pe.th32ProcessID), name});
                } while (::Process32NextW(snap, &pe));
            }
            ::CloseHandle(snap);
        }
    }
#endif

    QList<ProcessGroup> result;
    result.reserve(order.size());
    for (const auto &key : order) {
        ProcessGroup g;
        g.pid = key.first;
        g.name = key.second;
        g.windows = grouped.value(QStringLiteral("%1|%2").arg(key.first).arg(key.second));
        result.append(g);
    }

    std::sort(result.begin(), result.end(), [](const ProcessGroup &a, const ProcessGroup &b) {
        const bool aHas = !a.windows.isEmpty();
        const bool bHas = !b.windows.isEmpty();
        if (aHas != bHas) {
            return aHas; // 有窗口的排前面
        }
        const int cmp = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        if (cmp != 0) {
            return cmp < 0;
        }
        return a.pid < b.pid;
    });

    return result;
}

quintptr findWindowByTitle(const QString &title) {
    if (title.isEmpty()) {
        return 0;
    }
    const QList<WindowInfo> wins = enumWindowsDetail();

    // 先精确匹配
    for (const WindowInfo &w : wins) {
        if (w.title == title) {
            return w.hwnd;
        }
    }
    // 再包含匹配
    for (const WindowInfo &w : wins) {
        if (w.title.contains(title, Qt::CaseInsensitive)) {
            return w.hwnd;
        }
    }
    return 0;
}

QString windowTitle(quintptr hwnd) {
#ifdef Q_OS_WIN
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!h) {
        return QString();
    }
    const int len = ::GetWindowTextLengthW(h);
    if (len <= 0) {
        return QString();
    }
    wchar_t *buf = new wchar_t[static_cast<size_t>(len) + 1];
    const int got = ::GetWindowTextW(h, buf, len + 1);
    const QString title = got > 0 ? QString::fromWCharArray(buf, got) : QString();
    delete[] buf;
    return title;
#else
    Q_UNUSED(hwnd);
    return QString();
#endif
}

bool isWindow(quintptr hwnd) {
#ifdef Q_OS_WIN
    return hwnd != 0 && ::IsWindow(reinterpret_cast<HWND>(hwnd)) != 0;
#else
    Q_UNUSED(hwnd);
    return false;
#endif
}

bool isMinimized(quintptr hwnd) {
#ifdef Q_OS_WIN
    return hwnd != 0 && ::IsIconic(reinterpret_cast<HWND>(hwnd)) != 0;
#else
    Q_UNUSED(hwnd);
    return false;
#endif
}

void restoreWindow(quintptr hwnd) {
#ifdef Q_OS_WIN
    if (hwnd != 0) {
        ::ShowWindow(reinterpret_cast<HWND>(hwnd), kSW_ShowNoActivate);
    }
#else
    Q_UNUSED(hwnd);
#endif
}

QPair<int, int> clientSize(quintptr hwnd) {
#ifdef Q_OS_WIN
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!h) {
        return {1, 1};
    }

    RECT rc{};
    // 注意：最小化时 GetClientRect 不一定返回 0！
    // 实测微信最小化后返回 144x20（被压扁的值，不是 0），若只看 w>0 就会
    // 把这个垃圾值当成真实客户区直接返回。所以先用 IsIconic 排除最小化。
    if (!::IsIconic(h) && ::GetClientRect(h, &rc)) {
        const int w = rc.right - rc.left;
        const int hgt = rc.bottom - rc.top;
        if (w > 0 && hgt > 0) {
            return {w, hgt};
        }
    }

    // 客户区为 0（最小化）时需要反推。
    //
    // 实测结论（自建 830x620 窗口，最小化后反推，真实客户区 814x581）：
    //   写死 -16/-46          814x574   纵向差 -7   ← 原版做法，46 应为 39
    //   AdjustWindowRectEx    814x581   精确        ← 正确
    //   DwmGetWindowAttribute 684x592   横向差 -130 ← 看似最准，实则不可用
    //
    // 为什么 DWM 反而最差：DWMWA_EXTENDED_FRAME_BOUNDS 在窗口最小化时返回的是
    // 缩略图/屏幕外那个小矩形（实测 146x28，坐标常为 -32000），不是还原后的尺寸。
    // 而本函数只在 GetClientRect 返回 0（即已最小化）时才走到这里，
    // 所以 DWM 在这里永远拿到的是最小值 —— 它只对可见窗口有意义。
    //
    // 因此：优先 AdjustWindowRectEx，失败才用经验值。
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (::GetWindowPlacement(h, &wp)) {
        const int outerW = static_cast<int>(wp.rcNormalPosition.right - wp.rcNormalPosition.left);
        const int outerH = static_cast<int>(wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
        if (outerW <= 0 || outerH <= 0) {
            return {1, 1};
        }

        RECT adj{0, 0, 0, 0};
        if (::AdjustWindowRectEx(&adj, static_cast<DWORD>(::GetWindowLongW(h, GWL_STYLE)),
                                 FALSE, static_cast<DWORD>(::GetWindowLongW(h, GWL_EXSTYLE)))) {
            const int borderW = static_cast<int>(adj.right - adj.left);
            const int borderH = static_cast<int>(adj.bottom - adj.top);
            // 边框值必须为正且小于外层尺寸，否则结果会变成 1x1 或负数
            if (borderW > 0 && borderH > 0 && borderW < outerW && borderH < outerH) {
                return {outerW - borderW, outerH - borderH};
            }
        }

        // 兜底：带标题栏的普通窗口边框为宽 16、高 39
        return {std::max(1, outerW - 16), std::max(1, outerH - 39)};
    }
#endif
    return {1, 1};
}

CursorTarget windowUnderCursor() {
    CursorTarget t;
#ifdef Q_OS_WIN
    POINT pt{};
    if (!::GetCursorPos(&pt)) {
        return t;
    }
    HWND h = ::WindowFromPoint(pt);
    if (!h) {
        return t;
    }
    const HWND root = ::GetAncestor(h, kGA_Root);
    if (root) {
        h = root;
    }
    POINT client = pt;
    if (!::ScreenToClient(h, &client)) {
        return t;
    }
    t.hwnd = reinterpret_cast<quintptr>(h);
    t.x = client.x;
    t.y = client.y;
#endif
    return t;
}

bool isMouseButtonDown(int vk) {
#ifdef Q_OS_WIN
    return (::GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    Q_UNUSED(vk);
    return false;
#endif
}

} // namespace ClickEngineApi

// ---------------------------------------------------------------- ClickEngine

using namespace ClickEngineApi;

ClickEngine::ClickEngine() = default;

ClickEngine::~ClickEngine() {
    stop();
    waitForStop();
}

bool ClickEngine::start(const Profile &profile, QString *errorOut, quintptr targetHwnd) {
    if (m_running.load()) {
        if (errorOut) {
            *errorOut = QStringLiteral("连点已在运行中");
        }
        return false;
    }

    if (profile.actions.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("请先添加至少一个点击动作");
        }
        return false;
    }

    // 上一次的线程对象可能还留着：引擎跑完（达到重复次数、或全是"一直按"）
    // 只是让 runLoop 自然返回，线程对象本身还在。先收干净再起新的。
    //
    // 这一步是必须的：原来的 std::thread 版本在这里会直接崩溃 ——
    //   terminate called without an active exception
    // 因为对「已经自然结束但还没 join」的 std::thread 做移动赋值，标准要求
    // 调用 std::terminate。用户看到的现象是"连点跑完一轮后再点开始，程序没了"。
    // （已用 A/B 探针实测复现：std::thread 版第 2 轮必崩，QThread 版连跑 4 轮正常）
    //
    // 放在 m_running.store(true) 之前，这样提前 return 不会把状态位留在"运行中"。
    if (m_thread) {
        if (m_thread->isRunning()) {
            m_thread->wait(QDeadlineTimer(3000));
        }
        if (m_thread->isRunning()) {
            // 还没停下来的线程不能删（QThread 析构时会断言/崩溃），放弃这次启动。
            if (errorOut) {
                *errorOut = QStringLiteral("上一次连点还没停下来，请稍后再试");
            }
            return false;
        }
        delete m_thread;
        m_thread = nullptr;
    }

    m_profile = profile;
    m_clickX = qBound(0.0, profile.clickX, 1.0);
    m_clickY = qBound(0.0, profile.clickY, 1.0);
    m_background = (profile.targetMode == QLatin1String("background"));
    m_targetHwnd = 0;

    if (m_background) {
        // 目标窗口解析：优先用 UI 给的句柄（它刚刷新过列表，更可信），
        // 没有再按标题找；都失败就回退前台模式。
        if (targetHwnd != 0 && isWindow(targetHwnd)) {
            m_targetHwnd = targetHwnd;
        } else {
            m_targetHwnd = findWindowByTitle(profile.targetTitle);
        }
        if (m_targetHwnd == 0 || !isWindow(m_targetHwnd)) {
            qWarning().noquote()
                << QStringLiteral("未找到目标窗口: %1，回退到前台模式").arg(profile.targetTitle);
            m_background = false;
            m_targetHwnd = 0;
        } else if (isMinimized(m_targetHwnd)) {
            restoreWindow(m_targetHwnd);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    m_heldButtons.clear();
    m_heldKeys.clear();
    m_stopRequested.store(false);
    m_running.store(true);

    qInfo().noquote() << QStringLiteral("引擎启动 | 模式=%1 | 动作数=%2 | 间隔=%3ms | 重复=%4")
                             .arg(m_background ? QStringLiteral("background") : QStringLiteral("foreground"))
                             .arg(m_profile.actions.size())
                             .arg(m_profile.interval)
                             .arg(m_profile.repeat == 0 ? QStringLiteral("无限")
                                                        : QString::number(m_profile.repeat));

    m_thread = QThread::create([this] { runLoop(); });
    m_thread->setObjectName(QStringLiteral("click-engine"));
    m_thread->start();
    return true;
}

void ClickEngine::stop() {
    if (!m_running.load() && !m_thread) {
        return;
    }
    m_stopRequested.store(true);
    m_running.store(false);
    qInfo().noquote() << QStringLiteral("引擎停止");
}

void ClickEngine::waitForStop(int timeoutMs) {
    if (!m_thread) {
        return;
    }
    // QThread::wait 自带超时（Qt6 用 QDeadlineTimer），这里直接等；循环体里每轮
    // 都会检查 m_stopRequested，所以正常情况下几毫秒就返回。
    if (m_thread->isRunning()) {
        m_thread->wait(QDeadlineTimer(timeoutMs));
    }
    if (!m_thread->isRunning()) {
        delete m_thread;
        m_thread = nullptr;
    }
}

void ClickEngine::runLoop() {
    const int intervalMs = m_profile.interval;
    const int repeat = m_profile.repeat;
    const int loopDelayMs = m_profile.loopDelay;
    const QList<ClickAction> actions = m_profile.actions;
    const int total = static_cast<int>(actions.size());

    const double intervalSec = intervalMs / 1000.0;
    const double loopDelaySec = loopDelayMs / 1000.0;

    m_heldButtons.clear();
    m_heldKeys.clear();

    QVector<bool> heldIndices(total, false);
    int heldCount = 0;
    int count = 0;

    while (!m_stopRequested.load()) {
        for (int i = 0; i < total; ++i) {
            if (m_stopRequested.load()) {
                break;
            }
            if (heldIndices[i]) {
                continue;
            }

            if (m_actionCallback) {
                m_actionCallback(i);
            }

            const ClickAction &a = actions.at(i);
            if (executeAction(a)) {
                heldIndices[i] = true;
                ++heldCount;
            }

            if (a.delayAfter > 0.0) {
                preciseSleep(a.delayAfter);
            }
        }

        if (m_stopRequested.load()) {
            break;
        }

        ++count;
        if (repeat > 0 && count >= repeat) {
            break;
        }

        if (heldCount >= total) {
            // 全部动作都是"一直按"，没有可重复执行的东西了
            preciseSleep(0.05);
            continue;
        }

        if (loopDelaySec > 0.0) {
            preciseSleep(loopDelaySec);
        } else if (intervalSec > 0.0) {
            preciseSleep(intervalSec);
        }
    }

    releaseHeldKeys();
    m_running.store(false);

    if (m_actionCallback) {
        m_actionCallback(-1);
    }
}

bool ClickEngine::executeAction(const ClickAction &action) {
    const QString type = action.type;

    if (type == QLatin1String("mouse")) {
        const QString button = action.button.isEmpty() ? QStringLiteral("left") : action.button;

        if (action.mode == QLatin1String("hold")) {
            if (action.holdDuration <= 0.0) {
                // 一直按：记住它，停止时统一松开
                doMouse(button, true);
                m_heldButtons.append(button);
                return true;
            }
            doMouse(button, true);
            preciseSleep(action.holdDuration);
            doMouse(button, false);
            return false;
        }

        doMouse(button, true);
        doMouse(button, false);
        return false;
    }

    if (type == QLatin1String("keyboard")) {
        const int vk = keyNameToVk(action.button);
        if (vk == 0) {
            qWarning().noquote() << QStringLiteral("未知按键: %1").arg(action.button);
            return false;
        }

        if (action.mode == QLatin1String("hold")) {
            if (action.holdDuration <= 0.0) {
                doKeyboard(vk, true);
                m_heldKeys.append(vk);
                return true;
            }
            doKeyboard(vk, true);
            preciseSleep(action.holdDuration);
            doKeyboard(vk, false);
            return false;
        }

        doKeyboard(vk, true);
        doKeyboard(vk, false);
        return false;
    }

    if (type == QLatin1String("combo")) {
        QVector<int> vks;
        vks.reserve(action.keys.size());
        for (const QString &k : action.keys) {
            const int vk = keyNameToVk(k);
            if (vk != 0) {
                vks.append(vk);
            }
        }
        if (vks.isEmpty()) {
            return false;
        }

        for (int vk : vks) {
            doKeyboard(vk, true);
            preciseSleep(0.001);
        }

        if (action.mode == QLatin1String("hold")) {
            if (action.holdDuration <= 0.0) {
                for (int vk : vks) {
                    m_heldKeys.append(vk);
                }
                return true;
            }
            preciseSleep(action.holdDuration);
        } else {
            preciseSleep(0.01);
        }

        // 反向松开，符合常规的按键顺序习惯
        for (int i = static_cast<int>(vks.size()) - 1; i >= 0; --i) {
            doKeyboard(vks.at(i), false);
            preciseSleep(0.001);
        }
        return false;
    }

    qWarning().noquote() << QStringLiteral("未知动作类型: %1").arg(type);
    return false;
}

void ClickEngine::releaseHeldKeys() {
    for (const QString &btn : m_heldButtons) {
        doMouse(btn, false);
    }
    m_heldButtons.clear();

    for (int vk : m_heldKeys) {
        doKeyboard(vk, false);
    }
    m_heldKeys.clear();
}

void ClickEngine::doMouse(const QString &button, bool down) {
    if (m_background && m_targetHwnd != 0) {
        postMouse(m_targetHwnd, button, down, m_clickX, m_clickY);
    } else {
        sendMouse(button, down);
    }
}

void ClickEngine::doKeyboard(int vk, bool down) {
    if (m_background && m_targetHwnd != 0) {
        postKeyboard(m_targetHwnd, vk, down);
    } else {
        sendKeyboard(vk, down);
    }
}

void ClickEngine::sendMouse(const QString &button, bool down) {
#ifdef Q_OS_WIN
    const MouseFlags f = mouseFlagsFor(button);

    INPUT input{};
    input.type = kInputMouse;
    input.mi.dx = 0;
    input.mi.dy = 0;
    input.mi.mouseData = f.data;
    input.mi.dwFlags = down ? f.down : f.up;
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;

    const UINT sent = ::SendInput(1, &input, sizeof(INPUT));
    if (sent != 1) {
        qWarning().noquote()
            << QStringLiteral("SendInput 失败（鼠标 %1），错误码=%2。"
                              "常见原因：目标窗口以管理员权限运行，而本程序没有。")
                   .arg(button)
                   .arg(::GetLastError());
    }
#else
    Q_UNUSED(button);
    Q_UNUSED(down);
#endif
}

void ClickEngine::sendKeyboard(int vk, bool down) {
#ifdef Q_OS_WIN
    INPUT input{};
    input.type = kInputKeyboard;
    input.ki.wVk = static_cast<WORD>(vk);
    input.ki.wScan = 0;
    input.ki.dwFlags = down ? 0 : kKeyEventKeyUp;
    input.ki.time = 0;
    input.ki.dwExtraInfo = 0;

    const UINT sent = ::SendInput(1, &input, sizeof(INPUT));
    if (sent != 1) {
        qWarning().noquote()
            << QStringLiteral("SendInput 失败（键盘 vk=%1），错误码=%2").arg(vk).arg(::GetLastError());
    }
#else
    Q_UNUSED(vk);
    Q_UNUSED(down);
#endif
}

void ClickEngine::postMouse(quintptr hwnd, const QString &button, bool down, double relX, double relY) {
#ifdef Q_OS_WIN
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!h) {
        return;
    }

    const QPair<int, int> size = clientSize(hwnd);
    const int absX = static_cast<int>(relX * size.first);
    const int absY = static_cast<int>(relY * size.second);
    const LPARAM lparam = MAKELPARAM(absX, absY);

    const PostMouseMsg m = postMouseMsgFor(button);

    // 先补一条 WM_MOUSEMOVE：网页和很多游戏 UI 依赖它的 hover 状态，
    // 只发 DOWN/UP 会出现"点了没反应"。
    ::PostMessageW(h, kWM_MouseMove, 0, lparam);

    WPARAM wparam = down ? m.mk : 0;
    if (m.xbutton != 0 && down) {
        // X 按钮的高字必须填 XBUTTON1/XBUTTON2；
        // 松开时高字必须是 0，这是 Windows 的约定。
        wparam |= (static_cast<WPARAM>(m.xbutton) << 16);
    }

    const UINT msg = down ? m.down : m.up;
    if (!::PostMessageW(h, msg, wparam, lparam)) {
        qWarning().noquote()
            << QStringLiteral("PostMessage 鼠标消息失败（%1），错误码=%2").arg(button).arg(::GetLastError());
    }
#else
    Q_UNUSED(hwnd);
    Q_UNUSED(button);
    Q_UNUSED(down);
    Q_UNUSED(relX);
    Q_UNUSED(relY);
#endif
}

void ClickEngine::postKeyboard(quintptr hwnd, int vk, bool down) {
#ifdef Q_OS_WIN
    HWND h = reinterpret_cast<HWND>(hwnd);
    if (!h) {
        return;
    }

    // 只发 KEYDOWN / KEYUP。绝不补 WM_CHAR：
    // 目标窗口的消息循环里 TranslateMessage 会自动派生 WM_CHAR，
    // 手动再补一条会让字符重复（输入 "a" 得到 "aa"）。
    const UINT msg = down ? kWM_KeyDown : kWM_KeyUp;
    if (!::PostMessageW(h, msg, static_cast<WPARAM>(vk), 0)) {
        qWarning().noquote()
            << QStringLiteral("PostMessage 键盘消息失败（vk=%1），错误码=%2").arg(vk).arg(::GetLastError());
    }
#else
    Q_UNUSED(hwnd);
    Q_UNUSED(vk);
    Q_UNUSED(down);
#endif
}

void ClickEngine::preciseSleep(double seconds) {
    if (seconds <= 0.0) {
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto end = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                 std::chrono::duration<double>(seconds));

    if (seconds < 0.003) {
        // 极短间隔：直接忙等，避免 sleep 的系统调度误差
        while (std::chrono::steady_clock::now() < end) {
            if (m_stopRequested.load()) {
                return;
            }
        }
        return;
    }

    // 较长间隔：先睡一半，再逐步收敛，最后 2ms 忙等
    while (true) {
        if (m_stopRequested.load()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= end) {
            break;
        }
        const double remaining =
            std::chrono::duration<double>(end - now).count();
        if (remaining > 0.002) {
            std::this_thread::sleep_for(std::chrono::duration<double>(remaining * 0.5));
        }
        // 剩余不足 2ms 时不再 sleep，直接进入下一轮循环忙等到期
    }
}
