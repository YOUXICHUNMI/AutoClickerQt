#include "ui/styles.h"

#include <QStringList>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace Styles {

namespace {

// ============================================================
// 颜色方案
// ============================================================

// Catppuccin Mocha (深色)
const QString kBaseDark       = QStringLiteral("#1e1e2e");
const QString kMantleDark     = QStringLiteral("#181825");
const QString kSurface0Dark   = QStringLiteral("#313244");
const QString kSurface1Dark   = QStringLiteral("#45475a");
const QString kSurface2Dark   = QStringLiteral("#585b70");
const QString kOverlay0Dark   = QStringLiteral("#6c7086");
const QString kTextDark       = QStringLiteral("#cdd6f4");
const QString kBlueDark       = QStringLiteral("#89b4fa");
const QString kGreenDark      = QStringLiteral("#a6e3a1");
const QString kGreenHoverDark = QStringLiteral("#94d389");
const QString kRedDark        = QStringLiteral("#f38ba8");
const QString kRedHoverDark   = QStringLiteral("#e07891");

// Catppuccin Latte (浅色)
const QString kBaseLight       = QStringLiteral("#eff1f5");
const QString kMantleLight     = QStringLiteral("#e6e9ef");
const QString kSurface0Light   = QStringLiteral("#ccd0da");
const QString kSurface1Light   = QStringLiteral("#bcc0cc");
const QString kSurface2Light   = QStringLiteral("#acb0be");
const QString kOverlay0Light   = QStringLiteral("#6c6f85");
const QString kTextLight       = QStringLiteral("#4c4f69");
const QString kBlueLight       = QStringLiteral("#1e66f5");
const QString kGreenLight      = QStringLiteral("#40a02b");
const QString kGreenHoverLight = QStringLiteral("#368a22");
const QString kRedLight        = QStringLiteral("#d20f39");
const QString kRedHoverLight   = QStringLiteral("#b30d31");

// 一套配色（对应 Python 的 _DARK / _LIGHT 字典）
struct Palette {
    QString base, mantle, surface0, surface1, surface2, overlay0;
    QString text, blue, green, greenHover, red, redHover;
};

const Palette kPaletteDark{
    kBaseDark,     kMantleDark,     kSurface0Dark, kSurface1Dark,   kSurface2Dark,
    kOverlay0Dark, kTextDark,       kBlueDark,     kGreenDark,      kGreenHoverDark,
    kRedDark,      kRedHoverDark,
};

const Palette kPaletteLight{
    kBaseLight,     kMantleLight,     kSurface0Light, kSurface1Light,   kSurface2Light,
    kOverlay0Light, kTextLight,       kBlueLight,     kGreenLight,      kGreenHoverLight,
    kRedLight,      kRedHoverLight,
};

// 把模板里的 {占位符} 换成实际颜色。
//
// 为什么用占位符而不是写两份 QSS：原 Python 版是 f-string，模板只有一份；
// C++ 里没有等价物，用 QString::arg 链会超长且难维护，
// 所以保留一份带 {} 占位的模板，用简单替换生成。
QString applyPalette(const QString &tmpl, const Palette &p) {
    QString out = tmpl;
    out.replace(QStringLiteral("{base}"), p.base);
    out.replace(QStringLiteral("{mantle}"), p.mantle);
    out.replace(QStringLiteral("{surface0}"), p.surface0);
    out.replace(QStringLiteral("{surface1}"), p.surface1);
    out.replace(QStringLiteral("{surface2}"), p.surface2);
    out.replace(QStringLiteral("{overlay0}"), p.overlay0);
    out.replace(QStringLiteral("{text}"), p.text);
    out.replace(QStringLiteral("{blue}"), p.blue);
    out.replace(QStringLiteral("{green}"), p.green);
    out.replace(QStringLiteral("{green_hover}"), p.greenHover);
    out.replace(QStringLiteral("{red}"), p.red);
    out.replace(QStringLiteral("{red_hover}"), p.redHover);
    return out;
}

// QSS 模板。与 Python 版 ui/styles.py 的 _build_style 逐行对应，
// 唯一差别是颜色写成了 {占位符}。
QString styleTemplate() {
    static const QString tmpl = QStringLiteral(R"QSS(
QMainWindow, QDialog, QWidget#central {
    background-color: {base};
    color: {text};
    font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
    font-size: 13px;
}

/* 可滚动内容区：窗口缩小时靠滚动条解决空间不足，
   而不是让控件互相挤压重叠 */
QScrollArea#contentScroll {
    background-color: {base};
    border: none;
}
QWidget#contentPane {
    background-color: {base};
}

/* 底部常驻控制栏：始终贴底，不随内容滚动 */
QWidget#footerBar {
    background-color: {mantle};
    border-top: 1px solid {surface1};
}

QScrollBar:horizontal {
    background: {mantle};
    height: 10px;
    border: none;
    margin: 0px;
}
QScrollBar::handle:horizontal {
    background: {surface1};
    border-radius: 5px;
    min-width: 30px;
}
QScrollBar::handle:horizontal:hover {
    background: {surface2};
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
}

QLabel {
    color: {text};
    background: transparent;
}
QLabel:disabled {
    color: {overlay0};
}

QGroupBox {
    border: 1px solid {surface1};
    border-radius: 6px;
    margin-top: 12px;
    padding-top: 14px;
    font-weight: bold;
    color: {blue};
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 6px;
}

QPushButton {
    background-color: {surface0};
    border: 1px solid {surface1};
    border-radius: 5px;
    padding: 6px 16px;
    color: {text};
    min-height: 22px;
}
QPushButton:hover {
    background-color: {surface1};
    border-color: {blue};
}
QPushButton:pressed {
    background-color: {mantle};
}
QPushButton#startBtn {
    background-color: {green};
    color: {base};
    font-weight: bold;
    font-size: 15px;
    min-height: 36px;
    border: none;
    border-radius: 8px;
}
QPushButton#startBtn:hover {
    background-color: {green_hover};
}
QPushButton#startBtn:disabled {
    background-color: {surface1};
    color: {overlay0};
}
QPushButton#stopBtn {
    background-color: {red};
    color: {base};
    font-weight: bold;
    font-size: 15px;
    min-height: 36px;
    border: none;
    border-radius: 8px;
}
QPushButton#stopBtn:hover {
    background-color: {red_hover};
}

QComboBox {
    background-color: {surface0};
    border: 1px solid {surface1};
    border-radius: 5px;
    padding: 4px 10px;
    color: {text};
    min-height: 22px;
}
QComboBox:hover {
    border-color: {blue};
}
QComboBox::drop-down {
    border: none;
    width: 24px;
}
QComboBox QAbstractItemView {
    background-color: {surface0};
    border: 1px solid {surface1};
    selection-background-color: {blue};
    color: {text};
    outline: none;
}

QSpinBox, QDoubleSpinBox, QLineEdit {
    background-color: {surface0};
    border: 1px solid {surface1};
    border-radius: 5px;
    padding: 4px 8px;
    color: {text};
    min-height: 22px;
}
QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus {
    border-color: {blue};
}
QSpinBox:disabled, QDoubleSpinBox:disabled, QLineEdit:disabled {
    background-color: {mantle};
    border: 1px solid {surface0};
    color: {overlay0};
}

QTableWidget {
    background-color: {mantle};
    border: 1px solid {surface1};
    border-radius: 5px;
    gridline-color: {surface0};
    color: {text};
    outline: none;
}
QTableWidget::item {
    padding: 4px 8px;
    min-height: 24px;
}
QTableWidget::item:selected {
    background-color: {surface0};
    color: {blue};
}
QHeaderView::section {
    background-color: {surface0};
    border: none;
    border-right: 1px solid {surface1};
    border-bottom: 1px solid {surface1};
    padding: 6px 8px;
    color: {blue};
    font-weight: bold;
}

QListWidget, QListView {
    background-color: {mantle};
    border: 1px solid {surface1};
    border-radius: 5px;
    color: {text};
    outline: none;
}
QListWidget::item, QListView::item {
    padding: 4px 8px;
}
QListWidget::item:selected, QListView::item:selected {
    background-color: {surface0};
    color: {blue};
}
QListWidget::item:disabled, QListView::item:disabled {
    color: {overlay0};
}
QListWidget::item:alternate, QListView::item:alternate {
    background-color: {base};
}

QCheckBox {
    color: {text};
    spacing: 6px;
}
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border-radius: 3px;
    border: 1px solid {surface1};
    background-color: {surface0};
}
QCheckBox::indicator:checked {
    background-color: {blue};
    border-color: {blue};
}

QStatusBar {
    background-color: {mantle};
    color: {overlay0};
    border-top: 1px solid {surface0};
}

QScrollBar:vertical {
    background: {mantle};
    width: 10px;
    border: none;
    margin: 0px;
}
QScrollBar::handle:vertical {
    background: {surface1};
    border-radius: 5px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover {
    background: {surface2};
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}

QToolTip {
    background-color: {surface0};
    color: {text};
    border: 1px solid {surface1};
    border-radius: 4px;
    padding: 4px;
}

QMenu {
    background-color: {surface0};
    border: 1px solid {surface1};
    color: {text};
    padding: 4px;
}
QMenu::item {
    padding: 6px 24px;
    border-radius: 4px;
}
QMenu::item:selected {
    background-color: {blue};
    color: {base};
}
)QSS");
    return tmpl;
}

} // namespace

Theme themeFromString(const QString &name) {
    const QString lower = name.trimmed().toLower();
    if (lower == QLatin1String("dark")) {
        return Theme::Dark;
    }
    if (lower == QLatin1String("light")) {
        return Theme::Light;
    }
    return Theme::Auto;
}

QString themeToString(Theme theme) {
    switch (theme) {
    case Theme::Dark:
        return QStringLiteral("dark");
    case Theme::Light:
        return QStringLiteral("light");
    case Theme::Auto:
        break;
    }
    return QStringLiteral("auto");
}

QString styleSheet(Theme theme) {
    bool useDark = true;
    switch (theme) {
    case Theme::Dark:
        useDark = true;
        break;
    case Theme::Light:
        useDark = false;
        break;
    case Theme::Auto:
        useDark = detectSystemDark();
        break;
    }
    return applyPalette(styleTemplate(), useDark ? kPaletteDark : kPaletteLight);
}

bool detectSystemDark() {
#ifdef Q_OS_WIN
    // HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
    //   AppsUseLightTheme = 0 表示暗色，1 表示浅色
    // 用 RegGetValueW 而不是 RegOpenKeyEx + QueryValueEx：
    // 一条调用完成读取，出错也不留需要关闭的句柄。
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS st = ::RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    if (st == ERROR_SUCCESS) {
        return value == 0;
    }

    // 读不到时返回暗色，与原 Python 版一致（那边异常分支是 return True）。
    return true;
#else
    // 非 Windows：看 COLORSCHEME 环境变量（如 "dark" / "light" / "prefer-dark"）
    const QByteArray scheme = qgetenv("COLORSCHEME").toLower();
    if (scheme.contains("dark")) {
        return true;
    }
    if (scheme.contains("light")) {
        return false;
    }
    return true;
#endif
}

} // namespace Styles
