#ifndef ACHIEVEMENTSDIALOG_H
#define ACHIEVEMENTSDIALOG_H

#include <QDialog>
#include "api.h"
#include "Achievements.h"

// 成就页：成就进度/奖励/称号装备
class AchievementsDialog : public QDialog {
    Q_OBJECT
public:
    explicit AchievementsDialog(ApiClient *api, const QString &accessToken,
                                const QString &currentTitle, QWidget *parent = nullptr);

signals:
    // 装备/卸下称号成功（服务端返回最新 user，StartScreen 刷新头衔条）
    void titleChanged(const UserInfo &user);

private slots:
    void onProfileUpdated(const UserInfo &user);
    void onApiError(const QString &msg);

private:
    ApiClient *m_api;
    QString m_token;
    QString m_title;

    void rebuild();
    void equip(const QString &title);
};

#endif  // ACHIEVEMENTSDIALOG_H
