#include "Inventory.h"

#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

namespace Inventory {

static const QString kMoney = QStringLiteral("money");
static const QString kGifts = QStringLiteral("giftsLog");
static const int MAX_GIFTS = 300;

int money()
{
    return QSettings().value(kMoney, 0).toInt();
}

void addMoney(int delta)
{
    QSettings st;
    st.setValue(kMoney, st.value(kMoney, 0).toInt() + delta);
}

bool spendMoney(int cost)
{
    QSettings st;
    const int cur = st.value(kMoney, 0).toInt();
    if (cur < cost)
        return false;
    st.setValue(kMoney, cur - cost);
    return true;
}

int candyCount(int idx)
{
    return QSettings().value(QStringLiteral("candy%1").arg(idx), 0).toInt();
}

void addCandy(int idx, int n)
{
    QSettings st;
    const QString key = QStringLiteral("candy%1").arg(idx);
    st.setValue(key, st.value(key, 0).toInt() + n);
}

int itemCount(const QString &key)
{
    return QSettings().value(QStringLiteral("item_%1").arg(key), 0).toInt();
}

void addItem(const QString &key, int n)
{
    QSettings st;
    const QString full = QStringLiteral("item_%1").arg(key);
    st.setValue(full, st.value(full, 0).toInt() + n);
}

void addGift(const QString &from, const QString &rawText)
{
    // rawText 约定格式："emoji 送你一份礼物名！"
    QString emoji;
    QString name;
    QString t = rawText.trimmed();

    // 取开头第一个空格前的部分作为 emoji（emoji 不含空格）
    const int sp = t.indexOf(QLatin1Char(' '));
    if (sp > 0) {
        emoji = t.left(sp);
        name = t.mid(sp + 1);
    } else {
        emoji = QStringLiteral("🎁");
        name = t;
    }
    // 去掉固定句式 "送你一份" 和结尾叹号
    name.remove(QStringLiteral("送你一份"));
    if (name.endsWith(QLatin1Char('！')) || name.endsWith(QLatin1Char('!')))
        name.chop(1);
    name = name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("神秘礼物");

    QSettings st;
    QJsonArray arr = QJsonDocument::fromJson(
        st.value(kGifts).toByteArray()).array();
    QJsonObject g;
    g[QStringLiteral("e")] = emoji;
    g[QStringLiteral("n")] = name;
    g[QStringLiteral("f")] = from;
    g[QStringLiteral("t")] = QDateTime::currentDateTime()
        .toString(QStringLiteral("yyyy-MM-dd hh:mm"));
    arr.prepend(g);   // 最新在前
    while (arr.size() > MAX_GIFTS)
        arr.removeLast();
    st.setValue(kGifts, QJsonDocument(arr).toJson(QJsonDocument::Compact));

    // 兼容旧的累计计数（弹窗"第 N 份礼物"仍读它）
    st.setValue(QStringLiteral("giftsReceived"),
                st.value(QStringLiteral("giftsReceived"), 0).toInt() + 1);
}

QList<Gift> gifts()
{
    QList<Gift> out;
    const QJsonArray arr = QJsonDocument::fromJson(
        QSettings().value(kGifts).toByteArray()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Gift g;
        g.emoji = o.value(QStringLiteral("e")).toString(QStringLiteral("🎁"));
        g.name = o.value(QStringLiteral("n")).toString(QStringLiteral("神秘礼物"));
        g.from = o.value(QStringLiteral("f")).toString(QStringLiteral("好友"));
        g.time = o.value(QStringLiteral("t")).toString();
        out.append(g);
    }
    return out;
}

int giftsTotal()
{
    return gifts().size();
}

}  // namespace Inventory
