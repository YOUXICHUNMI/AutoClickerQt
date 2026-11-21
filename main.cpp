#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLoggingCategory>

#include "ui/main_window.h"
#include "ui/styles.h"
#include "utils/app_paths.h"
#include "utils/config_manager.h"
#include "utils/logger.h"

Q_LOGGING_CATEGORY(lcApp, "app")

// AutoClicker Qt —— Windows 自动连点器（C++/Qt6 版）
//
// 启动顺序刻意与 Python 版保持一致：
//   1. 建 QApplication 并设元信息（主题检测会用到系统信息）
//   2. 初始化日志（越早越好，后续任何一步出错都能留下记录）
//   3. 确保数据目录存在（配置与日志都写在这，见 AppPaths::dataDir）
//   4. 读配置里的主题并套用 QSS（在窗口构造前套用，避免先亮后暗的闪一下）
//   5. 建主窗口
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AutoClicker"));
    QApplication::setOrganizationName(QStringLiteral("YOUXICHUNMI"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    Logger::init();
    Logger::installGlobalHandler();

    const QString dataDir = AppPaths::dataDir();
    AppPaths::ensureDir(AppPaths::dataPath(QStringLiteral("config")));
    AppPaths::ensureDir(AppPaths::dataPath(QStringLiteral("log")));

    qCInfo(lcApp).noquote() << QStringLiteral("==============================================");
    qCInfo(lcApp).noquote() << QStringLiteral("AutoClicker Qt 启动");
    qCInfo(lcApp).noquote() << QStringLiteral("数据目录: %1").arg(QDir::toNativeSeparators(dataDir));
    qCInfo(lcApp).noquote() << QStringLiteral("日志文件: %1")
                                   .arg(QDir::toNativeSeparators(Logger::logFilePath()));
    qCInfo(lcApp).noquote() << QStringLiteral("==============================================");

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/static/Ralsei.png")));

    // 主题要在主窗口创建之前套用，否则会先以默认样式绘制一帧再变色
    ConfigManager config;
    config.load();
    const QString themeName = config.settings().theme;
    app.setStyleSheet(Styles::styleSheet(Styles::themeFromString(themeName)));
    qCInfo(lcApp).noquote() << QStringLiteral("主题: %1").arg(themeName);

    MainWindow window;
    window.show();

    const int code = QApplication::exec();

    qCInfo(lcApp) << "窗口关闭，程序退出";
    Logger::shutdown();
    return code;
}
