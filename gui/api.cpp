// REST API 客户端实现
#include "api.h"
#include "ServerConfig.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QDir>
#include <QCryptographicHash>

ApiClient::ApiClient(QObject *parent) : QObject(parent) {}

void ApiClient::registerUser(const QString &username, const QString &password, const QString &displayName,
                             const QString &phone, const QString &code)
{
    QJsonObject obj;
    obj["username"] = username;
    obj["password"] = password;
    if (!displayName.isEmpty())
        obj["display_name"] = displayName;
    obj["phone"] = phone;
    obj["code"] = code;
    postJson("/api/auth/register", QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiClient::login(const QString &username, const QString &password)
{
    QJsonObject obj;
    obj["username"] = username;
    obj["password"] = password;
    postJson("/api/auth/login", QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiClient::guestLogin()
{
    postJson("/api/auth/guest", "{}");
}

void ApiClient::refreshAccessToken(const QString &refreshToken)
{
    QJsonObject obj;
    obj["refreshToken"] = refreshToken;
    postJson("/api/auth/refresh", QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiClient::logout(const QString &refreshToken)
{
    QJsonObject obj;
    obj["refreshToken"] = refreshToken;
    postJson("/api/auth/logout", QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiClient::getMe(const QString &accessToken)
{
    getJson("/api/auth/me", accessToken);
}

// ---------- 手机验证码登录 ----------
void ApiClient::sendCode(const QString &phone, const QString &purpose)
{
    QJsonObject obj;
    obj["phone"] = phone;
    obj["purpose"] = purpose;
    postJson("/api/auth/send-code", QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiClient::phoneLogin(const QString &phone, const QString &code)
{
    QJsonObject obj;
    obj["phone"] = phone;
    obj["code"] = code;
    postJson("/api/auth/phone-login", QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiClient::resetPassword(const QString &phone, const QString &code, const QString &newPassword)
{
    QJsonObject obj;
    obj["phone"] = phone;
    obj["code"] = code;
    obj["new_password"] = newPassword;
    postJson("/api/auth/reset-password", QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ApiClient::searchUsers(const QString &query, const QString &type, const QString &accessToken)
{
    QUrl url(ServerConfig::apiUrl(QStringLiteral("/api/auth/search")));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    q.addQueryItem(QStringLiteral("type"), type);
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(readError(reply));
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        QList<UserInfo> list;
        const QJsonArray arr = obj.value("users").toArray();
        for (const QJsonValue &v : arr) {
            list.append(parseUser(v.toObject()));
        }
        emit searchOk(list);
    });
}

// ---------- 个人资料 ----------
void ApiClient::updateProfile(const QString &accessToken, const QString &displayName)
{
    QJsonObject obj;
    obj["display_name"] = displayName;
    patchJsonAuth("/api/profile", QJsonDocument(obj).toJson(QJsonDocument::Compact), accessToken);
}

void ApiClient::bindPhone(const QString &accessToken, const QString &phone, const QString &code)
{
    QJsonObject obj;
    obj["phone"] = phone;
    obj["code"] = code;
    postJsonAuth("/api/profile/bind-phone", QJsonDocument(obj).toJson(QJsonDocument::Compact), accessToken);
}

void ApiClient::uploadAvatar(const QString &accessToken, const QString &filePath)
{
    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType, this);
    QHttpPart part;
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    const QString mime = suffix == "png" ? "image/png"
                       : suffix == "webp" ? "image/webp" : "image/jpeg";
    part.setHeader(QNetworkRequest::ContentTypeHeader, mime);
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QString("form-data; name=\"avatar\"; filename=\"avatar.%1\"").arg(suffix));
    auto *file = new QFile(filePath, multi);
    if (!file->open(QIODevice::ReadOnly)) {
        emit errorOccurred(QStringLiteral("无法读取文件: %1").arg(filePath));
        multi->deleteLater();
        return;
    }
    part.setBodyDevice(file);
    multi->append(part);

    QNetworkRequest req((QUrl(ServerConfig::apiUrl("/api/profile/avatar"))));
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
    QNetworkReply *reply = m_nam.post(req, multi);
    multi->setParent(reply);  // reply 销毁时连带释放

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(readError(reply));
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        UserInfo user = parseUser(obj.value("user").toObject());
        emit profileUpdated(user);
    });
}

// 头像磁盘缓存目录：AppDataLocation/avatarCache/<md5(url)>.png（存裁圆后的 96px PNG）
QString ApiClient::avatarCachePath(const QString &url)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/avatarCache");
    QDir().mkpath(dir);
    const QString name = QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex());
    const QString path = dir + QLatin1Char('/') + name + QStringLiteral(".png");
    return QFile::exists(path) ? path : QString();
}

void ApiClient::downloadAvatar(const QString &url)
{
    // 磁盘缓存命中：直接发信号，不走网络（cpolar 公网下载慢，缓存是主要提速手段）
    const QString cached = avatarCachePath(url);
    if (!cached.isEmpty()) {
        QPixmap pm(cached);
        if (!pm.isNull()) {
            emit avatarDownloaded(url, pm);
            return;
        }
    }
    QNetworkRequest req((QUrl(url)));
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return;  // 下载失败静默忽略，显示默认头像
        }
        QPixmap pm;
        pm.loadFromData(reply->readAll());
        if (pm.isNull())
            return;
        QPixmap out;
        if (url.contains(QLatin1String("/avatars/"))) {
            // 头像裁圆 96px 存磁盘缓存（QTextEdit 的 <img> 不支持 CSS 圆角，预裁圆后聊天区可直接引用）
            QPixmap scaled = pm.scaled(96, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            QPixmap circle(96, 96);
            circle.fill(Qt::transparent);
            QPainter p(&circle);
            p.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.addEllipse(0, 0, 96, 96);
            p.setClipPath(path);
            p.drawPixmap(0, 0, scaled);
            p.end();
            out = circle;
        } else {
            out = pm;   // 背景图等非头像：保留原图
        }
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/avatarCache");
        QDir().mkpath(dir);
        const QString name = QString::fromLatin1(
            QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex());
        out.save(dir + QLatin1Char('/') + name + QStringLiteral(".png"), "PNG");
        emit avatarDownloaded(url, out);
    });
}

void ApiClient::uploadProfileBg(const QString &accessToken, const QString &filePath)
{
    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType, this);
    QHttpPart part;
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    const QString mime = suffix == "png" ? "image/png"
                       : suffix == "webp" ? "image/webp" : "image/jpeg";
    part.setHeader(QNetworkRequest::ContentTypeHeader, mime);
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"bg\"; filename=\"bg.%1\"").arg(suffix));
    auto *file = new QFile(filePath, multi);
    if (!file->open(QIODevice::ReadOnly)) {
        emit errorOccurred(QStringLiteral("无法读取文件: %1").arg(filePath));
        multi->deleteLater();
        return;
    }
    part.setBodyDevice(file);
    multi->append(part);

    QNetworkRequest req((QUrl(ServerConfig::apiUrl("/api/profile/bg"))));
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
    QNetworkReply *reply = m_nam.post(req, multi);
    multi->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(readError(reply));
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        emit profileUpdated(parseUser(obj.value("user").toObject()));
    });
}

void ApiClient::setTitle(const QString &accessToken, const QString &title)
{
    QJsonObject obj;
    obj["title"] = title;
    postJsonAuth("/api/profile/title",
                 QJsonDocument(obj).toJson(QJsonDocument::Compact), accessToken);
}

void ApiClient::getUserProfile(const QString &accessToken, int userId)
{
    QNetworkRequest req((QUrl(ServerConfig::apiUrl(
        QStringLiteral("/api/users/%1/profile").arg(userId)))));
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(readError(reply));
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        emit userProfileOk(parseUser(obj.value("user").toObject()));
    });
}

// ---------- 阶段 2：好友 ----------
void ApiClient::listFriends(const QString &accessToken)
{
    getJson("/api/friends", accessToken);
}

void ApiClient::listFriendRequests(const QString &accessToken)
{
    getJson("/api/friends/requests", accessToken);
}

void ApiClient::sendFriendRequest(const QString &accessToken, const QString &username,
                                  const QString &message)
{
    QJsonObject obj;
    obj["username"] = username;
    if (!message.isEmpty())
        obj["message"] = message;
    postJsonAuth("/api/friends/request", QJsonDocument(obj).toJson(QJsonDocument::Compact), accessToken);
}

void ApiClient::acceptFriendRequest(const QString &accessToken, int requestId)
{
    QJsonObject obj;
    obj["request_id"] = requestId;
    postJsonAuth("/api/friends/accept", QJsonDocument(obj).toJson(QJsonDocument::Compact), accessToken);
}

void ApiClient::rejectFriendRequest(const QString &accessToken, int requestId)
{
    QJsonObject obj;
    obj["request_id"] = requestId;
    postJsonAuth("/api/friends/reject", QJsonDocument(obj).toJson(QJsonDocument::Compact), accessToken);
}

void ApiClient::blockFriend(const QString &accessToken, int userId)
{
    const QString path = QStringLiteral("/api/friends/%1/block").arg(userId);
    postJsonAuth(path, "{}", accessToken);
}

void ApiClient::unblockFriend(const QString &accessToken, int userId)
{
    const QString path = QStringLiteral("/api/friends/%1/unblock").arg(userId);
    postJsonAuth(path, "{}", accessToken);
}

void ApiClient::listBlockedFriends(const QString &accessToken)
{
    getJson("/api/friends/blocked", accessToken);
}

QString ApiClient::loadRefreshToken()
{
    QSettings s;
    return s.value("auth/refreshToken").toString();
}

void ApiClient::saveRefreshToken(const QString &token)
{
    QSettings s;
    s.setValue("auth/refreshToken", token);
}

void ApiClient::clearRefreshToken()
{
    QSettings s;
    s.remove("auth/refreshToken");
}

// 通用 POST JSON 请求，按 path 后缀分派信号
void ApiClient::postJson(const QString &path, const QByteArray &json)
{
    QNetworkRequest req((QUrl(ServerConfig::apiUrl(path))));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_nam.post(req, json);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QString msg = reply->errorString();
            // 服务器通常返回 {"error":"..."} JSON 体
            QByteArray body = reply->readAll();
            if (!body.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (doc.isObject()) {
                    QString err = doc.object().value("error").toString();
                    if (!err.isEmpty()) msg = err;
                }
            }
            emit errorOccurred(msg);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = doc.object();
        if (path.endsWith("/register")) {
            UserInfo user = parseUser(obj.value("user").toObject());
            emit registerOk(user, obj.value("accessToken").toString(),
                            obj.value("refreshToken").toString());
        } else if (path.endsWith("/login") || path.endsWith("/phone-login") || path.endsWith("/guest")) {
            UserInfo user = parseUser(obj.value("user").toObject());
            emit loginOk(user, obj.value("accessToken").toString(),
                         obj.value("refreshToken").toString());
        } else if (path.endsWith("/reset-password")) {
            UserInfo user = parseUser(obj.value("user").toObject());
            emit resetPasswordOk(user, obj.value("accessToken").toString(),
                                 obj.value("refreshToken").toString());
        } else if (path.endsWith("/send-code")) {
            emit sendCodeOk(obj.value("dev_code").toString());
        } else if (path.endsWith("/refresh")) {
            emit refreshOk(obj.value("accessToken").toString());
        } else if (path.endsWith("/logout")) {
            emit logoutOk();
        }
    });
}

void ApiClient::getJson(const QString &path, const QString &accessToken)
{
    QNetworkRequest req((QUrl(ServerConfig::apiUrl(path))));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QString msg = reply->errorString();
            QByteArray body = reply->readAll();
            if (!body.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (doc.isObject()) {
                    QString err = doc.object().value("error").toString();
                    if (!err.isEmpty()) msg = err;
                }
            }
            emit errorOccurred(msg);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = doc.object();
        if (path.endsWith("/me")) {
            UserInfo user = parseUser(obj.value("user").toObject());
            emit meOk(user);
        } else if (path.endsWith("/friends/requests")) {
            QList<FriendRequestInfo> list;
            const QJsonArray arr = obj.value("requests").toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject ro = v.toObject();
                FriendRequestInfo ri;
                ri.request_id = ro.value("request_id").toInt();
                ri.from = parseUser(ro.value("from").toObject());
                ri.message = ro.value("message").toString();
                list.append(ri);
            }
            emit friendRequestsOk(list);
        } else if (path.endsWith("/friends/blocked")) {
            QList<UserInfo> list;
            const QJsonArray arr = obj.value("blocked").toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject bo = v.toObject();
                UserInfo u;
                u.id = bo.value("id").toInt();
                u.username = bo.value("username").toString();
                u.display_name = bo.value("display_name").toString();
                list.append(u);
            }
            emit blockedFriendsOk(list);
        } else if (path.endsWith("/friends")) {
            QList<FriendInfo> list;
            const QJsonArray arr = obj.value("friends").toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject fo = v.toObject();
                FriendInfo f;
                f.id = fo.value("id").toInt();
                f.username = fo.value("username").toString();
                f.display_name = fo.value("display_name").toString();
                f.avatar_url = fo.value("avatar_url").toString();
                f.bg_url = fo.value("bg_url").toString();
                f.title = fo.value("title").toString();
                f.online = fo.value("online").toBool();
                list.append(f);
            }
            emit friendsOk(list);
        } else if (path.endsWith("/api/inventory")) {
            emit inventoryOk(obj.value("money").toInt(), obj.value("candy0").toInt(),
                             obj.value("candy1").toInt(), obj.value("candy2").toInt());
        } else if (path.endsWith("/api/mails")) {
            QList<MailInfo> list;
            const QJsonArray arr = obj.value("mails").toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject mo = v.toObject();
                MailInfo m;
                m.id = mo.value("id").toInt();
                m.title = mo.value("title").toString();
                m.body = mo.value("body").toString();
                m.attachKind = mo.value("attach_kind").toString();
                m.attachAmount = mo.value("attach_amount").toInt();
                m.claimed = mo.value("claimed").toInt() != 0;
                m.createdAt = mo.value("created_at").toString();
                list.append(m);
            }
            emit mailsOk(list);
        }
    });
}

void ApiClient::postJsonAuth(const QString &path, const QByteArray &json, const QString &accessToken)
{
    QNetworkRequest req((QUrl(ServerConfig::apiUrl(path))));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
    QNetworkReply *reply = m_nam.post(req, json);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QString msg = reply->errorString();
            QByteArray body = reply->readAll();
            if (!body.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (doc.isObject()) {
                    QString err = doc.object().value("error").toString();
                    if (!err.isEmpty()) msg = err;
                }
            }
            emit errorOccurred(msg);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (path.endsWith("/friends/request")) {
            emit friendRequestSent(obj.value("auto_accepted").toBool(false));
        } else if (path.endsWith("/friends/accept")) {
            emit friendAccepted();
        } else if (path.endsWith("/friends/reject")) {
            emit friendRejected();
        } else if (path.endsWith("/block")) {
            emit friendBlocked();
        } else if (path.endsWith("/unblock")) {
            emit friendUnblocked();
        } else if (path.endsWith("/profile") || path.endsWith("/profile/bind-phone")
                   || path.endsWith("/profile/title")) {
            UserInfo user = parseUser(obj.value("user").toObject());
            emit profileUpdated(user);
        } else if (path.endsWith("/api/inventory")) {
            emit inventoryOk(obj.value("money").toInt(), obj.value("candy0").toInt(),
                             obj.value("candy1").toInt(), obj.value("candy2").toInt());
        } else if (path.endsWith("/api/mails/claim")) {
            const QJsonObject inv = obj.value("inventory").toObject();
            emit mailClaimOk(obj.value("id").toInt(), inv.value("money").toInt(),
                             inv.value("candy0").toInt(), inv.value("candy1").toInt(),
                             inv.value("candy2").toInt());
        }
    });
}

void ApiClient::patchJsonAuth(const QString &path, const QByteArray &json, const QString &accessToken)
{
    QNetworkRequest req((QUrl(ServerConfig::apiUrl(path))));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
    QNetworkReply *reply = m_nam.sendCustomRequest(req, "PATCH", json);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(readError(reply));
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (path.endsWith("/profile")) {
            UserInfo user = parseUser(obj.value("user").toObject());
            emit profileUpdated(user);
        }
    });
}

// ---------- 道具库存 / 系统邮件 ----------
void ApiClient::fetchInventory(const QString &accessToken)
{
    getJson("/api/inventory", accessToken);
}

// 进行中对局列表：轮询用，失败静默（不打扰用户，等下一轮刷新）
void ApiClient::listRooms(const QString &accessToken)
{
    QNetworkRequest req((QUrl(ServerConfig::apiUrl(QStringLiteral("/api/rooms")))));
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        QList<LiveRoomInfo> list;
        const QJsonArray arr = obj.value("rooms").toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject ro = v.toObject();
            LiveRoomInfo ri;
            ri.roomId = ro.value("room_id").toString();
            ri.red = parseUser(ro.value("red").toObject());
            ri.black = parseUser(ro.value("black").toObject());
            ri.spectators = ro.value("spectators").toInt();
            list.append(ri);
        }
        emit roomsOk(list);
    });
}

// 邀约机器人目标列表：轮询用，失败静默（下轮再拉）
void ApiClient::fetchBotTargets(const QString &accessToken)
{
    QNetworkRequest req((QUrl(ServerConfig::apiUrl(QStringLiteral("/api/bot/targets")))));
    req.setRawHeader("Authorization", ("Bearer " + accessToken).toUtf8());
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        QList<int> targets;
        for (const QJsonValue &v : obj.value("targets").toArray())
            targets.append(v.toInt());
        emit botTargetsOk(targets);
    });
}

void ApiClient::pushInventory(const QString &accessToken, const QString &key, int delta)
{
    QJsonObject deltas;
    deltas[key] = delta;
    QJsonObject obj;
    obj["deltas"] = deltas;
    postJsonAuth("/api/inventory", QJsonDocument(obj).toJson(QJsonDocument::Compact), accessToken);
}

void ApiClient::listMails(const QString &accessToken)
{
    getJson("/api/mails", accessToken);
}

void ApiClient::claimMail(const QString &accessToken, int mailId)
{
    QJsonObject obj;
    obj["id"] = mailId;
    postJsonAuth("/api/mails/claim", QJsonDocument(obj).toJson(QJsonDocument::Compact), accessToken);
}

UserInfo ApiClient::parseUser(const QJsonObject &obj)
{
    UserInfo u;
    u.id = obj.value("id").toInt();
    u.user_id = obj.value("user_id").toString();
    u.username = obj.value("username").toString();
    u.display_name = obj.value("display_name").toString();
    u.phone = obj.value("phone").toString();
    u.avatar_url = obj.value("avatar_url").toString();
    u.bg_url = obj.value("bg_url").toString();
    u.title = obj.value("title").toString();
    return u;
}

QString ApiClient::readError(QNetworkReply *reply)
{
    QString msg = reply->errorString();
    QByteArray body = reply->readAll();
    if (!body.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
            QString err = doc.object().value("error").toString();
            if (!err.isEmpty()) msg = err;
        }
    }
    return msg;
}
