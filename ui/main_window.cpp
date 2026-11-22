#include "ui/main_window.h"

#include "core/click_engine.h"
#include "core/hotkey_listener.h"
#include "ui/action_editor.h"
#include "ui/process_picker.h"
#include "ui/styles.h"
#include "utils/app_paths.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCursor>
#include <QComboBox>
#include <QCompleter>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QMovie>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QWindow>
#include <QScrollArea>
#include <QSize>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcMain, "main")

namespace {

// 列表项里存"原始窗口标题"（不含 "  [进程名]" 后缀）的自定义 role。
// 为什么不用 displayRole：显示文本带进程名，回写配置时要的是纯标题，
// 否则下次按标题查找会失败。与 Python 版的 TITLE_ROLE 对应。
constexpr int kTitleRole = Qt::UserRole + 1;

// 列表项里存"进程可执行文件名"（例如 chrome.exe）的 role。
// 光靠标题恢复目标窗口不可靠：浏览器切页面、记事本换文件都会改标题，
// 进程名则在程序重启后依然一致。
constexpr int kExeRole = Qt::UserRole + 2;

// 动作类型的中文显示与存储值互转
QString actionTypeToText(const QString &type) {
    if (type == QLatin1String("mouse")) return QStringLiteral("鼠标");
    if (type == QLatin1String("keyboard")) return QStringLiteral("键盘");
    if (type == QLatin1String("combo")) return QStringLiteral("组合键");
    return type;
}

// 动作在表格里显示的"按钮/按键"列文本
QString actionButtonText(const ClickAction &a) {
    if (a.type == QLatin1String("mouse")) {
        return a.button;
    }
    if (a.type == QLatin1String("keyboard")) {
        return a.button;
    }
    if (a.type == QLatin1String("combo")) {
        return a.keys.join(QStringLiteral(" + "));
    }
    return QString();
}

}  // namespace

// ============================================================================
// 构造 / 析构
// ============================================================================

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("AutoClicker - 自动连点器"));
    setMinimumSize(560, 460);
    resize(820, 720);

    m_engine = std::make_unique<ClickEngine>();
    m_hotkey = std::make_unique<HotkeyListener>(this);
    m_config.load();

    buildUi();
    applyDefaultSize();
    buildTray();
    connectSignals();
    loadProfilesToUi();
    registerHotkeys();

    qCInfo(lcMain).noquote() << QStringLiteral("数据目录: %1").arg(AppPaths::dataDir());
}

MainWindow::~MainWindow() {
    if (m_engine) {
        m_engine->stop();
        m_engine->waitForStop();
    }
    if (m_hotkey) {
        m_hotkey->unregisterAll();
    }
}

// ============================================================================
// UI 构建
// ============================================================================

void MainWindow::buildUi() {
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    setCentralWidget(central);

    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 顶部运行状态：左边是 Ralsei 动画（静止时显示静态图标），右边是状态文字
    auto *runBar = new QHBoxLayout();
    runBar->setSpacing(8);

    m_runIconLabel = new QLabel(this);
    m_runIconLabel->setFixedSize(48, 48);
    m_runIconLabel->setAlignment(Qt::AlignCenter);
    m_runMovie = new QMovie(QStringLiteral(":/static/Ralsei_Run.gif"), QByteArray(), this);
    m_runMovie->setScaledSize(QSize(48, 48));
    connect(m_runMovie, &QMovie::frameChanged, this, [this](int) {
        m_runIconLabel->setPixmap(m_runMovie->currentPixmap());
    });
    // 未运行时显示静态图标，运行时才切到 GIF
    m_runIconLabel->setPixmap(QIcon(QStringLiteral(":/static/Ralsei.png")).pixmap(48, 48));
    runBar->addWidget(m_runIconLabel);

    m_runStatusLabel = new QLabel(QStringLiteral("待机中"), this);
    m_runStatusLabel->setStyleSheet(
        QStringLiteral("font-size: 14px; font-weight: bold; color: #888;"));
    runBar->addWidget(m_runStatusLabel);
    runBar->addStretch(1);

    // 内容区放进 QScrollArea：窗口变矮时靠滚动，而不是把控件压扁重叠
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("contentScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("contentPane"));
    content->setMinimumWidth(510);
    scroll->setWidget(content);

    auto *main = new QVBoxLayout(content);
    main->setContentsMargins(12, 10, 12, 10);
    main->setSpacing(10);
    main->addLayout(runBar);

    // ---------------- 配置文件 ----------------
    auto *profGroup = new QGroupBox(QStringLiteral("配置文件"), content);
    auto *profLayout = new QGridLayout(profGroup);
    profLayout->setHorizontalSpacing(8);
    profLayout->setVerticalSpacing(8);

    m_profileCombo = new QComboBox(profGroup);
    m_profileCombo->setMinimumWidth(120);
    m_profileCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_newProfileBtn = new QPushButton(QStringLiteral("新建"), profGroup);
    m_renameProfileBtn = new QPushButton(QStringLiteral("重命名"), profGroup);
    m_copyProfileBtn = new QPushButton(QStringLiteral("复制"), profGroup);
    m_deleteProfileBtn = new QPushButton(QStringLiteral("删除"), profGroup);
    for (QPushButton *b : {m_newProfileBtn, m_renameProfileBtn, m_copyProfileBtn,
                           m_deleteProfileBtn}) {
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    m_themeCombo = new QComboBox(profGroup);
    m_themeCombo->addItem(QStringLiteral("深色"), QStringLiteral("dark"));
    m_themeCombo->addItem(QStringLiteral("浅色"), QStringLiteral("light"));
    m_themeCombo->addItem(QStringLiteral("自动 (跟随系统)"), QStringLiteral("auto"));

    profLayout->addWidget(new QLabel(QStringLiteral("当前配置:"), profGroup), 0, 0);
    profLayout->addWidget(m_profileCombo, 0, 1);
    profLayout->addWidget(m_newProfileBtn, 0, 2);
    profLayout->addWidget(m_renameProfileBtn, 0, 3);
    profLayout->addWidget(m_copyProfileBtn, 0, 4);
    profLayout->addWidget(m_deleteProfileBtn, 0, 5);
    profLayout->setColumnStretch(1, 1);

    profLayout->addWidget(new QLabel(QStringLiteral("主题:"), profGroup), 1, 0);
    profLayout->addWidget(m_themeCombo, 1, 1, Qt::AlignLeft);
    profLayout->setColumnStretch(1, 1);

    main->addWidget(profGroup);

    // ---------------- 动作序列 ----------------
    auto *actionGroup = new QGroupBox(QStringLiteral("点击动作序列 (按顺序执行)"), content);
    auto *actionLayout = new QVBoxLayout(actionGroup);
    actionLayout->setSpacing(8);

    m_actionTable = new QTableWidget(0, 5, actionGroup);
    m_actionTable->setHorizontalHeaderLabels({QStringLiteral("#"), QStringLiteral("类型"),
                                              QStringLiteral("按钮/按键"),
                                              QStringLiteral("模式"), QStringLiteral("参数")});
    m_actionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_actionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_actionTable->setMinimumHeight(120);
    m_actionTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_actionTable->verticalHeader()->setVisible(false);
    m_actionTable->verticalHeader()->setDefaultSectionSize(28);
    m_actionTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_actionTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_actionTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_actionTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_actionTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    actionLayout->addWidget(m_actionTable, 1);

    auto *actionBtnRow = new QHBoxLayout();
    m_addActionBtn = new QPushButton(QStringLiteral("+ 添加动作"), actionGroup);
    m_editActionBtn = new QPushButton(QStringLiteral("编辑"), actionGroup);
    m_deleteActionBtn = new QPushButton(QStringLiteral("删除"), actionGroup);
    m_moveUpBtn = new QPushButton(QStringLiteral("上移"), actionGroup);
    m_moveDownBtn = new QPushButton(QStringLiteral("下移"), actionGroup);
    for (QPushButton *b : {m_addActionBtn, m_editActionBtn, m_deleteActionBtn, m_moveUpBtn,
                           m_moveDownBtn}) {
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        actionBtnRow->addWidget(b);
    }
    actionBtnRow->addStretch(1);
    actionLayout->addLayout(actionBtnRow);

    main->addWidget(actionGroup, 1);

    // ---------------- 连点参数 ----------------
    auto *paramGroup = new QGroupBox(QStringLiteral("连点参数"), content);
    auto *paramLayout = new QGridLayout(paramGroup);
    paramLayout->setHorizontalSpacing(10);
    paramLayout->setVerticalSpacing(8);

    m_intervalSpin = new QSpinBox(paramGroup);
    m_intervalSpin->setRange(0, 100000);
    m_intervalSpin->setSuffix(QStringLiteral(" ms  (0=最快)"));
    m_intervalSpin->setToolTip(
        QStringLiteral("每轮循环之间的间隔时间。设为0表示尽可能快地连点。"));
    m_intervalSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_repeatSpin = new QSpinBox(paramGroup);
    m_repeatSpin->setRange(0, 999999);
    m_repeatSpin->setSuffix(QStringLiteral(" 次  (0=无限)"));
    m_repeatSpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_loopDelaySpin = new QSpinBox(paramGroup);
    m_loopDelaySpin->setRange(0, 100000);
    m_loopDelaySpin->setSuffix(QStringLiteral(" ms"));
    m_loopDelaySpin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    paramLayout->addWidget(new QLabel(QStringLiteral("循环间隔:"), paramGroup), 0, 0);
    paramLayout->addWidget(m_intervalSpin, 0, 1);
    paramLayout->addWidget(new QLabel(QStringLiteral("重复次数:"), paramGroup), 0, 2);
    paramLayout->addWidget(m_repeatSpin, 0, 3);
    paramLayout->addWidget(new QLabel(QStringLiteral("循环间额外延迟:"), paramGroup), 1, 0);
    paramLayout->addWidget(m_loopDelaySpin, 1, 1, 1, 3);
    paramLayout->setColumnStretch(1, 1);
    paramLayout->setColumnStretch(3, 1);

    main->addWidget(paramGroup);

    // ---------------- 目标窗口 ----------------
    auto *targetGroup = new QGroupBox(QStringLiteral("目标窗口 (后台点击)"), content);
    auto *targetLayout = new QGridLayout(targetGroup);
    targetLayout->setHorizontalSpacing(8);
    targetLayout->setVerticalSpacing(8);

    m_targetModeCombo = new QComboBox(targetGroup);
    m_targetModeCombo->addItem(QStringLiteral("前台模式 (SendInput)"),
                               QStringLiteral("foreground"));
    m_targetModeCombo->addItem(QStringLiteral("后台模式 (PostMessage)"),
                               QStringLiteral("background"));
    m_targetModeCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_windowCombo = new QComboBox(targetGroup);
    m_windowCombo->setMinimumWidth(150);
    m_windowCombo->setEditable(true);
    m_windowCombo->setInsertPolicy(QComboBox::NoInsert);
    m_windowCombo->setEnabled(false);
    {
        auto *completer = new QCompleter(m_windowCombo->model(), m_windowCombo);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_windowCombo->setCompleter(completer);
        m_windowCombo->lineEdit()->setPlaceholderText(
            QStringLiteral("输入关键字搜索窗口/进程..."));
    }
    m_windowCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_refreshWindowsBtn = new QPushButton(QStringLiteral("刷新"), targetGroup);
    m_refreshWindowsBtn->setEnabled(false);
    m_refreshWindowsBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_pickProcessBtn = new QPushButton(QStringLiteral("📋 进程"), targetGroup);
    m_pickProcessBtn->setEnabled(false);
    m_pickProcessBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    targetLayout->addWidget(new QLabel(QStringLiteral("模式:"), targetGroup), 0, 0);
    targetLayout->addWidget(m_targetModeCombo, 0, 1, Qt::AlignLeft);
    targetLayout->addWidget(m_refreshWindowsBtn, 0, 2);
    targetLayout->addWidget(m_pickProcessBtn, 0, 3);
    // 空列吃剩余宽度，按钮才不会被推到最右边
    targetLayout->setColumnStretch(4, 1);

    targetLayout->addWidget(new QLabel(QStringLiteral("目标窗口:"), targetGroup), 1, 0);
    targetLayout->addWidget(m_windowCombo, 1, 1, 1, 4);

    auto *coordRow = new QHBoxLayout();
    coordRow->setSpacing(8);
    coordRow->addWidget(new QLabel(QStringLiteral("点击坐标:"), targetGroup));
    coordRow->addWidget(new QLabel(QStringLiteral("X:"), targetGroup));
    m_clickXSpin = new QDoubleSpinBox(targetGroup);
    m_clickXSpin->setRange(0.0, 100.0);
    m_clickXSpin->setSingleStep(0.1);
    m_clickXSpin->setDecimals(1);
    m_clickXSpin->setSuffix(QStringLiteral(" %"));
    m_clickXSpin->setValue(50.0);
    m_clickXSpin->setMinimumWidth(90);
    m_clickXSpin->setEnabled(false);
    m_clickXSpin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    coordRow->addWidget(m_clickXSpin);
    coordRow->addWidget(new QLabel(QStringLiteral("Y:"), targetGroup));
    m_clickYSpin = new QDoubleSpinBox(targetGroup);
    m_clickYSpin->setRange(0.0, 100.0);
    m_clickYSpin->setSingleStep(0.1);
    m_clickYSpin->setDecimals(1);
    m_clickYSpin->setSuffix(QStringLiteral(" %"));
    m_clickYSpin->setValue(50.0);
    m_clickYSpin->setMinimumWidth(90);
    m_clickYSpin->setEnabled(false);
    m_clickYSpin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    coordRow->addWidget(m_clickYSpin);
    m_pickCoordBtn = new QPushButton(QStringLiteral("🎯 拾取坐标"), targetGroup);
    m_pickCoordBtn->setEnabled(false);
    m_pickCoordBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    coordRow->addWidget(m_pickCoordBtn);
    coordRow->addStretch(1);
    targetLayout->addLayout(coordRow, 2, 0, 1, 4);

    m_pixelLabel = new QLabel(targetGroup);
    m_pixelLabel->setStyleSheet(QStringLiteral("color: #a6adc8; font-size: 11px;"));
    // 单独一行：它文字较长，混进坐标行会把整组的最小宽度顶上去
    targetLayout->addWidget(m_pixelLabel, 3, 0, 1, 4);

    main->addWidget(targetGroup);

    // ---------------- 热键 ----------------
    auto *hotkeyGroup = new QGroupBox(QStringLiteral("热键设置 (后台生效)"), content);
    auto *hotkeyLayout = new QGridLayout(hotkeyGroup);
    hotkeyLayout->setHorizontalSpacing(10);
    hotkeyLayout->setVerticalSpacing(8);

    m_startHotkeyEdit = new KeyCaptureEdit(hotkeyGroup);
    m_startHotkeyEdit->setPlaceholderText(QStringLiteral("点击后按下热键..."));
    m_startHotkeyEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_stopHotkeyEdit = new KeyCaptureEdit(hotkeyGroup);
    m_stopHotkeyEdit->setPlaceholderText(QStringLiteral("点击后按下热键..."));
    m_stopHotkeyEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_applyHotkeyBtn = new QPushButton(QStringLiteral("应用热键"), hotkeyGroup);
    m_applyHotkeyBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    hotkeyLayout->addWidget(new QLabel(QStringLiteral("启动热键:"), hotkeyGroup), 0, 0);
    hotkeyLayout->addWidget(m_startHotkeyEdit, 0, 1);
    hotkeyLayout->addWidget(new QLabel(QStringLiteral("停止热键:"), hotkeyGroup), 0, 2);
    hotkeyLayout->addWidget(m_stopHotkeyEdit, 0, 3);
    hotkeyLayout->addWidget(m_applyHotkeyBtn, 0, 4);
    hotkeyLayout->setColumnStretch(1, 1);
    hotkeyLayout->setColumnStretch(3, 1);

    main->addWidget(hotkeyGroup);
    main->addStretch(1);

    outer->addWidget(scroll, 1);

    // ---------------- 底部常驻控制栏 ----------------
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("footerBar"));
    auto *ctrlRow = new QHBoxLayout(footer);
    ctrlRow->setContentsMargins(12, 8, 12, 8);
    ctrlRow->setSpacing(10);

    m_startBtn = new QPushButton(QStringLiteral("▶  开始连点"), footer);
    m_startBtn->setObjectName(QStringLiteral("startBtn"));
    m_startBtn->setMinimumHeight(40);
    m_startBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_stopBtn = new QPushButton(QStringLiteral("■  停止"), footer);
    m_stopBtn->setObjectName(QStringLiteral("stopBtn"));
    m_stopBtn->setMinimumHeight(40);
    m_stopBtn->setEnabled(false);
    m_stopBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    ctrlRow->addWidget(m_startBtn);
    ctrlRow->addWidget(m_stopBtn);
    outer->addWidget(footer, 0);

    statusBar()->showMessage(QStringLiteral("就绪 | 热键启动/停止可后台使用"));
}

void MainWindow::applyDefaultSize() {
    // 尺寸要按「这台窗口最终会落在哪个屏幕」来算，不能用 primaryScreen()：
    // 本机是双屏（主屏 1920x1080 dpr=1.0，副屏 dpr=1.5），构造阶段窗口还没有
    // 所属屏幕，取 primaryScreen() 会在一部分运行里拿到副屏的 DPR 1.5，于是
    // 900x928 被放大成 1350x1392、y 变成 -151，整个窗口跑到屏幕外，
    // 点击命中测试也随之错位。这里改为优先用 windowHandle()->screen()，
    // 没有则用鼠标所在屏，最后才回落到 primaryScreen()。
    int contentHeight = 0;
    if (auto *scroll = findChild<QScrollArea *>(QStringLiteral("contentScroll"))) {
        if (QWidget *inner = scroll->widget()) {
            contentHeight = inner->minimumSizeHint().height();
        }
    }

    QScreen *screen = nullptr;
    if (windowHandle() != nullptr) {
        screen = windowHandle()->screen();
    }
    if (screen == nullptr) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        resize(820, 720);
        if (layout()) {
            layout()->activate();
        }
        return;
    }

    const QRect avail = screen->availableGeometry();
    const int width = qMin(900, qMax(minimumWidth(), avail.width() - 120));
    // 116 = 底部控制栏 ~60 + 状态栏 ~24 + 标题栏 ~32
    const int wantHeight = contentHeight > 0 ? contentHeight + 116 : 720;
    const int height = qMin(wantHeight, static_cast<int>(avail.height() * 0.9));

    resize(width, qMax(height, minimumHeight()));
    if (layout()) {
        layout()->activate();
    }
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    // 首次显示时窗口才真正归属某个屏幕，此时再校正一次尺寸并夹回屏幕内，
    // 防止构造阶段的尺寸被错误的 DPR 放大后沿用下来（见 applyDefaultSize）。
    if (m_firstShow) {
        m_firstShow = false;
        applyDefaultSize();
        clampToScreen();
        // showEvent 里 frameGeometry() 还没算上系统加的那圈边框（实测差 16px），
        // 所以等窗口真正映射完成（下一轮事件循环）再夹一次。
        QTimer::singleShot(0, this, [this] { clampToScreen(); });
    }
}

void MainWindow::clampToScreen() {
    QScreen *screen = windowHandle() ? windowHandle()->screen()
                                     : QGuiApplication::screenAt(frameGeometry().center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect avail = screen->availableGeometry();
    const QRect frame = frameGeometry();

    // 窗口比屏幕还大就先缩，否则再怎么挪都放不下。
    if (frame.width() > avail.width() || frame.height() > avail.height()) {
        resize(qMin(frame.width(), avail.width()), qMin(frame.height(), avail.height()));
    }

    const QRect f = frameGeometry();
    // 四个方向都要夹：只夹右下角会让窗口顶部跑到屏幕外，
    // 标题栏看不见也拖不回来（实测出现过 y = -151）。
    int x = f.x();
    int y = f.y();
    if (x < avail.left()) {
        x = avail.left();
    }
    if (y < avail.top()) {
        y = avail.top();
    }
    if (x + f.width() > avail.right() + 1) {
        x = qMax(avail.left(), avail.right() + 1 - f.width());
    }
    if (y + f.height() > avail.bottom() + 1) {
        y = qMax(avail.top(), avail.bottom() + 1 - f.height());
    }
    if (x != f.x() || y != f.y()) {
        move(x, y);
    }
}

void MainWindow::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qCInfo(lcMain) << "系统托盘不可用，跳过托盘图标";
        return;
    }

    m_tray = new QSystemTrayIcon(this);
    m_tray->setToolTip(QStringLiteral("AutoClicker"));

    // 图标必须显式设置，否则 Windows 会打
    // QSystemTrayIcon::setVisible: No Icon set，托盘里也是空白格
    QIcon trayIcon(QStringLiteral(":/static/Ralsei.png"));
    if (trayIcon.isNull()) {
        trayIcon = style()->standardIcon(QStyle::SP_ComputerIcon);
    }
    m_tray->setIcon(trayIcon);

    auto *menu = new QMenu(this);
    auto *showAction = menu->addAction(QStringLiteral("显示窗口"));
    connect(showAction, &QAction::triggered, this, [this] {
        showNormal();
        activateWindow();
    });
    auto *quitAction = menu->addAction(QStringLiteral("退出"));
    connect(quitAction, &QAction::triggered, this, &MainWindow::onQuit);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        onTrayActivated(static_cast<int>(r));
    });

    m_tray->show();
}

void MainWindow::connectSignals() {
    connect(m_profileCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onProfileChanged);
    connect(m_themeCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onThemeChanged);

    connect(m_newProfileBtn, &QPushButton::clicked, this, &MainWindow::onAddProfile);
    connect(m_renameProfileBtn, &QPushButton::clicked, this, &MainWindow::onRenameProfile);
    connect(m_copyProfileBtn, &QPushButton::clicked, this, &MainWindow::onCopyProfile);
    connect(m_deleteProfileBtn, &QPushButton::clicked, this, &MainWindow::onDeleteProfile);

    connect(m_addActionBtn, &QPushButton::clicked, this, &MainWindow::onAddAction);
    connect(m_editActionBtn, &QPushButton::clicked, this, &MainWindow::onEditAction);
    connect(m_deleteActionBtn, &QPushButton::clicked, this, &MainWindow::onDeleteAction);
    connect(m_moveUpBtn, &QPushButton::clicked, this, &MainWindow::onMoveActionUp);
    connect(m_moveDownBtn, &QPushButton::clicked, this, &MainWindow::onMoveActionDown);

    connect(m_intervalSpin, &QSpinBox::valueChanged, this, &MainWindow::onAutoSave);
    connect(m_repeatSpin, &QSpinBox::valueChanged, this, &MainWindow::onAutoSave);
    connect(m_loopDelaySpin, &QSpinBox::valueChanged, this, &MainWindow::onAutoSave);

    connect(m_targetModeCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onTargetModeChanged);
    connect(m_refreshWindowsBtn, &QPushButton::clicked, this, &MainWindow::onRefreshWindows);
    connect(m_pickProcessBtn, &QPushButton::clicked, this, &MainWindow::onPickProcess);
    connect(m_pickCoordBtn, &QPushButton::clicked, this, &MainWindow::onPickCoordinate);
    connect(m_windowCombo, &QComboBox::currentIndexChanged,
            this, &MainWindow::onWindowSelectionChanged);
    // 可编辑下拉框：输入关键字后回车跳到第一个匹配项
    connect(m_windowCombo->lineEdit(), &QLineEdit::returnPressed,
            this, &MainWindow::onWindowSearchEnter);
    connect(m_clickXSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::onAutoSave);
    connect(m_clickYSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::onAutoSave);

    connect(m_applyHotkeyBtn, &QPushButton::clicked, this, &MainWindow::onApplyHotkeys);

    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);

    // 引擎回调来自工作线程，必须切回 UI 线程再动界面
    m_engine->setActionCallback([this](int index) {
        QMetaObject::invokeMethod(this, [this, index] { onActionCallback(index); },
                                  Qt::QueuedConnection);
    });

    connect(m_hotkey.get(), &HotkeyListener::startPressed, this, &MainWindow::onStart);
    connect(m_hotkey.get(), &HotkeyListener::stopPressed, this, &MainWindow::onStop);
}

// ============================================================================
// 配置加载
// ============================================================================

void MainWindow::loadProfilesToUi() {
    m_buildingUi = true;
    m_profileCombo->clear();
    for (const Profile &p : m_config.profiles()) {
        m_profileCombo->addItem(p.name);
    }
    // Qt6 里 QList::size() 返回 qsizetype，与 int 一起传给 qBound/qMax 会推导失败，
    // 所以先显式收成 int。
    const int lastIndex = static_cast<int>(m_config.profiles().size()) - 1;
    const int active = qBound(0, m_config.config().activeProfile, qMax(0, lastIndex));
    m_profileCombo->setCurrentIndex(active);
    m_buildingUi = false;

    loadActiveProfileToUi();
    loadThemeToUi();
}

void MainWindow::loadThemeToUi() {
    m_buildingUi = true;
    const QString theme = m_config.settings().theme;
    int idx = m_themeCombo->findData(theme);
    if (idx < 0) {
        idx = m_themeCombo->findData(QStringLiteral("auto"));
    }
    m_themeCombo->setCurrentIndex(idx);
    m_buildingUi = false;
}

void MainWindow::loadActiveProfileToUi() {
    m_buildingUi = true;

    const Profile &p = m_config.activeProfile();
    m_intervalSpin->setValue(p.interval);
    m_repeatSpin->setValue(p.repeat);
    m_loopDelaySpin->setValue(p.loopDelay);
    m_startHotkeyEdit->setText(p.startHotkey);
    m_stopHotkeyEdit->setText(p.stopHotkey);

    // 坐标必须在切目标模式之前回填。
    // onTargetModeChanged() 末尾会调 autoSaveProfile()，那时如果坐标框还是
    // 默认的 50/50，就会把磁盘上刚读出来的真实值覆盖掉 —— 表现为
    // 「重启后坐标永远是 50%」，这是个真实修过的 bug。
    m_clickXSpin->setValue(p.clickX * 100.0);
    m_clickYSpin->setValue(p.clickY * 100.0);

    const int modeIdx = m_targetModeCombo->findData(p.targetMode);
    m_targetModeCombo->setCurrentIndex(modeIdx >= 0 ? modeIdx : 0);
    m_buildingUi = false;

    // 目标模式切换要真的执行一次，才能把窗口列表/控件可用性摆对
    onTargetModeChanged(m_targetModeCombo->currentIndex());

    refreshActionTable();
}

void MainWindow::refreshActionTable() {
    const Profile &p = m_config.activeProfile();
    const QList<ClickAction> &actions = p.actions;

    m_actionTable->setRowCount(actions.size());
    for (int i = 0; i < actions.size(); ++i) {
        const ClickAction &a = actions.at(i);
        m_actionTable->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        m_actionTable->setItem(i, 1, new QTableWidgetItem(actionTypeToText(a.type)));
        m_actionTable->setItem(i, 2, new QTableWidgetItem(actionButtonText(a)));
        m_actionTable->setItem(
            i, 3,
            new QTableWidgetItem(a.mode == QLatin1String("hold") ? QStringLiteral("长按")
                                                                : QStringLiteral("单击")));

        QString params;
        if (a.mode == QLatin1String("hold")) {
            params = a.holdDuration <= 0
                         ? QStringLiteral("一直按")
                         : QStringLiteral("长按 %1s").arg(a.holdDuration, 0, 'f', 3);
        }
        if (a.delayAfter > 0) {
            const QString delay = QStringLiteral("延迟 %1s").arg(a.delayAfter, 0, 'f', 3);
            params = params.isEmpty() ? delay
                                      : QStringLiteral("%1 | %2").arg(params, delay);
        }
        m_actionTable->setItem(i, 4, new QTableWidgetItem(params));
    }
}

void MainWindow::refreshWindows() {
    m_buildingUi = true;

    // 刷新前记住当前选中项的标题与进程名，刷新后按它们找回
    QString savedTitle = m_windowCombo->currentData(kTitleRole).toString();
    QString savedExe = m_windowCombo->currentData(kExeRole).toString();
    if (savedTitle.isEmpty() && savedExe.isEmpty()) {
        // 下拉框还没选中过任何东西（例如刚启动），用配置里记的
        savedTitle = m_config.activeProfile().targetTitle;
        savedExe = m_config.activeProfile().targetExe;
    }

    m_windowCombo->clear();

    const QList<ClickEngineApi::WindowInfo> windows = ClickEngineApi::enumWindowsDetail();
    for (const auto &w : windows) {
        const QString display = w.procName.isEmpty()
                                    ? w.title
                                    : QStringLiteral("%1  [%2]").arg(w.title, w.procName);
        m_windowCombo->addItem(display);
        const int idx = m_windowCombo->count() - 1;
        m_windowCombo->setItemData(idx, QVariant::fromValue<qulonglong>(w.hwnd));
        m_windowCombo->setItemData(idx, w.title, kTitleRole);
        m_windowCombo->setItemData(idx, w.procName, kExeRole);
    }

    m_buildingUi = false;

    selectWindowByHint(savedTitle, savedExe);

    qCInfo(lcMain).noquote() << QStringLiteral("窗口列表已刷新，共 %1 个窗口").arg(windows.size());
    updatePixelDisplay();
}

bool MainWindow::selectWindowByHint(const QString &title, const QString &exe) {
    if (title.isEmpty() && exe.isEmpty()) {
        return false;
    }

    const bool prev = m_buildingUi;
    m_buildingUi = true;

    auto pick = [this](int index) {
        m_windowCombo->setCurrentIndex(index);
    };

    // 1. 进程名与标题都相同：程序没重启，只是重进了界面
    if (!exe.isEmpty() && !title.isEmpty()) {
        for (int i = 0; i < m_windowCombo->count(); ++i) {
            if (m_windowCombo->itemData(i, kExeRole).toString() == exe &&
                m_windowCombo->itemData(i, kTitleRole).toString() == title) {
                pick(i);
                m_buildingUi = prev;
                return true;
            }
        }
    }

    // 2. 只有进程名相同：程序重启过，标题已经变了。
    //    同名进程可能有多个实例（开了两个记事本），优先选能通过 isWindow 校验的，
    //    列表本身只含有窗口的项，所以直接取第一个即可。
    if (!exe.isEmpty()) {
        for (int i = 0; i < m_windowCombo->count(); ++i) {
            if (m_windowCombo->itemData(i, kExeRole).toString() == exe) {
                pick(i);
                m_buildingUi = prev;
                return true;
            }
        }
    }

    // 3. 标题完全相同（老配置没有 target_exe 时走这里）
    if (!title.isEmpty()) {
        for (int i = 0; i < m_windowCombo->count(); ++i) {
            if (m_windowCombo->itemData(i, kTitleRole).toString() == title) {
                pick(i);
                m_buildingUi = prev;
                return true;
            }
        }

        // 4. 标题包含（标题带了状态后缀，例如 " - 记事本" 之类）
        const int idx = m_windowCombo->findText(title, Qt::MatchContains);
        if (idx >= 0) {
            pick(idx);
            m_buildingUi = prev;
            return true;
        }
    }

    m_buildingUi = prev;
    return false;
}

// ============================================================================
// 保存
// ============================================================================

Profile *MainWindow::activeProfile() {
    if (m_config.profiles().isEmpty()) {
        return nullptr;
    }
    const int lastIndex = static_cast<int>(m_config.profiles().size()) - 1;
    const int idx = qBound(0, m_config.config().activeProfile, lastIndex);
    return &m_config.profiles()[idx];
}

void MainWindow::onAutoSave() {
    autoSaveProfile();
}

void MainWindow::autoSaveProfile() {
    if (m_buildingUi) {
        return;
    }
    Profile *p = activeProfile();
    if (!p) {
        return;
    }

    p->interval = m_intervalSpin->value();
    p->repeat = m_repeatSpin->value();
    p->loopDelay = m_loopDelaySpin->value();
    p->startHotkey = m_startHotkeyEdit->text().trimmed();
    p->stopHotkey = m_stopHotkeyEdit->text().trimmed();
    p->targetMode = m_targetModeCombo->currentData().toString();

    if (m_windowCombo->isEnabled() && m_windowCombo->currentIndex() >= 0) {
        const QString rawTitle = m_windowCombo->currentData(kTitleRole).toString();
        p->targetTitle = rawTitle.isEmpty() ? m_windowCombo->currentText() : rawTitle;
        p->targetExe = m_windowCombo->currentData(kExeRole).toString();
    } else {
        p->targetTitle.clear();
        p->targetExe.clear();
    }

    p->clickX = m_clickXSpin->value() / 100.0;
    p->clickY = m_clickYSpin->value() / 100.0;

    // 注意：这里不写回 m_targetHwnd。运行时句柄只活在内存里，
    // Profile 结构里也没有这个字段，ConfigManager::save() 自然写不出去
    // —— 对应 Python 版 prof.pop("target_hwnd") 的双重保险。

    if (!m_config.save()) {
        qCWarning(lcMain) << "自动保存配置失败";
    }
}

// ============================================================================
// 配置操作
// ============================================================================

void MainWindow::onProfileChanged(int index) {
    if (m_buildingUi || index < 0) {
        return;
    }
    m_config.setActiveProfile(index);
    loadActiveProfileToUi();
    registerHotkeys();
}

void MainWindow::onThemeChanged(int index) {
    if (m_buildingUi || index < 0) {
        return;
    }
    const QString theme = m_themeCombo->itemData(index).toString();
    m_config.settings().theme = theme;
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        app->setStyleSheet(Styles::styleSheet(Styles::themeFromString(theme)));
    }
    m_config.save();
}

void MainWindow::onAddProfile() {
    Profile p;
    p.name = QStringLiteral("配置 %1").arg(m_config.profiles().size() + 1);
    ClickAction a;
    a.type = QStringLiteral("mouse");
    a.button = QStringLiteral("left");
    a.mode = QStringLiteral("click");
    p.actions.append(a);
    m_config.profiles().append(p);
    m_config.setActiveProfile(m_config.profiles().size() - 1);
    m_config.save();
    loadProfilesToUi();
}

void MainWindow::onRenameProfile() {
    Profile *p = activeProfile();
    if (!p) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名配置"),
                                               QStringLiteral("新名称:"), QLineEdit::Normal,
                                               p->name, &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    p->name = name.trimmed();
    m_config.save();
    loadProfilesToUi();
}

void MainWindow::onCopyProfile() {
    Profile *p = activeProfile();
    if (!p) {
        return;
    }
    Profile copy = *p;
    copy.name = p->name + QStringLiteral(" (副本)");
    m_config.profiles().append(copy);
    m_config.setActiveProfile(m_config.profiles().size() - 1);
    m_config.save();
    loadProfilesToUi();
}

void MainWindow::onDeleteProfile() {
    if (m_config.profiles().size() <= 1) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("至少需要保留一个配置文件"));
        return;
    }
    const int idx = m_config.config().activeProfile;
    m_config.profiles().removeAt(idx);
    m_config.setActiveProfile(qMax(0, idx - 1));
    m_config.save();
    loadProfilesToUi();
}

// ============================================================================
// 动作操作
// ============================================================================

void MainWindow::onAddAction() {
    Profile *p = activeProfile();
    if (!p) {
        return;
    }
    ActionEditorDialog dlg(ClickAction(), this);
    if (dlg.exec() == QDialog::Accepted) {
        p->actions.append(dlg.result());
        autoSaveProfile();
        refreshActionTable();
    }
}

void MainWindow::onEditAction() {
    Profile *p = activeProfile();
    if (!p) {
        return;
    }
    const int row = m_actionTable->currentRow();
    if (row < 0 || row >= p->actions.size()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择一个动作"));
        return;
    }
    ActionEditorDialog dlg(p->actions.at(row), this);
    if (dlg.exec() == QDialog::Accepted) {
        p->actions[row] = dlg.result();
        autoSaveProfile();
        refreshActionTable();
    }
}

void MainWindow::onDeleteAction() {
    Profile *p = activeProfile();
    if (!p) {
        return;
    }
    const int row = m_actionTable->currentRow();
    if (row < 0 || row >= p->actions.size()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选择一个动作"));
        return;
    }
    p->actions.removeAt(row);
    autoSaveProfile();
    refreshActionTable();
}

void MainWindow::onMoveActionUp() {
    Profile *p = activeProfile();
    if (!p) {
        return;
    }
    const int row = m_actionTable->currentRow();
    if (row <= 0 || row >= p->actions.size()) {
        return;
    }
    p->actions.swapItemsAt(row, row - 1);
    autoSaveProfile();
    refreshActionTable();
    m_actionTable->selectRow(row - 1);
}

void MainWindow::onMoveActionDown() {
    Profile *p = activeProfile();
    if (!p) {
        return;
    }
    const int row = m_actionTable->currentRow();
    if (row < 0 || row >= p->actions.size() - 1) {
        return;
    }
    p->actions.swapItemsAt(row, row + 1);
    autoSaveProfile();
    refreshActionTable();
    m_actionTable->selectRow(row + 1);
}

// ============================================================================
// 目标窗口
// ============================================================================

void MainWindow::onTargetModeChanged(int index) {
    const bool isBackground = (index == 1);

    m_windowCombo->setEnabled(isBackground);
    m_refreshWindowsBtn->setEnabled(isBackground);
    m_pickProcessBtn->setEnabled(isBackground);
    m_clickXSpin->setEnabled(isBackground);
    m_clickYSpin->setEnabled(isBackground);
    m_pickCoordBtn->setEnabled(isBackground);

    if (isBackground) {
        // refreshWindows 内部已经会按当前选中/配置里的 title+exe 找回目标，
        // 所以这里不再重复一遍匹配逻辑。
        refreshWindows();
        updatePixelDisplay();
    } else {
        m_pixelLabel->clear();
    }

    if (!m_buildingUi) {
        autoSaveProfile();
    }
}

void MainWindow::onRefreshWindows() {
    refreshWindows();
}

void MainWindow::onWindowSelectionChanged() {
    if (m_buildingUi) {
        return;
    }
    updatePixelDisplay();
}

void MainWindow::onWindowSearchEnter() {
    const QString text = m_windowCombo->currentText().trimmed().toLower();
    if (text.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_windowCombo->count(); ++i) {
        if (m_windowCombo->itemText(i).toLower().contains(text)) {
            m_windowCombo->setCurrentIndex(i);
            updatePixelDisplay();
            return;
        }
    }
}

void MainWindow::onPickProcess() {
    ProcessPickerDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const quintptr hwnd = dlg.selectedHwnd();
    const QString title = dlg.selectedTitle();
    if (hwnd == 0) {
        return;
    }

    // 先看列表里是否已有该窗口
    int found = -1;
    for (int i = 0; i < m_windowCombo->count(); ++i) {
        if (static_cast<quintptr>(m_windowCombo->itemData(i).toULongLong()) == hwnd) {
            found = i;
            break;
        }
    }

    const bool prev = m_buildingUi;
    m_buildingUi = true;
    if (found < 0) {
        // 不在列表里（进程选择器比窗口列表更全），补进去
        QString procName;
        for (const auto &w : ClickEngineApi::enumWindowsDetail()) {
            if (w.hwnd == hwnd) {
                procName = w.procName;
                break;
            }
        }
        const QString display =
            procName.isEmpty() ? title : QStringLiteral("%1  [%2]").arg(title, procName);
        m_windowCombo->addItem(display);
        found = m_windowCombo->count() - 1;
        m_windowCombo->setItemData(found, QVariant::fromValue<qulonglong>(hwnd));
        m_windowCombo->setItemData(found, title, kTitleRole);
        m_windowCombo->setItemData(found, procName, kExeRole);
    }
    m_windowCombo->setCurrentIndex(found);
    m_buildingUi = prev;

    autoSaveProfile();
    updatePixelDisplay();

    const QString shown = title.size() > 40 ? title.left(40) + QStringLiteral("...") : title;
    statusBar()->showMessage(QStringLiteral("已选择: %1").arg(shown), 4000);
}

void MainWindow::updatePixelDisplay() {
    if (m_targetModeCombo->currentIndex() != 1) {
        m_pixelLabel->clear();
        return;
    }

    quintptr hwnd = 0;
    if (m_windowCombo->currentIndex() >= 0) {
        const quintptr candidate =
            static_cast<quintptr>(m_windowCombo->currentData().toULongLong());
        if (candidate != 0 && ClickEngineApi::isWindow(candidate)) {
            hwnd = candidate;
        }
    }
    if (hwnd == 0) {
        const QString title = m_windowCombo->currentData(kTitleRole).toString();
        const QString fallback = title.isEmpty() ? m_windowCombo->currentText() : title;
        if (!fallback.isEmpty()) {
            hwnd = ClickEngineApi::findWindowByTitle(fallback);
        }
    }

    if (hwnd == 0) {
        m_pixelLabel->setText(m_windowCombo->count() == 0
                                  ? QStringLiteral("(未找到窗口，请刷新列表)")
                                  : QStringLiteral("(请先选择目标窗口)"));
        return;
    }

    const QPair<int, int> size = ClickEngineApi::clientSize(hwnd);
    const int px = static_cast<int>(m_clickXSpin->value() / 100.0 * size.first);
    const int py = static_cast<int>(m_clickYSpin->value() / 100.0 * size.second);
    m_pixelLabel->setText(QStringLiteral("→ 像素 (%1, %2)  窗口 %3x%4")
                              .arg(px)
                              .arg(py)
                              .arg(size.first)
                              .arg(size.second));
}

// ============================================================================
// 坐标拾取
// ============================================================================

void MainWindow::onPickCoordinate() {
    if (m_picking) {
        return;
    }
    m_picking = true;
    m_pickState = 0;
    statusBar()->showMessage(QStringLiteral("请在目标位置点击鼠标左键... (按 Esc 取消)"));
    m_pickCoordBtn->setText(QStringLiteral("取消拾取"));

    // 隐藏自身：否则点到的可能是本窗口
    hide();

    if (!m_pickTimer) {
        m_pickTimer = new QTimer(this);
        m_pickTimer->setInterval(30);
        connect(m_pickTimer, &QTimer::timeout, this, &MainWindow::onPickTimer);
    }
    m_pickTimer->start();
}

void MainWindow::onPickTimer() {
    // 状态机：先等初始这次点击松开，再等下一次按下
    if (m_pickState == 0) {
        if (!ClickEngineApi::isMouseButtonDown()) {
            m_pickState = 1;
        }
        return;
    }

    if (!ClickEngineApi::isMouseButtonDown()) {
        return;
    }

    const ClickEngineApi::CursorTarget target = ClickEngineApi::windowUnderCursor();
    if (target.hwnd != 0) {
        const QPair<int, int> size = ClickEngineApi::clientSize(target.hwnd);
        if (size.first > 0 && size.second > 0) {
            const double relX =
                qBound(0.0, static_cast<double>(target.x) / size.first, 1.0);
            const double relY =
                qBound(0.0, static_cast<double>(target.y) / size.second, 1.0);

            const QString title = ClickEngineApi::windowTitle(target.hwnd);
            const bool prev = m_buildingUi;
            m_buildingUi = true;
            m_clickXSpin->setValue(relX * 100.0);
            m_clickYSpin->setValue(relY * 100.0);

            // 把拾取到的窗口同步选中到下拉框
            int found = -1;
            for (int i = 0; i < m_windowCombo->count(); ++i) {
                if (static_cast<quintptr>(m_windowCombo->itemData(i).toULongLong()) ==
                    target.hwnd) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                QString procName;
                for (const auto &w : ClickEngineApi::enumWindowsDetail()) {
                    if (w.hwnd == target.hwnd) {
                        procName = w.procName;
                        break;
                    }
                }
                const QString display = procName.isEmpty()
                                            ? title
                                            : QStringLiteral("%1  [%2]").arg(title, procName);
                m_windowCombo->addItem(display);
                found = m_windowCombo->count() - 1;
                m_windowCombo->setItemData(found, QVariant::fromValue<qulonglong>(target.hwnd));
                m_windowCombo->setItemData(found, title, kTitleRole);
                m_windowCombo->setItemData(found, procName, kExeRole);
            }
            m_windowCombo->setCurrentIndex(found);
            m_buildingUi = prev;

            autoSaveProfile();
            updatePixelDisplay();

            const int px = static_cast<int>(relX * size.first);
            const int py = static_cast<int>(relY * size.second);
            const QString shown =
                title.size() > 30 ? title.left(30) + QStringLiteral("...") : title;
            statusBar()->showMessage(
                QStringLiteral("已拾取: %1 (%2, %3)").arg(shown).arg(px).arg(py), 5000);
        } else {
            statusBar()->showMessage(QStringLiteral("拾取失败: 窗口客户区为 0"), 4000);
        }
    } else {
        statusBar()->showMessage(QStringLiteral("拾取失败: 未找到窗口"), 4000);
    }

    cancelPick();
}

void MainWindow::cancelPick() {
    m_picking = false;
    m_pickState = 0;
    if (m_pickTimer) {
        m_pickTimer->stop();
    }
    m_pickCoordBtn->setText(QStringLiteral("🎯 拾取坐标"));
    showNormal();
    activateWindow();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && m_picking) {
        cancelPick();
        statusBar()->showMessage(QStringLiteral("已取消拾取"), 3000);
        return;
    }
    QMainWindow::keyPressEvent(event);
}

// ============================================================================
// 连点控制
// ============================================================================

void MainWindow::onStart() {
    if (m_engine->isRunning()) {
        return;
    }

    Profile *p = activeProfile();
    if (!p) {
        return;
    }
    if (p->actions.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先添加至少一个点击动作"));
        return;
    }

    // 先把界面上的当前值写回配置，确保启动用的是用户刚改的参数
    autoSaveProfile();

    quintptr targetHwnd = 0;
    if (p->targetMode == QLatin1String("background")) {
        if (m_windowCombo->currentIndex() >= 0) {
            targetHwnd = static_cast<quintptr>(m_windowCombo->currentData().toULongLong());
        }
        if (targetHwnd == 0 || !ClickEngineApi::isWindow(targetHwnd)) {
            targetHwnd = ClickEngineApi::findWindowByTitle(p->targetTitle);
        }
        // 标题找不回来（程序重启过、或标题变了）时按进程名兜底，
        // 这样「上次连的是哪个程序」在重进之后依然有效。
        if (targetHwnd == 0 && !p->targetExe.isEmpty()) {
            const QList<ClickEngineApi::WindowInfo> all = ClickEngineApi::enumWindowsDetail();
            for (const auto &w : all) {
                if (w.procName.compare(p->targetExe, Qt::CaseInsensitive) == 0) {
                    targetHwnd = w.hwnd;
                    break;
                }
            }
        }
        if (targetHwnd == 0) {
            QMessageBox::warning(
                this, QStringLiteral("提示"),
                QStringLiteral("未找到目标窗口: %1\n请刷新窗口列表并重新选择")
                    .arg(p->targetTitle));
            return;
        }
        if (ClickEngineApi::isMinimized(targetHwnd)) {
            const auto ret = QMessageBox::question(
                this, QStringLiteral("窗口已最小化"),
                QStringLiteral("目标窗口 \"%1\" 当前已最小化。\n"
                               "最小化窗口可能无法正确接收鼠标点击。\n\n"
                               "是否自动还原窗口并继续？")
                    .arg(p->targetTitle),
                QMessageBox::Yes | QMessageBox::No);
            if (ret != QMessageBox::Yes) {
                return;
            }
            ClickEngineApi::restoreWindow(targetHwnd);
            QThread::msleep(300);
        }
    }

    // 句柄只放内存，不写配置
    m_targetHwnd = targetHwnd;

    QString error;
    if (!m_engine->start(*p, &error, targetHwnd)) {
        QMessageBox::critical(this, QStringLiteral("启动失败"), error);
        return;
    }

    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    statusBar()->showMessage(QStringLiteral("运行中..."));
    m_runStatusLabel->setText(QStringLiteral("运行中..."));
    m_runStatusLabel->setStyleSheet(
        QStringLiteral("font-size: 14px; font-weight: bold; color: #a6e3a1;"));
    if (m_runMovie) {
        m_runMovie->start();
    }

    if (m_tray) {
        m_tray->showMessage(QStringLiteral("AutoClicker"), QStringLiteral("连点已启动"),
                            QSystemTrayIcon::Information, 2000);
    }
    qCInfo(lcMain).noquote() << QStringLiteral("连点已启动: 配置=%1").arg(p->name);
}

void MainWindow::onStop() {
    m_engine->stop();
    onEngineFinished();
}

void MainWindow::onEngineFinished() {
    m_startBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("已停止"));
    m_runStatusLabel->setText(QStringLiteral("已停止"));
    m_runStatusLabel->setStyleSheet(
        QStringLiteral("font-size: 14px; font-weight: bold; color: #f38ba8;"));
    if (m_runMovie) {
        m_runMovie->stop();
    }
    if (m_runIconLabel) {
        m_runIconLabel->setPixmap(QIcon(QStringLiteral(":/static/Ralsei.png")).pixmap(48, 48));
    }

    if (m_tray) {
        m_tray->showMessage(QStringLiteral("AutoClicker"), QStringLiteral("连点已停止"),
                            QSystemTrayIcon::Information, 2000);
    }
}

void MainWindow::onActionCallback(int index) {
    if (index < 0) {
        onEngineFinished();
        return;
    }
    statusBar()->showMessage(QStringLiteral("运行中... 执行动作 #%1").arg(index + 1));
}

// ============================================================================
// 热键 / 托盘 / 关闭
// ============================================================================

void MainWindow::registerHotkeys() {
    const Profile &p = m_config.activeProfile();
    const QString startKey = p.startHotkey.isEmpty() ? QStringLiteral("F6") : p.startHotkey;
    const QString stopKey = p.stopHotkey.isEmpty() ? QStringLiteral("F7") : p.stopHotkey;

    if (!m_hotkey->available()) {
        qCWarning(lcMain) << "当前平台不支持全局热键，已跳过注册";
        statusBar()->showMessage(QStringLiteral("警告: 当前平台不支持全局热键"));
        return;
    }

    QString error;
    if (!m_hotkey->registerHotkeys(startKey, stopKey, &error)) {
        qCWarning(lcMain) << "热键注册失败:" << error;
        statusBar()->showMessage(QStringLiteral("热键不可用: %1").arg(error));
    }
}

void MainWindow::onApplyHotkeys() {
    autoSaveProfile();
    registerHotkeys();
    statusBar()->showMessage(QStringLiteral("热键已更新"), 3000);
}

void MainWindow::onTrayActivated(int reason) {
    if (reason == static_cast<int>(QSystemTrayIcon::DoubleClick)) {
        showNormal();
        activateWindow();
    }
}

void MainWindow::onQuit() {
    if (m_picking) {
        cancelPick();
    }
    m_engine->stop();
    m_engine->waitForStop();
    m_hotkey->unregisterAll();
    if (m_tray) {
        m_tray->hide();
    }
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_picking) {
        cancelPick();
    }

    autoSaveProfile();

    if (m_config.settings().minimizeToTray) {
        event->ignore();
        hide();
        if (m_tray) {
            m_tray->showMessage(QStringLiteral("AutoClicker"),
                                QStringLiteral("已最小化到托盘，双击图标恢复"),
                                QSystemTrayIcon::Information, 2000);
        }
        return;
    }

    m_engine->stop();
    m_engine->waitForStop();
    m_hotkey->unregisterAll();
    if (m_tray) {
        m_tray->hide();
    }
    qCInfo(lcMain) << "窗口关闭，程序退出";
    event->accept();
}
