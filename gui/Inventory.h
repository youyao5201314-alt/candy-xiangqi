#ifndef INVENTORY_H
#define INVENTORY_H

#include <QString>
#include <QList>

// 背包/货币/礼物 本地存档（QSettings）
// 货币与糖果沿用既有键：money / candy0..2；礼物与道具为本次新增
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

}  // namespace Inventory

#endif  // INVENTORY_H
