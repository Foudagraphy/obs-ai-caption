#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QSlider>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include "../PluginSettings.h"

class CaptionSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit CaptionSettingsDialog(QWidget *parent = nullptr);
    ~CaptionSettingsDialog();

    void loadSettings(const PluginSettings &settings);
    PluginSettings getSettings() const;

private slots:
    void onSaveClicked();
    void onCancelClicked();
    void onToggleGeminiKeyVisibility();
    void onToggleGroqKeyVisibility();
    void onProviderChanged(int index);
    void onTestClicked();

private:
    QComboBox *providerCombo;

    QWidget *geminiKeyRow;
    QLineEdit *geminiKeyEdit;
    QPushButton *toggleGeminiKeyBtn;
    QLabel *geminiHelpLabel;

    QWidget *groqKeyRow;
    QLineEdit *groqKeyEdit;
    QPushButton *toggleGroqKeyBtn;

    QComboBox *audioSourceCombo;
    QComboBox *textSourceCombo;
    QTextEdit *customKeywordsEdit;

    QSpinBox *preRollSpin;
    QDoubleSpinBox *silenceTimeoutSpin;
    QSlider *vadSlider;
    QLabel *vadLabel;

    QCheckBox *syncRecordingCheck;
    QCheckBox *autoClearCheck;
    QCheckBox *saveSrtCheck;
    QSpinBox *srtMaxCharsSpin;

    QPushButton *testBtn;
    QLabel *testStatusLabel;

    QPushButton *saveBtn;
    QPushButton *cancelBtn;

    void setupUI();
    void populateObsSources();
};
