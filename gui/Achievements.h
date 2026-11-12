#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H

#include <QString>
#include <QStringList>
#include <QList>

// 成就系统（本地统计 QSettings，称号装备走服务端 /api/profile/title）
namespace Achievements {

struct Def {
    QString id;       // 成就 id（兼作解锁标记）
    QString icon;     // emoji 图标
    QString name;     // 成就名
    QString desc;     // 达成条件描述
    QString stat;     // 统计键（stat_<stat>）
    int target;       // 目标值
    int reward;       // 货币奖励（一次性）
    QString title;    // 解锁的可装备称号（空=无称号）
};

// 全部成就定义（顺序即展示顺序）
QList<Def> defs();

// 终局杀法名（board.cpp getKillTypeName 的返回）→ 统计键；普通杀法返回空串
QString killStatKey(const QString &killName);

// 记录一次事件（aiWin/onlineWin/endgame/mimi/killXxx），解锁新成就并一次性发奖
// 返回新解锁成就的提示行（供调用方弹"成就解锁"窗）
QStringList recordEvent(const QString &stat, int increment = 1);

int progress(const QString &stat);
bool isUnlocked(const QString &id);
QStringList unlockedIds();
QString equippedTitle();   // 当前装备的成就称号（QSettings 本地镜像，空=未装备）
void setEquippedTitle(const QString &title);

}  // namespace Achievements

#endif  // ACHIEVEMENTS_H
