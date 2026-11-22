@echo off
title تثبيت بلجن AI Closed Captions لبرنامج OBS Studio
chcp 65001 > nul

echo ========================================================
echo     تثبيت بلجن AI Closed Captions لبرنامج OBS Studio
echo ========================================================
echo.

:: 1. التحقق من صلاحيات المدير
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [تنبيه] يرجى الضغط كليك يمين على الملف واختيار:
    echo "Run as Administrator" (تشغيل كمسؤول) ليتمكن من نسخ الملف.
    echo.
    pause
    exit /b
)

:: 2. التحقق من إغلاق OBS
tasklist /fi "imagename eq obs64.exe" | find ":" > nul
if errorlevel 1 (
    echo [تنبيه] برنامج OBS Studio مفتوح حالياً.
    echo يرجى إغلاق برنامج OBS أولاً ثم الضغط على أي زر للمتابعة.
    pause > nul
)

:: 3. العثور على مجلد OBS الافتراضي
set "OBS_DIR=C:\Program Files\obs-studio"

if not exist "%OBS_DIR%\obs-plugins\64bit" (
    echo لم يتم العثور على OBS في المسار الافتراضي (%OBS_DIR%).
    set /p "OBS_DIR=يرجى إدخال مسار مجلد obs-studio لديك: "
)

if not exist "%OBS_DIR%\obs-plugins\64bit" (
    echo [خطأ] المسار غير صحيح. لم يتم العثور على مجلد obs-plugins.
    pause
    exit /b
)

:: 4. نسخ ملف الـ DLL
echo جاري تثبيت البلجن...
copy /Y "%~dp0obs-plugins\64bit\obs-ai-caption.dll" "%OBS_DIR%\obs-plugins\64bit\" > nul

if %errorLevel% equ 0 (
    echo.
    echo ========================================================
    echo ✅ تم تثبيت البلجن بنجاح داخل برنامج OBS Studio!
    echo ========================================================
    echo.
    echo الآن:
    echo 1. افتح برنامج OBS Studio.
    echo 2. من القائمة العلوية ادخل على: Tools ➔ AI Closed Captions.
    echo 3. ضع مفتاح الـ API الخاص بك واكتب مصطلحاتك واستمتع بالترجمة!
    echo.
) else (
    echo [خطأ] فشل في نسخ الملف. تأكد من إغلاق OBS تماماً وصلاحيات المسؤول.
)

pause
