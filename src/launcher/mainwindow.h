// Hauptfenster des Launchers: Reiter links, Seiten rechts, unten Spielen.
// Jedes Bedienelement schreibt seinen Schlüssel sofort in ts2.ini.
#pragma once
#include <QDir>
#include <QWidget>

#include <memory>

#include "ini.h"

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class KeySlot;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void show_page(int i);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QWidget* page_game();
    QWidget* page_graphics();
    QWidget* page_controls();
    QWidget* page_weapons();
    QWidget* page_sound();
    QWidget* page_about();
    QWidget* language_panel();
    void refresh_language();
    void extract_texts();

    // Bindungen an ts2.ini
    QCheckBox* check(const QString& text, const char* sec, const char* key);
    QSpinBox* spin(const char* sec, const char* key, int lo, int hi, const QString& suffix = {});
    QDoubleSpinBox* dspin(const char* sec, const char* key, double lo, double hi, double step, int decimals);
    QWidget* percent_slider(const char* sec, const char* key);
    KeySlot* single_key(const char* sec, const char* key);

    void refresh_status();
    void run_setup();
    void play();
    QString guess_homefront() const;

    QDir dir_;       // Ordner von ts2.exe und ts2.ini
    QDir game_;      // game/ mit ts2.bin und ts2/
    Ini ini_;
    QStackedWidget* pages_ = nullptr;
    class QButtonGroup* nav_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* game_status_ = nullptr;
    QLineEdit* homefront_ = nullptr;
    QPlainTextEdit* setup_log_ = nullptr;
    QPushButton* setup_btn_ = nullptr;
    QPushButton* play_btn_ = nullptr;
    QProcess* setup_proc_ = nullptr;
    class QComboBox* lang_ = nullptr;
    QLabel* lang_status_ = nullptr;
    QLineEdit* xbox_image_ = nullptr;
};
