#include "mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "keyslot.h"
#include "theme.h"
#include "ts2options.h"
#include "xboxtext.h"

using theme::Severity;

namespace {

// Abstand wie im FuturamaPC-Launcher: eine halbe Zeilenhöhe
int gap_of(const QWidget* w) { return w->fontMetrics().height() / 2; }

// Umbrechendes Label: Höhe aus der tatsächlichen Breite. Die Raster rechnen
// sonst mit der Schätzbreite von QLabel und lassen über und unter dem Text
// große Lücken; nach jeder Breitenänderung wird neu gemessen.
class WrapLabel : public QLabel {
public:
    using QLabel::QLabel;
    QSize sizeHint() const override {
        QSize s = QLabel::sizeHint();
        if (width() > 0) s.setHeight(heightForWidth(width()));
        return s;
    }
    QSize minimumSizeHint() const override {
        QSize s = QLabel::minimumSizeHint();
        s.setHeight(width() > 0 ? heightForWidth(width()) : fontMetrics().height());
        return s;
    }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QLabel::resizeEvent(e);
        if (e->size().width() != e->oldSize().width()) updateGeometry();
    }
};

// Gedämpfter Hinweis (Platzhalterfarbe der Palette)
QLabel* hint(const QString& t) {
    auto* l = new WrapLabel(t);
    l->setWordWrap(true);
    l->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, theme::muted_color(l));
    l->setPalette(p);
    return l;
}

// Statuszeile (Rich Text mit Farben aus theme::colored)
QLabel* status_label() {
    auto* l = new WrapLabel;
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return l;
}

// Formular in einer Gruppe: Beschriftung links, Bedienelement rechts. Ein
// Raster statt QFormLayout, weil umbrechende Hinweise dort zu hoch werden.
// Listen und Zahlen bleiben kompakt; Hinweise, Statuszeilen und Pfadzeilen
// nehmen die ganze Breite.
struct Form {
    QGridLayout* g = nullptr;
    int row = 0;

    static bool grows(QWidget* w) {
        auto* l = qobject_cast<QLabel*>(w);
        return (l && l->wordWrap()) || w->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding;
    }
    QLabel* label(const QString& text) {
        if (text.isEmpty()) return nullptr;
        auto* l = new QLabel(text);
        l->setProperty("form_label", true);
        g->addWidget(l, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        return l;
    }
    void add(const QString& text, QWidget* w) {
        if (auto* l = label(text)) l->setBuddy(w);
        if (grows(w)) g->addWidget(w, row, 1, 1, 2);
        else g->addWidget(w, row, 1, Qt::AlignLeft | Qt::AlignVCenter);
        ++row;
    }
    void add(const QString& text, QLayout* l) {
        label(text);
        g->addLayout(l, row, 1, 1, 2);
        ++row;
    }
    // über die ganze Breite (Hinweise oben, Gruppen nur aus Kästchen)
    void span(QWidget* w) {
        g->addWidget(w, row, 0, 1, 3);
        ++row;
    }
};

Form form_group(const QString& title, QGroupBox** box_out) {
    auto* box = new QGroupBox(title);
    Form f;
    f.g = new QGridLayout(box);
    f.g->setHorizontalSpacing(box->fontMetrics().height());
    f.g->setVerticalSpacing(gap_of(box) * 2 / 3);
    f.g->setColumnStretch(2, 1);
    *box_out = box;
    return f;
}

// Eine Beschriftungsspalte für alle Formulare einer Seite (die Felder stehen
// über die Gruppen hinweg untereinander)
void align_labels(QWidget* content) {
    QList<QLabel*> labels;
    for (auto* l : content->findChildren<QLabel*>())
        if (l->property("form_label").toBool()) labels << l;
    int width = 0;
    for (auto* l : labels) width = std::max(width, l->sizeHint().width());
    for (auto* l : labels) l->setMinimumWidth(width);
}

// Seite mit Bildlauf und Gruppen
QWidget* scroll_page(QWidget* content, QVBoxLayout* v) {
    const int gap = gap_of(content);
    v->setContentsMargins(gap, gap, gap, gap);
    v->setSpacing(gap);
    v->addStretch(1);
    align_labels(content);
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidget(content);
    return sa;
}

QDir find_game(const QDir& start) {
    QDir d = start;
    for (int i = 0; i < 6; ++i) {
        if (QFile::exists(d.filePath("game/ts2"))) return QDir(d.filePath("game"));
        if (!d.cdUp()) break;
    }
    return QDir(start.filePath("game"));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent),
      dir_(QCoreApplication::applicationDirPath()),
      game_(find_game(dir_)),
      ini_(QDir::toNativeSeparators(dir_.filePath("ts2.ini")).toStdWString()) {
    ts2opt::ini_complete(std::filesystem::path(ini_.path()));
    setWindowTitle(QStringLiteral("Zeitbrecher Zwo – TimeSplitters 2"));
    setWindowIcon(theme::app_icon());
    setMinimumSize(860, 640);

    auto* root = new QVBoxLayout(this);
    const int gap = gap_of(this);
    root->setContentsMargins(gap * 2, gap * 2, gap * 2, gap * 2);
    root->setSpacing(gap);
    root->addWidget(build_header());

    pages_ = new QTabWidget;
    for (int i = 0; i < kTabs; ++i) pages_->addTab(build_tab(i), tab_name(i));
    root->addWidget(pages_, 1);

    // Fuß: Status links, rechts Zurücksetzen und Spielen
    auto* footer = new QHBoxLayout;
    status_ = status_label();
    QPalette sp = status_->palette();
    sp.setColor(QPalette::WindowText, theme::muted_color(status_));
    status_->setPalette(sp);
    reset_btn_ = new QPushButton(QStringLiteral("Auf Standard zurücksetzen"));
    reset_btn_->setToolTip(QStringLiteral("Alle Einstellungen dieses Reiters auf ihre Vorgaben setzen"));
    connect(reset_btn_, &QPushButton::clicked, this, &MainWindow::reset_tab);
    play_btn_ = new QPushButton(QStringLiteral("Spielen"));
    play_btn_->setDefault(true);
    theme::scale_font(play_btn_, 1.35, true);
    play_btn_->setMinimumHeight(play_btn_->fontMetrics().height() * 2);
    play_btn_->setMinimumWidth(play_btn_->fontMetrics().horizontalAdvance(play_btn_->text()) +
                               fontMetrics().height() * 6);
    play_btn_->setToolTip(QStringLiteral("ts2.exe starten und den Launcher schließen"));
    connect(play_btn_, &QPushButton::clicked, this, &MainWindow::play);
    // wie FuturamaPC: Zurücksetzen rechts in eigener Zeile über dem Fuß
    auto* reset_row = new QHBoxLayout;
    reset_row->addStretch(1);
    reset_row->addWidget(reset_btn_);
    root->addLayout(reset_row);
    footer->addWidget(status_, 1);
    footer->addSpacing(gap);
    footer->addWidget(play_btn_, 0, Qt::AlignVCenter);
    root->addLayout(footer);

    connect(pages_, &QTabWidget::currentChanged, this,
            [this](int i) { reset_btn_->setEnabled(i >= 0 && i < kTabs && !tab_keys_[i].empty()); });
    reset_btn_->setEnabled(!tab_keys_[0].empty());
    refresh_status();
}

void MainWindow::show_page(int i) {
    if (i < 0 || i >= pages_->count()) return;
    pages_->setCurrentIndex(i);
}

QString MainWindow::tab_name(int i) {
    static const char* const names[kTabs] = {"Spiel", "Freischalten", "Grafik", "Steuerung", "Waffen", "Ton", "Über"};
    return QString::fromUtf8(names[i]);
}

QWidget* MainWindow::build_tab(int i) {
    building_ = i;
    tab_keys_[i].clear();
    switch (i) {
        case 0: return page_game();
        case 1: return page_unlock();
        case 2: return page_graphics();
        case 3: return page_controls();
        case 4: return page_weapons();
        case 5: return page_sound();
        default: return page_about();
    }
}

// Kopf wie im FuturamaPC-Launcher: Symbol, Titel, Untertitel, darunter die Pfade
QWidget* MainWindow::build_header() {
    auto* header = new QWidget;
    auto* outer = new QVBoxLayout(header);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout;
    const int unit = fontMetrics().height();
    auto* icon = new QLabel;
    icon->setPixmap(theme::app_icon_pixmap(unit * 3, devicePixelRatioF()));
    row->addWidget(icon, 0, Qt::AlignVCenter);
    row->addSpacing(unit / 2);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("Zeitbrecher Zwo"));
    theme::scale_font(title, 1.9, true);
    auto* subtitle = hint(QStringLiteral("Launcher für den PC-Port von TimeSplitters 2"));
    subtitle->setWordWrap(false);
    titles->addWidget(title);
    titles->addWidget(subtitle);
    row->addLayout(titles);
    row->addStretch(1);
    outer->addLayout(row);
    info_ = hint(QString());
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outer->addWidget(info_);
    return header;
}

// ---- Bindungen

void MainWindow::track(const char* sec, const char* key) { tab_keys_[building_].emplace_back(sec, key); }

QCheckBox* MainWindow::check(const QString& text, const char* sec, const char* key) {
    track(sec, key);
    auto* c = new QCheckBox(text);
    c->setChecked(ini_.get_bool(sec, key));
    connect(c, &QCheckBox::toggled, this, [this, sec, key](bool on) { ini_.set_bool(sec, key, on); });
    return c;
}

QSpinBox* MainWindow::spin(const char* sec, const char* key, int lo, int hi, const QString& suffix) {
    track(sec, key);
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setSuffix(suffix);
    s->setValue(ini_.get_int(sec, key));
    connect(s, &QSpinBox::valueChanged, this, [this, sec, key](int v) { ini_.set_int(sec, key, v); });
    return s;
}

QDoubleSpinBox* MainWindow::dspin(const char* sec, const char* key, double lo, double hi, double step, int decimals) {
    track(sec, key);
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setValue(ini_.get_double(sec, key));
    connect(s, &QDoubleSpinBox::valueChanged, this,
            [this, sec, key, decimals](double v) { ini_.set_double(sec, key, v, decimals); });
    return s;
}

QWidget* MainWindow::percent_slider(const char* sec, const char* key) {
    track(sec, key);
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* sl = new QSlider(Qt::Horizontal);
    sl->setRange(0, 200);
    sl->setMinimumWidth(fontMetrics().height() * 16);
    sl->setValue(int(ini_.get_double(sec, key) * 100.0 + 0.5));
    auto* lab = new QLabel(QStringLiteral("%1 %").arg(sl->value()));
    lab->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("200 %")) + fontMetrics().height() / 2);
    connect(sl, &QSlider::valueChanged, this, [this, sec, key, lab](int v) {
        lab->setText(QStringLiteral("%1 %").arg(v));
        ini_.set_double(sec, key, v / 100.0);
    });
    h->addWidget(sl, 1);
    h->addWidget(lab);
    return w;
}

KeySlot* MainWindow::single_key(const char* sec, const char* key) {
    track(sec, key);
    auto* k = new KeySlot;
    k->set_vk(ini_.get_int(sec, key));
    connect(k, &KeySlot::changed, this, [this, sec, key](int vk) { ini_.set_int(sec, key, vk); });
    return k;
}

// ---- Seiten

QWidget* MainWindow::page_game() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    v->addWidget(setup_group());
    v->addWidget(language_group());

    QGroupBox* dbg;
    auto d = form_group(QStringLiteral("Debug-Optionen"), &dbg);
    d.span(hint(QStringLiteral("Watchdog: meldet in ts2.log, wo das Spiel hängt, wenn es länger als die "
                               "Wartezeit nicht weiterläuft (Fehlersuche, z. B. Robot Factory).")));
    d.span(check(QStringLiteral("Watchdog"), "debug", "watchdog"));
    d.add(QStringLiteral("Wartezeit"), spin("debug", "watchdog_ms", 200, 60000, QStringLiteral(" ms")));
    v->addWidget(dbg);
    return scroll_page(content, v);
}

// Freischaltungen ([freischalten]) und Cheats ([cheats], dazu die
// Cheat-Schalter aus [freischalten])
QWidget* MainWindow::page_unlock() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);

    QGroupBox* box;
    auto f = form_group(QStringLiteral("Freischalten"), &box);
    f.span(hint(QStringLiteral("Ohne Haken: Fortschritt wie auf der Konsole – Level, Karten, Figuren und "
                               "Challenges werden nacheinander frei und in profil.bin gespeichert. "
                               "Die Haken schalten sofort alles frei (wie die Konsolen-Codes).")));
    f.span(check(QStringLiteral("Alle Story-Level"), "freischalten", "story"));
    f.span(check(QStringLiteral("Alle Arcade-Karten und -Spielarten"), "freischalten", "arcade"));
    f.span(check(QStringLiteral("Alle Challenges"), "freischalten", "challenge"));
    f.span(check(QStringLiteral("Alle Figuren"), "freischalten", "figuren"));
    f.span(check(QStringLiteral("Hauptmenü mit „Options“"), "freischalten", "optionen"));
    f.span(check(QStringLiteral("Alles (Figuren, Level, Extras)"), "freischalten", "alles"));
    v->addWidget(box);

    QGroupBox* cheats;
    auto c = form_group(QStringLiteral("Cheats"), &cheats);
    c.span(hint(QStringLiteral("Munition und Cooldown gelten nur für die Spieler, die Gegner bleiben wie sie "
                               "sind. „Alle Cheats/Extras“ schaltet die Liste im Optionsmenü des Spiels frei "
                               "(Optionen → Cheats, dort auch TS2s eigene „Infinite Ammo“ für alle).")));
    c.span(check(QStringLiteral("Unbegrenzte Munition"), "cheats", "munition"));
    c.span(check(QStringLiteral("Kein Cooldown (Schussabstand, Nachladen, Waffenwechsel)"), "cheats",
                 "kein_cooldown"));
    c.span(check(QStringLiteral("Unverwundbar"), "freischalten", "unverwundbar"));
    c.span(check(QStringLiteral("Alle Cheats/Extras"), "freischalten", "cheats"));
    v->addWidget(cheats);
    return scroll_page(content, v);
}

// Einrichten aus der eigenen Homefront-Installation (ts2.exe --einrichten)
QWidget* MainWindow::setup_group() {
    QGroupBox* box;
    auto f = form_group(QStringLiteral("Einrichten"), &box);
    game_status_ = status_label();
    f.span(game_status_);
    f.span(hint(QStringLiteral(
        "Einmalig aus der eigenen Installation von Homefront: The Revolution. Danach wird Homefront nicht mehr gebraucht.")));
    auto* row = new QHBoxLayout;
    homefront_ = new QLineEdit(guess_homefront());
    homefront_->setPlaceholderText(QStringLiteral("Ordner von Homefront: The Revolution"));
    auto* browse = new QPushButton(QStringLiteral("Durchsuchen …"));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Homefront-Ordner wählen"), homefront_->text());
        if (!d.isEmpty()) homefront_->setText(QDir::toNativeSeparators(d));
    });
    setup_btn_ = new QPushButton(QStringLiteral("Einrichten"));
    connect(setup_btn_, &QPushButton::clicked, this, &MainWindow::run_setup);
    row->addWidget(homefront_, 1);
    row->addWidget(browse);
    row->addWidget(setup_btn_);
    f.add(QStringLiteral("Homefront-Ordner"), row);
    setup_log_ = new QPlainTextEdit;
    setup_log_->setReadOnly(true);
    setup_log_->setMaximumHeight(fontMetrics().height() * 8);
    setup_log_->hide();
    f.span(setup_log_);
    return box;
}

// Spieltexte: Auswahl [sprache] text, Texte aus dem eigenen Xbox-Image
// (PAL: de, fr, it, es, en) nach game/sprache/<code>.bin holen.
QWidget* MainWindow::language_group() {
    QGroupBox* box;
    auto f = form_group(QStringLiteral("Sprache"), &box);
    f.span(hint(QStringLiteral(
        "Der PC-Port hat nur englische Texte. Die Xbox-PAL-Fassung bringt Deutsch, Französisch, Italienisch und "
        "Spanisch mit: einmal aus dem eigenen Image holen. Die Sprachausgabe bleibt englisch.")));
    track("sprache", "text");
    lang_ = new QComboBox;
    const QList<QPair<QString, QString>> langs = {
        {QStringLiteral("pc"), QStringLiteral("Englisch (PC-Port)")}, {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("fr"), QStringLiteral("Französisch")},        {QStringLiteral("it"), QStringLiteral("Italienisch")},
        {QStringLiteral("es"), QStringLiteral("Spanisch")},           {QStringLiteral("en"), QStringLiteral("Englisch (Xbox, britisch)")}};
    for (const auto& l : langs) lang_->addItem(l.second, l.first);
    const int cur = lang_->findData(ini_.get("sprache", "text"));
    lang_->setCurrentIndex(cur < 0 ? 0 : cur);
    connect(lang_, &QComboBox::currentIndexChanged, this, [this](int) {
        ini_.set("sprache", "text", lang_->currentData().toString());
        refresh_language();
    });
    f.add(QStringLiteral("Spieltexte"), lang_);
    lang_status_ = status_label();
    f.add(QString(), lang_status_);
    auto* row = new QHBoxLayout;
    xbox_image_ = new QLineEdit(ini_.get("sprache", "xbox_image"));
    xbox_image_->setPlaceholderText(QStringLiteral("Eigenes Xbox-Image von TimeSplitters 2 (.iso, PAL)"));
    auto* browse = new QPushButton(QStringLiteral("Durchsuchen …"));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Xbox-Image wählen"), xbox_image_->text(),
                                                          QStringLiteral("Xbox-Image (*.iso *.xiso);;Alle Dateien (*)"));
        if (!file.isEmpty()) xbox_image_->setText(QDir::toNativeSeparators(file));
    });
    auto* go = new QPushButton(QStringLiteral("Texte holen"));
    connect(go, &QPushButton::clicked, this, &MainWindow::extract_texts);
    row->addWidget(xbox_image_, 1);
    row->addWidget(browse);
    row->addWidget(go);
    f.add(QStringLiteral("Xbox-Image"), row);
    refresh_language();
    return box;
}

void MainWindow::refresh_language() {
    const QString code = lang_->currentData().toString();
    if (code == QStringLiteral("pc")) {
        lang_status_->setText(QStringLiteral("Texte wie im PC-Port."));
        return;
    }
    const bool have = QFile::exists(game_.filePath(QStringLiteral("sprache/%1.bin").arg(code)));
    lang_status_->setText(have ? theme::colored(QStringLiteral("Texte vorhanden."), Severity::Ok)
                               : theme::colored(QStringLiteral("Texte fehlen:"), Severity::Error) +
                                     QStringLiteral(" unten das eigene Xbox-Image wählen und „Texte holen“."));
}

void MainWindow::extract_texts() {
    const QString image = xbox_image_->text().trimmed();
    if (image.isEmpty()) return;
    ini_.set("sprache", "xbox_image", image);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto r = xboxtext::extract(image.toStdWString(), game_.filePath(QStringLiteral("sprache")).toStdWString());
    QApplication::restoreOverrideCursor();
    if (!r.error.empty()) {
        lang_status_->setText(theme::colored(QString::fromStdString(r.error).toHtmlEscaped(), Severity::Error));
        return;
    }
    QStringList got;
    for (const auto& l : r.languages) got << QString::fromStdString(l);
    refresh_language();
    lang_status_->setText(lang_status_->text() + QStringLiteral(" Geholt: %1.").arg(got.join(QStringLiteral(", "))));
}

QWidget* MainWindow::page_graphics() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    QGroupBox* box;
    auto f = form_group(QStringLiteral("Bild"), &box);
    track("grafik", "breite");
    track("grafik", "hoehe");
    auto* res = new QComboBox;
    const QList<QSize> sizes = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    const QSize cur(ini_.get_int("grafik", "breite"), ini_.get_int("grafik", "hoehe"));
    for (const QSize& s : sizes) res->addItem(QStringLiteral("%1 × %2").arg(s.width()).arg(s.height()), s);
    if (!sizes.contains(cur)) res->addItem(QStringLiteral("%1 × %2").arg(cur.width()).arg(cur.height()), cur);
    res->setCurrentIndex(res->findData(cur));
    connect(res, &QComboBox::currentIndexChanged, this, [this, res](int) {
        const QSize s = res->currentData().toSize();
        ini_.set_int("grafik", "breite", s.width());
        ini_.set_int("grafik", "hoehe", s.height());
    });
    f.add(QStringLiteral("Render-Auflösung"), res);
    track("grafik", "supersampling");
    auto* ss = new QComboBox;
    ss->addItem(QStringLiteral("Aus"), 1.0);
    ss->addItem(QStringLiteral("1,5×"), 1.5);
    ss->addItem(QStringLiteral("2× (empfohlen)"), 2.0);
    ss->addItem(QStringLiteral("3×"), 3.0);
    {
        const double cur = std::max(1.0, ini_.get_double("grafik", "supersampling"));
        int idx = -1;
        for (int i = 0; i < ss->count(); ++i)
            if (qAbs(ss->itemData(i).toDouble() - cur) < 0.01) idx = i;
        if (idx < 0) {
            ss->addItem(QStringLiteral("%1×").arg(QString::number(cur, 'g', 3).replace(QLatin1Char('.'), QLatin1Char(','))), cur);
            idx = ss->count() - 1;
        }
        ss->setCurrentIndex(idx);
    }
    connect(ss, &QComboBox::currentIndexChanged, this, [this, ss](int) {
        ini_.set("grafik", "supersampling", QString::number(ss->currentData().toDouble(), 'g', 3));
    });
    f.add(QStringLiteral("Supersampling"), ss);
    f.add(QString(), hint(QStringLiteral("Zeichnet das Bild so viel größer und rechnet es gefiltert auf Fenster bzw. "
                                             "Monitor: glattere Kanten (Zäune, Gitter). Das Spiel selbst rechnet weiter "
                                             "in der Render-Auflösung.")));
    track("grafik", "takt");
    auto* rate = new QComboBox;
    rate->addItem(QStringLiteral("60 Hz (wie das Spiel)"), 60);
    rate->addItem(QStringLiteral("120 Hz (Spieler und Kamera nativ)"), 120);
    rate->setCurrentIndex(ini_.get_int("grafik", "takt") == 120 ? 1 : 0);
    connect(rate, &QComboBox::currentIndexChanged, this,
            [this, rate](int) { ini_.set_int("grafik", "takt", rate->currentData().toInt()); });
    f.add(QStringLiteral("Bildtakt"), rate);
    f.add(QString(), check(QStringLiteral("Randloses Vollbild (Alt+Enter schaltet um)"), "grafik", "vollbild"));
    f.add(QString(), check(QStringLiteral("An den Monitor koppeln (Bildsync)"), "grafik", "bildsync"));
    f.add(QString(), hint(QStringLiteral("Ein Bild je Bildwechsel, gleichmäßig; wirkt nur, wenn der Monitor zum "
                                             "Bildtakt passt (120 Hz bei 120, 60 Hz bei 60). Aus: kürzeste "
                                             "Latenz, ohne G-Sync/FreeSync kann es selten ruckeln.")));
    f.add(QStringLiteral("Sichtfeld"), spin("grafik", "sichtfeld", 70, 150, QStringLiteral(" %")));
    f.add(QString(), hint(QStringLiteral("In Prozent des Spiels; das Zielfernrohr zoomt im selben Verhältnis.")));
    f.add(QStringLiteral("Schatten"), dspin("grafik", "schatten", 0.0, 1.0, 0.05, 2));
    f.add(QString(), hint(QStringLiteral("Deckkraft der Schattenflecken unter Kisten und Figuren (Xbox: schwach). "
                                             "0 = wie der PC-Port (oft schwarz).")));
    v->addWidget(box);

    f = form_group(QStringLiteral("HDR"), &box);
    f.add(QString(), check(QStringLiteral("HDR-Ausgabe, wenn Windows-HDR an ist"), "grafik", "hdr"));
    f.add(QStringLiteral("HDR-Weiß"), spin("grafik", "hdr_weiss", 80, 1000, QStringLiteral(" Nits")));
    f.add(QString(), hint(QStringLiteral("Helligkeit von Weiß im Spiel. Ohne HDR-Ausgabe bildet Windows das Bild "
                                             "selbst ab (wirkte überstrahlt).")));
    v->addWidget(box);

    f = form_group(QStringLiteral("Bildanpassung"), &box);
    f.span(hint(QStringLiteral("Alle Werte neutral: keine Anpassung.")));
    f.add(QStringLiteral("Gamma"), dspin("grafik", "gamma", 0.5, 2.5, 0.05, 2));
    f.add(QStringLiteral("Kontrast"), dspin("grafik", "kontrast", 0.5, 2.0, 0.05, 2));
    f.add(QStringLiteral("Helligkeit"), dspin("grafik", "helligkeit", -0.5, 0.5, 0.02, 2));
    f.add(QStringLiteral("Sättigung"), dspin("grafik", "saettigung", 0.0, 2.0, 0.05, 2));
    v->addWidget(box);
    return scroll_page(content, v);
}

QWidget* MainWindow::page_controls() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    QGroupBox* box;
    auto f = form_group(QStringLiteral("Maus"), &box);
    f.add(QStringLiteral("Empfindlichkeit"), dspin("maus", "empfindlichkeit", 0.1, 5.0, 0.05, 2));
    f.add(QString(), check(QStringLiteral("Oben und unten vertauschen"), "maus", "umkehren"));
    f.add(QStringLiteral("Nickgrenze"), spin("maus", "nickgrenze", 40, 89, QStringLiteral("°")));
    f.add(QString(), check(QStringLiteral("Menüs mit der Maus"), "maus", "menue"));
    v->addWidget(box);

    // Controller ([pad]): XInput, das zuletzt benutzte Gerät gilt
    f = form_group(QStringLiteral("Controller"), &box);
    f.span(hint(QStringLiteral("Xbox-Controller (XInput), Belegung wie TimeSplitters 2 auf der Xbox: linker Stick "
                               "gehen, rechter zielen, RT Feuer, LT Zweitfunktion, R3 Zielen, L3 Ducken, A Benutzen, "
                               "Y Nachladen, Steuerkreuz Waffe und Zoomstufen, Start Pause, Back Ziele. Es gilt das "
                               "zuletzt benutzte Gerät.")));
    f.add(QString(), check(QStringLiteral("Controller benutzen"), "pad", "an"));
    f.add(QString(), check(QStringLiteral("Vibration"), "pad", "vibration"));
    f.add(QStringLiteral("Vibrationsstärke"), spin("pad", "vibration_staerke", 0, 200, QStringLiteral(" %")));
    f.add(QStringLiteral("Blickempfindlichkeit"), dspin("pad", "empfindlichkeit", 0.1, 3.0, 0.05, 2));
    f.add(QString(), check(QStringLiteral("Oben und unten vertauschen"), "pad", "umkehren"));
    f.add(QStringLiteral("Totzone der Sticks"), spin("pad", "totzone", 0, 90, QStringLiteral(" %")));
    f.add(QString(), check(QStringLiteral("Zielen schaltet um statt gehalten zu werden (wie auf der Xbox)"), "pad",
                           "zielen_umschalten"));
    {
        // Belegung: Knopf, zwei zugleich mit +, zwei Belegungen mit Komma
        static const struct {
            const char* key;
            const char* label;
        } kPadActions[] = {
            {"rennen", "Rennen"},         {"springen", "Springen"},         {"freecam", "Freie Kamera"},
            {"feuer", "Feuer"},           {"alt_feuer", "Zweitfunktion"},   {"zielen", "Zielen / Zoom"},
            {"ducken", "Ducken"},         {"benutzen", "Benutzen"},         {"nachladen", "Nachladen"},
            {"naechste_waffe", "Nächste Waffe"}, {"vorige_waffe", "Vorige Waffe"},
            {"zoom_rein", "Zoom rein"},   {"zoom_raus", "Zoom raus"},       {"pause", "Pause"},
            {"dreieck", "Ziele (Dreieck)"}, {"quadrat", "Quadrat"},
        };
        const QStringList choices = {QString(),          QStringLiteral("A"),      QStringLiteral("B"),
                                     QStringLiteral("X"), QStringLiteral("Y"),      QStringLiteral("LB"),
                                     QStringLiteral("RB"), QStringLiteral("LT"),    QStringLiteral("RT"),
                                     QStringLiteral("L3"), QStringLiteral("R3"),    QStringLiteral("Start"),
                                     QStringLiteral("Back"), QStringLiteral("Hoch"), QStringLiteral("Runter"),
                                     QStringLiteral("Links"), QStringLiteral("Rechts"), QStringLiteral("L3+R3"),
                                     QStringLiteral("LB+RB"), QStringLiteral("Back+Start")};
        f.span(hint(QStringLiteral("Knöpfe: A B X Y LB RB LT RT L3 R3 Start Back, Steuerkreuz Hoch Runter Links "
                                   "Rechts; zwei zugleich mit „+“, zwei Belegungen mit Komma, leer = keine. "
                                   "A/B/Steuerkreuz bedienen außerdem die Menüs.")));
        for (const auto& a : kPadActions) {
            track("pad", a.key);
            auto* c = new QComboBox;
            c->setEditable(true);
            c->addItems(choices);
            c->setCurrentText(ini_.get("pad", a.key));
            c->setMinimumContentsLength(10);
            const char* key = a.key;
            connect(c, &QComboBox::currentTextChanged, this,
                    [this, key](const QString& t) { ini_.set("pad", key, t.trimmed()); });
            f.add(QString::fromUtf8(a.label), c);
        }
    }
    v->addWidget(box);

    f = form_group(QStringLiteral("Freie Kamera"), &box);
    f.span(hint(QStringLiteral("Taste löst die Kamera vom Spieler: WASD, Maus, Leertaste/C steigen/sinken, "
                                  "Umschalt schneller. Dieselbe Taste bringt sie zurück. Controller: L3+R3 "
                                  "(Belegung oben), Sticks, Steuerkreuz hoch/runter oder RT/LT steigen/sinken, "
                                  "LB/RB schneller.")));
    f.add(QString(), check(QStringLiteral("Freie Kamera"), "kamera", "freecam"));
    f.add(QStringLiteral("Taste"), single_key("kamera", "freecam_taste"));
    {
        // [kamera] freecam_raeume: 0 wie das Spiel, 1 außerhalb des Levels alle, 2 immer alle
        track("kamera", "freecam_raeume");
        auto* rooms = new QComboBox;
        rooms->addItem(QStringLiteral("Wie das Spiel"), 0);
        rooms->addItem(QStringLiteral("Außerhalb des Levels alle"), 1);
        rooms->addItem(QStringLiteral("Immer alle"), 2);
        rooms->setCurrentIndex(std::clamp(ini_.get_int("kamera", "freecam_raeume"), 0, 2));
        connect(rooms, &QComboBox::currentIndexChanged, this,
                [this, rooms](int) { ini_.set_int("kamera", "freecam_raeume", rooms->currentData().toInt()); });
        f.add(QStringLiteral("Räume zeichnen"), rooms);
        f.span(hint(QStringLiteral("Das Spiel zeichnet nur, was die Türen und Durchgänge vom Raum der Kamera aus "
                                   "zeigen. Über oder außerhalb des Levels fehlen dann ganze Teile; „Außerhalb des "
                                   "Levels alle“ zeichnet dort alles. „Immer alle“ zeigt auch Räume hinter der "
                                   "Kulisse (z. B. Eiswände durch den Schnee).")));
    }
    f.span(hint(QStringLiteral("Spieler hierher: setzt den Spieler auf den Boden unter der Kamera und beendet die "
                               "freie Kamera (Controller: Benutzen). Ohne Boden darunter wird abgelehnt.")));
    f.add(QStringLiteral("Spieler hierher"), single_key("kamera", "teleport_taste"));
    v->addWidget(box);

    f = form_group(QStringLiteral("Bewegung"), &box);
    f.add(QString(), check(QStringLiteral("Springen"), "bewegung", "springen"));
    f.add(QStringLiteral("Sprungtaste"), single_key("bewegung", "sprungtaste"));
    f.add(QStringLiteral("Sprunghöhe"), dspin("bewegung", "sprunghoehe", 0.5, 4.0, 0.1, 2));
    f.add(QString(), check(QStringLiteral("Rennen"), "bewegung", "rennen"));
    f.add(QStringLiteral("Renntaste"), single_key("bewegung", "renntaste"));
    f.add(QStringLiteral("Renntempo"), spin("bewegung", "renntempo", 100, 300, QStringLiteral(" %")));
    f.add(QString(), check(QStringLiteral("Renntaste schaltet um statt gehalten zu werden"), "bewegung", "rennen_umschalten"));
    v->addWidget(box);

    // Tasten: bis zu drei Felder je Aktion, Liste mit Komma in [tasten]
    auto* keys = new QGroupBox(QStringLiteral("Tasten"));
    auto* g = new QGridLayout(keys);
    g->setHorizontalSpacing(fontMetrics().height());
    g->setVerticalSpacing(gap_of(this) * 2 / 3);
    g->addWidget(hint(QStringLiteral("Bis zu drei Tasten je Aktion. Klick, dann Taste oder Maustaste drücken; "
                                     "Rechtsklick leert. Mausrad: Waffe wechseln.")),
                 0, 0, 1, 5);
    int row = 1;
    QList<QList<KeySlot*>> all;
    for (const auto& k : ts2opt::key_defaults()) {
        track("tasten", k.key);
        g->addWidget(new QLabel(QString::fromUtf8(k.label)), row, 0);
        const QStringList cur = ini_.get("tasten", k.key).split(QLatin1Char(','));
        QList<KeySlot*> fields;
        for (int i = 0; i < 3; ++i) {
            auto* s = new KeySlot;
            s->set_vk(i < cur.size() ? cur[i].trimmed().toInt() : 0);
            g->addWidget(s, row, 1 + i);
            fields << s;
        }
        const char* key = k.key;
        for (auto* s : fields)
            connect(s, &KeySlot::changed, this, [this, fields, key](int) {
                QStringList codes;
                for (auto* x : fields)
                    if (x->vk()) codes << QString::number(x->vk());
                ini_.set("tasten", key, codes.isEmpty() ? QStringLiteral("0") : codes.join(QLatin1Char(',')));
            });
        all << fields;
        ++row;
    }
    g->setColumnStretch(4, 1);
    auto* reset = new QPushButton(QStringLiteral("Standardbelegung"));
    reset->setToolTip(QStringLiteral("Nur die Tasten auf die Vorgaben setzen"));
    connect(reset, &QPushButton::clicked, this, [this, all] {
        int i = 0;
        for (const auto& k : ts2opt::key_defaults()) {
            for (int j = 0; j < 3; ++j) all[i][j]->set_vk(k.vk[j]);
            ini_.set("tasten", k.key, QString::fromStdString(ts2opt::vk_list(k.vk)));
            ++i;
        }
    });
    g->addWidget(reset, row, 1, 1, 3, Qt::AlignLeft);
    v->addWidget(keys);
    return scroll_page(content, v);
}

// Zeilen der Waffentabelle (0x14250C2A8, 46 Zeilen) mit den deutschen Namen
// des Spiels. Spielerwaffen nach der Gegenstandstabelle 0x142512A20 (je
// Gegenstand linke und rechte Hand); "(auch x2)": dieselbe Zeile dient auch
// der Doppelausführung. Gegner und Geschütze nach dem Code: 29 Walzenroboter
// (Figur 16), 42/43 Splitter (Figuren 55/161 bzw. 128), 32 Angriff der
// Zombies, 27 Geschützturm (Levelobjekt). Die übrigen KI-Zeilen sind nach
// ihren Werten benannt. Ausgeblendet: 18 (ohne Klang und Modell, unbenutzt),
// 34 Digitalkamera und 35 Temporal-Uplink (keine Waffen).
namespace {
const struct {
    int row;
    const char* name;
    bool ai;  // nur Gegner und Geschütze
} kWeapons[] = {
    {0, "Pistole mit Schalldämpfer (auch x2)", false},
    {1, "Luger mit Schalldämpfer (auch x2)", false},
    {2, "Luger-Pistole (auch x2)", false},
    {3, "Garrett-Revolver (auch x2)", false},
    {4, "Tactical 12-Gauge", false},
    {5, "Minigun", false},
    {6, "Scharfschützengewehr", false},
    {7, "Winchester", false},
    {8, "Flammenwerfer", false},
    {9, "Feuerlöscher", false},
    {10, "Russisches S47 (auch x2)", false},
    {11, "ElectroTool", false},
    {12, "Scifi-Pistole", false},
    {40, "Scifi-Pistole (2. Fassung)", false},
    {13, "Raketenwerfer", false},
    {14, "Lenkraketen-Werfer", false},
    {15, "Laserwaffe", false},
    {16, "Plasma-Automatikgewehr", false},
    {17, "Granatwerfer", false},
    {19, "Tretmine", false},
    {20, "Mine mit Fernzünder", false},
    {21, "Mine mit Zeitzünder", false},
    {22, "TNT", false},
    {23, "Tommy Gun (auch x2)", false},
    {24, "SBP90 MG (auch x2)", false},
    {25, "Schrotflinte (auch x2)", false},
    {26, "Ziegelstein", false},
    {36, "Armbrust", false},
    {37, "Schießpulver", false},
    {45, "Fäuste (unbewaffnet)", false},
    {27, "Geschützturm", true},
    {30, "Geschützturm (Variante)", true},
    {41, "Geschützturm (Plasma)", true},
    {44, "Geschützturm (Salven)", true},
    {29, "Walzenroboter", true},
    {28, "Roboterkanone (schnell)", true},
    {31, "Roboterkanone (endlos)", true},
    {38, "Maschinengewehr (KI)", true},
    {39, "Elektroschlag (KI)", true},
    {32, "Zombie (Nahkampf)", true},
    {33, "Nahkampf (KI)", true},
    {42, "Splitter (Nahkampf)", true},
    {43, "Drohnen-Splitter", true},
};
constexpr int kWeaponRows = 46;

// Schlüssel feuerrate_<zeile>; einmal angelegt, sie leben so lange wie das Programm
const char* weapon_key(int w) {
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> k;
        for (int i = 0; i < kWeaponRows; ++i) k.push_back("feuerrate_" + std::to_string(i));
        return k;
    }();
    return keys[size_t(w)].c_str();
}
}  // namespace

QWidget* MainWindow::page_weapons() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    QGroupBox* box;
    auto f = form_group(QStringLiteral("Waffen"), &box);
    f.span(hint(QStringLiteral("Gilt für alle, auch für die KI. 100 % = wie das Spiel.")));
    f.add(QStringLiteral("Tempo des Waffenwechsels"), spin("waffen", "wechseltempo", 25, 1000, QStringLiteral(" %")));
    f.add(QStringLiteral("Feuerrate aller Waffen"), spin("waffen", "feuerrate", 10, 1000, QStringLiteral(" %")));
    v->addWidget(box);

    auto* single = new QGroupBox(QStringLiteral("Feuerrate einzelner Waffen"));
    auto* g = new QGridLayout(single);
    g->setHorizontalSpacing(fontMetrics().height());
    g->setVerticalSpacing(gap_of(this) * 2 / 3);
    g->addWidget(hint(QStringLiteral("0 = wie oben. „(auch x2)“: gilt auch, wenn zwei davon gleichzeitig "
                                     "geführt werden.")),
                 0, 0, 1, 6);
    int line = 1;
    for (bool ai : {false, true}) {
        if (ai) {
            auto* head = new QLabel(QStringLiteral("Gegner und Geschütze"));
            theme::scale_font(head, 1.0, true);
            g->addWidget(head, line++, 0, 1, 6);
        }
        int i = 0;
        for (const auto& e : kWeapons) {
            if (e.ai != ai) continue;
            const char* key = weapon_key(e.row);
            track("waffen", key);
            g->addWidget(new QLabel(QString::fromUtf8(e.name)), line + i / 3, (i % 3) * 2);
            auto* s = new QSpinBox;
            s->setRange(0, 1000);
            s->setSuffix(QStringLiteral(" %"));
            s->setSpecialValueText(QStringLiteral("wie oben"));
            s->setValue(ini_.get("waffen", key).toInt());
            connect(s, &QSpinBox::valueChanged, this, [this, key](int val) {
                if (val == 0) ini_.set("waffen", key, QString());  // leer: wie oben
                else ini_.set_int("waffen", key, val);
            });
            g->addWidget(s, line + i / 3, (i % 3) * 2 + 1);
            ++i;
        }
        line += (i + 2) / 3;
    }
    for (int c = 0; c < 3; ++c) g->setColumnStretch(c * 2, 1);
    v->addWidget(single);
    return scroll_page(content, v);
}

QWidget* MainWindow::page_sound() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    QGroupBox* box;
    auto f = form_group(QStringLiteral("Ton"), &box);
    f.add(QString(), check(QStringLiteral("Ton an"), "ton", "an"));
    f.add(QStringLiteral("Gesamt"), percent_slider("ton", "gesamt"));
    f.add(QStringLiteral("Musik"), percent_slider("ton", "musik"));
    f.add(QStringLiteral("Effekte"), percent_slider("ton", "effekte"));
    f.add(QString(), check(QStringLiteral("5.1 ausgeben, wenn das Gerät es kann"), "ton", "surround"));
    f.add(QString(), check(QStringLiteral("Raum-Hall für Geräusche im Level"), "ton", "hall"));
    v->addWidget(box);
    return scroll_page(content, v);
}

QWidget* MainWindow::page_about() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    auto* box = new QGroupBox(QStringLiteral("zeitbrecher_zwo"));
    auto* l = new QVBoxLayout(box);
    auto* t = new QLabel(QStringLiteral(
        "Eigenständiger PC-Port von TimeSplitters 2 aus der Fassung, die in <i>Homefront: The Revolution</i> steckt.<br><br>"
        "Enthält keine Daten oder Code des Spiels: beides kommt beim Einrichten aus der eigenen Homefront-Installation.<br><br>"
        "Erweiterungen nach dem Vorbild von split2-recomp_ff (Sprung, Rennen, Feuerrate, Menümaus), "
        "Freischaltungen wie die Konsolen-Codes der Homefront-Fassung.<br><br>"
        "Alle Einstellungen stehen in <b>ts2.ini</b> neben <b>ts2.exe</b>; dieser Launcher schreibt jede Änderung sofort."));
    t->setWordWrap(true);
    t->setTextFormat(Qt::RichText);
    l->addWidget(t);
    v->addWidget(box);
    return scroll_page(content, v);
}

// ---- Zurücksetzen, Einrichten und Spielen

// Alle Schlüssel des aktuellen Reiters auf die Vorgaben aus ts2options setzen
// und die Seite neu bauen (die Bedienelemente lesen ts2.ini beim Bauen)
void MainWindow::reset_tab() {
    const int i = pages_->currentIndex();
    if (i < 0 || i >= kTabs || tab_keys_[i].empty()) return;
    QMessageBox box(QMessageBox::Question, QStringLiteral("Auf Standard zurücksetzen"),
                    QStringLiteral("Alle Einstellungen auf dem Reiter „%1“ auf ihre Standardwerte zurücksetzen?").arg(tab_name(i)),
                    QMessageBox::NoButton, this);
    QPushButton* yes = box.addButton(QStringLiteral("Zurücksetzen"), QMessageBox::AcceptRole);
    QPushButton* no = box.addButton(QStringLiteral("Abbrechen"), QMessageBox::RejectRole);
    box.setDefaultButton(no);
    box.setEscapeButton(no);
    box.exec();
    if (box.clickedButton() != yes) return;

    for (const auto& [sec, key] : tab_keys_[i])
        ini_.set(sec.c_str(), key.c_str(), QString::fromStdString(ts2opt::default_value(sec, key)));

    // Spiel-Reiter: Ordner und Protokoll des Einrichtens bleiben stehen
    const QString hf = i == 0 ? homefront_->text() : QString();
    const QString log = i == 0 ? setup_log_->toPlainText() : QString();
    const bool log_shown = i == 0 && setup_log_->isVisible();
    QWidget* old = pages_->widget(i);
    const QSignalBlocker block(pages_);
    pages_->removeTab(i);
    pages_->insertTab(i, build_tab(i), tab_name(i));
    pages_->setCurrentIndex(i);
    old->deleteLater();
    if (i == 0) {
        homefront_->setText(hf);
        setup_log_->setPlainText(log);
        setup_log_->setVisible(log_shown);
        if (setup_proc_) setup_btn_->setEnabled(false);
        refresh_status();
    }
    status_->setText(QStringLiteral("„%1“ auf Standard zurückgesetzt und gespeichert.").arg(tab_name(i)));
}

void MainWindow::refresh_status() {
    const bool ready = QFile::exists(game_.filePath("ts2.bin")) && QFile::exists(game_.filePath("ts2/data"));
    const bool exe = QFile::exists(dir_.filePath("ts2.exe"));
    const QString game_path = QDir::toNativeSeparators(game_.absolutePath());
    game_status_->setText(ready ? theme::colored(QStringLiteral("Eingerichtet"), Severity::Ok) +
                                      QStringLiteral(" in %1").arg(game_path.toHtmlEscaped())
                                : theme::colored(QStringLiteral("Noch nicht eingerichtet."), Severity::Error));
    info_->setText(QStringLiteral("Spiel: %1\nKonfiguration: %2")
                       .arg(ready ? game_path : QStringLiteral("noch nicht eingerichtet"),
                            QDir::toNativeSeparators(dir_.filePath("ts2.ini"))));
    play_btn_->setEnabled(ready && exe && !setup_proc_);
    if (!exe) {
        status_->setText(theme::colored(QStringLiteral("ts2.exe fehlt neben dem Launcher."), Severity::Error));
        play_btn_->setToolTip(QStringLiteral("ts2.exe fehlt neben dem Launcher."));
    } else if (!ready) {
        status_->setText(QStringLiteral("Alle Einstellungen gespeichert. Vor dem ersten Spielen auf „Spiel“ einrichten."));
    } else {
        status_->setText(QStringLiteral("Alle Einstellungen gespeichert."));
    }
}

QString MainWindow::guess_homefront() const {
    // Steam: Bibliotheken aus libraryfolders.vdf
    QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    const QString steam_path = steam.value(QStringLiteral("SteamPath")).toString();
    QStringList libs;
    if (!steam_path.isEmpty()) {
        libs << steam_path;
        QFile vdf(steam_path + QStringLiteral("/steamapps/libraryfolders.vdf"));
        if (vdf.open(QIODevice::ReadOnly)) {
            const QString text = QString::fromUtf8(vdf.readAll());
            QRegularExpression re(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
            for (auto it = re.globalMatch(text); it.hasNext();) libs << it.next().captured(1).replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        }
    }
    for (const QString& lib : libs) {
        const QString d = lib + QStringLiteral("/steamapps/common/Homefront_The_Revolution");
        if (QFile::exists(d + QStringLiteral("/Bin64/Homefront2_Release.exe"))) return QDir::toNativeSeparators(QDir::cleanPath(d));
    }
    return {};
}

void MainWindow::run_setup() {
    const QString hf = homefront_->text().trimmed();
    if (!QFile::exists(hf + QStringLiteral("/Bin64/Homefront2_Release.exe"))) {
        setup_log_->show();
        setup_log_->appendPlainText(QStringLiteral("Dort liegt keine Bin64\\Homefront2_Release.exe."));
        return;
    }
    setup_btn_->setEnabled(false);
    play_btn_->setEnabled(false);
    setup_log_->show();
    setup_log_->appendPlainText(QStringLiteral("Einrichten aus %1 … (1,7 GB, einige Minuten)").arg(hf));
    setup_proc_ = new QProcess(this);
    setup_proc_->setProgram(dir_.filePath("ts2.exe"));
    setup_proc_->setArguments({QStringLiteral("--einrichten"), hf});
    setup_proc_->setWorkingDirectory(dir_.absolutePath());
    setup_proc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(setup_proc_, &QProcess::readyRead, this, [this] {
        setup_log_->appendPlainText(QString::fromLocal8Bit(setup_proc_->readAll()).trimmed());
    });
    connect(setup_proc_, &QProcess::finished, this, [this](int code) {
        setup_log_->appendPlainText(code == 0 ? QStringLiteral("Fertig.") : QStringLiteral("Fehlgeschlagen (%1).").arg(code));
        setup_btn_->setEnabled(true);
        setup_proc_->deleteLater();
        setup_proc_ = nullptr;
        game_ = find_game(dir_);
        refresh_status();
    });
    setup_proc_->start();
}

void MainWindow::play() {
    if (QProcess::startDetached(dir_.filePath("ts2.exe"), {}, dir_.absolutePath())) close();
    else status_->setText(theme::colored(QStringLiteral("ts2.exe ließ sich nicht starten."), Severity::Error));
}
