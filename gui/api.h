#ifndef API_H
#define API_H

#include <QObject>
#include <QString>
#include <QList>
#include <QNetworkAccessManager>

// 用户公开信息（与服务器 users 表对齐）
struct UserInfo {
    int id = 0;
    QString user_id;      // 用户可见唯一 ID（如 "CB839204"）
    QString username;
    QString display_name;
    QString phone;        // 已绑定的手机号（未绑定为空串）
    QString avatar_url;   // 头像相对路径（如 /avatars/3_17xx.png）
    QString bg_url;       // 个人主页背景相对路径（如 /bgs/bg_3_17xx.jpg），空=默认
    QString title;        // 头衔（全服第一/全服第二/全服最拉，服务器按手机号实时计算）
};

// 好友信息（GET /api/friends 返回元素）
struct FriendInfo {
    int id = 0;
    QString username;
    QString display_name;
    QString avatar_url;   // 头像相对路径（如 /avatars/3_17xx.png），空=无自定义头像
    QString bg_url;       // 背景图相对路径（空=默认背景）
    QString title;        // 头衔
    bool online = false;
};

// 收到的好友请求（GET /api/friends/requests 返回元素）
struct FriendRequestInfo {
    int request_id = 0;
    UserInfo from;
    QString message;   // 验证消息（可选）
};

// REST API 客户端：register / login / refresh / logout / me
// 所有方法异步，结果通过信号返回（错误也走 errorOccurred 信号，不抛异常）
// 阶段 1 只用前 5 个接口；阶段 2 起会扩展好友/对局历史等
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);

    // POST /api/auth/register  body: {username, password, display_name?, phone, code}
    void registerUser(const QString &username, const QString &password, const QString &displayName,
                      const QString &phone, const QString &code);

    // POST /api/auth/login  body: {username, password}
    void login(const QString &username, const QString &password);

    // POST /api/auth/refresh  body: {refreshToken}
    void refreshAccessToken(const QString &refreshToken);

    // POST /api/auth/logout  body: {refreshToken}
    void logout(const QString &refreshToken);

    // GET /api/auth/me  需要 Authorization: Bearer <accessToken>
    void getMe(const QString &accessToken);

    // ---------- 手机验证码登录 ----------
    // POST /api/auth/send-code  body: {phone, purpose}  purpose ∈ {login, bind}
    void sendCode(const QString &phone, const QString &purpose);
    // POST /api/auth/phone-login  body: {phone, code}  响应与 /login 同构 → loginOk
    void phoneLogin(const QString &phone, const QString &code);

    // POST /api/auth/reset-password  body: {phone, code, new_password}
    // 响应与 /login 同构 → resetPasswordOk
    void resetPassword(const QString &phone, const QString &code, const QString &newPassword);

    // GET /api/auth/search?q=xxx&type=id|name  需要 auth header → searchOk
    void searchUsers(const QString &query, const QString &type, const QString &accessToken);

    // POST /api/auth/guest  无需参数 → 响应与 /login 同构 → loginOk
    void guestLogin();

    // ---------- 个人资料 ----------
    // PATCH /api/profile  body: {display_name}
    void updateProfile(const QString &accessToken, const QString &displayName);
    // POST /api/profile/bind-phone  body: {phone, code}
    void bindPhone(const QString &accessToken, const QString &phone, const QString &code);
    // POST /api/profile/avatar  multipart 上传
    void uploadAvatar(const QString &accessToken, const QString &filePath);
    // POST /api/profile/bg  multipart 上传个人主页背景
    void uploadProfileBg(const QString &accessToken, const QString &filePath);
    // POST /api/profile/title  body: {title}（空串=卸下成就称号）
    void setTitle(const QString &accessToken, const QString &title);
    // GET /api/users/:id/profile 查看好友资料 → userProfileOk
    void getUserProfile(const QString &accessToken, int userId);
    // 下载头像图（GET /avatars/xxx.png）
    void downloadAvatar(const QString &url);

    // ---------- 阶段 2：好友 ----------
    // GET /api/friends
    void listFriends(const QString &accessToken);
    // GET /api/friends/requests
    void listFriendRequests(const QString &accessToken);
    // POST /api/friends/request  body: {username, message?}
    void sendFriendRequest(const QString &accessToken, const QString &username,
                           const QString &message = QString());
    // POST /api/friends/accept  body: {request_id}
    void acceptFriendRequest(const QString &accessToken, int requestId);
    // POST /api/friends/reject  body: {request_id}
    void rejectFriendRequest(const QString &accessToken, int requestId);
    // POST /api/friends/:id/block
    void blockFriend(const QString &accessToken, int userId);
    // POST /api/friends/:id/unblock
    void unblockFriend(const QString &accessToken, int userId);
    // GET /api/friends/blocked
    void listBlockedFriends(const QString &accessToken);

    // refresh_token 持久化（QSettings）
    static QString loadRefreshToken();
    static void saveRefreshToken(const QString &token);
    static void clearRefreshToken();

signals:
    void registerOk(const UserInfo &user, const QString &accessToken, const QString &refreshToken);
    void loginOk(const UserInfo &user, const QString &accessToken, const QString &refreshToken);
    void resetPasswordOk(const UserInfo &user, const QString &accessToken, const QString &refreshToken);
    void refreshOk(const QString &accessToken);
    void meOk(const UserInfo &user);
    void logoutOk();
    void searchOk(const QList<UserInfo> &users);
    // 任何 API 错误（HTTP 4xx/5xx 或网络失败）都走这个信号
    void errorOccurred(const QString &msg);

    // 阶段 2 好友信号
    void friendsOk(const QList<FriendInfo> &friends);
    void friendRequestsOk(const QList<FriendRequestInfo> &requests);
    void friendRequestSent(bool autoAccepted);
    void friendAccepted();
    void friendRejected();
    void friendBlocked();
    void friendUnblocked();
    void blockedFriendsOk(const QList<UserInfo> &blocked);

    // 手机验证码信号
    void sendCodeOk(const QString &devCode);  // 开发期回显验证码
    // 个人资料信号（PATCH / bind-phone / avatar 响应都是 {user}）
    void profileUpdated(const UserInfo &user);
    // 头像下载完成
    void avatarDownloaded(const QString &url, const QPixmap &pixmap);
    // 查看好友资料完成
    void userProfileOk(const UserInfo &user);

private:
    QNetworkAccessManager m_nam;

    // 内部：发 POST JSON 请求（不带鉴权，注册/登录用）
    void postJson(const QString &path, const QByteArray &json);
    // 内部：发 GET 请求，带 Bearer token
    void getJson(const QString &path, const QString &accessToken);
    // 内部：发 POST JSON 请求，带 Bearer token
    void postJsonAuth(const QString &path, const QByteArray &json, const QString &accessToken);
    // 内部：发 PATCH JSON 请求，带 Bearer token
    void patchJsonAuth(const QString &path, const QByteArray &json, const QString &accessToken);

    // 把服务器返回的 user JSON 对象解析成 UserInfo
    static UserInfo parseUser(const class QJsonObject &obj);
    // 从 reply 读取错误消息（{"error":"..."}）
    static QString readError(class QNetworkReply *reply);
};

#endif  // API_H
