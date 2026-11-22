# OBS AI Closed Captions Plugin (مساعد الترجمة الذكي لـ OBS Studio) 🎙️⚡

بلجن أصلي (Native C++ Plugin) لبرنامج **OBS Studio** مخصص للترجمة الفورية وتوليد ملفات الـ `.srt` باللهجة المصرية مع كتابة المصطلحات والكلمات الإنجليزية تلقائياً بالحروف الإنجليزية (Code-switching)، مدعوم بمحرك **Groq Whisper** فائق السرعة.

---

## ✨ المميزات الرئيسية

1. **بلجن أصلية 100% داخل OBS (Native C++ DLL)**:
   - مدمجة في نواة OBS (تظهر في قائمة `Tools ➔ AI Closed Captions`).
   - لا تعتمد على صفحات ويب ولا متصفحات ولا منافذ سيرفرات داخلية.
2. **التقاط الصوت مباشرة من خلاط OBS (`Mic/Aux`)**:
   - تسحب الصوت عالي النقاء 16kHz mono مباشرة من خلاط الصوت بدون أي مشاكل أذونات في الويندوز.
3. **محرك Groq Whisper AI للعامية المصرية والمصطلحات**:
   - استخدام موديل `whisper-large-v3` عبر سيرفرات Groq فائقة السرعة (~200ms).
   - خانة مخصصة للمصطلحات الإنجليزية الخاصة بشغلك (`Lightroom, Capture One, Color Grading, Photoshop, Masking, Exposure, LUT...`) لكتابتها إنجليزي بدقة متناهية وسط الكلام العربي.
4. **تزامن تلقائي مع زر التسجيل في OBS**:
   - يبدأ الترجمة مع `Start Recording` تلقائياً.
   - عند الضغط على `Stop Recording` يتم حفظ ملف الترجمة بصيغة `.srt` بجانب الفيديو بنفس اسمه فوراً.
   - تحديث لايف لمصدر النص في المشهد مع ميزة الإخفاء التلقائي بعد الصمت.
5. **سهولة التوزيع المطلقة على الأصدقاء والزملاء**:
   - ملف تثبيت تلقائي `install.bat` ينسخ ملف الـ DLL لمكانه الصحيح بضغطة واحدة.

---

## 🛠️ كيفية عمل Build للـ DLL عبر سحابة GitHub (بدون تثبيت أي برامج على جهازك)

المشروع مزود بملف **GitHub Actions Workflow** جاهز (`.github/workflows/build.yml`):

1. **إنشاء مستودع على حسابك في GitHub**:
   - ادخل على [github.com/new](https://github.com/new) وأنشئ مستودعاً جديداً (مثلاً `obs-ai-caption`).
2. **رفع ملفات هذا المجلد إلى المستودع**:
   ```bash
   git init
   git add .
   git commit -m "Initial commit of OBS AI Caption plugin"
   git branch -M main
   git remote add origin https://github.com/YOUR_USERNAME/obs-ai-caption.git
   git push -u origin main
   ```
3. **تحميل ملف التثبيت الجاهز (Release Artifact)**:
   - بمجرد الرفع، ستذهب إلى تبويب **Actions** في صفحة مستودعك على GitHub.
   - ستجد سيرفرات مايكروسوفت السحابية قامت ببناء ملف الـ `obs-ai-caption.dll` وضغطه في ملف:
     `OBS-AI-Captions-Windows-x64.zip`
   - حمله وشاركه مع زملائك وأصحابك فوراً!
