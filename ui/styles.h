#pragma once

#include <QString>

// 界面样式表（QSS），支持深色 / 浅色 / 自动主题。
//
// 颜色方案与原 Python 版 ui/styles.py 完全一致：
//   深色 = Catppuccin Mocha，浅色 = Catppuccin Latte。
// 目标不是「看起来差不多」而是「逐像素一致」，所以 QSS 正文是原样搬来的。
namespace Styles {

// 主题标识，和配置文件里 settings.theme 的取值一一对应
enum class Theme {
    Dark,
    Light,
    Auto,
};

// 从配置字符串解析主题（"dark" / "light" / "auto"，无法识别时按 auto）
Theme themeFromString(const QString &name);

// 主题转回配置字符串
QString themeToString(Theme theme);

// 生成指定主题的完整 QSS。Auto 会按系统当前是否为暗色模式解析。
QString styleSheet(Theme theme);

// 探测系统是否使用暗色主题。
//   Windows：读注册表 HKCU\...\Themes\Personalize\AppsUseLightTheme
//   其它：读 COLORSCHEME 环境变量，判断不了时返回 true（与 Python 版一致）
bool detectSystemDark();

} // namespace Styles
