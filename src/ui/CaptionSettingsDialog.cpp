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
    setWindowTitle("AI Closed Captions Settings - مساعد الترجمة والذكاء الاصطناعي");
    resize(600, 640);
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
    QVBoxLayout *engineMainLayout = new QVBoxLayout(tabEngine);
    QFormLayout *engineLayout = new QFormLayout();

    providerCombo = new QComboBox(this);
    providerCombo->addItem("🌟 Google Gemini 2.0 Flash (محرك Antigravity - الأفضل للعامية والمصطلحات)", "gemini");
    providerCombo->addItem("Groq Whisper Large v3 (محرك Groq السريع)", "groq");
    providerCombo->setStyleSheet("QComboBox { font-weight: bold; font-size: 13px; padding: 4px; }");
    connect(providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CaptionSettingsDialog::onProviderChanged);
    engineLayout->addRow("محرك الذكاء الاصطناعي (AI Provider):", providerCombo);

    // 1. Google Gemini API Key Row
    geminiKeyRow = new QWidget(this);
    QVBoxLayout *gCol = new QVBoxLayout(geminiKeyRow);
    gCol->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *gRow = new QHBoxLayout();
    geminiKeyEdit = new QLineEdit(geminiKeyRow);
    geminiKeyEdit->setEchoMode(QLineEdit::Password);
    geminiKeyEdit->setPlaceholderText("AIzaSy...");
    toggleGeminiKeyBtn = new QPushButton("👁️", geminiKeyRow);
    toggleGeminiKeyBtn->setFixedWidth(36);
    connect(toggleGeminiKeyBtn, &QPushButton::clicked, this, &CaptionSettingsDialog::onToggleGeminiKeyVisibility);
    gRow->addWidget(geminiKeyEdit);
    gRow->addWidget(toggleGeminiKeyBtn);

    geminiHelpLabel = new QLabel(
        "<span style='color: #27ae60; font-weight: bold;'>مجاني تماماً 100%: </span>"
        "<a href=\"https://aistudio.google.com/app/apikey\" style=\"color: #0078d4; font-weight: bold;\">"
        "اضغط هنا لفتح صفحة Google AI Studio ونسخ المفتاح في 10 ثوانٍ</a>",
        geminiKeyRow
    );
    geminiHelpLabel->setOpenExternalLinks(true);

    gCol->addLayout(gRow);
    gCol->addWidget(geminiHelpLabel);
    engineLayout->addRow("Google Gemini API Key:", geminiKeyRow);

    // 2. Groq API Key Row
    groqKeyRow = new QWidget(this);
    QHBoxLayout *groqRowLayout = new QHBoxLayout(groqKeyRow);
    groqRowLayout->setContentsMargins(0, 0, 0, 0);
    groqKeyEdit = new QLineEdit(groqKeyRow);
    groqKeyEdit->setEchoMode(QLineEdit::Password);
    groqKeyEdit->setPlaceholderText("gsk_...");
    toggleGroqKeyBtn = new QPushButton("👁️", groqKeyRow);
    toggleGroqKeyBtn->setFixedWidth(36);
    connect(toggleGroqKeyBtn, &QPushButton::clicked, this, &CaptionSettingsDialog::onToggleGroqKeyVisibility);
    groqRowLayout->addWidget(groqKeyEdit);
    groqRowLayout->addWidget(toggleGroqKeyBtn);
    engineLayout->addRow("Groq API Key:", groqKeyRow);

    // Technical Keywords
    customKeywordsEdit = new QTextEdit(this);
    customKeywordsEdit->setFixedHeight(85);
    customKeywordsEdit->setPlaceholderText("مثال: Lightroom, Capture One, Photoshop, Masking, Exposure, LUT, White Balance, Color Grading");
    engineLayout->addRow("المصطلحات الإنجليزية الخاصة بشغلك:\n(يكتبها إنجليزي وسط العربي تلقائياً)", customKeywordsEdit);

    engineMainLayout->addLayout(engineLayout);
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
    preRollSpin->setValue(350);
    preRollSpin->setSuffix(" ms");
    audioLayout->addRow("التسجيل المسبق (Pre-roll):\n(يمنع أكل أول حرف في الجملة)", preRollSpin);

    silenceTimeoutSpin = new QDoubleSpinBox(this);
    silenceTimeoutSpin->setRange(0.3, 2.0);
    silenceTimeoutSpin->setSingleStep(0.1);
    silenceTimeoutSpin->setValue(0.6);
    silenceTimeoutSpin->setSuffix(" ثانية");
    audioLayout->addRow("مهلة السكوت لتقطيع الجمل:", silenceTimeoutSpin);

    vadSlider = new QSlider(Qt::Horizontal, this);
    vadSlider->setRange(5, 50);
    vadSlider->setValue(10);
    vadLabel = new QLabel("عتبة التقاط الصوت: 0.010 (عزل ممتاز لأنفاس المايك والضوضاء)", this);
    connect(vadSlider, &QSlider::valueChanged, [this](int val) {
        float fVal = val / 1000.0f;
        QString note = (val <= 10) ? "(حساسية متوازنة - ممتازة)" : (val <= 20 ? "(حساسية متوسطة)" : "(حساسية منخفضة)");
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

    testStatusLabel = new QLabel("اضغط للتحدث 3 ثوانٍ والتأكد من دقة الترجمة بالعامية والمصطلحات فوراً دون الحاجة لتسجيل فيديو.", this);
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

    onProviderChanged(0);
}

void CaptionSettingsDialog::onProviderChanged(int index) {
    QString currentProv = providerCombo->currentData().toString();
    if (currentProv == "gemini") {
        geminiKeyRow->setVisible(true);
        groqKeyRow->setVisible(false);
    } else {
        geminiKeyRow->setVisible(false);
        groqKeyRow->setVisible(true);
    }
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

void CaptionSettingsDialog::onToggleGeminiKeyVisibility() {
    if (geminiKeyEdit->echoMode() == QLineEdit::Password) {
        geminiKeyEdit->setEchoMode(QLineEdit::Normal);
        toggleGeminiKeyBtn->setText("🔒");
    } else {
        geminiKeyEdit->setEchoMode(QLineEdit::Password);
        toggleGeminiKeyBtn->setText("👁️");
    }
}

void CaptionSettingsDialog::onToggleGroqKeyVisibility() {
    if (groqKeyEdit->echoMode() == QLineEdit::Password) {
        groqKeyEdit->setEchoMode(QLineEdit::Normal);
        toggleGroqKeyBtn->setText("🔒");
    } else {
        groqKeyEdit->setEchoMode(QLineEdit::Password);
        toggleGroqKeyBtn->setText("👁️");
    }
}

void CaptionSettingsDialog::onTestClicked() {
    PluginSettings current = getSettings();
    if (current.getActiveApiKey().empty()) {
        std::string provName = (current.provider == "gemini") ? "Google Gemini" : "Groq";
        testStatusLabel->setText(QString("<span style='color: #d9534f; font-weight: bold;'>⚠️ يرجى كتابة مفتاح %1 API أولاً!</span>")
                                 .arg(QString::fromStdString(provName)));
        return;
    }

    testBtn->setEnabled(false);
    testStatusLabel->setText("<span style='color: #e67e22; font-weight: bold;'>🔴 جاري الاستماع للميكروفون لمدة 3 ثوانٍ... تكلم الآن بصوتك!</span>");

    PluginManager::instance().startQuickTest(current, 3, [this](bool ok, const std::string &msg) {
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
    int provIdx = providerCombo->findData(QString::fromStdString(s.provider));
    if (provIdx >= 0) providerCombo->setCurrentIndex(provIdx);
    onProviderChanged(provIdx >= 0 ? provIdx : 0);

    geminiKeyEdit->setText(QString::fromStdString(s.gemini_api_key));
    groqKeyEdit->setText(QString::fromStdString(s.groq_api_key));

    int audioIdx = audioSourceCombo->findData(QString::fromStdString(s.audio_source_name));
    if (audioIdx >= 0) audioSourceCombo->setCurrentIndex(audioIdx);

    int textIdx = textSourceCombo->findData(QString::fromStdString(s.text_source_name));
    if (textIdx >= 0) textSourceCombo->setCurrentIndex(textIdx);

    customKeywordsEdit->setPlainText(QString::fromStdString(s.custom_keywords));
    preRollSpin->setValue(s.pre_roll_ms);
    silenceTimeoutSpin->setValue(s.silence_threshold_ms / 1000.0);

    int sliderVal = static_cast<int>(s.vad_sensitivity * 1000);
    if (sliderVal < 5) sliderVal = 10;
    if (sliderVal > 50) sliderVal = 50;
    vadSlider->setValue(sliderVal);

    srtMaxCharsSpin->setValue(s.srt_max_line_length);

    syncRecordingCheck->setChecked(s.sync_with_recording);
    autoClearCheck->setChecked(s.auto_clear_text);
    saveSrtCheck->setChecked(s.srt_enabled);
}

PluginSettings CaptionSettingsDialog::getSettings() const {
    PluginSettings s;
    s.provider = providerCombo->currentData().toString().toStdString();
    s.gemini_api_key = geminiKeyEdit->text().trimmed().toStdString();
    s.groq_api_key = groqKeyEdit->text().trimmed().toStdString();
    s.gemini_model = "gemini-2.0-flash";
    s.groq_model = "whisper-large-v3-turbo";

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
