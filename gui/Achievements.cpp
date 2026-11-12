#include "Achievements.h"

#include "Inventory.h"

#include <QSettings>

namespace Achievements {

namespace {
QString statKey(const QString &stat) { return QStringLiteral("stat_%1").arg(stat); }
QString unlockedKey() { return QStringLiteral("achUnlocked"); }
}  // namespace

QList<Def> defs()
{
    static const QList<Def> kDefs = {
        { QStringLiteral("win1"),  QStringLiteral("🥉"),
          QStringLiteral("初出茅庐"), QStringLiteral("人机对战击败奶娃 1 次"),
          QStringLiteral("aiWin"), 1, 1000, QStringLiteral("初出茅庐") },
        { QStringLiteral("win3"),  QStringLiteral("🐲"),
          QStringLiteral("奶娃杀手"), QStringLiteral("人机对战击败奶娃 3 次"),
          QStringLiteral("aiWin"), 3, 5000, QStringLiteral("奶娃杀手") },
        { QStringLiteral("win10"), QStringLiteral("🔥"),
          QStringLiteral("奶娃克星"), QStringLiteral("人机对战击败奶娃 10 次"),
          QStringLiteral("aiWin"), 10, 8000, QStringLiteral("奶娃克星") },
        { QStringLiteral("win20"), QStringLiteral("💀"),
          QStringLiteral("奶娃噩梦"), QStringLiteral("人机对战击败奶娃 20 次"),
          QStringLiteral("aiWin"), 20, 15000, QStringLiteral("奶娃噩梦") },

        { QStringLiteral("killCannon"), QStringLiteral("💥"),
          QStringLiteral("炮王"), QStringLiteral("用重炮杀击败对手 1 次"),
          QStringLiteral("killCannon"), 1, 2000, QStringLiteral("炮王") },
        { QStringLiteral("killRook"), QStringLiteral("♜"),
          QStringLiteral("车王"), QStringLiteral("用双车错击败对手 1 次"),
          QStringLiteral("killRook"), 1, 2000, QStringLiteral("车王") },
        { QStringLiteral("killHorse"), QStringLiteral("🐎"),
          QStringLiteral("马到成功"), QStringLiteral("用马后炮击败对手 1 次"),
          QStringLiteral("killHorse"), 1, 2000, QStringLiteral("马到成功") },
        { QStringLiteral("killSmother"), QStringLiteral("💣"),
          QStringLiteral("闷宫炮手"), QStringLiteral("用闷宫击败对手 1 次"),
          QStringLiteral("killSmother"), 1, 2000, QStringLiteral("闷宫炮手") },
        { QStringLiteral("killFace"), QStringLiteral("😼"),
          QStringLiteral("笑面虎"), QStringLiteral("用对面笑（白脸将）击败对手 1 次"),
          QStringLiteral("killFace"), 1, 2000, QStringLiteral("笑面虎") },

        { QStringLiteral("online1"), QStringLiteral("🌐"),
          QStringLiteral("网战新星"), QStringLiteral("互联网对战获胜 1 次"),
          QStringLiteral("onlineWin"), 1, 3000, QStringLiteral("网战新星") },
        { QStringLiteral("endgame1"), QStringLiteral("🧩"),
          QStringLiteral("残局大师"), QStringLiteral("通关任意残局挑战 1 次"),
          QStringLiteral("endgame"), 1, 3000, QStringLiteral("残局大师") },

        { QStringLiteral("mimi"), QStringLiteral("🤡"),
          QStringLiteral("冤大头"), QStringLiteral("在商城买下「mimimimi」"),
          QStringLiteral("mimi"), 1, 0, QStringLiteral("冤大头") },
    };
    return kDefs;
}

QString killStatKey(const QString &killName)
{
    if (killName == QStringLiteral("重炮"))              return QStringLiteral("killCannon");
    if (killName == QStringLiteral("双车错"))            return QStringLiteral("killRook");
    if (killName == QStringLiteral("马后炮"))            return QStringLiteral("killHorse");
    if (killName == QStringLiteral("闷宫"))              return QStringLiteral("killSmother");
    if (killName.startsWith(QStringLiteral("对面笑")))   return QStringLiteral("killFace");
    return QString();
}

int progress(const QString &stat)
{
    return QSettings().value(statKey(stat), 0).toInt();
}

QStringList unlockedIds()
{
    return QSettings().value(unlockedKey()).toStringList();
}

bool isUnlocked(const QString &id)
{
    return unlockedIds().contains(id);
}

QString equippedTitle()
{
    return QSettings().value(QStringLiteral("equippedTitle")).toString();
}

void setEquippedTitle(const QString &title)
{
    QSettings().setValue(QStringLiteral("equippedTitle"), title);
}

QStringList recordEvent(const QString &stat, int increment)
{
    QSettings st;
    const int v = st.value(statKey(stat), 0).toInt() + increment;
    st.setValue(statKey(stat), v);

    QStringList unlocked = st.value(unlockedKey()).toStringList();
    QStringList notices;
    bool changed = false;
    for (const Def &d : defs()) {
        if (d.stat != stat)
            continue;
        if (unlocked.contains(d.id))
            continue;
        if (v < d.target)
            continue;
        unlocked.append(d.id);
        changed = true;
        if (d.reward > 0)
            Inventory::addMoney(d.reward);
        QString line = QStringLiteral("%1 【%2】%3")
            .arg(d.icon, d.name, d.desc);
        if (!d.title.isEmpty())
            line += QStringLiteral("\n   获得称号：%1").arg(d.title);
        if (d.reward > 0)
            line += QStringLiteral("\n   奖励：%1￥").arg(d.reward);
        notices.append(line);
    }
    if (changed)
        st.setValue(unlockedKey(), unlocked);
    return notices;
}

}  // namespace Achievements
