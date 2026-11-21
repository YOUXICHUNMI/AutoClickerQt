#include "ui/process_picker.h"

#include "core/click_engine.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLoggingCategory>
#include <QPushButton>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcPicker, "process_picker")

namespace {

// 列表项里存窗口标题的自定义 role。
// 与主窗口的 kTitleRole 取值一致（UserRole + 1），两边需要保持同步。
constexpr int kTitleRole = Qt::UserRole + 1;

}  // namespace

ProcessPickerDialog::ProcessPickerDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("选择进程"));
    setMinimumSize(520, 520);
    resize(520, 600);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    // --- 搜索框 ---
    layout->addWidget(new QLabel(QStringLiteral("输入关键字搜索进程名或窗口标题:"), this));
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(
        QStringLiteral("例如: chrome、记事本、explorer..."));
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ProcessPickerDialog::onSearchChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed,
            this, &ProcessPickerDialog::onSearchEnter);
    layout->addWidget(m_searchEdit);

    // --- 进程列表 ---
    m_listWidget = new QListWidget(this);
    m_listWidget->setAlternatingRowColors(true);
    connect(m_listWidget, &QListWidget::itemDoubleClicked,
            this, &ProcessPickerDialog::onItemDoubleClicked);
    layout->addWidget(m_listWidget);

    // --- 底部状态 + 按钮 ---
    m_countLabel = new QLabel(this);
    m_countLabel->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    layout->addWidget(m_countLabel);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    m_okBtn = new QPushButton(QStringLiteral("确定"), this);
    m_okBtn->setDefault(true);
    connect(m_okBtn, &QPushButton::clicked, this, &ProcessPickerDialog::onAccept);
    m_cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(m_okBtn);
    btnRow->addWidget(m_cancelBtn);
    layout->addLayout(btnRow);

    loadProcesses();
}

void ProcessPickerDialog::loadProcesses() {
    m_listWidget->clear();

    const QList<ClickEngineApi::ProcessGroup> groups =
        ClickEngineApi::getProcessWindowList();

    int withWindow = 0;
    for (const auto &group : groups) {
        if (!group.windows.isEmpty()) {
            for (const auto &win : group.windows) {
                const QString display = QStringLiteral("%1  [%2]  —  %3")
                                            .arg(group.name)
                                            .arg(group.pid)
                                            .arg(win.second);
                auto *item = new QListWidgetItem(display, m_listWidget);
                item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(win.first));
                item->setData(kTitleRole, win.second);
                ++withWindow;            }
        } else {
            const QString display = QStringLiteral("%1  [%2]  (无窗口)")
                                        .arg(group.name)
                                        .arg(group.pid);
            auto *item = new QListWidgetItem(display, m_listWidget);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(quintptr(0)));
            // 无窗口的进程没有可点击的目标，整行禁用
            Qt::ItemFlags flags = item->flags();
            flags &= ~Qt::ItemIsSelectable;
            flags &= ~Qt::ItemIsEnabled;
            item->setFlags(flags);
        }
    }

    m_countLabel->setText(QStringLiteral("共 %1 个进程，其中 %2 个有窗口")
                              .arg(m_listWidget->count())
                              .arg(withWindow));
    qCInfo(lcPicker).noquote() << QStringLiteral("进程列表已加载: %1 个进程, %2 个有窗口")
                            .arg(m_listWidget->count())
                            .arg(withWindow);
}

void ProcessPickerDialog::onSearchChanged(const QString &text) {
    const QString needle = text.trimmed().toLower();
    int visible = 0;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem *item = m_listWidget->item(i);
        const bool match = needle.isEmpty() || item->text().toLower().contains(needle);
        item->setHidden(!match);
        if (match) {
            ++visible;
        }
    }
    m_countLabel->setText(QStringLiteral("匹配 %1 项").arg(visible));
}

void ProcessPickerDialog::onSearchEnter() {
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem *item = m_listWidget->item(i);
        if (item->isHidden()) {
            continue;
        }
        if (!(item->flags() & Qt::ItemIsSelectable)) {
            continue;
        }
        m_listWidget->setCurrentRow(i);
        return;
    }
}

void ProcessPickerDialog::onItemDoubleClicked(QListWidgetItem *item) {
    if (!item) {
        return;
    }
    const quintptr hwnd = static_cast<quintptr>(item->data(Qt::UserRole).toULongLong());
    if (hwnd == 0) {
        return;
    }
    m_selectedHwnd = hwnd;
    m_selectedTitle = item->data(kTitleRole).toString();
    accept();
}

void ProcessPickerDialog::onAccept() {
    QListWidgetItem *item = m_listWidget->currentItem();
    if (!item) {
        return;
    }
    const quintptr hwnd = static_cast<quintptr>(item->data(Qt::UserRole).toULongLong());
    if (hwnd == 0) {
        return;
    }
    m_selectedHwnd = hwnd;
    m_selectedTitle = item->data(kTitleRole).toString();
    accept();
}
