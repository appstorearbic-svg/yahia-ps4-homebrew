# GTXMOD Mod Installer — PS4

هذا مشروع تطبيق PS4 مستقل عن `GTXMOD Menu` القديم. يقرأ كتالوج المودات من موقع GTXMOD مباشرة، ويعرض الاسم والتصنيف والوصف والحجم، ثم ينزّل ملف ZIP ويقرأ `installer_paths.json` داخله قبل أي كتابة على ملفات اللعبة.

## الربط بالموقع

مصدر القائمة هو `https://gtxmod-ps4.pages.dev/mods.json`، ومصدر ملفات ZIP هو الرابط الموجود في حقل `download` لكل مود. الروابط النسبية تُحل من نطاق الموقع نفسه، والعميل يرفض روابط HTTP غير المشفرة والروابط التي لا تنتمي إلى `gtxmod-ps4.pages.dev`. يُبقي التطبيق تحقق TLS الافتراضي مفعلًا ولا يتجاوز التحقق من الشهادة.

يتوقع التطبيق عناصر المودات ذات الحقول `id` و`name` و`category` و`description` و`download` و`size`. التصنيفات التي يعرضها هي: الكل، سيارات، طيارات، موطورات، قوارب، شخصيات، مابات، وسكربتات.

## قواعد التثبيت

يتحقق التطبيق من وجود `/data/orbx/gta/dlclist.xml` ومجلدات GTA المعروفة تحت `/data/orbx/gta` قبل بدء النسخ. لا ينفّذ مسارات عامة يوفرها ZIP: بل يقبل فقط القواعد المتفق عليها بعد مطابقتها حرفيًا.

إذا وجد `اسم_مجلد/dlc.rpf` مباشرة داخل ZIP، ينسخه إلى `/data/orbx/gta/rpf/dlc_addon/اسم_مجلد/dlc.rpf`، ويضيف السطر `/data/orbx/gta/rpf/dlc_addon/اسم_مجلد/` قبل `</Paths>` في `data/orbx/gta/dlclist.xml`. يرفض أسماء المجلدات التي تحتوي محارف مسار أو رموزًا غير آمنة. قبل استبدال ملف موجود، يحفظ نسخة احتياطية.

إذا وجد `vehicles.rpf` في جذر ZIP، ينسخه إلى `/data/orbx/gta/rpf/levels/gta5/vehicles.rpf`، ولا يغيّر `dlclist.xml`. تُحفظ الملفات الاحتياطية في `/data/GTXMOD/backups/`، والملفات المؤقتة في `/data/GTXMOD/staging/`. لا يُعاد إدراج سطر DLC إذا كان موجودًا بالفعل.

## التحكم

- أعلى/أسفل: اختيار مود.
- يمين/يسار: تغيير التصنيف.
- X: فتح تفاصيل المود ثم تأكيد التنزيل والتثبيت.
- مثلث: تحديث قائمة الموقع.
- O: رجوع أو خروج.

## البناء

يبنى التطبيق من `src/gtxmod-mod-installer/Makefile` باستخدام OpenOrbis Toolchain، ويستخدم `SceHttp` و`SceNet` و`SceSsl` لاتصال HTTPS ومكتبة miniz المرفقة لقراءة ZIP. يوجد workflow منفصل باسم `Build GTXMOD Mod Installer` ينتج artifact باسم `GTXMOD-Mod-Installer-PS4` عند تشغيله يدويًا أو عند تغييرات هذا التطبيق.

```sh
export OO_PS4_TOOLCHAIN="$PWD/tools/OpenOrbis/PS4Toolchain"
cd src/gtxmod-mod-installer
make -j2
```

معرّف التطبيق الجديد `YAHI00003`، وملف الحزمة `IV0000-YAHI00003_00-GTXMODINSTAL0001.pkg`. لم يتغير مصدر تطبيق GTXMOD Menu القديم.
