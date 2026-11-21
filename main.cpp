#include <QApplication>
#include <QIcon>
#include <QLabel>
#include <QDir>

#include "utils/app_paths.h"
#include "utils/logger.h"

// AutoClicker Qt —— Windows 自动连点器（C++/Qt6 版）
//
// 当前处于初始化阶段：目录结构、构建系统、路径与日志层已就位。
// 连点引擎（core/）、界面（ui/）随后补齐。
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

    qInfo().noquote() << QStringLiteral("==============================================");
    qInfo().noquote() << QStringLiteral("AutoClicker Qt 启动");
    qInfo().noquote() << QStringLiteral("数据目录: %1").arg(QDir::toNativeSeparators(dataDir));
    qInfo().noquote() << QStringLiteral("日志文件: %1").arg(QDir::toNativeSeparators(Logger::logFilePath()));
    qInfo().noquote() << QStringLiteral("==============================================");

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/static/Ralsei.png")));

    // 初始化阶段的占位窗口，确认资源系统与主题链路可用。
    QLabel placeholder(QStringLiteral("AutoClicker Qt\n\n项目初始化完成，界面与连点引擎正在移植中。"));
    placeholder.setAlignment(Qt::AlignCenter);
    placeholder.setWindowTitle(QStringLiteral("AutoClicker - 自动连点器"));
    placeholder.setWindowIcon(QIcon(QStringLiteral(":/static/Ralsei.png")));
    placeholder.resize(420, 220);
    placeholder.show();

    const int code = QApplication::exec();

    qInfo().noquote() << QStringLiteral("窗口关闭，程序退出");
    Logger::shutdown();
    return code;
}
