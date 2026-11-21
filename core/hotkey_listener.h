#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

// 全局热键监听。
//
// 平台差异（与 Python 版的重要区别）：
//   Python 版用 keyboard 库，README 明确写了「需要管理员权限」；
//   Qt 版在 Windows 上改用 RegisterHotKey —— 普通权限即可，
//   也不需要装第三方库。这是移植过程中的一处改进。
//
//   RegisterHotKey 的代价是热键必须是「修饰键 + 主键」或单个 VK 键，
//   且会被系统独占（别的程序再注册同一组合会失败），失败原因会明确回报。
//
// 非 Windows 平台上 available() 返回 false，UI 按「热键不可用」降级。
class HotkeyListener : public QObject {
    Q_OBJECT

public:
    explicit HotkeyListener(QObject *parent = nullptr);
    ~HotkeyListener() override;

    // 是否支持全局热键（当前仅 Windows）
    bool available() const;

    // 注册/重注册热键，内部会先注销旧的。
    // startKey / stopKey 形如 "F6"、"Ctrl+Alt+S"。
    // 失败返回 false，errorOut 给出可直接展示给用户的中文原因。
    bool registerHotkeys(const QString &startKey, const QString &stopKey,
                         QString *errorOut = nullptr);

    // 注销所有已注册的热键
    void unregisterAll();

    // 当前已注册的键名（日志/调试用）
    QStringList registeredKeys() const;

    // 由原生窗口过程调用：hotkeyId 是 WM_HOTKEY 的 wParam。
    // 返回 true 表示已处理（已发出对应信号）。
    bool dispatchHotkeyId(quintptr hotkeyId);

signals:
    void startPressed();
    void stopPressed();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
