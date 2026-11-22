#include "config_manager.h"
#include "app_paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QDebug>

// ---------------------------------------------------------------- ClickAction

bool ClickAction::isHoldForever() const {
    return mode == QLatin1String("hold") && holdDuration <= 0.0;
}

// --------------------------------------------------------------- JSON 读写

namespace {

ClickAction actionFromJson(const QJsonObject &obj) {
    ClickAction a;
    a.type = obj.value(QStringLiteral("type")).toString(a.type);
    a.button = obj.value(QStringLiteral("button")).toString(a.button);
    a.mode = obj.value(QStringLiteral("mode")).toString(a.mode);
    a.holdDuration = obj.value(QStringLiteral("hold_duration")).toDouble(a.holdDuration);
    a.delayAfter = obj.value(QStringLiteral("delay_after")).toDouble(a.delayAfter);

    const QJsonValue keysVal = obj.value(QStringLiteral("keys"));
    if (keysVal.isArray()) {
        for (const QJsonValue &v : keysVal.toArray()) {
            if (v.isString()) {
                a.keys.append(v.toString());
            }
        }
    }
    return a;
}

QJsonObject actionToJson(const ClickAction &a) {
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), a.type);

    if (a.type == QLatin1String("combo")) {
        QJsonArray keys;
        for (const QString &k : a.keys) {
            keys.append(k);
        }
        obj.insert(QStringLiteral("keys"), keys);
    } else {
        obj.insert(QStringLiteral("button"), a.button);
    }

    obj.insert(QStringLiteral("mode"), a.mode);
    if (a.mode == QLatin1String("hold")) {
        obj.insert(QStringLiteral("hold_duration"), a.holdDuration);
    } else {
        obj.insert(QStringLiteral("delay_after"), a.delayAfter);
    }
    return obj;
}

Profile profileFromJson(const QJsonObject &obj) {
    Profile p;
    p.name = obj.value(QStringLiteral("name")).toString(p.name);

    const QJsonValue actionsVal = obj.value(QStringLiteral("actions"));
    if (actionsVal.isArray()) {
        p.actions.clear();
        for (const QJsonValue &v : actionsVal.toArray()) {
            if (v.isObject()) {
                p.actions.append(actionFromJson(v.toObject()));
            }
        }
    }

    p.interval = obj.value(QStringLiteral("interval")).toInt(p.interval);
    p.repeat = obj.value(QStringLiteral("repeat")).toInt(p.repeat);
    p.loopDelay = obj.value(QStringLiteral("loop_delay")).toInt(p.loopDelay);
    p.startHotkey = obj.value(QStringLiteral("start_hotkey")).toString(p.startHotkey);
    p.stopHotkey = obj.value(QStringLiteral("stop_hotkey")).toString(p.stopHotkey);
    p.enabled = obj.value(QStringLiteral("enabled")).toBool(p.enabled);
    p.targetMode = obj.value(QStringLiteral("target_mode")).toString(p.targetMode);
    p.targetTitle = obj.value(QStringLiteral("target_title")).toString(p.targetTitle);
    p.targetExe = obj.value(QStringLiteral("target_exe")).toString(p.targetExe);
    p.clickX = obj.value(QStringLiteral("click_x")).toDouble(p.clickX);
    p.clickY = obj.value(QStringLiteral("click_y")).toDouble(p.clickY);
    return p;
}

QJsonObject profileToJson(const Profile &p) {
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), p.name);

    QJsonArray actions;
    for (const ClickAction &a : p.actions) {
        actions.append(actionToJson(a));
    }
    obj.insert(QStringLiteral("actions"), actions);

    obj.insert(QStringLiteral("interval"), p.interval);
    obj.insert(QStringLiteral("repeat"), p.repeat);
    obj.insert(QStringLiteral("loop_delay"), p.loopDelay);
    obj.insert(QStringLiteral("start_hotkey"), p.startHotkey);
    obj.insert(QStringLiteral("stop_hotkey"), p.stopHotkey);
    obj.insert(QStringLiteral("enabled"), p.enabled);
    obj.insert(QStringLiteral("target_mode"), p.targetMode);
    obj.insert(QStringLiteral("target_title"), p.targetTitle);
    obj.insert(QStringLiteral("target_exe"), p.targetExe);
    obj.insert(QStringLiteral("click_x"), p.clickX);
    obj.insert(QStringLiteral("click_y"), p.clickY);

    // 明确不写 target_hwnd —— 见 config_manager.h 的铁律说明。
    return obj;
}

AppSettings settingsFromJson(const QJsonObject &obj) {
    AppSettings s;
    s.minimizeToTray = obj.value(QStringLiteral("minimize_to_tray")).toBool(s.minimizeToTray);
    s.startMinimized = obj.value(QStringLiteral("start_minimized")).toBool(s.startMinimized);
    s.highPrecision = obj.value(QStringLiteral("high_precision")).toBool(s.highPrecision);
    s.theme = obj.value(QStringLiteral("theme")).toString(s.theme);
    return s;
}

QJsonObject settingsToJson(const AppSettings &s) {
    QJsonObject obj;
    obj.insert(QStringLiteral("minimize_to_tray"), s.minimizeToTray);
    obj.insert(QStringLiteral("start_minimized"), s.startMinimized);
    obj.insert(QStringLiteral("high_precision"), s.highPrecision);
    obj.insert(QStringLiteral("theme"), s.theme);
    return obj;
}

} // namespace

// -------------------------------------------------------------- ConfigManager

ConfigManager::ConfigManager() {
    m_config = makeDefault();
}

QString ConfigManager::configDir() {
    return AppPaths::ensureDir(AppPaths::dataPath(QStringLiteral("config")));
}

QString ConfigManager::configFile() {
    return QDir(configDir()).filePath(QStringLiteral("config.json"));
}

AppConfig ConfigManager::makeDefault() {
    AppConfig cfg;

    Profile p;
    p.name = QStringLiteral("默认配置");

    ClickAction a;
    a.type = QStringLiteral("mouse");
    a.button = QStringLiteral("left");
    a.mode = QStringLiteral("click");
    a.holdDuration = 0.05;
    a.delayAfter = 0.001;
    p.actions.append(a);

    p.interval = 1;
    p.repeat = 0;
    p.loopDelay = 0;
    p.startHotkey = QStringLiteral("F6");
    p.stopHotkey = QStringLiteral("F7");
    p.enabled = true;
    p.targetMode = QStringLiteral("foreground");
    p.targetTitle.clear();
    p.clickX = 0.5;
    p.clickY = 0.5;

    cfg.profiles.append(p);
    cfg.activeProfile = 0;

    cfg.settings.minimizeToTray = false;
    cfg.settings.startMinimized = false;
    cfg.settings.highPrecision = true;
    cfg.settings.theme = QStringLiteral("auto");

    return cfg;
}

void ConfigManager::load() {
    m_config = makeDefault();

    const QString path = configFile();
    QFile file(path);
    if (!file.exists()) {
        qInfo().noquote() << QStringLiteral("配置文件不存在，使用默认配置: %1")
                                 .arg(QDir::toNativeSeparators(path));
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << QStringLiteral("打开配置文件失败，使用默认配置: %1")
                                    .arg(file.errorString());
        return;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning().noquote() << QStringLiteral("解析配置文件失败，使用默认配置: %1").arg(err.errorString());
        return;
    }

    const QJsonObject root = doc.object();

    const QJsonValue profilesVal = root.value(QStringLiteral("profiles"));
    if (profilesVal.isArray() && !profilesVal.toArray().isEmpty()) {
        // 用默认配置作为兜底骨架，保证字段齐全
        const AppConfig defaults = makeDefault();
        const Profile &fallback = defaults.profiles.first();

        QList<Profile> loaded;
        for (const QJsonValue &v : profilesVal.toArray()) {
            if (!v.isObject()) {
                continue;
            }
            // 先铺一层默认值，再覆盖读到的字段 —— 等价于原版的 _merge
            QJsonObject merged = profileToJson(fallback);
            const QJsonObject got = v.toObject();
            for (auto it = got.constBegin(); it != got.constEnd(); ++it) {
                merged.insert(it.key(), it.value());
            }
            loaded.append(profileFromJson(merged));
        }
        if (!loaded.isEmpty()) {
            m_config.profiles = loaded;
        }
    }

    m_config.activeProfile = root.value(QStringLiteral("active_profile")).toInt(0);
    if (m_config.activeProfile < 0 || m_config.activeProfile >= m_config.profiles.size()) {
        m_config.activeProfile = 0;
    }

    const QJsonObject merged = settingsToJson(makeDefault().settings);
    const QJsonObject gotSettings = root.value(QStringLiteral("settings")).toObject();
    QJsonObject settingsObj = merged;
    for (auto it = gotSettings.constBegin(); it != gotSettings.constEnd(); ++it) {
        settingsObj.insert(it.key(), it.value());
    }
    m_config.settings = settingsFromJson(settingsObj);

    qInfo().noquote() << QStringLiteral("配置已加载: %1 个配置，当前=%2")
                             .arg(m_config.profiles.size())
                             .arg(activeProfile().name);
}

bool ConfigManager::save() {
    const QString path = configFile();

    QJsonObject root;

    QJsonArray profilesArr;
    for (const Profile &p : m_config.profiles) {
        profilesArr.append(profileToJson(p));
    }
    root.insert(QStringLiteral("profiles"), profilesArr);
    root.insert(QStringLiteral("active_profile"), m_config.activeProfile);
    root.insert(QStringLiteral("settings"), settingsToJson(m_config.settings));

    const QJsonDocument doc(root);
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    // QSaveFile：先写临时文件再原子替换，避免写一半崩溃导致配置损坏
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        qWarning().noquote() << QStringLiteral("保存配置失败（无法打开文件）: %1").arg(out.errorString());
        return false;
    }
    if (out.write(data) != data.size()) {
        qWarning().noquote() << QStringLiteral("保存配置失败（写入不完整）: %1").arg(out.errorString());
        out.cancelWriting();
        return false;
    }
    if (!out.commit()) {
        qWarning().noquote() << QStringLiteral("保存配置失败（提交失败）: %1").arg(out.errorString());
        return false;
    }

    qDebug().noquote() << QStringLiteral("配置已保存: %1").arg(QDir::toNativeSeparators(path));
    return true;
}

void ConfigManager::reset() {
    m_config = makeDefault();
}

Profile &ConfigManager::activeProfile() {
    if (m_config.profiles.isEmpty()) {
        m_config.profiles.append(makeDefault().profiles.first());
        m_config.activeProfile = 0;
    }
    if (m_config.activeProfile < 0 || m_config.activeProfile >= m_config.profiles.size()) {
        m_config.activeProfile = 0;
    }
    return m_config.profiles[m_config.activeProfile];
}

const Profile &ConfigManager::activeProfile() const {
    static const Profile fallback;
    if (m_config.profiles.isEmpty()) {
        return fallback;
    }
    if (m_config.activeProfile < 0 || m_config.activeProfile >= m_config.profiles.size()) {
        return m_config.profiles.first();
    }
    return m_config.profiles.at(m_config.activeProfile);
}

void ConfigManager::setActiveProfile(int index) {
    if (index >= 0 && index < m_config.profiles.size()) {
        m_config.activeProfile = index;
    }
}
