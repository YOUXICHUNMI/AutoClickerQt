#pragma once

#include <QMainWindow>
#include <QTimer>

#include <memory>

#include "utils/config_manager.h"

class ClickEngine;
class HotkeyListener;

class KeyCaptureEdit;
class QComboBox;
class QCompleter;
class QDoubleSpinBox;
class QLabel;
class QMovie;
class QPushButton;
class QSpinBox;
class QSystemTrayIcon;
class QTableWidget;

// 主窗口。
//
// 移植自 Python 版 ui/main_window.py（1180 行）。保持的行为要点：
//   - 内容区放进 QScrollArea，窗口缩小时靠滚动而不是挤压控件
//   - 底部「开始/停止」放常驻 footer，永远可见
//   - 后台模式的目标句柄只存在内存里，绝不写进配置
//   - 坐标用相对客户区的百分比，避免窗口移动后失效
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    // 配置
    void onProfileChanged(int index);
    void onThemeChanged(int index);
    void onAddProfile();
    void onRenameProfile();
    void onCopyProfile();
    void onDeleteProfile();

    // 动作
    void onAddAction();
    void onEditAction();
    void onDeleteAction();
    void onMoveActionUp();
    void onMoveActionDown();

    // 目标窗口
    void onTargetModeChanged(int index);
    void onRefreshWindows();
    void onWindowSearchEnter();
    void onPickProcess();
    void onPickCoordinate();
    void onPickTimer();
    void onWindowSelectionChanged();

    // 连点
    void onStart();
    void onStop();
    void onActionCallback(int index);
    void onEngineFinished();

    // 热键
    void onApplyHotkeys();

    // 托盘
    void onTrayActivated(int reason);
    void onQuit();

    // 内部
    void onAutoSave();

private:
    void buildUi();
    void buildTray();
    void connectSignals();
    void applyDefaultSize();

    void loadProfilesToUi();
    void loadThemeToUi();
    void loadActiveProfileToUi();
    void refreshActionTable();
    void refreshWindows();
    void updatePixelDisplay();
    void registerHotkeys();

    Profile *activeProfile();
    void autoSaveProfile();
    void cancelPick();

    // UI 构建过程中抑制信号回调（对应 Python 的 _building_ui）
    bool m_buildingUi = false;

    // 坐标拾取状态机：0 = 等待松开初始点击，1 = 等待新点击
    bool m_picking = false;
    int m_pickState = 0;
    QTimer *m_pickTimer = nullptr;

    ConfigManager m_config;
    std::unique_ptr<ClickEngine> m_engine;
    std::unique_ptr<HotkeyListener> m_hotkey;

    // 配置区
    QComboBox *m_profileCombo = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QPushButton *m_newProfileBtn = nullptr;
    QPushButton *m_renameProfileBtn = nullptr;
    QPushButton *m_copyProfileBtn = nullptr;
    QPushButton *m_deleteProfileBtn = nullptr;

    // 动作表
    QTableWidget *m_actionTable = nullptr;
    QPushButton *m_addActionBtn = nullptr;
    QPushButton *m_editActionBtn = nullptr;
    QPushButton *m_deleteActionBtn = nullptr;
    QPushButton *m_moveUpBtn = nullptr;
    QPushButton *m_moveDownBtn = nullptr;

    // 连点参数
    QSpinBox *m_intervalSpin = nullptr;
    QSpinBox *m_repeatSpin = nullptr;
    QSpinBox *m_loopDelaySpin = nullptr;

    // 目标窗口
    QComboBox *m_targetModeCombo = nullptr;
    QComboBox *m_windowCombo = nullptr;
    QPushButton *m_refreshWindowsBtn = nullptr;
    QPushButton *m_pickProcessBtn = nullptr;
    QDoubleSpinBox *m_clickXSpin = nullptr;
    QDoubleSpinBox *m_clickYSpin = nullptr;
    QPushButton *m_pickCoordBtn = nullptr;
    QLabel *m_pixelLabel = nullptr;

    // 热键（用 KeyCaptureEdit：点进去按一下就绑定，而不是手打键名）
    KeyCaptureEdit *m_startHotkeyEdit = nullptr;
    KeyCaptureEdit *m_stopHotkeyEdit = nullptr;
    QPushButton *m_applyHotkeyBtn = nullptr;

    // 底部
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QLabel *m_runStatusLabel = nullptr;
    QLabel *m_runIconLabel = nullptr;
    QMovie *m_runMovie = nullptr;

    // 托盘
    QSystemTrayIcon *m_tray = nullptr;

    // 运行时缓存的目标句柄（不落盘）
    quintptr m_targetHwnd = 0;
};
