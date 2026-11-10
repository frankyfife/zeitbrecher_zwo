#include "mainwindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include "keyslot.h"
#include "theme.h"
#include "ts2options.h"
#include "xboxtext.h"

namespace {

QFrame* panel(QWidget* parent = nullptr) {
    auto* f = new QFrame(parent);
    f->setObjectName("panel");
    return f;
}

QLabel* heading(const QString& t) {
    auto* l = new QLabel(t);
    l->setObjectName("heading");
    return l;
}

QLabel* hint(const QString& t) {
    auto* l = new QLabel(t);
    l->setObjectName("hint");
    l->setWordWrap(true);
    return l;
}

// Seite mit Bildlauf und Paneelen
QWidget* scroll_page(QWidget* content) {
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    content->setObjectName("page");
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
    setMinimumSize(1100, 720);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 96, 28, 20);
    root->setSpacing(14);

    // Reiter oben wie im Launcher des Xbox-Forks
    pages_ = new QTabWidget;
    pages_->setDocumentMode(true);
    const struct {
        QString name;
        QWidget* (MainWindow::*make)();
    } tabs[] = {
        {QStringLiteral("Spiel"), &MainWindow::page_game},         {QStringLiteral("Grafik"), &MainWindow::page_graphics},
        {QStringLiteral("Steuerung"), &MainWindow::page_controls}, {QStringLiteral("Waffen"), &MainWindow::page_weapons},
        {QStringLiteral("Ton"), &MainWindow::page_sound},          {QStringLiteral("Über"), &MainWindow::page_about},
    };
    for (const auto& t : tabs) pages_->addTab((this->*t.make)(), t.name.toUpper());
    root->addWidget(pages_, 1);

    auto* bottom = new QHBoxLayout;
    status_ = new QLabel;
    status_->setObjectName("hint");
    bottom->addWidget(status_, 1);
    play_btn_ = new QPushButton(QStringLiteral("SPIELEN"));
    play_btn_->setObjectName("play");
    connect(play_btn_, &QPushButton::clicked, this, &MainWindow::play);
    bottom->addWidget(play_btn_);
    root->addLayout(bottom);
    refresh_status();
}

void MainWindow::show_page(int i) {
    if (i < 0 || i >= pages_->count()) return;
    pages_->setCurrentIndex(i);
}

void MainWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    theme::paint_background(p, rect());
    theme::paint_title(p, QRect(32, 18, width() - 64, 90));
}

// ---- Bindungen

QCheckBox* MainWindow::check(const QString& text, const char* sec, const char* key) {
    auto* c = new QCheckBox(text);
    c->setChecked(ini_.get_bool(sec, key));
    connect(c, &QCheckBox::toggled, this, [this, sec, key](bool on) { ini_.set_bool(sec, key, on); });
    return c;
}

QSpinBox* MainWindow::spin(const char* sec, const char* key, int lo, int hi, const QString& suffix) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setSuffix(suffix);
    s->setValue(ini_.get_int(sec, key));
    connect(s, &QSpinBox::valueChanged, this, [this, sec, key](int v) { ini_.set_int(sec, key, v); });
    return s;
}

QDoubleSpinBox* MainWindow::dspin(const char* sec, const char* key, double lo, double hi, double step, int decimals) {
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
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* sl = new QSlider(Qt::Horizontal);
    sl->setRange(0, 200);
    sl->setValue(int(ini_.get_double(sec, key) * 100.0 + 0.5));
    auto* lab = new QLabel(QStringLiteral("%1 %").arg(sl->value()));
    lab->setMinimumWidth(56);
    connect(sl, &QSlider::valueChanged, this, [this, sec, key, lab](int v) {
        lab->setText(QStringLiteral("%1 %").arg(v));
        ini_.set_double(sec, key, v / 100.0);
    });
    h->addWidget(sl, 1);
    h->addWidget(lab);
    return w;
}

KeySlot* MainWindow::single_key(const char* sec, const char* key) {
    auto* k = new KeySlot;
    k->set_vk(ini_.get_int(sec, key));
    connect(k, &KeySlot::changed, this, [this, sec, key](int vk) { ini_.set_int(sec, key, vk); });
    return k;
}

// ---- Seiten

QWidget* MainWindow::page_game() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    auto* p1 = panel();
    auto* f1 = new QVBoxLayout(p1);
    f1->addWidget(heading(QStringLiteral("Einrichten")));
    game_status_ = new QLabel;
    game_status_->setWordWrap(true);
    f1->addWidget(game_status_);
    f1->addWidget(hint(QStringLiteral(
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
    f1->addLayout(row);
    setup_log_ = new QPlainTextEdit;
    setup_log_->setReadOnly(true);
    setup_log_->setMaximumHeight(140);
    setup_log_->hide();
    f1->addWidget(setup_log_);
    v->addWidget(p1);
    v->addWidget(language_panel());

    auto* pr = panel();
    auto* fr = new QVBoxLayout(pr);
    fr->addWidget(heading(QStringLiteral("Fehlerbehebungen (TS2 Redux)")));
    const bool have_redux = QFile::exists(game_.filePath(QStringLiteral("redux.bin")));
    fr->addWidget(hint(have_redux
        ? QStringLiteral("Aus der eigenen Kopie von TS2 Redux (github.com/HFTSRedux/TS2Redux), game/redux.bin.")
        : QStringLiteral("<span style='color:#ff4fd8'>game/redux.bin fehlt:</span> einmal "
                         "tools/re/redux_patches.py mit der eigenen TS2-Redux-Kopie ausführen.")));
    fr->addWidget(check(QStringLiteral("Spielfehler (Reaper-Absturz, Regen auf der Kamera)"), "redux", "spielfehler"));
    fr->addWidget(check(QStringLiteral("Unsichtbarkeitsfehler"), "redux", "unsichtbar"));
    fr->addWidget(check(QStringLiteral("HUD- und Textanzeige"), "redux", "anzeige"));
    fr->addWidget(check(QStringLiteral("Menü-Layout wie auf der Konsole"), "redux", "menue"));
    v->addWidget(pr);

    auto* p2 = panel();
    auto* f2 = new QVBoxLayout(p2);
    f2->addWidget(heading(QStringLiteral("Freischalten")));
    f2->addWidget(hint(QStringLiteral("Wie die Konsolen-Codes der Homefront-Fassung.")));
    f2->addWidget(check(QStringLiteral("Alle Story-Level"), "freischalten", "story"));
    f2->addWidget(check(QStringLiteral("Arcade"), "freischalten", "arcade"));
    f2->addWidget(check(QStringLiteral("Challenge"), "freischalten", "challenge"));
    f2->addWidget(check(QStringLiteral("Hauptmenü mit „Options“"), "freischalten", "optionen"));
    f2->addWidget(check(QStringLiteral("Alle Cheats/Extras"), "freischalten", "cheats"));
    f2->addWidget(check(QStringLiteral("Unverwundbar"), "freischalten", "unverwundbar"));
    f2->addWidget(check(QStringLiteral("Alles (Figuren, Level, Extras)"), "freischalten", "alles"));
    v->addWidget(p2);
    v->addStretch();
    return scroll_page(content);
}

// Spieltexte: Auswahl [sprache] text, Texte aus dem eigenen Xbox-Image
// (PAL: de, fr, it, es, en) nach game/sprache/<code>.bin holen.
QWidget* MainWindow::language_panel() {
    auto* p = panel();
    auto* f = new QVBoxLayout(p);
    f->addWidget(heading(QStringLiteral("Sprache")));
    f->addWidget(hint(QStringLiteral(
        "Der PC-Port hat nur englische Texte. Die Xbox-PAL-Fassung bringt Deutsch, Französisch, Italienisch und "
        "Spanisch mit: einmal aus dem eigenen Image holen. Die Sprachausgabe bleibt englisch.")));
    auto* row = new QHBoxLayout;
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
    row->addWidget(new QLabel(QStringLiteral("Spieltexte")));
    row->addWidget(lang_, 1);
    f->addLayout(row);
    lang_status_ = new QLabel;
    lang_status_->setWordWrap(true);
    f->addWidget(lang_status_);
    auto* row2 = new QHBoxLayout;
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
    row2->addWidget(xbox_image_, 1);
    row2->addWidget(browse);
    row2->addWidget(go);
    f->addLayout(row2);
    refresh_language();
    return p;
}

void MainWindow::refresh_language() {
    const QString code = lang_->currentData().toString();
    if (code == QStringLiteral("pc")) {
        lang_status_->setText(QStringLiteral("Texte wie im PC-Port."));
        return;
    }
    const bool have = QFile::exists(game_.filePath(QStringLiteral("sprache/%1.bin").arg(code)));
    lang_status_->setText(have ? QStringLiteral("<span style='color:#7dff6a'>Texte vorhanden.</span>")
                               : QStringLiteral("<span style='color:#ff4fd8'>Texte fehlen:</span> "
                                                "unten das eigene Xbox-Image wählen und „Texte holen“."));
}

void MainWindow::extract_texts() {
    const QString image = xbox_image_->text().trimmed();
    if (image.isEmpty()) return;
    ini_.set("sprache", "xbox_image", image);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto r = xboxtext::extract(image.toStdWString(), game_.filePath(QStringLiteral("sprache")).toStdWString());
    QApplication::restoreOverrideCursor();
    if (!r.error.empty()) {
        lang_status_->setText(QStringLiteral("<span style='color:#ff4fd8'>%1</span>").arg(QString::fromStdString(r.error)));
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
    auto* p1 = panel();
    auto* f = new QFormLayout(p1);
    f->addRow(heading(QStringLiteral("Bild")));
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
    f->addRow(QStringLiteral("Render-Auflösung"), res);
    auto* rate = new QComboBox;
    rate->addItem(QStringLiteral("60 Hz (wie das Spiel)"), 60);
    rate->addItem(QStringLiteral("120 Hz (Spieler und Kamera nativ)"), 120);
    rate->setCurrentIndex(ini_.get_int("grafik", "takt") == 120 ? 1 : 0);
    connect(rate, &QComboBox::currentIndexChanged, this,
            [this, rate](int) { ini_.set_int("grafik", "takt", rate->currentData().toInt()); });
    f->addRow(QStringLiteral("Bildtakt"), rate);
    f->addRow(QString(), check(QStringLiteral("Randloses Vollbild (Alt+Enter schaltet um)"), "grafik", "vollbild"));
    f->addRow(QString(), check(QStringLiteral("Zusätzlich auf den Monitor warten (Bildsync)"), "grafik", "bildsync"));
    f->addRow(QString(), check(QStringLiteral("HDR-Ausgabe, wenn Windows-HDR an ist"), "grafik", "hdr"));
    f->addRow(QStringLiteral("HDR-Weiß"), spin("grafik", "hdr_weiss", 80, 1000, QStringLiteral(" Nits")));
    f->addRow(hint(QStringLiteral("Helligkeit von Weiß im Spiel. Ohne HDR-Ausgabe bildet Windows das Bild selbst ab "
                                  "(wirkte überstrahlt).")));
    f->addRow(QStringLiteral("Sichtfeld"), spin("grafik", "sichtfeld", 70, 150, QStringLiteral(" %")));
    f->addRow(hint(QStringLiteral("In Prozent des Spiels; das Zielfernrohr zoomt im selben Verhältnis.")));
    v->addWidget(p1);

    auto* p2 = panel();
    auto* f2 = new QFormLayout(p2);
    f2->addRow(heading(QStringLiteral("Bildanpassung")));
    f2->addRow(hint(QStringLiteral("Alle Werte neutral: keine Anpassung.")));
    f2->addRow(QStringLiteral("Gamma"), dspin("grafik", "gamma", 0.5, 2.5, 0.05, 2));
    f2->addRow(QStringLiteral("Kontrast"), dspin("grafik", "kontrast", 0.5, 2.0, 0.05, 2));
    f2->addRow(QStringLiteral("Helligkeit"), dspin("grafik", "helligkeit", -0.5, 0.5, 0.02, 2));
    f2->addRow(QStringLiteral("Sättigung"), dspin("grafik", "saettigung", 0.0, 2.0, 0.05, 2));
    v->addWidget(p2);
    v->addStretch();
    return scroll_page(content);
}

QWidget* MainWindow::page_controls() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    auto* p1 = panel();
    auto* f = new QFormLayout(p1);
    f->addRow(heading(QStringLiteral("Maus")));
    f->addRow(QStringLiteral("Empfindlichkeit"), dspin("maus", "empfindlichkeit", 0.1, 5.0, 0.05, 2));
    f->addRow(QString(), check(QStringLiteral("Oben und unten vertauschen"), "maus", "umkehren"));
    f->addRow(QStringLiteral("Nickgrenze"), spin("maus", "nickgrenze", 40, 89, QStringLiteral("°")));
    f->addRow(QString(), check(QStringLiteral("Menüs mit der Maus"), "maus", "menue"));
    f->addRow(heading(QStringLiteral("Freie Kamera")));
    f->addRow(hint(QStringLiteral("Taste löst die Kamera vom Spieler: WASD, Maus, Leertaste/C steigen/sinken, "
                                  "Umschalt schneller. Dieselbe Taste bringt sie zurück.")));
    f->addRow(QString(), check(QStringLiteral("Freie Kamera"), "kamera", "freecam"));
    f->addRow(QStringLiteral("Taste"), single_key("kamera", "freecam_taste"));
    v->addWidget(p1);

    auto* p2 = panel();
    auto* f2 = new QFormLayout(p2);
    f2->addRow(heading(QStringLiteral("Bewegung")));
    f2->addRow(QString(), check(QStringLiteral("Springen"), "bewegung", "springen"));
    f2->addRow(QStringLiteral("Sprungtaste"), single_key("bewegung", "sprungtaste"));
    f2->addRow(QStringLiteral("Sprunghöhe"), dspin("bewegung", "sprunghoehe", 0.5, 4.0, 0.1, 2));
    f2->addRow(QString(), check(QStringLiteral("Rennen"), "bewegung", "rennen"));
    f2->addRow(QStringLiteral("Renntaste"), single_key("bewegung", "renntaste"));
    f2->addRow(QStringLiteral("Renntempo"), spin("bewegung", "renntempo", 100, 300, QStringLiteral(" %")));
    f2->addRow(QString(), check(QStringLiteral("Renntaste schaltet um statt gehalten zu werden"), "bewegung", "rennen_umschalten"));
    v->addWidget(p2);

    auto* p3 = panel();
    auto* g = new QGridLayout(p3);
    g->addWidget(heading(QStringLiteral("Tasten")), 0, 0, 1, 4);
    g->addWidget(hint(QStringLiteral("Bis zu drei Tasten je Aktion. Klick, dann Taste oder Maustaste drücken; "
                                     "Rechtsklick leert. Mausrad: Waffe wechseln.")),
                 1, 0, 1, 4);
    int row = 2;
    QList<QList<KeySlot*>> all;
    for (const auto& k : ts2opt::key_defaults()) {
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
    auto* reset = new QPushButton(QStringLiteral("Standardbelegung"));
    connect(reset, &QPushButton::clicked, this, [this, all] {
        int i = 0;
        for (const auto& k : ts2opt::key_defaults()) {
            for (int j = 0; j < 3; ++j) all[i][j]->set_vk(k.vk[j]);
            ini_.set("tasten", k.key, QString::fromStdString(ts2opt::vk_list(k.vk)));
            ++i;
        }
    });
    g->addWidget(reset, row, 0);
    v->addWidget(p3);
    v->addStretch();
    return scroll_page(content);
}

// Waffen in der Hand des Spielers, nach ihrer Zeile in der Waffentabelle
// (0x14250C2A8), Namen nach ihren Modelldateien wie im Xbox-Fork
// (split2-recomp_ff, launcher); die Zeilen stimmen mit dem PC-Port überein
// (gleiche Schussabstände je Paar, z. B. Flammenwerfer/Feuerlöscher 1 Takt).
namespace {
const struct {
    int row;
    const char* name;
} kWeapons[] = {
    {0, "Schallgedämpfte Pistole"}, {1, "Schallgedämpfte Luger"}, {2, "Luger"}, {3, "Magnum"},
    {4, "Sturm-Schrotflinte"}, {5, "Minigun"}, {6, "Scharfschützengewehr"}, {7, "Repetiergewehr"},
    {8, "Flammenwerfer"}, {9, "Feuerlöscher"}, {10, "AK-47"}, {11, "Elektrowaffe"},
    {12, "Sci-Fi-Pistole"}, {13, "Raketenwerfer"}, {14, "Lenkraketenwerfer"}, {15, "Laser"},
    {16, "Plasma-MG"}, {17, "Granatwerfer"}, {19, "Annäherungsmine"}, {20, "Fernzündermine"},
    {21, "Zeitzündermine"}, {22, "TNT"}, {23, "Tommy Gun"}, {24, "Uzi"},
    {25, "Doppelläufige Flinte"}, {26, "Ziegelstein"}, {36, "Armbrust"}, {37, "Pulverfass"},
};
}  // namespace

QWidget* MainWindow::page_weapons() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    auto* p1 = panel();
    auto* f = new QFormLayout(p1);
    f->addRow(heading(QStringLiteral("Waffen")));
    f->addRow(hint(QStringLiteral("Gilt für alle, auch für die KI. 100 % = wie das Spiel.")));
    f->addRow(QStringLiteral("Tempo des Waffenwechsels"), spin("waffen", "wechseltempo", 25, 1000, QStringLiteral(" %")));
    f->addRow(QStringLiteral("Feuerrate aller Waffen"), spin("waffen", "feuerrate", 10, 1000, QStringLiteral(" %")));
    v->addWidget(p1);
    auto* p2 = panel();
    auto* g = new QGridLayout(p2);
    g->addWidget(heading(QStringLiteral("Feuerrate einzelner Waffen")), 0, 0, 1, 6);
    g->addWidget(hint(QStringLiteral("0 = wie oben. Zuerst die Waffen des Spielers, dann die übrigen Zeilen der "
                                     "Waffentabelle (KI, Geschütze).")), 1, 0, 1, 6);
    static std::vector<std::string> keys;  // Schlüssel müssen leben, solange das Fenster lebt
    keys.clear();
    keys.reserve(46);
    for (int w = 0; w < 46; ++w) keys.push_back("feuerrate_" + std::to_string(w));
    std::vector<std::pair<int, QString>> order;
    std::vector<bool> named(46, false);
    for (const auto& e : kWeapons) order.emplace_back(e.row, QString::fromUtf8(e.name)), named[e.row] = true;
    for (int w = 0; w < 46; ++w)
        if (!named[w]) order.emplace_back(w, QStringLiteral("Zeile %1").arg(w));
    for (int i = 0; i < int(order.size()); ++i) {
        const int w = order[i].first;
        g->addWidget(new QLabel(order[i].second), 2 + i / 3, (i % 3) * 2);
        auto* s = new QSpinBox;
        s->setRange(0, 1000);
        s->setSuffix(QStringLiteral(" %"));
        s->setSpecialValueText(QStringLiteral("wie oben"));
        s->setValue(ini_.get("waffen", keys[w].c_str()).toInt());
        const char* key = keys[w].c_str();
        connect(s, &QSpinBox::valueChanged, this, [this, key](int val) {
            if (val == 0) ini_.set("waffen", key, QString());  // leer: wie oben
            else ini_.set_int("waffen", key, val);
        });
        g->addWidget(s, 2 + i / 3, (i % 3) * 2 + 1);
    }
    v->addWidget(p2);
    v->addStretch();
    return scroll_page(content);
}

QWidget* MainWindow::page_sound() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    auto* p1 = panel();
    auto* f = new QFormLayout(p1);
    f->addRow(heading(QStringLiteral("Ton")));
    f->addRow(QString(), check(QStringLiteral("Ton an"), "ton", "an"));
    f->addRow(QStringLiteral("Gesamt"), percent_slider("ton", "gesamt"));
    f->addRow(QStringLiteral("Musik"), percent_slider("ton", "musik"));
    f->addRow(QStringLiteral("Effekte"), percent_slider("ton", "effekte"));
    f->addRow(QString(), check(QStringLiteral("5.1 ausgeben, wenn das Gerät es kann"), "ton", "surround"));
    v->addWidget(p1);
    v->addStretch();
    return scroll_page(content);
}

QWidget* MainWindow::page_about() {
    auto* content = new QWidget;
    auto* v = new QVBoxLayout(content);
    auto* p1 = panel();
    auto* l = new QVBoxLayout(p1);
    l->addWidget(heading(QStringLiteral("zeitbrecher_zwo")));
    auto* t = new QLabel(QStringLiteral(
        "Eigenständiger PC-Port von TimeSplitters 2 aus der Fassung, die in <i>Homefront: The Revolution</i> steckt.<br><br>"
        "Enthält keine Daten oder Code des Spiels: beides kommt beim Einrichten aus der eigenen Homefront-Installation.<br><br>"
        "Erweiterungen nach dem Vorbild von split2-recomp_ff (Sprung, Rennen, Feuerrate, Menümaus), "
        "Freischaltungen nach TS2Redux.<br><br>"
        "Alle Einstellungen stehen in <b>ts2.ini</b> neben <b>ts2.exe</b>; dieser Launcher schreibt jede Änderung sofort."));
    t->setWordWrap(true);
    t->setTextFormat(Qt::RichText);
    l->addWidget(t);
    v->addWidget(p1);
    v->addStretch();
    return scroll_page(content);
}

// ---- Einrichten und Spielen

void MainWindow::refresh_status() {
    const bool ready = QFile::exists(game_.filePath("ts2.bin")) && QFile::exists(game_.filePath("ts2/data"));
    const bool exe = QFile::exists(dir_.filePath("ts2.exe"));
    game_status_->setText(ready ? QStringLiteral("<span style='color:#7dff6a'>Eingerichtet</span> in %1")
                                      .arg(QDir::toNativeSeparators(game_.absolutePath()))
                                : QStringLiteral("<span style='color:#ff4fd8'>Noch nicht eingerichtet.</span>"));
    play_btn_->setEnabled(ready && exe);
    status_->setText(exe ? QStringLiteral("Einstellungen: %1").arg(QDir::toNativeSeparators(dir_.filePath("ts2.ini")))
                         : QStringLiteral("ts2.exe fehlt neben dem Launcher."));
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
        game_ = find_game(dir_);
        refresh_status();
        setup_proc_->deleteLater();
        setup_proc_ = nullptr;
    });
    setup_proc_->start();
}

void MainWindow::play() {
    if (QProcess::startDetached(dir_.filePath("ts2.exe"), {}, dir_.absolutePath())) close();
    else status_->setText(QStringLiteral("ts2.exe ließ sich nicht starten."));
}
