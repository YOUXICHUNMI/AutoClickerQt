#pragma once

#include <QDialog>
#include <QLineEdit>

#include "utils/config_manager.h"

class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QPushButton;
class QStackedWidget;

// 按键捕获输入框。
//
// 点击后按下任意键即可绑定，支持 Ctrl/Alt/Shift/Win 组合。
// 与 Python 版 ui/action_editor.py 的 KeyCaptureEdit 行为一致。
class KeyCaptureEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit KeyCaptureEdit(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    // 把 Qt 键码 + 修饰键转成配置里使用的键名（如 "F6"、"Ctrl+S"）
    QString keyToName(int key, Qt::KeyboardModifiers modifiers, const QString &text) const;
};

// 动作编辑对话框：添加/修改一个点击动作。
class ActionEditorDialog : public QDialog {
    Q_OBJECT

public:
    // action 为空表示新增，否则是编辑已有动作
    explicit ActionEditorDialog(const ClickAction &action = ClickAction(),
                                QWidget *parent = nullptr);

    // 用户点确定后的结果；点取消时应忽略
    ClickAction result() const { return m_result; }

private slots:
    void onTypeChanged(int index);
    void onModeChanged(int index);
    void onAddComboKey();
    void onRemoveComboKey();
    void onAccept();

private:
    void buildUi();
    void loadAction(const ClickAction &action);

    ClickAction m_result;
    bool m_accepted = false;

    QComboBox *m_typeCombo = nullptr;
    QStackedWidget *m_pages = nullptr;

    // 鼠标页
    QComboBox *m_mouseButtonCombo = nullptr;

    // 键盘页
    KeyCaptureEdit *m_keyEdit = nullptr;

    // 组合键页
    KeyCaptureEdit *m_comboKeyEdit = nullptr;
    QListWidget *m_comboList = nullptr;
    QPushButton *m_addComboBtn = nullptr;
    QPushButton *m_removeComboBtn = nullptr;

    // 模式区
    QComboBox *m_modeCombo = nullptr;
    QDoubleSpinBox *m_holdSpin = nullptr;
    QDoubleSpinBox *m_delaySpin = nullptr;
};
