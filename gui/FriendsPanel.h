#ifndef FRIENDSPANEL_H
#define FRIENDSPANEL_H

#include <QWidget>
#include <QList>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QLineEdit>
#include "api.h"

class QListWidget;
class QListWidgetItem;
class QLabel;
class QTimer;
class ApiClient;
class InternetClient;
class ChatDialog;

// 互联网模式好友面板：
// - 好友列表（在线状态实时刷新）
// - 添加好友（按用户名）/ 处理收到的好友请求（弹窗）
// - 邀请在线好友对战 / 接收邀请弹窗
// - 阶段 2 匹配成功后弹提示（阶段 3 起改为直接进入对局）
class FriendsPanel : public QWidget
{
    Q_OBJECT
public:
    // allowInvite=false 为独立「好友」入口（简洁模式）：隐藏邀请对战/退出登录
    explicit FriendsPanel(ApiClient *api, InternetClient *net,
                          const UserInfo &self, const QString &accessToken,
                          QWidget *parent = nullptr, bool allowInvite = true);

    // WebSocket hello 后调用：拉好友列表 + 待处理请求
    void start();

    // 好友头像相对路径（如 /avatars/girl1.jpg），找不到好友或无自定义头像返回空
    QString friendAvatarUrl(int id);

signals:
    void logout();   // 用户点退出登录
    void back();     // 用户点返回：断开实时连接回主菜单（不清登录态）
    void spectateRequested(const QString &roomId);   // 点了某场进行中对局 → 观战

private slots:
    void refreshFriends();
    void refreshRooms();          // 轮询进行中的对局（观战入口）
    void onRoomsOk(const QList<LiveRoomInfo> &rooms);
    void onFriendsOk(const QList<FriendInfo> &friends);
    void onFriendRequestsOk(const QList<FriendRequestInfo> &requests);
    void onFriendRequestSent(bool autoAccepted);
    void onFriendAccepted();
    void onFriendBlocked();
    void onFriendUnblocked();

    void onAddFriend();
    void onSearchOk(const QList<UserInfo> &users);  // 添加好友搜索结果
    void onInviteSelected();

    // 实时事件（对战邀请/匹配已在 StartScreen 全局处理）
    void onPresenceChanged(int userId, bool online);
    void onRealtimeFriendRequest(int requestId, const UserInfo &from, const QString &message);
    void onRealtimeFriendAdded(const UserInfo &user);
    void onServerError(const QString &code, const QString &msg);
    void onGiftReceived(int fromUserId, const QString &fromName, const QString &text);
    void onDmReceived(int fromUserId, const QString &fromName, const QString &fromAvatar,
                      const QString &text);

private:
    ApiClient *m_api;
    InternetClient *m_net;
    UserInfo m_self;
    QString m_token;
    QList<FriendInfo> m_friends;
    QHash<int, ChatDialog*> m_chatDialogs;   // 每个好友一个私信窗口
    QHash<QString, int> m_friendIdByUrl;     // 头像URL → 好友id（下载完成回填图标）
    QHash<QString, QPixmap> m_friendAvatars; // 头像URL → 原图缓存（重建列表时秒回填，防图标丢失）
    QSet<QString> m_fetchedAvatarUrls;       // 已下载过的头像URL，刷新时不重复下载

    QListWidget *m_list = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_status = nullptr;

    // "正在对局"观战区（仅 allowInvite=true 的对战页）
    QListWidget *m_roomsList = nullptr;
    QTimer *m_roomsTimer = nullptr;
    QList<LiveRoomInfo> m_lastRooms;          // 最近一次列表（头像下载完成后重绘）
    QHash<QString, QPixmap> m_roomAvatars;    // 头像URL → 原图缓存
    QSet<QString> m_roomAvatarFetching;       // 已发起下载的URL
    void rebuildRoomList();
    void onSpectateRoom();

    void rebuildList();
    void askFriendRequest(int requestId, const UserInfo &from, const QString &message);
    void doFriendSearch();   // 添加好友对话框：按昵称/ID搜索
    FriendInfo *findFriend(int id);
    static QPixmap circleAvatar(const QPixmap &src, int size);   // 圆形裁剪
    static QPixmap defaultAvatar(const QString &name, int id);   // 无头像时的彩色首字圆底

    // 添加好友搜索对话框（exec 期间有效，QPointer 防悬空）
    QPointer<QListWidget> m_searchList;
    QPointer<QLineEdit> m_searchEdit;
    QString m_pendingIdQuery;   // 纯数字 ID 待补 CB 前缀重查
};

#endif // FRIENDSPANEL_H
