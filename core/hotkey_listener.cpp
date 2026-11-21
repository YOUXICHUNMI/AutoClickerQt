#include "core/hotkey_listener.h"

#include "core/click_engine.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QStringList>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

// 日志分类名会成为日志行的 [模块] 字段（见 utils/logger.cpp）
Q_LOGGING_CATEGORY(lcHotkey, "hotkey")

namespace {

#ifdef Q_OS_WIN

struct ModifierEntry {
    const char *name;
    UINT flag;
};

const ModifierEntry kModifiers[] = {
    {"CTRL", MOD_CONTROL},
    {"CONTROL", MOD_CONTROL},
    {"ALT", MOD_ALT},
    {"SHIFT", MOD_SHIFT},
    {"WIN", MOD_WIN},
    {"WINDOWS", MOD_WIN},
    {"META", MOD_WIN},
    {"CMD", MOD_WIN},
};

// 把 "Ctrl+Alt+F6" 解析成 modifier 标志 + 主键 VK。
// 返回 false 表示无法解析（键名不认识、没有主键、或出现多个主键）。
bool parseHotkey(const QString &spec, UINT *modsOut, UINT *vkOut, QString *errOut) {
    const QString trimmed = spec.trimmed();
    if (trimmed.isEmpty()) {
        if (errOut) *errOut = QStringLiteral("热键为空");
        return false;
    }

    const QStringList parts = trimmed.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        if (errOut) *errOut = QStringLiteral("热键格式无效: %1").arg(spec);
        return false;
    }

    UINT mods = 0;
    QString mainKey;
    for (const QString &raw : parts) {
        const QString part = raw.trimmed();
        if (part.isEmpty()) {
            continue;
        }

        const QByteArray upper = part.toUpper().toUtf8();
        bool isModifier = false;
        for (const auto &m : kModifiers) {
            if (upper == m.name) {
                mods |= m.flag;
                isModifier = true;
                break;
            }
        }
        if (isModifier) {
            continue;
        }

        if (!mainKey.isEmpty()) {
            if (errOut) *errOut = QStringLiteral("热键只能有一个主键: %1").arg(spec);
            return false;
        }
        mainKey = part;
    }

    if (mainKey.isEmpty()) {
        if (errOut) *errOut = QStringLiteral("热键缺少主键（只有修饰键）: %1").arg(spec);
        return false;
    }

    const int vk = ClickEngineApi::keyNameToVk(mainKey);
    if (vk <= 0) {
        if (errOut) *errOut = QStringLiteral("无法识别的按键: %1").arg(mainKey);
        return false;
    }

    *modsOut = mods;
    *vkOut = static_cast<UINT>(vk);
    return true;
}

#endif  // Q_OS_WIN

}  // namespace

// ============================================================================
// Impl
// ============================================================================

struct HotkeyListener::Impl {
    bool available = false;

#ifdef Q_OS_WIN
    static constexpr int kStartId = 0xA001;
    static constexpr int kStopId = 0xA002;

    HWND hwnd = nullptr;
    bool startRegistered = false;
    bool stopRegistered = false;
#endif

    QString startKey;
    QString stopKey;
};

#ifdef Q_OS_WIN

namespace {

// 窗口过程需要把消息转给具体的 HotkeyListener 实例。
// 用窗口用户数据存指针，避免引入全局单例。
HotkeyListener *ownerOf(HWND hwnd) {
    return reinterpret_cast<HotkeyListener *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK hotkeyWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    } else if (msg == WM_HOTKEY) {
        HotkeyListener *owner = ownerOf(hwnd);
        if (owner && owner->dispatchHotkeyId(static_cast<quintptr>(wparam))) {
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

#endif  // Q_OS_WIN

HotkeyListener::HotkeyListener(QObject *parent)
    : QObject(parent), d(std::make_unique<Impl>()) {
#ifdef Q_OS_WIN
    HINSTANCE inst = GetModuleHandleW(nullptr);
    static const wchar_t *kClassName = L"AutoClickerQtHotkeyWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = hotkeyWndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;

    if (!RegisterClassExW(&wc)) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            qCWarning(lcHotkey) << "注册热键窗口类失败, GetLastError =" << err;
            return;
        }
    }

    // 消息专用窗口：不依附主窗口，这样主窗口隐藏到托盘时热键依然有效
    d->hwnd = CreateWindowExW(0, kClassName, L"AutoClickerQtHotkey",
                              0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, inst, this);
    if (!d->hwnd) {
        qCWarning(lcHotkey) << "创建热键消息窗口失败, GetLastError =" << GetLastError();
        return;
    }
    d->available = true;
#else
    qCInfo(lcHotkey) << "非 Windows 平台：全局热键不可用";
#endif
}

HotkeyListener::~HotkeyListener() {
    unregisterAll();

#ifdef Q_OS_WIN
    if (d->hwnd) {
        DestroyWindow(d->hwnd);
        d->hwnd = nullptr;
    }
#endif
}

bool HotkeyListener::available() const {
    return d->available;
}

bool HotkeyListener::registerHotkeys(const QString &startKey, const QString &stopKey,
                                     QString *errorOut) {
    unregisterAll();

    if (!d->available) {
        if (errorOut) *errorOut = QStringLiteral("当前平台不支持全局热键");
        return false;
    }

#ifdef Q_OS_WIN
    if (!d->hwnd) {
        if (errorOut) *errorOut = QStringLiteral("热键消息窗口不存在");
        return false;
    }

    UINT startMods = 0, startVk = 0;
    UINT stopMods = 0, stopVk = 0;

    if (!parseHotkey(startKey, &startMods, &startVk, errorOut)) {
        return false;
    }
    if (!parseHotkey(stopKey, &stopMods, &stopVk, errorOut)) {
        return false;
    }

    if (startMods == stopMods && startVk == stopVk) {
        if (errorOut) {
            *errorOut = QStringLiteral("启动热键与停止热键不能相同: %1").arg(startKey);
        }
        return false;
    }

    // MOD_NOREPEAT: 按住不放时不重复触发，避免反复启停
    if (!::RegisterHotKey(d->hwnd, Impl::kStartId, startMods | MOD_NOREPEAT, startVk)) {
        const DWORD err = GetLastError();
        if (errorOut) {
            *errorOut = QStringLiteral("注册启动热键 %1 失败（可能已被其他程序占用，"
                                       "错误码 %2）").arg(startKey).arg(err);
        }
        return false;
    }
    d->startRegistered = true;
    d->startKey = startKey;

    if (!::RegisterHotKey(d->hwnd, Impl::kStopId, stopMods | MOD_NOREPEAT, stopVk)) {
        const DWORD err = GetLastError();
        if (errorOut) {
            *errorOut = QStringLiteral("注册停止热键 %1 失败（可能已被其他程序占用，"
                                       "错误码 %2）").arg(stopKey).arg(err);
        }
        // 撤掉已注册的启动热键，避免半注册状态
        ::UnregisterHotKey(d->hwnd, Impl::kStartId);
        d->startRegistered = false;
        d->startKey.clear();
        return false;
    }
    d->stopRegistered = true;
    d->stopKey = stopKey;

    qCInfo(lcHotkey) << QStringLiteral("热键已注册: 启动=%1, 停止=%2").arg(startKey, stopKey);
    return true;
#else
    Q_UNUSED(startKey)
    Q_UNUSED(stopKey)
    if (errorOut) *errorOut = QStringLiteral("当前平台不支持全局热键");
    return false;
#endif
}

void HotkeyListener::unregisterAll() {
#ifdef Q_OS_WIN
    if (d->hwnd) {
        int count = 0;
        if (d->startRegistered) {
            ::UnregisterHotKey(d->hwnd, Impl::kStartId);
            d->startRegistered = false;
            ++count;
        }
        if (d->stopRegistered) {
            ::UnregisterHotKey(d->hwnd, Impl::kStopId);
            d->stopRegistered = false;
            ++count;
        }
        if (count > 0) {
            qCInfo(lcHotkey) << QStringLiteral("已注销 %1 个热键").arg(count);
        }
    }
#endif
    d->startKey.clear();
    d->stopKey.clear();
}

QStringList HotkeyListener::registeredKeys() const {
    QStringList out;
    if (!d->startKey.isEmpty()) out << d->startKey;
    if (!d->stopKey.isEmpty()) out << d->stopKey;
    return out;
}

bool HotkeyListener::dispatchHotkeyId(quintptr hotkeyId) {
#ifdef Q_OS_WIN
    if (hotkeyId == static_cast<quintptr>(Impl::kStartId)) {
        emit startPressed();
        return true;
    }
    if (hotkeyId == static_cast<quintptr>(Impl::kStopId)) {
        emit stopPressed();
        return true;
    }
#else
    Q_UNUSED(hotkeyId)
#endif
    return false;
}
