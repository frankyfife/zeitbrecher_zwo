// Hauptfenster des Launchers: Kopf mit Titel und Pfaden, Reiter mit
// Gruppen, unten Status, „Auf Standard zurücksetzen“ und Spielen.
// Jedes Bedienelement schreibt seinen Schlüssel sofort in ts2.ini.
#pragma once
#include <QDir>
#include <QWidget>

#include <string>
#include <utility>
#include <vector>

#include "ini.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QSpinBox;
class QTabWidget;
class KeySlot;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void show_page(int i);

private:
    static constexpr int kTabs = 7;
    static QString tab_name(int i);
    QWidget* build_tab(int i);
    QWidget* build_header();
    QWidget* page_game();
    QWidget* page_unlock();
    QWidget* page_graphics();
    QWidget* page_controls();
    QWidget* page_weapons();
    QWidget* page_sound();
    QWidget* page_about();
    QWidget* setup_group();
    QWidget* language_group();
    void refresh_language();
    void extract_texts();

    // Bindungen an ts2.ini; jede merkt sich ihren Schlüssel für den Reiter,
    // der gerade gebaut wird (für „Auf Standard zurücksetzen“)
    void track(const char* sec, const char* key);
    QCheckBox* check(const QString& text, const char* sec, const char* key);
    QSpinBox* spin(const char* sec, const char* key, int lo, int hi, const QString& suffix = {});
    QDoubleSpinBox* dspin(const char* sec, const char* key, double lo, double hi, double step, int decimals);
    QWidget* percent_slider(const char* sec, const char* key);
    KeySlot* single_key(const char* sec, const char* key);

    void reset_tab();
    void refresh_status();
    void run_setup();
    void play();
    QString guess_homefront() const;

    QDir dir_;       // Ordner von ts2.exe und ts2.ini
    QDir game_;      // game/ mit ts2.bin und ts2/
    Ini ini_;
    QTabWidget* pages_ = nullptr;
    int building_ = 0;  // Reiter, dessen Seite gerade gebaut wird
    std::vector<std::pair<std::string, std::string>> tab_keys_[kTabs];
    QLabel* info_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* game_status_ = nullptr;
    QLineEdit* homefront_ = nullptr;
    QPlainTextEdit* setup_log_ = nullptr;
    QPushButton* setup_btn_ = nullptr;
    QPushButton* reset_btn_ = nullptr;
    QPushButton* play_btn_ = nullptr;
    QProcess* setup_proc_ = nullptr;
    QComboBox* lang_ = nullptr;
    QLabel* lang_status_ = nullptr;
    QLineEdit* xbox_image_ = nullptr;
};
