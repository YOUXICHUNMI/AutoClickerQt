#include "ui/action_editor.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

// ============================================================================
// KeyCaptureEdit
// ============================================================================

KeyCaptureEdit::KeyCaptureEdit(QWidget *parent) : QLineEdit(parent) {
    setPlaceholderText(QStringLiteral("点击此处后按下按键..."));
    setReadOnly(false);
}

void KeyCaptureEdit::keyPressEvent(QKeyEvent *event) {
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();
    const QString text = event->text();

    // 单独按下修饰键 -> 直接填入修饰键名
    // （原版用 keyboard 库的小写名 ctrl/shift/alt/win，配置里保持同一套写法）
    switch (key) {
    case Qt::Key_Control:
        setText(QStringLiteral("ctrl"));
        return;
    case Qt::Key_Shift:
        setText(QStringLiteral("shift"));
        return;
    case Qt::Key_Alt:
        setText(QStringLiteral("alt"));
        return;
    case Qt::Key_Meta:
        setText(QStringLiteral("win"));
        return;
    default:
        break;
    }

    // 忽略锁定类按键：按下它们不影响要绑定的键
    if (key == Qt::Key_CapsLock || key == Qt::Key_NumLock || key == Qt::Key_ScrollLock) {
        return;
    }

    QStringList modParts;
    if (mods & Qt::ControlModifier) modParts << QStringLiteral("ctrl");
    if (mods & Qt::AltModifier) modParts << QStringLiteral("alt");
    if (mods & Qt::ShiftModifier) modParts << QStringLiteral("shift");
    if (mods & Qt::MetaModifier) modParts << QStringLiteral("win");

    const QString keyName = keyToName(key, mods, text);
    if (keyName.isEmpty()) {
        return;
    }

    if (modParts.isEmpty()) {
        setText(keyName);
    } else {
        modParts << keyName;
        setText(modParts.join(QLatin1Char('+')));
    }
}

QString KeyCaptureEdit::keyToName(int key, Qt::KeyboardModifiers modifiers,
                                  const QString &text) const {
    // 功能键 F1..F16
    if (key >= Qt::Key_F1 && key <= Qt::Key_F16) {
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    }

    // 主键盘数字 0..9（小键盘另有 Num0..Num9）
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        if (modifiers & Qt::KeypadModifier) {
            return QStringLiteral("Num%1").arg(key - Qt::Key_0);
        }
        return QString::number(key - Qt::Key_0);
    }

    // 字母 A..Z
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QChar(static_cast<char16_t>('A' + (key - Qt::Key_A)));
    }

    // 特殊键
    switch (key) {
    case Qt::Key_Space:
        return QStringLiteral("Space");
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QStringLiteral("Enter");
    case Qt::Key_Tab:
        return QStringLiteral("Tab");
    case Qt::Key_Escape:
        return QStringLiteral("Escape");
    case Qt::Key_Backspace:
        return QStringLiteral("Backspace");
    case Qt::Key_Delete:
        return QStringLiteral("Delete");
    case Qt::Key_Insert:
        return QStringLiteral("Insert");
    case Qt::Key_Home:
        return QStringLiteral("Home");
    case Qt::Key_End:
        return QStringLiteral("End");
    case Qt::Key_PageUp:
        return QStringLiteral("PageUp");
    case Qt::Key_PageDown:
        return QStringLiteral("PageDown");
    case Qt::Key_Up:
        return QStringLiteral("Up");
    case Qt::Key_Down:
        return QStringLiteral("Down");
    case Qt::Key_Left:
        return QStringLiteral("Left");
    case Qt::Key_Right:
        return QStringLiteral("Right");
    case Qt::Key_CapsLock:
        return QStringLiteral("CapsLock");
    case Qt::Key_NumLock:
        return QStringLiteral("NumLock");
    case Qt::Key_ScrollLock:
        return QStringLiteral("ScrollLock");
    case Qt::Key_Print:
        return QStringLiteral("PrintScreen");
    case Qt::Key_Pause:
        return QStringLiteral("Pause");
    default:
        break;
    }

    // 符号键：取输入字符并转大写（与 Python 版 str.upper() 行为一致）
    if (!text.isEmpty()) {
        const QChar ch = text.at(0);
        if (ch.isPrint()) {
            return QString(ch).toUpper();
        }
    }
    return QString();
}

// ============================================================================
// ActionEditorDialog
// ============================================================================

ActionEditorDialog::ActionEditorDialog(const ClickAction &action, QWidget *parent)
    : QDialog(parent), m_result(action) {
    setWindowTitle(QStringLiteral("编辑动作"));
    setMinimumWidth(420);
    buildUi();
    loadAction(action);
}

void ActionEditorDialog::buildUi() {
    auto *layout = new QVBoxLayout(this);

    // --- 动作类型 ---
    auto *typeRow = new QHBoxLayout();
    typeRow->addWidget(new QLabel(QStringLiteral("动作类型:"), this));
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({QStringLiteral("鼠标点击"), QStringLiteral("键盘按键"),
                           QStringLiteral("组合键")});
    connect(m_typeCombo, &QComboBox::currentIndexChanged,
            this, &ActionEditorDialog::onTypeChanged);
    typeRow->addWidget(m_typeCombo);
    typeRow->addStretch(1);
    layout->addLayout(typeRow);

    // --- 堆叠区域：按类型显示不同设置 ---
    m_pages = new QStackedWidget(this);

    // 鼠标页
    auto *mousePage = new QWidget(m_pages);
    auto *mouseForm = new QFormLayout(mousePage);
    m_mouseButtonCombo = new QComboBox(mousePage);
    m_mouseButtonCombo->addItems({QStringLiteral("left"), QStringLiteral("right"),
                                  QStringLiteral("middle"), QStringLiteral("x1"),
                                  QStringLiteral("x2")});
    mouseForm->addRow(QStringLiteral("鼠标按钮:"), m_mouseButtonCombo);
    m_pages->addWidget(mousePage);

    // 键盘页
    auto *kbPage = new QWidget(m_pages);
    auto *kbForm = new QFormLayout(kbPage);
    m_keyEdit = new KeyCaptureEdit(kbPage);
    kbForm->addRow(QStringLiteral("按键:"), m_keyEdit);
    auto *hint = new QLabel(QStringLiteral("提示: 点击输入框后直接按下要绑定的按键"), kbPage);
    hint->setStyleSheet(QStringLiteral("color: #6c7086; font-size: 11px;"));
    kbForm->addRow(hint);
    m_pages->addWidget(kbPage);

    // 组合键页
    auto *comboPage = new QWidget(m_pages);
    auto *comboLayout = new QVBoxLayout(comboPage);
    auto *comboForm = new QFormLayout();
    m_comboKeyEdit = new KeyCaptureEdit(comboPage);
    comboForm->addRow(QStringLiteral("添加按键:"), m_comboKeyEdit);
    comboLayout->addLayout(comboForm);

    auto *addRow = new QHBoxLayout();
    m_addComboBtn = new QPushButton(QStringLiteral("+ 添加到组合"), comboPage);
    connect(m_addComboBtn, &QPushButton::clicked, this, &ActionEditorDialog::onAddComboKey);
    addRow->addWidget(m_addComboBtn);
    addRow->addStretch(1);
    comboLayout->addLayout(addRow);

    comboLayout->addWidget(new QLabel(QStringLiteral("组合键序列 (按下顺序):"), comboPage));
    m_comboList = new QListWidget(comboPage);
    m_comboList->setMinimumHeight(100);
    comboLayout->addWidget(m_comboList);

    auto *delRow = new QHBoxLayout();
    m_removeComboBtn = new QPushButton(QStringLiteral("- 删除选中"), comboPage);
    connect(m_removeComboBtn, &QPushButton::clicked, this, &ActionEditorDialog::onRemoveComboKey);
    delRow->addWidget(m_removeComboBtn);
    delRow->addStretch(1);
    comboLayout->addLayout(delRow);
    m_pages->addWidget(comboPage);

    layout->addWidget(m_pages);

    // --- 点击模式 ---
    auto *modeGroup = new QGroupBox(QStringLiteral("点击模式"), this);
    auto *modeForm = new QFormLayout(modeGroup);
    m_modeCombo = new QComboBox(modeGroup);
    m_modeCombo->addItems({QStringLiteral("单击 (Click)"), QStringLiteral("长按 (Hold)")});
    connect(m_modeCombo, &QComboBox::currentIndexChanged,
            this, &ActionEditorDialog::onModeChanged);
    modeForm->addRow(QStringLiteral("模式:"), m_modeCombo);

    m_holdSpin = new QDoubleSpinBox(modeGroup);
    m_holdSpin->setRange(0.0, 10.0);
    m_holdSpin->setSingleStep(0.01);
    m_holdSpin->setSuffix(QStringLiteral(" 秒"));
    m_holdSpin->setDecimals(3);
    m_holdSpin->setSpecialValueText(QStringLiteral("一直按 (0s)"));
    modeForm->addRow(QStringLiteral("长按时长:"), m_holdSpin);

    m_delaySpin = new QDoubleSpinBox(modeGroup);
    m_delaySpin->setRange(0.0, 10.0);
    m_delaySpin->setSingleStep(0.001);
    m_delaySpin->setSuffix(QStringLiteral(" 秒"));
    m_delaySpin->setDecimals(3);
    modeForm->addRow(QStringLiteral("动作后延迟:"), m_delaySpin);
    layout->addWidget(modeGroup);

    // --- 按钮 ---
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    auto *okBtn = new QPushButton(QStringLiteral("确定"), this);
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, &ActionEditorDialog::onAccept);
    auto *cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(okBtn);
    btnRow->addWidget(cancelBtn);
    layout->addLayout(btnRow);
}

void ActionEditorDialog::loadAction(const ClickAction &action) {
    if (action.type == QLatin1String("keyboard")) {
        m_typeCombo->setCurrentIndex(1);
        m_keyEdit->setText(action.button);
    } else if (action.type == QLatin1String("combo")) {
        m_typeCombo->setCurrentIndex(2);
        for (const QString &k : action.keys) {
            m_comboList->addItem(k);
        }
    } else {
        m_typeCombo->setCurrentIndex(0);
        m_mouseButtonCombo->setCurrentText(action.button);
    }

    m_modeCombo->setCurrentIndex(action.mode == QLatin1String("hold") ? 1 : 0);
    m_holdSpin->setValue(action.holdDuration);
    m_delaySpin->setValue(action.delayAfter);
    onModeChanged(m_modeCombo->currentIndex());
}

void ActionEditorDialog::onTypeChanged(int index) {
    m_pages->setCurrentIndex(index);
}

void ActionEditorDialog::onModeChanged(int index) {
    const bool isHold = (index == 1);
    // 两个字段始终保持可见，只是非当前模式的灰显禁用
    m_holdSpin->setEnabled(isHold);
    m_delaySpin->setEnabled(!isHold);
}

void ActionEditorDialog::onAddComboKey() {
    const QString key = m_comboKeyEdit->text().trimmed();
    if (key.isEmpty()) {
        return;
    }
    m_comboList->addItem(key);
    m_comboKeyEdit->clear();
}

void ActionEditorDialog::onRemoveComboKey() {
    const int row = m_comboList->currentRow();
    if (row >= 0) {
        delete m_comboList->takeItem(row);
    }
}

void ActionEditorDialog::onAccept() {
    const int typeIndex = m_typeCombo->currentIndex();

    ClickAction out;
    out.mode = (m_modeCombo->currentIndex() == 1) ? QStringLiteral("hold")
                                                  : QStringLiteral("click");
    // 两个时长都保留原值，只更新当前模式对应的那个 ——
    // 这样用户在两种模式间来回切换时，之前填的数字不会丢
    if (out.mode == QLatin1String("hold")) {
        out.holdDuration = m_holdSpin->value();
        out.delayAfter = m_delaySpin->value();
    } else {
        out.delayAfter = m_delaySpin->value();
        out.holdDuration = m_holdSpin->value();
    }

    if (typeIndex == 0) {
        out.type = QStringLiteral("mouse");
        out.button = m_mouseButtonCombo->currentText();
    } else if (typeIndex == 1) {
        const QString key = m_keyEdit->text().trimmed();
        if (key.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请设置按键"));
            return;
        }
        out.type = QStringLiteral("keyboard");
        out.button = key;
    } else {
        QStringList keys;
        for (int i = 0; i < m_comboList->count(); ++i) {
            keys << m_comboList->item(i)->text();
        }
        if (keys.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("请至少添加一个按键到组合键"));
            return;
        }
        out.type = QStringLiteral("combo");
        out.keys = keys;
    }

    m_result = out;
    m_accepted = true;
    accept();
}
