#ifndef PROFILEVIEWDIALOG_H
#define PROFILEVIEWDIALOG_H

#include <QDialog>
#include "api.h"

class QLabel;

// 查看好友资料（只读）：背景图铺满 + 头像 + 称号 + 昵称 + ID
class ProfileViewDialog : public QDialog {
    Q_OBJECT
public:
    ProfileViewDialog(ApiClient *api, const QString &accessToken,
                      int userId, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onUser(const UserInfo &u);
    void onImage(const QString &url, const QPixmap &pm);
    void onError(const QString &msg);

private:
    ApiClient *m_api;
    QString m_token;
    int m_userId;
    QString m_bgUrl;
    QPixmap m_bgPix;                    // 背景原图（paintEvent 铺满整个对话框）
    QLabel *m_avatarLbl = nullptr;
    QLabel *m_nameLbl = nullptr;
    QLabel *m_titleLbl = nullptr;
    QLabel *m_idLbl = nullptr;
    QLabel *m_stateLbl = nullptr;

    static QPixmap cropCircle(const QPixmap &src, int size);
};

#endif  // PROFILEVIEWDIALOG_H
