#include "CaptionSettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <obs.h>

CaptionSettingsDialog::CaptionSettingsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("AI Closed Captions Settings - مساعد الترجمة الذكي");
    resize(540, 560);
    setupUI();
    populateObsSources();
}

CaptionSettingsDialog::~CaptionSettingsDialog() {
}

void CaptionSettingsDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QTabWidget *tabs = new QTabWidget(this);

    // --- Tab 1: AI Engine & Keywords ---
    QWidget *tabEngine = new QWidget();
    QFormLayout *engineLayout = new QFormLayout(tabEngine);

    apiKeyEdit = new QLineEdit(this);
    apiKeyEdit->setEchoMode(QLineEdit::Password);
    apiKeyEdit->setPlaceholderText("gsk_...");
    
    toggleKeyBtn = new QPushButton("👁️", this);
    toggleKeyBtn->setFixedWidth(36);
    connect(toggleKeyBtn, &QPushButton::clicked, this, &CaptionSettingsDialog::onToggleKeyVisibility);

    QHBoxLayout *keyLayout = new QHBoxLayout();
    keyLayout->addWidget(apiKeyEdit);
    keyLayout->addWidget(toggleKeyBtn);
    engineLayout->addRow("Groq API Key:", keyLayout);

    modelCombo = new QComboBox(this);
    modelCombo->addItem("whisper-large-v3 (الأعلى دقة للعامية والمصطلحات)", "whisper-large-v3");
    modelCombo->addItem("whisper-large-v3-turbo (الأسرع)", "whisper-large-v3-turbo");
    engineLayout->addRow("موديل الذكاء الاصطناعي:", modelCombo);

    customKeywordsEdit = new QTextEdit(this);
    customKeywordsEdit->setFixedHeight(90);
    customKeywordsEdit->setPlaceholderText("مثال: Lightroom, Capture One, Color Grading, Photoshop, Masking, Exposure, LUT, White Balance");
    engineLayout->addRow("المصطلحات الإنجليزية الخاصة بشغلك:\n(يكتبها إنجليزي وسط العربي)", customKeywordsEdit);

    tabs->addTab(tabEngine, "محرك الذكاء الاصطناعي والمصطلحات");

    // --- Tab 2: OBS Sources & Recording Sync ---
    QWidget *tabObs = new QWidget();
    QFormLayout *obsLayout = new QFormLayout(tabObs);

    audioSourceCombo = new QComboBox(this);
    obsLayout->addRow("مصدر صوت الميكروفون:", audioSourceCombo);

    textSourceCombo = new QComboBox(this);
    obsLayout->addRow("مصدر النص في المشهد (Text Source):", textSourceCombo);

    syncRecordingCheck = new QCheckBox("مزامنة تلقائية مع زر التسجيل في OBS (يبدأ وينهي ويحفظ الـ SRT أوتوماتيك)", this);
    syncRecordingCheck->setChecked(true);
    obsLayout->addRow(syncRecordingCheck);

    autoClearCheck = new QCheckBox("إخفاء الترجمة من الشاشة بعد 4 ثوانٍ من الصمت", this);
    autoClearCheck->setChecked(true);
    obsLayout->addRow(autoClearCheck);

    saveSrtCheck = new QCheckBox("حفظ ملف الترجمة .srt بجانب الفيديو بنفس اسمه فوراً", this);
    saveSrtCheck->setChecked(true);
    obsLayout->addRow(saveSrtCheck);

    tabs->addTab(tabObs, "ربط مصادر OBS والتسجيل");

    // --- Tab 3: Audio & SRT Tuning ---
    QWidget *tabAudio = new QWidget();
    QFormLayout *audioLayout = new QFormLayout(tabAudio);

    preRollSpin = new QSpinBox(this);
    preRollSpin->setRange(100, 1000);
    preRollSpin->setSingleStep(50);
    preRollSpin->setValue(400);
    preRollSpin->setSuffix(" ms");
    audioLayout->addRow("التسجيل المسبق (Pre-roll):\n(يمنع أكل أول حرف في الجملة)", preRollSpin);

    silenceTimeoutSpin = new QDoubleSpinBox(this);
    silenceTimeoutSpin->setRange(0.4, 2.0);
    silenceTimeoutSpin->setSingleStep(0.1);
    silenceTimeoutSpin->setValue(0.8);
    silenceTimeoutSpin->setSuffix(" ثانية");
    audioLayout->addRow("مهلة السكوت لتقطيع الجمل:", silenceTimeoutSpin);

    vadSlider = new QSlider(Qt::Horizontal, this);
    vadSlider->setRange(5, 80);
    vadSlider->setValue(20);
    vadLabel = new QLabel("عتبة التقاط الصوت: 0.020", this);
    connect(vadSlider, &QSlider::valueChanged, [this](int val) {
        vadLabel->setText(QString("عتبة التقاط الصوت: %1").arg(val / 1000.0, 0, 'f', 3));
    });
    audioLayout->addRow(vadLabel, vadSlider);

    srtMaxCharsSpin = new QSpinBox(this);
    srtMaxCharsSpin->setRange(40, 120);
    srtMaxCharsSpin->setValue(80);
    audioLayout->addRow("أقصى عدد أحرف للسطر في الـ SRT:", srtMaxCharsSpin);

    tabs->addTab(tabAudio, "جودة الصوت والـ SRT");

    mainLayout->addWidget(tabs);

    // Dialog buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    saveBtn = new QPushButton("حفظ الإعدادات", this);
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &CaptionSettingsDialog::onSaveClicked);
    btnLayout->addWidget(saveBtn);

    cancelBtn = new QPushButton("إلغاء", this);
    connect(cancelBtn, &QPushButton::clicked, this, &CaptionSettingsDialog::onCancelClicked);
    btnLayout->addWidget(cancelBtn);

    mainLayout->addLayout(btnLayout);
}

void CaptionSettingsDialog::populateObsSources() {
    audioSourceCombo->clear();
    textSourceCombo->clear();

    audioSourceCombo->addItem("Mic/Aux (الافتراضي)", "Mic/Aux");
    textSourceCombo->addItem("Captions", "Captions");

    auto enumCallback = [](void *param, obs_source_t *source) -> bool {
        CaptionSettingsDialog *dlg = static_cast<CaptionSettingsDialog*>(param);
        if (!source) return true;

        const char *name = obs_source_get_name(source);
        uint32_t flags = obs_source_get_output_flags(source);
        const char *id = obs_source_get_id(source);

        if ((flags & OBS_SOURCE_AUDIO) != 0) {
            dlg->audioSourceCombo->addItem(name, name);
        }

        if (id && (strcmp(id, "text_gdiplus") == 0 || strcmp(id, "text_gdiplus_v2") == 0 ||
                   strcmp(id, "text_gdiplus_v3") == 0 || strcmp(id, "text_ft2_source") == 0 ||
                   strcmp(id, "text_ft2_source_v2") == 0)) {
            dlg->textSourceCombo->addItem(name, name);
        }

        return true;
    };

    obs_enum_sources(enumCallback, this);
}

void CaptionSettingsDialog::onToggleKeyVisibility() {
    if (apiKeyEdit->echoMode() == QLineEdit::Password) {
        apiKeyEdit->setEchoMode(QLineEdit::Normal);
        toggleKeyBtn->setText("🔒");
    } else {
        apiKeyEdit->setEchoMode(QLineEdit::Password);
        toggleKeyBtn->setText("👁️");
    }
}

void CaptionSettingsDialog::loadSettings(const PluginSettings &s) {
    apiKeyEdit->setText(QString::fromStdString(s.api_key));
    
    int modelIdx = modelCombo->findData(QString::fromStdString(s.model));
    if (modelIdx >= 0) modelCombo->setCurrentIndex(modelIdx);

    int audioIdx = audioSourceCombo->findData(QString::fromStdString(s.audio_source_name));
    if (audioIdx >= 0) audioSourceCombo->setCurrentIndex(audioIdx);

    int textIdx = textSourceCombo->findData(QString::fromStdString(s.text_source_name));
    if (textIdx >= 0) textSourceCombo->setCurrentIndex(textIdx);

    customKeywordsEdit->setPlainText(QString::fromStdString(s.custom_keywords));
    preRollSpin->setValue(s.pre_roll_ms);
    silenceTimeoutSpin->setValue(s.silence_threshold_ms / 1000.0);
    vadSlider->setValue(static_cast<int>(s.vad_sensitivity * 1000));
    srtMaxCharsSpin->setValue(s.srt_max_line_length);

    syncRecordingCheck->setChecked(s.sync_with_recording);
    autoClearCheck->setChecked(s.auto_clear_text);
    saveSrtCheck->setChecked(s.srt_enabled);
}

PluginSettings CaptionSettingsDialog::getSettings() const {
    PluginSettings s;
    s.api_key = apiKeyEdit->text().trimmed().toStdString();
    s.model = modelCombo->currentData().toString().toStdString();
    s.audio_source_name = audioSourceCombo->currentData().toString().toStdString();
    s.text_source_name = textSourceCombo->currentData().toString().toStdString();
    s.custom_keywords = customKeywordsEdit->toPlainText().trimmed().toStdString();

    s.pre_roll_ms = preRollSpin->value();
    s.silence_threshold_ms = static_cast<int>(silenceTimeoutSpin->value() * 1000);
    s.vad_sensitivity = vadSlider->value() / 1000.0f;
    s.srt_max_line_length = srtMaxCharsSpin->value();

    s.sync_with_recording = syncRecordingCheck->isChecked();
    s.auto_clear_text = autoClearCheck->isChecked();
    s.srt_enabled = saveSrtCheck->isChecked();
    return s;
}

void CaptionSettingsDialog::onSaveClicked() {
    accept();
}

void CaptionSettingsDialog::onCancelClicked() {
    reject();
}
