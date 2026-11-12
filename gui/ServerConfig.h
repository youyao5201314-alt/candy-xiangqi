#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <QString>
#include <QSettings>

// 互联网服务器地址配置
// 地址不写死在客户端：首次使用时由玩家在登录界面手动填写（房主提供），
// 之后也可以在主菜单左下角「服务器」随时修改，值持久化在 QSettings。
namespace ServerConfig {

// 当前服务器 URL（未配置时返回空串）
inline QString serverUrl()
{
    QSettings s;
    return s.value("server/url").toString().trimmed();
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
