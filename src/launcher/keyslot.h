// Ein Feld für eine Taste: Klick, dann Taste oder Maustaste drücken.
// Rechtsklick leert das Feld. Gespeichert wird der virtuelle Tastencode
// (VK_*), wie ts2.exe ihn mit GetAsyncKeyState abfragt.
#pragma once
#include <QPushButton>

class KeySlot : public QPushButton {
    Q_OBJECT
public:
    explicit KeySlot(QWidget* parent = nullptr);
    int vk() const { return vk_; }
    void set_vk(int vk);
    static QString vk_name(int vk);

signals:
    void changed(int vk);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

private:
    void start_capture();
    void finish(int vk);
    void cancel();
    int vk_ = 0;
    bool capturing_ = false;
};
