#ifndef PROFILEDIALOG_H
#define PROFILEDIALOG_H

#include <QDialog>
#include <QTimer>
#include "api.h"

class QLabel;
class QLineEdit;
class QPushButton;

// 个人资料对话框：头像上传 / 昵称修改 / 手机号绑定
class ProfileDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProfileDialog(ApiClient *api, const UserInfo &user,
                           const QString &accessToken, QWidget *parent = nullptr);

signals:
    void profileUpdated(const UserInfo &user);

private slots:
    void onChangeAvatar();
    void onChangeBg();
    void onSaveNickname();
    void onSendBindCode();
    void onBindPhone();
    void onBindCountdown();
    void onApiError(const QString &msg);
    void onProfileUpdated(const UserInfo &user);
    void onAvatarDownloaded(const QString &url, const QPixmap &pm);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    ApiClient *m_api;
    QString m_token;
    UserInfo m_user;

    // UI
    QString m_bgUrlFull;                // 当前背景完整 URL（用于区分下载回调）
    QPixmap m_bgPix;                    // 背景原图（paintEvent 铺满整个对话框）
    QLabel *m_avatarLbl = nullptr;
    QPushButton *m_changeAvatarBtn = nullptr;
    QLabel *m_titleLbl = nullptr;       // 头衔（全服第一/第二/最拉）
    QLabel *m_userIdLbl = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPushButton *m_saveNameBtn = nullptr;
    QLabel *m_phoneStatusLbl = nullptr;
    QLineEdit *m_phoneEdit = nullptr;
    QLineEdit *m_bindCodeEdit = nullptr;
    QPushButton *m_sendBindBtn = nullptr;
    QPushButton *m_bindBtn = nullptr;
    QLabel *m_errorLabel = nullptr;
    QTimer *m_bindTimer = nullptr;
    int m_bindCountdown = 0;

    void setupUi();
    void refreshAvatar(const QPixmap &pm);
    static QPixmap makeCircular(const QPixmap &src, int size);
};

#endif // PROFILEDIALOG_H
