#ifndef INVENTORY_H
#define INVENTORY_H

#include <QString>
#include <QList>
#include <functional>

// 背包/货币/礼物 本地存档（QSettings）
// 货币与糖果沿用既有键：money / candy0..2；礼物与道具为本次新增
// 登录后可通过 setSyncHook 把本地变动增量上报服务器（服务端 coins/candy0..2 列）
namespace Inventory {

// 一份收到的礼物
struct Gift {
    QString emoji;       // 礼物表情（🎁🌹🍫🏆）
    QString name;        // 礼物名（祝福/鲜花/巧克力/奖杯）
    QString from;        // 赠送者昵称
    QString time;        // 本地接收时间（yyyy-MM-dd hh:mm）
};

// ---- 货币 ----
int money();
void addMoney(int delta);
bool spendMoney(int cost);   // 余额不足返回 false 且不扣

// ---- 糖果（idx=0/1/2，与商城一致） ----
int candyCount(int idx);
void addCandy(int idx, int n = 1);

// ---- 通用道具（key 如 "mimimimi"，存 item_<key>） ----
int itemCount(const QString &key);
void addItem(const QString &key, int n = 1);

// ---- 礼物 ----
void addGift(const QString &from, const QString &rawText);  // rawText 形如 "🌹 送你一份鲜花！"
QList<Gift> gifts();         // 最新的在前
int giftsTotal();

// ---- 服务端同步 ----
// 变动钩子：key ∈ money/candy0..2，delta 增量；登录后由 StartScreen 注入
void setSyncHook(const std::function<void(const QString &key, int delta)> &fn);
// 服务器全量覆盖本地（登录拉取/领取邮件后调用；直接写 QSettings，不触发钩子）
void setFromServer(int money, int candy0, int candy1, int candy2);
// ---- 账号归属 ----
// 本地缓存属于哪个账号；切换账号时清空货币/糖果缓存，防止新账号继承上一账号余额
void setOwner(int userId);
int ownerId();

}  // namespace Inventory

#endif  // INVENTORY_H
