#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// 进程选择对话框：列出所有进程及其窗口，支持关键字搜索。
//
// 与原 Python 版 ui/process_picker.py 行为一致：
//   - 有窗口的进程按「进程名 [pid] — 窗口标题」逐条列出，可选中；
//   - 无窗口的进程也列出来（便于确认进程确实在跑），但整行禁用不可选；
//   - 搜索框实时过滤，回车跳到第一个匹配项。
//
// 数据来源是 ClickEngineApi::getProcessWindowList()，
// 它按 (pid, 进程名) 分组 —— 所以 Chrome 这类多进程应用能看到多个 PID。
class ProcessPickerDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProcessPickerDialog(QWidget *parent = nullptr);

    // 选中的窗口句柄；未选择时为 0
    quintptr selectedHwnd() const { return m_selectedHwnd; }

    // 选中的窗口标题
    QString selectedTitle() const { return m_selectedTitle; }

private slots:
    void onSearchChanged(const QString &text);
    void onSearchEnter();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onAccept();

private:
    void loadProcesses();

    quintptr m_selectedHwnd = 0;
    QString m_selectedTitle;

    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_listWidget = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_okBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
};
