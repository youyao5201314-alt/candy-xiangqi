#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <QString>
#include <QSettings>

// 互联网服务器地址配置
// 默认地址写死为阿里云 ECS 固定 IP（frp 隧道），玩家开箱即用无需填写；
// 仍可在主菜单左下角「服务器」修改，值持久化在 QSettings（修改后优先于默认值）。
namespace ServerConfig {

// 默认服务器地址（注册表为空时使用）
inline QString defaultServerUrl()
{
    return QStringLiteral("http://118.31.237.140:3000");
}

// 当前服务器 URL（未配置时返回默认地址）
inline QString serverUrl()
{
    QSettings s;
    QString u = s.value("server/url").toString().trimmed();
    return u.isEmpty() ? defaultServerUrl() : u;
}

// 是否已配置服务器地址
inline bool isConfigured()
{
    return !serverUrl().isEmpty();
}

// 保存服务器 URL（去空格、去末尾斜杠）
inline void setServerUrl(const QString &url)
{
    QString u = url.trimmed();
    while (u.endsWith(QLatin1Char('/')))
        u.chop(1);
    QSettings s;
    s.setValue("server/url", u);
}

// 拼接 REST 路径，如 "/api/auth/login" → "https://xxx/api/auth/login"
inline QString apiUrl(const QString &path)
{
    return serverUrl() + path;
}

// 拼接 WebSocket 路径，http→ws / https→wss
inline QString wsUrl(const QString &path)
{
    QString u = serverUrl();
    if (u.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        u.replace(0, 8, QStringLiteral("wss://"));
    else if (u.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive))
        u.replace(0, 7, QStringLiteral("ws://"));
    return u + path;
}
}  // namespace ServerConfig

#endif  // SERVERCONFIG_H
