#ifndef STICKERPICKER_H
#define STICKERPICKER_H

#include <QMenu>

class QTextEdit;

// 奶龙表情选择器：网格弹窗，点选后发 picked(id)，id 形如 "s01"
class StickerPicker : public QMenu {
    Q_OBJECT
public:
    explicit StickerPicker(QWidget *parent = nullptr);

    // 把一条聊天消息追加到 QTextEdit（自动识别 [sticker:sNN] 渲染成图片）
    // whoHtml 为发送者前缀 HTML，text 为原始消息文本
    static void appendMessage(QTextEdit *view, const QString &whoHtml,
                              const QString &text);

    // 判断是否表情标记，是的话返回 id（如 s01），否则返回空串
    static QString stickerId(const QString &text);

signals:
    void picked(const QString &id);
    void emojiPicked(const QString &emoji);
};

#endif // STICKERPICKER_H
