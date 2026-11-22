#include "CaptionSettingsDialog.h"
#include "../PluginManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <obs.h>

CaptionSettingsDialog::CaptionSettingsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("AI Closed Captions Settings - مساعد الترجمة الذكي");
    resize(580, 620);
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
    silenceTimeoutSpin->setRange(0.3, 2.0);
    silenceTimeoutSpin->setSingleStep(0.1);
    silenceTimeoutSpin->setValue(0.7);
    silenceTimeoutSpin->setSuffix(" ثانية");
    audioLayout->addRow("مهلة السكوت لتقطيع الجمل:", silenceTimeoutSpin);

    vadSlider = new QSlider(Qt::Horizontal, this);
    vadSlider->setRange(1, 40);
    vadSlider->setValue(5);
    vadLabel = new QLabel("عتبة التقاط الصوت: 0.005 (حساسية ممتازة للكلام الطبيعي)", this);
    connect(vadSlider, &QSlider::valueChanged, [this](int val) {
        float fVal = val / 1000.0f;
        QString note = (val <= 6) ? "(حساسية عالية - ممتازة)" : (val <= 15 ? "(حساسية متوسطة)" : "(حساسية منخفضة)");
        vadLabel->setText(QString("عتبة التقاط الصوت: %1 %2").arg(fVal, 0, 'f', 3).arg(note));
    });
    audioLayout->addRow(vadLabel, vadSlider);

    srtMaxCharsSpin = new QSpinBox(this);
    srtMaxCharsSpin->setRange(40, 120);
    srtMaxCharsSpin->setValue(80);
    audioLayout->addRow("أقصى عدد أحرف للسطر في الـ SRT:", srtMaxCharsSpin);

    tabs->addTab(tabAudio, "جودة الصوت والـ SRT");

    mainLayout->addWidget(tabs);

    // --- Quick Test Section ---
    QGroupBox *testGroup = new QGroupBox("🎙️ اختبار فوري للمايك والذكاء الاصطناعي (Quick Test)", this);
    QVBoxLayout *testGroupLayout = new QVBoxLayout(testGroup);

    QHBoxLayout *testBtnLayout = new QHBoxLayout();
    testBtn = new QPushButton("🎙️ بدء اختبار المايك والذكاء الاصطناعي (3 ثوانٍ)", this);
    testBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #0078d4;"
        "  color: white;"
        "  font-weight: bold;"
        "  font-size: 13px;"
        "  padding: 8px 18px;"
        "  border-radius: 5px;"
        "}"
        "QPushButton:hover { background-color: #106ebe; }"
        "QPushButton:disabled { background-color: #888; color: #ccc; }"
    );
    connect(testBtn, &QPushButton::clicked, this, &CaptionSettingsDialog::onTestClicked);
    testBtnLayout->addWidget(testBtn);

    testStatusLabel = new QLabel("اضغط للتحدث 3 ثوانٍ والتأكد من استجابة المايك وترجمة Groq فوراً دون الحاجة لتسجيل فيديو.", this);
    testStatusLabel->setWordWrap(true);
    testStatusLabel->setStyleSheet("color: #444; font-size: 12px; padding: 4px;");

    testGroupLayout->addLayout(testBtnLayout);
    testGroupLayout->addWidget(testStatusLabel);

    mainLayout->addWidget(testGroup);

    // Dialog action buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    saveBtn = new QPushButton("حفظ الإعدادات", this);
    saveBtn->setDefault(true);
    saveBtn->setStyleSheet("QPushButton { font-weight: bold; padding: 6px 16px; }");
    connect(saveBtn, &QPushButton::clicked, this, &CaptionSettingsDialog::onSaveClicked);
    btnLayout->addWidget(saveBtn);

    cancelBtn = new QPushButton("إلغاء", this);
    cancelBtn->setStyleSheet("QPushButton { padding: 6px 16px; }");
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
            if (dlg->audioSourceCombo->findData(name) < 0) {
                dlg->audioSourceCombo->addItem(name, name);
            }
        }

        if (id && (strcmp(id, "text_gdiplus") == 0 || strcmp(id, "text_gdiplus_v2") == 0 ||
                   strcmp(id, "text_gdiplus_v3") == 0 || strcmp(id, "text_ft2_source") == 0 ||
                   strcmp(id, "text_ft2_source_v2") == 0)) {
            if (dlg->textSourceCombo->findData(name) < 0) {
                dlg->textSourceCombo->addItem(name, name);
            }
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

void CaptionSettingsDialog::onTestClicked() {
    PluginSettings current = getSettings();
    if (current.api_key.empty()) {
        testStatusLabel->setText("<span style='color: #d9534f; font-weight: bold;'>⚠️ يرجى كتابة Groq API Key أولاً!</span>");
        return;
    }

    testBtn->setEnabled(false);
    testStatusLabel->setText("<span style='color: #e67e22; font-weight: bold;'>🔴 جاري الاستماع للميكروفون لمدة 3 ثوانٍ... تكلم الآن بصوتك!</span>");

    PluginManager::instance().startQuickTest(current, 3, [this](bool ok, const std::string &msg) {
        // Safe dispatch back to Qt main GUI thread
        QMetaObject::invokeMethod(this, [this, ok, msg]() {
            testBtn->setEnabled(true);
            if (ok) {
                testStatusLabel->setText(
                    QString("<span style='color: #27ae60; font-weight: bold;'>✅ نجح الاختبار بنجاح!<br>النص المترجم: \"%1\"<br>(تم إرسال النص إلى شاشة OBS أيضاً!)</span>")
                    .arg(QString::fromStdString(msg))
                );
            } else {
                testStatusLabel->setText(
                    QString("<span style='color: #e74c3c; font-weight: bold;'>❌ فشل الاختبار: %1</span>")
                    .arg(QString::fromStdString(msg))
                );
            }
        }, Qt::QueuedConnection);
    });
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

    int sliderVal = static_cast<int>(s.vad_sensitivity * 1000);
    if (sliderVal < 1) sliderVal = 5;
    if (sliderVal > 40) sliderVal = 40;
    vadSlider->setValue(sliderVal);

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
