#include "logger.h"
#include "app_paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <cstdio>
#include <exception>

namespace Logger {

namespace {

QMutex g_mutex;
QFile *g_file = nullptr;
QString g_logPath;
QtMessageHandler g_prevHandler = nullptr;
bool g_installed = false;

const char *levelTag(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return "DEBUG  ";
    case QtInfoMsg:
        return "INFO   ";
    case QtWarningMsg:
        return "WARNING";
    case QtCriticalMsg:
        return "ERROR  ";
    case QtFatalMsg:
        return "FATAL  ";
    }
    return "INFO   ";
}

QString todayLogPath() {
    const QString logDir = AppPaths::ensureDir(AppPaths::dataPath("log"));
    const QString name = QStringLiteral("app_%1.log")
                             .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    return QDir(logDir).filePath(name);
}

void ensureFileOpen() {
    const QString want = todayLogPath();
    if (g_file && g_logPath == want) {
        return;
    }
    if (g_file) {
        g_file->close();
        delete g_file;
        g_file = nullptr;
    }
    g_file = new QFile(want);
    if (!g_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete g_file;
        g_file = nullptr;
        return;
    }
    g_logPath = want;
}

void writeLine(const QString &line) {
    // 控制台
    fprintf(stderr, "%s\n", line.toUtf8().constData());
    fflush(stderr);

    // 文件
    ensureFileOpen();
    if (g_file) {
        QTextStream out(g_file);
        out.setEncoding(QStringConverter::Utf8);
        out << line << '\n';
        out.flush();
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));

    QString module = QStringLiteral("app");
    if (ctx.category && *ctx.category) {
        module = QString::fromUtf8(ctx.category);
    } else if (ctx.file) {
        module = QFileInfo(QString::fromUtf8(ctx.file)).baseName();
    }
    if (module.isEmpty()) {
        module = QStringLiteral("app");
    }

    const QString line = QStringLiteral("[%1] [%2] [%3] %4")
                             .arg(ts, QString::fromLatin1(levelTag(type)),
                                  module.leftJustified(14, QLatin1Char(' ')), msg);

    QMutexLocker locker(&g_mutex);
    writeLine(line);

    if (type == QtFatalMsg) {
        abort();
    }
    if (g_prevHandler) {
        g_prevHandler(type, ctx, msg);
    }
}

} // namespace

void init() {
    if (g_installed) {
        return;
    }
    g_installed = true;
    g_prevHandler = qInstallMessageHandler(messageHandler);

    QMutexLocker locker(&g_mutex);
    ensureFileOpen();
}

void shutdown() {
    QMutexLocker locker(&g_mutex);
    if (g_file) {
        g_file->close();
        delete g_file;
        g_file = nullptr;
    }
}

QString logFilePath() {
    QMutexLocker locker(&g_mutex);
    return g_logPath;
}

void installGlobalHandler() {
    std::set_terminate([] {
        try {
            std::exception_ptr ep = std::current_exception();
            if (ep) {
                std::rethrow_exception(ep);
            }
        } catch (const std::exception &e) {
            qCritical().noquote()
                << QStringLiteral("============ 未捕获异常 ============\n[EXCEPTION] %1\n====================================")
                       .arg(QString::fromUtf8(e.what()));
        } catch (...) {
            qCritical().noquote() << QStringLiteral("============ 未捕获异常（未知类型）============");
        }
        std::abort();
    });
}

} // namespace Logger
