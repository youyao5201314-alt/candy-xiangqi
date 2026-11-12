// 好友面板实现
#include "FriendsPanel.h"
#include "ChatDialog.h"
#include "InternetClient.h"
#include "MainWindow.h"
#include "ProfileViewDialog.h"
#include "Inventory.h"
#include "ServerConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QListWidget>
#include <QApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QFont>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QSettings>
#include <QPainter>
#include <QTimer>
#include <QPainterPath>
#include <QRegularExpression>

FriendsPanel::FriendsPanel(ApiClient *api, InternetClient *net,
                           const UserInfo &self, const QString &accessToken,
                           QWidget *parent, bool allowInvite)
    : QWidget(parent), m_api(api), m_net(net), m_self(self), m_token(accessToken)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(30, 24, 30, 24);

    m_title = new QLabel(this);
    m_title->setStyleSheet(QStringLiteral("font-size:20px;font-weight:bold;"));
    m_title->setText(allowInvite
        ? QStringLiteral("好友 — %1").arg(
            self.display_name.isEmpty() ? self.username : self.display_name)
        : QStringLiteral("我的好友"));
    root->addWidget(m_title);

    m_status = new QLabel(QStringLiteral("正在连接服务器…"), this);
    m_status->setStyleSheet(QStringLiteral("color:#888;"));
    root->addWidget(m_status);

    // "正在对局"观战区：仅对战页提供（定时轮询，双击/按钮进观战）
    if (allowInvite) {
        auto *roomsTitle = new QLabel(QStringLiteral("⚔️ 正在对局（双击或点「观战」进入观战）"), this);
        roomsTitle->setStyleSheet(QStringLiteral("font-size:14px;color:#555;"));
        root->addWidget(roomsTitle);
        m_roomsList = new QListWidget(this);
        m_roomsList->setStyleSheet(QStringLiteral("font-size:15px;"));
        m_roomsList->setIconSize(QSize(28, 28));
        m_roomsList->setMaximumHeight(112);
        m_roomsList->setAlternatingRowColors(false);
        root->addWidget(m_roomsList);
        m_roomsTimer = new QTimer(this);
        connect(m_roomsTimer, &QTimer::timeout, this, &FriendsPanel::refreshRooms);
        connect(m_roomsList, &QListWidget::itemDoubleClicked, this,
                [this](QListWidgetItem *) { onSpectateRoom(); });
    }

    m_list = new QListWidget(this);
    m_list->setStyleSheet(QStringLiteral("font-size:16px;"));
    m_list->setMinimumHeight(240);
    m_list->setIconSize(QSize(36, 36));
    root->addWidget(m_list, 1);

    // 底部工具条：高频操作平铺，低频操作收进「更多」菜单，退出登录靠右
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto *backBtn = new QPushButton(QStringLiteral("返回"), this);
    auto *addBtn = new QPushButton(QStringLiteral("添加好友"), this);
    auto *inviteBtn = new QPushButton(QStringLiteral("邀请对战"), this);
    auto *chatBtn = new QPushButton(QStringLiteral("私信"), this);
    auto *moreBtn = new QToolButton(this);
    moreBtn->setText(QStringLiteral("更多 ▾"));
    moreBtn->setPopupMode(QToolButton::InstantPopup);
    auto *moreMenu = new QMenu(moreBtn);
    moreBtn->setMenu(moreMenu);
    auto *spectateAct = moreMenu->addAction(QStringLiteral("观战"));
    moreMenu->addSeparator();
    auto *giftAct = moreMenu->addAction(QStringLiteral("送礼"));
    auto *profileAct = moreMenu->addAction(QStringLiteral("资料"));
    auto *blockAct = moreMenu->addAction(QStringLiteral("拉黑"));
    moreMenu->addSeparator();
    auto *refreshAct = moreMenu->addAction(QStringLiteral("刷新"));
    btnRow->addWidget(backBtn);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(inviteBtn);
    btnRow->addWidget(chatBtn);
    btnRow->addWidget(moreBtn);
    btnRow->addStretch();

    auto *logoutBtn = new QPushButton(QStringLiteral("退出登录"), this);
    btnRow->addWidget(logoutBtn);
    root->addLayout(btnRow);

    // 独立好友入口（简洁模式）：不提供开局邀请和退出登录
    if (!allowInvite) {
        inviteBtn->setVisible(false);
        spectateAct->setVisible(false);
        logoutBtn->setVisible(false);
    }

    // REST 信号
    connect(m_api, &ApiClient::friendsOk, this, &FriendsPanel::onFriendsOk);
    // 头像下载完成：按 URL 找到对应好友项，圆形裁剪后回填图标
    connect(m_api, &ApiClient::avatarDownloaded, this,
            [this](const QString &url, const QPixmap &pm) {
        // 观战区头像：入缓存并重绘对局列表。
        // 同一头像可能同时挂在好友列表（女号常在对局中），不能提前 return
        if (m_roomAvatarFetching.remove(url)) {
            m_roomAvatars.insert(url, pm);
            rebuildRoomList();
        }
        if (!m_friendIdByUrl.contains(url))
            return;
        m_friendAvatars.insert(url, pm);   // 入缓存：presence 重建列表时秒回填
        const int fid = m_friendIdByUrl.value(url);
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *it = m_list->item(i);
            if (it->data(Qt::UserRole).toInt() == fid) {
                it->setIcon(QIcon(circleAvatar(pm, 36)));
                break;
            }
        }
    });
    connect(m_api, &ApiClient::roomsOk, this, &FriendsPanel::onRoomsOk);
    connect(m_api, &ApiClient::friendRequestsOk, this, &FriendsPanel::onFriendRequestsOk);
    connect(m_api, &ApiClient::friendRequestSent, this, &FriendsPanel::onFriendRequestSent);
    connect(m_api, &ApiClient::friendAccepted, this, &FriendsPanel::onFriendAccepted);
    connect(m_api, &ApiClient::friendBlocked, this, &FriendsPanel::onFriendBlocked);
    connect(m_api, &ApiClient::friendUnblocked, this, &FriendsPanel::onFriendUnblocked);
    connect(m_api, &ApiClient::errorOccurred, this,
            [this](const QString &msg) {
                m_status->setText(msg);
                QMessageBox::warning(this, QStringLiteral("操作失败"), msg);
            });

    // 实时事件
    connect(m_net, &InternetClient::presenceChanged,
            this, &FriendsPanel::onPresenceChanged);
    connect(m_net, &InternetClient::friendRequestReceived,
            this, &FriendsPanel::onRealtimeFriendRequest);
    connect(m_net, &InternetClient::friendAdded,
            this, &FriendsPanel::onRealtimeFriendAdded);
    // 邀请/匹配已提升到 StartScreen 全局处理（任意页面可收邀请、应答、进对局），
    // 这里不再重复连接，避免双弹窗
    connect(m_net, &InternetClient::serverError,
            this, &FriendsPanel::onServerError);
    connect(m_net, &InternetClient::giftReceived,
            this, &FriendsPanel::onGiftReceived);
    connect(m_net, &InternetClient::dmReceived,
            this, &FriendsPanel::onDmReceived);

    // 按钮
    connect(addBtn, &QPushButton::clicked, this, &FriendsPanel::onAddFriend);
    connect(m_api, &ApiClient::searchOk, this, &FriendsPanel::onSearchOk);
    connect(inviteBtn, &QPushButton::clicked, this, &FriendsPanel::onInviteSelected);
    connect(blockAct, &QAction::triggered, this, [this]() {
        QListWidgetItem *cur = m_list->currentItem();
        if (!cur) {
            QMessageBox::information(this, QStringLiteral("拉黑"),
                                     QStringLiteral("请先选择一位好友"));
            return;
        }
        const int id = cur->data(Qt::UserRole).toInt();
        FriendInfo *f = findFriend(id);
        if (!f) return;
        const auto ret = QMessageBox::question(
            this, QStringLiteral("拉黑好友"),
            QStringLiteral("确定要拉黑 %1 吗？将同时删除好友关系。")
                .arg(f->display_name.isEmpty() ? f->username : f->display_name),
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes)
            m_api->blockFriend(m_token, id);
    });
    connect(profileAct, &QAction::triggered, this, [this]() {
        QListWidgetItem *cur = m_list->currentItem();
        if (!cur && !m_list->selectedItems().isEmpty())
            cur = m_list->selectedItems().first();
        if (!cur) {
            QMessageBox::information(this, QStringLiteral("好友资料"),
                                     QStringLiteral("请先选择一位好友"));
            return;
        }
        ProfileViewDialog dlg(m_api, m_token, cur->data(Qt::UserRole).toInt(), this);
        dlg.exec();
    });
    connect(giftAct, &QAction::triggered, this, [this]() {
        QListWidgetItem *cur = m_list->currentItem();
        if (!cur && !m_list->selectedItems().isEmpty())
            cur = m_list->selectedItems().first();
        if (!cur) {
            QMessageBox::information(this, QStringLiteral("送礼"),
                                     QStringLiteral("请先选择一位好友"));
            return;
        }
        const int id = cur->data(Qt::UserRole).toInt();
        FriendInfo *f = findFriend(id);
        if (!f) return;
        const QString name = f->display_name.isEmpty() ? f->username : f->display_name;
        const bool offline = !f->online;
        // 礼物清单：emoji / 名称 / 价格（0=免费祝福）
        struct GiftItem { QString emoji, name; int price; };
        const QList<GiftItem> gifts = {
            { QStringLiteral("🎁"), QStringLiteral("祝福"), 0 },
            { QStringLiteral("🌹"), QStringLiteral("鲜花"), 200 },
            { QStringLiteral("🍫"), QStringLiteral("巧克力"), 500 },
            { QStringLiteral("🏆"), QStringLiteral("奖杯"), 1000 },
        };
        // 礼物选择弹窗：2×2 网格
        QDialog pick(this);
        pick.setWindowTitle(QStringLiteral("给 %1 送礼").arg(name));
        auto *gl = new QGridLayout(&pick);
        int chosen = -1;
        for (int i = 0; i < gifts.size(); ++i) {
            auto *btn = new QPushButton(&pick);
            btn->setText(gifts[i].emoji + QStringLiteral("\n") + gifts[i].name +
                         (gifts[i].price > 0
                              ? QStringLiteral("\n%1￥").arg(gifts[i].price)
                              : QStringLiteral("\n免费")));
            btn->setFixedSize(110, 92);
            btn->setStyleSheet(QStringLiteral(
                "font-size:15px; padding:4px; background:#fff8f0; border:2px solid #ffd59e;"
                "border-radius:8px; QPushButton:hover{background:#ffe9c8;}"));
            connect(btn, &QPushButton::clicked, &pick, [&pick, &chosen, i]() {
                chosen = i;
                pick.accept();
            });
            gl->addWidget(btn, i / 2, i % 2);
        }
        if (pick.exec() != QDialog::Accepted || chosen < 0)
            return;
        const GiftItem &g = gifts[chosen];
        // 付费礼物先扣货币（与商城/结算同一余额）
        if (g.price > 0) {
            QSettings st;
            const int balance = st.value(QStringLiteral("money"), 0).toInt();
            if (balance < g.price) {
                QMessageBox::warning(this, QStringLiteral("送礼"),
                                     QStringLiteral("货币不足（需要 %1￥，当前 %2￥）\n去人机对战赢钱再来吧！")
                                         .arg(g.price).arg(balance));
                return;
            }
            st.setValue(QStringLiteral("money"), balance - g.price);
        }
        m_net->sendGift(id, g.emoji + QStringLiteral(" 送你一份") + g.name +
                                QStringLiteral("！"));
        if (offline)
            m_status->setText(QStringLiteral("%1 不在线，%2%3 将在 TA 上线时送达")
                                  .arg(name, g.emoji, g.name));
        else if (g.price > 0)
            m_status->setText(QStringLiteral("已向 %1 送出 %2%3（花费 %4￥）")
                                  .arg(name, g.emoji, g.name).arg(g.price));
        else
            m_status->setText(QStringLiteral("已向 %1 送出祝福").arg(name));
    });
    connect(chatBtn, &QPushButton::clicked, this, [this]() {
        // currentItem 与 selectedItems 都认（键盘焦点行与选中行在个别场景下不同）
        QListWidgetItem *cur = m_list->currentItem();
        if (!cur && !m_list->selectedItems().isEmpty())
            cur = m_list->selectedItems().first();
        if (!cur) {
            QMessageBox::information(this, QStringLiteral("私信"),
                                     QStringLiteral("请先选择一位好友"));
            return;
        }
        const int id = cur->data(Qt::UserRole).toInt();
        FriendInfo *f = findFriend(id);
        if (!f) return;
        // 离线也允许私信：服务端存离线消息，对方上线时自动送达
        onDmReceived(id,
                     f->display_name.isEmpty() ? f->username : f->display_name,
                     f->avatar_url,
                     QString());   // 空文本=只打开窗口不追加消息
        if (!f->online)
            m_status->setText(QStringLiteral("对方当前不在线，留言将在 TA 上线时送达"));
    });
    connect(refreshAct, &QAction::triggered, this, [this]() {
        refreshFriends();
        refreshRooms();
    });
    connect(spectateAct, &QAction::triggered, this, &FriendsPanel::onSpectateRoom);
    connect(backBtn, &QPushButton::clicked, this, &FriendsPanel::back);
    connect(logoutBtn, &QPushButton::clicked, this, &FriendsPanel::logout);
    // 双击在线好友直接邀请
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *) { onInviteSelected(); });
}

void FriendsPanel::start()
{
    m_status->setText(QStringLiteral("已连接，正在加载好友…"));
    refreshFriends();
    m_api->listFriendRequests(m_token);
    // 观战区：立即拉一次 + 定时轮询（8 秒）
    if (m_roomsList) {
        refreshRooms();
        if (m_roomsTimer)
            m_roomsTimer->start(8000);
    }
}

void FriendsPanel::refreshFriends()
{
    m_api->listFriends(m_token);
}

// ===================== "正在对局"观战区 =====================
void FriendsPanel::refreshRooms()
{
    if (m_roomsList)
        m_api->listRooms(m_token);
}

void FriendsPanel::onRoomsOk(const QList<LiveRoomInfo> &rooms)
{
    m_lastRooms = rooms;
    rebuildRoomList();
}

void FriendsPanel::rebuildRoomList()
{
    if (!m_roomsList)
        return;
    // 保留当前选中行（轮询重绘时不打断用户选择）
    const QString cur = m_roomsList->currentItem()
        ? m_roomsList->currentItem()->data(Qt::UserRole).toString()
        : QString();
    m_roomsList->clear();

    if (m_lastRooms.isEmpty()) {
        QListWidgetItem *it = new QListWidgetItem(
            QStringLiteral("暂时没有进行中的对局"), m_roomsList);
        it->setFlags(Qt::NoItemFlags);
        it->setForeground(QColor(0x99, 0x99, 0x99));
        return;
    }

    for (const LiveRoomInfo &r : m_lastRooms) {
        const QString redName = r.red.display_name.isEmpty()
            ? r.red.username : r.red.display_name;
        const QString blackName = r.black.display_name.isEmpty()
            ? r.black.username : r.black.display_name;
        QListWidgetItem *it = new QListWidgetItem(QStringLiteral(
            "🔴 %1   ⚔️   ⚫ %2      👁 %3 人围观").arg(redName, blackName).arg(r.spectators),
            m_roomsList);
        it->setData(Qt::UserRole, r.roomId);

        // 图标：红方头像（缓存直用，否则发一次下载，完成后重绘）
        const QString url = r.red.avatar_url;
        if (!url.isEmpty() && m_roomAvatars.contains(url)) {
            it->setIcon(QIcon(circleAvatar(m_roomAvatars.value(url), 28)));
        } else if (!url.isEmpty() && !m_roomAvatarFetching.contains(url)) {
            m_roomAvatarFetching.insert(url);
            m_api->downloadAvatar(ServerConfig::serverUrl() + url);
            it->setIcon(QIcon(defaultAvatar(redName, r.red.id)));
        } else {
            it->setIcon(QIcon(defaultAvatar(redName, r.red.id)));
        }
        if (r.roomId == cur)
            m_roomsList->setCurrentItem(it);
    }
}

void FriendsPanel::onSpectateRoom()
{
    if (!m_roomsList)
        return;
    QListWidgetItem *cur = m_roomsList->currentItem();
    if (!cur && !m_roomsList->selectedItems().isEmpty())
        cur = m_roomsList->selectedItems().first();
    if (!cur)
        return;
    const QString roomId = cur->data(Qt::UserRole).toString();
    if (roomId.isEmpty())
        return;
    m_status->setText(QStringLiteral("正在进入观战…"));
    emit spectateRequested(roomId);
}

void FriendsPanel::onFriendsOk(const QList<FriendInfo> &friends)
{
    m_friends = friends;
    rebuildList();
    if (friends.isEmpty()) {
        m_status->setText(QStringLiteral("还没有好友，点下方「添加好友」认识新朋友吧"));
        return;
    }
    int onlineCount = 0;
    for (const auto &f : friends)
        if (f.online) onlineCount++;
    m_status->setText(QStringLiteral("共 %1 位好友，%2 位在线")
                          .arg(friends.size()).arg(onlineCount));
}

void FriendsPanel::onFriendRequestsOk(const QList<FriendRequestInfo> &requests)
{
    // 进入面板时收到的历史待处理请求，逐个询问
    for (const auto &r : requests)
        askFriendRequest(r.request_id, r.from, r.message);
}

void FriendsPanel::onFriendRequestSent(bool autoAccepted)
{
    if (autoAccepted) {
        QMessageBox::information(this, QStringLiteral("好友"),
                                 QStringLiteral("对方之前已向你发过请求，已自动互为好友"));
        refreshFriends();
    } else {
        QMessageBox::information(this, QStringLiteral("好友请求已发送"),
                                 QStringLiteral("等待对方确认"));
    }
}

void FriendsPanel::onFriendAccepted()
{
    refreshFriends();
}

void FriendsPanel::onAddFriend()
{
    // 搜索式添加好友：支持昵称模糊搜索 + ID 精确搜索
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("添加好友"));
    dlg.resize(440, 460);
    auto *lay = new QVBoxLayout(&dlg);

    auto *tip = new QLabel(
        QStringLiteral("输入对方昵称（支持模糊）或 ID（如 CB839204），点搜索后选择结果"),
        &dlg);
    tip->setStyleSheet(QStringLiteral("color:#666;font-size:12px;"));
    tip->setWordWrap(true);
    lay->addWidget(tip);

    auto *searchRow = new QHBoxLayout;
    auto *searchEdit = new QLineEdit(&dlg);
    searchEdit->setPlaceholderText(QStringLiteral("昵称 或 ID"));
    auto *searchBtn = new QPushButton(QStringLiteral("搜索"), &dlg);
    searchRow->addWidget(searchEdit, 1);
    searchRow->addWidget(searchBtn);
    lay->addLayout(searchRow);

    auto *resultList = new QListWidget(&dlg);
    resultList->setStyleSheet(QStringLiteral("font-size:15px;"));
    lay->addWidget(resultList, 1);

    auto *msgEdit = new QLineEdit(&dlg);
    msgEdit->setPlaceholderText(QStringLiteral("验证消息（选填，最多 200 字）"));
    msgEdit->setMaxLength(200);
    lay->addWidget(msgEdit);

    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("发送好友请求"));
    lay->addWidget(btns);

    m_searchList = resultList;
    m_searchEdit = searchEdit;
    m_pendingIdQuery.clear();

    connect(searchBtn, &QPushButton::clicked, &dlg, [this]() { doFriendSearch(); });
    connect(searchEdit, &QLineEdit::returnPressed, &dlg, [this]() { doFriendSearch(); });
    connect(btns, &QDialogButtonBox::accepted, &dlg, [this, &dlg, resultList, msgEdit]() {
        QListWidgetItem *cur = resultList->currentItem();
        if (!cur) {
            QMessageBox::information(this, QStringLiteral("添加好友"),
                                     QStringLiteral("请先搜索并选中一位玩家"));
            return;
        }
        const QString username = cur->data(Qt::UserRole).toString();
        if (username.isEmpty()) {   // "未找到"占位项不可用
            QMessageBox::information(this, QStringLiteral("添加好友"),
                                     QStringLiteral("请先搜索并选中一位玩家"));
            return;
        }
        dlg.setProperty("user", username);
        dlg.setProperty("msg", msgEdit->text().trimmed());
        dlg.accept();
    });
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(&dlg, &QDialog::finished, this, [this]() {
        m_searchList = nullptr;   // QPointer 置空，防止迟到的搜索回调悬空
        m_searchEdit = nullptr;
    });

    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString username = dlg.property("user").toString();
    if (username.isEmpty())
        return;
    m_api->sendFriendRequest(m_token, username, dlg.property("msg").toString());
}

void FriendsPanel::doFriendSearch()
{
    if (m_searchList.isNull() || m_searchEdit.isNull())
        return;
    const QString q = m_searchEdit->text().trimmed();
    if (q.isEmpty())
        return;
    m_searchList->clear();
    m_pendingIdQuery.clear();

    static const QRegularExpression pureDigits(QStringLiteral("^\\d{1,10}$"));
    static const QRegularExpression cbId(QStringLiteral("^(CB|cb)?\\d{1,10}$"));

    auto *wait = new QListWidgetItem(QStringLiteral("搜索中…"), m_searchList.data());
    wait->setFlags(Qt::NoItemFlags);

    if (pureDigits.match(q).hasMatch()) {
        m_pendingIdQuery = q;                    // 空结果时自动补 CB 前缀重查
        m_api->searchUsers(q, QStringLiteral("id"), m_token);
    } else if (cbId.match(q).hasMatch()) {       // 用户直接输 CB839204 / cb839204
        QString id = q.toUpper();
        if (!id.startsWith(QStringLiteral("CB")))
            id.prepend(QStringLiteral("CB"));
        m_api->searchUsers(id, QStringLiteral("id"), m_token);
    } else {
        m_api->searchUsers(q, QStringLiteral("name"), m_token);
    }
}

void FriendsPanel::onSearchOk(const QList<UserInfo> &users)
{
    if (m_searchList.isNull())
        return;
    m_searchList->clear();

    if (users.isEmpty()) {
        if (!m_pendingIdQuery.isEmpty()) {       // 纯数字没查到 → 补 CB 前缀再查一次
            const QString q = m_pendingIdQuery;
            m_pendingIdQuery.clear();
            auto *wait = new QListWidgetItem(QStringLiteral("搜索中…"), m_searchList.data());
            wait->setFlags(Qt::NoItemFlags);
            m_api->searchUsers(QStringLiteral("CB") + q, QStringLiteral("id"), m_token);
            return;
        }
        auto *it = new QListWidgetItem(
            QStringLiteral("没有找到该玩家，可试试昵称模糊搜索"), m_searchList.data());
        it->setFlags(Qt::NoItemFlags);
        return;
    }
    m_pendingIdQuery.clear();
    for (const UserInfo &u : users) {
        auto *it = new QListWidgetItem(
            QStringLiteral("%1 (%2)   ID:%3").arg(
                u.display_name.isEmpty() ? u.username : u.display_name,
                u.username, u.user_id),
            m_searchList.data());
        it->setData(Qt::UserRole, u.username);
    }
}

void FriendsPanel::onInviteSelected()
{
    // currentItem 与 selectedItems 都认（键盘焦点行与选中行在个别场景下不同）
    QListWidgetItem *cur = m_list->currentItem();
    if (!cur && !m_list->selectedItems().isEmpty())
        cur = m_list->selectedItems().first();
    if (!cur) {
        QMessageBox::information(this, QStringLiteral("邀请对战"),
                                 QStringLiteral("请先选择一位好友"));
        return;
    }
    const int id = cur->data(Qt::UserRole).toInt();
    FriendInfo *f = findFriend(id);
    if (!f)
        return;
    if (!f->online) {
        QMessageBox::information(this, QStringLiteral("好友不在线"),
                                 QStringLiteral("%1 当前不在线，无法邀请").arg(f->display_name));
        return;
    }
    m_net->invite(id);
    m_status->setText(QStringLiteral("已向 %1 发送邀请，等待回应…").arg(f->display_name));
}

void FriendsPanel::onPresenceChanged(int userId, bool online)
{
    FriendInfo *f = findFriend(userId);
    if (f) {
        f->online = online;
        rebuildList();
    }
}

void FriendsPanel::onRealtimeFriendRequest(int requestId, const UserInfo &from,
                                            const QString &message)
{
    askFriendRequest(requestId, from, message);
}

void FriendsPanel::onRealtimeFriendAdded(const UserInfo &)
{
    refreshFriends();
}

void FriendsPanel::onServerError(const QString &code, const QString &msg)
{
    QMessageBox::warning(this, QStringLiteral("服务器") + code, msg);
    m_status->setText(msg);
}

void FriendsPanel::rebuildList()
{
    m_list->clear();
    // 空列表：放一只小奶龙陪着，顺便引导去加好友
    if (m_friends.isEmpty()) {
        const QStringList lines = {
            QStringLiteral(""),
            QStringLiteral(""),
            QStringLiteral("还没有好友，小奶龙一个人有点孤单…"),
            QStringLiteral(""),
            QStringLiteral("点击下方「添加好友」"),
            QStringLiteral("输入 TA 的 ID 或昵称，发出好友请求吧！"),
            QStringLiteral("加到好友后就能邀请对战、送礼物啦 ")
        };
        for (int i = 0; i < lines.size(); i++) {
            auto *item = new QListWidgetItem(lines[i], m_list);
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(Qt::NoItemFlags);   // 不可选中不可点
            if (i == 1) {
                QFont f = item->font();
                f.setPointSize(28);
                item->setFont(f);
            } else {
                item->setForeground(QBrush(QColor(150, 150, 150)));
            }
        }
        return;
    }
    for (const auto &f : m_friends) {
        const QString dot = f.online ? QStringLiteral("🟢 ") : QStringLiteral("⚪ ");
        const QString name = f.display_name.isEmpty() ? f.username : f.display_name;
        const QString text = dot +
            QStringLiteral("%1（%2）").arg(name, f.username);
        auto *item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, f.id);
        // 头像：先放彩色首字占位圆（永不留空），有自定义头像再替换为照片。
        // presence 事件会频繁整体重建列表，图标必须每次都能立刻恢复
        if (!f.avatar_url.isEmpty()) {
            const QString full = ServerConfig::serverUrl() + f.avatar_url;
            m_friendIdByUrl.insert(full, f.id);
            if (m_friendAvatars.contains(full)) {
                item->setIcon(QIcon(circleAvatar(m_friendAvatars.value(full), 36)));
            } else {
                item->setIcon(QIcon(defaultAvatar(name, f.id)));
                if (!m_fetchedAvatarUrls.contains(full)) {
                    m_fetchedAvatarUrls.insert(full);
                    m_api->downloadAvatar(full);
                }
            }
        } else {
            item->setIcon(QIcon(defaultAvatar(name, f.id)));
        }
    }
}

// 圆形裁剪头像（好友列表 36×36）
QPixmap FriendsPanel::circleAvatar(const QPixmap &src, int size)
{
    QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    int x = (scaled.width() - size) / 2, y = (scaled.height() - size) / 2;
    QPixmap cropped = scaled.copy(x, y, size, size);
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addEllipse(0, 0, size, size);
    p.setClipPath(path);
    p.drawPixmap(0, 0, cropped);
    p.end();
    return out;
}

// 无自定义头像时：按 id 取色的彩色圆底 + 名字首字
QPixmap FriendsPanel::defaultAvatar(const QString &name, int id)
{
    static const QColor palette[] = {
        QColor(255, 138, 128), QColor(129, 199, 132), QColor(129, 212, 250),
        QColor(206, 147, 216), QColor(255, 213, 79),  QColor(255, 171, 145),
        QColor(120, 204, 205), QColor(240, 152, 190)
    };
    const QColor base = palette[qAbs(id) % 8];
    const int size = 36;
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(base);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(18);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter,
               name.left(1).toUpper());
    p.end();
    return out;
}

void FriendsPanel::askFriendRequest(int requestId, const UserInfo &from,
                                     const QString &message)
{
    const QString name = from.display_name.isEmpty() ? from.username : from.display_name;
    QString text = QStringLiteral("%1（%2）请求加你为好友，是否同意？")
                       .arg(name).arg(from.username);
    if (!message.isEmpty())
        text += QStringLiteral("\n\n验证消息：%1").arg(message);
    const auto ret = QMessageBox::question(
        this, QStringLiteral("好友请求"), text,
        QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes)
        m_api->acceptFriendRequest(m_token, requestId);
    else
        m_api->rejectFriendRequest(m_token, requestId);
}

void FriendsPanel::onFriendBlocked()
{
    refreshFriends();
    m_status->setText(QStringLiteral("已拉黑该好友"));
}

void FriendsPanel::onFriendUnblocked()
{
    m_status->setText(QStringLiteral("已取消拉黑"));
}

void FriendsPanel::onGiftReceived(int fromUserId, const QString &fromName, const QString &text)
{
    Q_UNUSED(fromUserId);
    // 入背包：糖果计数/礼物清单/总数统一在 Inventory 维护
    Inventory::addGift(fromName, text);
    const Inventory::Gift g = Inventory::gifts().value(0);
    // 对局中不弹窗打扰
    if (qobject_cast<MainWindow*>(QApplication::activeWindow()))
        return;
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("收到礼物"));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral(
                    "<div align='center'>"
                    "<span style='font-size:46px;'>%1</span><br>"
                    "<b style='font-size:16px;'>%2</b> 送你一份%3<br>"
                    "<span style='color:#888;'>已放入背包，去「背包」查看吧</span>"
                    "</div>")
                    .arg(g.emoji.toHtmlEscaped(),
                         fromName.toHtmlEscaped(),
                         g.name.toHtmlEscaped()));
    box.exec();
}

void FriendsPanel::onDmReceived(int fromUserId, const QString &fromName,
                                const QString &fromAvatar, const QString &text)
{
    // 对局中（计数判定，activeWindow 在安卓/瞬态窗口下不可靠）：不弹独立聊天窗，
    // 消息写入聊天窗缓存（不 show），回菜单后打开可见
    const bool inGame = MainWindow::gameWindowOpen()
                        || qobject_cast<MainWindow*>(QApplication::activeWindow());
    ChatDialog *dlg = m_chatDialogs.value(fromUserId, nullptr);
    if (!dlg) {
        FriendInfo *f = findFriend(fromUserId);
        const QString avatar = f ? f->avatar_url : fromAvatar;  // 列表没加载完时用 dm 自带头像
        dlg = new ChatDialog(m_net, m_self.id, fromUserId, fromName, this,
                             avatar, m_api);
        // 窗口关闭时只隐藏+从表移除（下次收到再新建；Qt::WA_DeleteOnClose 更省事）
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QObject::destroyed, this, [this, fromUserId]() {
            m_chatDialogs.remove(fromUserId);
        });
        m_chatDialogs.insert(fromUserId, dlg);
    }
    if (!text.isEmpty())
        dlg->receiveMessage(fromName, text);
    if (inGame)
        return;   // 对局中只缓存不贴脸
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

FriendInfo *FriendsPanel::findFriend(int id)
{
    for (auto &f : m_friends)
        if (f.id == id) return &f;
    return nullptr;
}

QString FriendsPanel::friendAvatarUrl(int id)
{
    FriendInfo *f = findFriend(id);
    return f ? f->avatar_url : QString();
}
