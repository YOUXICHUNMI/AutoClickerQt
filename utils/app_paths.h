#pragma once

#include <QString>

// 路径解析：统一开发运行与发布运行下的目录定位。
//
// 规则与 Python 版 utils/app_paths.py 保持一致：
//   1. 环境变量 AUTOCLICKER_DATA_DIR 优先级最高（start.bat / script/run.* 会设置它）
//   2. 否则数据目录 = 可执行文件所在目录（发布后即 exe 同级）
//   3. 源码运行时则回落到项目根（便于开发时直接跑）
//
// 资源（图标 / GIF）通过 Qt 资源系统（qrc）内嵌，不依赖磁盘布局。
namespace AppPaths {

// 数据目录：config/ 与 log/ 都建在它下面。
QString dataDir();

// 数据目录下的子路径，例如 dataPath("config", "config.json")。
QString dataPath(const QString &first);

QString dataPath(const QString &first, const QString &second);

// 确保目录存在，返回目录路径。
QString ensureDir(const QString &dirPath);

} // namespace AppPaths
