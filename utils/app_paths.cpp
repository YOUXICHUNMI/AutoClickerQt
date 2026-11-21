#include "app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

namespace AppPaths {

namespace {
const char *kDataDirEnv = "AUTOCLICKER_DATA_DIR";

// 源码运行的判定：可执行文件位于 <root>/build 或 <root>/cmake-build-* 之下时，
// 认为还在开发期，数据目录回落到源码根，方便调试时看到 config/ 与 log/。
QString sourceRoot() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QDir dir(exeDir);

    // 逐级向上找，最多看 3 层，寻找带 CMakeLists.txt 的目录作为源码根。
    QDir probe(exeDir);
    for (int i = 0; i < 4; ++i) {
        if (QFileInfo::exists(probe.filePath("CMakeLists.txt"))) {
            return probe.absolutePath();
        }
        if (!probe.cdUp()) {
            break;
        }
    }
    Q_UNUSED(dir);
    return QString();
}
} // namespace

QString dataDir() {
    static QString cached;
    if (!cached.isEmpty()) {
        return cached;
    }

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString override = env.value(QString::fromLatin1(kDataDirEnv)).trimmed();
    if (!override.isEmpty()) {
        cached = QDir(override).absolutePath();
        return cached;
    }

    const QString root = sourceRoot();
    cached = root.isEmpty() ? QCoreApplication::applicationDirPath() : root;
    cached = QDir(cached).absolutePath();
    return cached;
}

QString dataPath(const QString &first) {
    return QDir(dataDir()).filePath(first);
}

QString dataPath(const QString &first, const QString &second) {
    return QDir(dataPath(first)).filePath(second);
}

QString ensureDir(const QString &dirPath) {
    QDir().mkpath(dirPath);
    return dirPath;
}

} // namespace AppPaths
