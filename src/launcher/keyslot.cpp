#include "keyslot.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include <windows.h>

KeySlot::KeySlot(QWidget* parent) : QPushButton(parent) {
    setObjectName("keyslot");
    setFocusPolicy(Qt::StrongFocus);
    // Während der Aufnahme eingedrückt (Akzentfarbe des Stils) und fett
    setCheckable(true);
    setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("Umschalt rechts")) + fontMetrics().height() * 2);
    setToolTip(QStringLiteral("Klicken, dann Taste oder Maustaste drücken. Rechtsklick leert das Feld."));
    set_vk(0);
}

QString KeySlot::vk_name(int vk) {
    switch (vk) {
        case 0: return QStringLiteral("–");
        case VK_LBUTTON: return QStringLiteral("Maus links");
        case VK_RBUTTON: return QStringLiteral("Maus rechts");
        case VK_MBUTTON: return QStringLiteral("Maus Mitte");
        case VK_XBUTTON1: return QStringLiteral("Maus zurück");
        case VK_XBUTTON2: return QStringLiteral("Maus vor");
        case VK_LSHIFT: return QStringLiteral("Umschalt links");
        case VK_RSHIFT: return QStringLiteral("Umschalt rechts");
        case VK_LCONTROL: return QStringLiteral("Strg links");
        case VK_RCONTROL: return QStringLiteral("Strg rechts");
        case VK_LMENU: return QStringLiteral("Alt links");
        case VK_RMENU: return QStringLiteral("Alt Gr");
        case VK_SPACE: return QStringLiteral("Leertaste");
        default: break;
    }
    // Name aus dem Tastaturlayout; erweiterte Tasten brauchen Bit 24
    bool extended = false;
    switch (vk) {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_END:
        case VK_HOME: case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            extended = true;
            break;
        default: break;
    }
    const UINT sc = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
    wchar_t name[64] = {};
    if (sc && GetKeyNameTextW(LONG(sc << 16) | (extended ? (1 << 24) : 0), name, 64) > 0)
        return QString::fromWCharArray(name);
    return QStringLiteral("Taste %1").arg(vk);
}

void KeySlot::set_vk(int vk) {
    vk_ = vk;
    capturing_ = false;
    setText(vk_name(vk));
    update_look();
}

void KeySlot::update_look() {
    setChecked(capturing_);
    QFont f = font();
    f.setBold(capturing_);
    setFont(f);
}

void KeySlot::start_capture() {
    capturing_ = true;
    setText(QStringLiteral("Taste drücken …"));
    update_look();
    setFocus();
    grabKeyboard();
}

void KeySlot::finish(int vk) {
    releaseKeyboard();
    set_vk(vk);
    emit changed(vk);
}

void KeySlot::cancel() {
    if (!capturing_) return;
    releaseKeyboard();
    set_vk(vk_);
}

void KeySlot::keyPressEvent(QKeyEvent* e) {
    if (!capturing_) {
        if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return) start_capture();
        else QPushButton::keyPressEvent(e);
        return;
    }
    int vk = int(e->nativeVirtualKey());
    const quint32 sc = e->nativeScanCode();
    const bool ext = (sc & 0x100) != 0 || (sc & 0xE000) == 0xE000;
    // links/rechts unterscheiden, wie ts2.exe es abfragt
    if (vk == VK_SHIFT) vk = ((sc & 0xFF) == 0x36) ? VK_RSHIFT : VK_LSHIFT;
    else if (vk == VK_CONTROL) vk = ext ? VK_RCONTROL : VK_LCONTROL;
    else if (vk == VK_MENU) vk = ext ? VK_RMENU : VK_LMENU;
    if (vk > 0 && vk < 256) finish(vk);
}

void KeySlot::mousePressEvent(QMouseEvent* e) {
    if (capturing_) {
        int vk = 0;
        switch (e->button()) {
            case Qt::LeftButton: vk = VK_LBUTTON; break;
            case Qt::RightButton: vk = VK_RBUTTON; break;
            case Qt::MiddleButton: vk = VK_MBUTTON; break;
            case Qt::BackButton: vk = VK_XBUTTON1; break;
            case Qt::ForwardButton: vk = VK_XBUTTON2; break;
            default: break;
        }
        if (vk) finish(vk);
        return;
    }
    if (e->button() == Qt::RightButton) {
        finish(0);
        return;
    }
    if (e->button() == Qt::LeftButton) {
        start_capture();
        return;
    }
    QPushButton::mousePressEvent(e);
}

void KeySlot::nextCheckState() {
    if (capturing_) update_look();
    else start_capture();
}

void KeySlot::focusOutEvent(QFocusEvent* e) {
    cancel();
    QPushButton::focusOutEvent(e);
}
