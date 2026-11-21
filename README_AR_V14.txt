تحديث GTXMOD V14
=================

هذا الإصدار يضيف شاشة بداية بثلاثة خيارات بدل خيارين:

1) تحميل مود
2) حذف المود
3) اسم الموقع

وظيفة «تحميل مود»
------------------
عند اختيار «تحميل مود» لا تظهر واجهة الكتالوج. تظهر لوحة إدخال إنكليزية مناسبة للكود الأحمر الذي يعرضه موقع GTXMOD تحت منشورات المودات.

الأزرار داخل شاشة الكود:
- الأسهم: تحريك المؤشر بين الحروف والأرقام.
- X: إضافة الحرف المحدد.
- R2: تبديل الحروف الإنكليزية بين ABC وabc، وتظهر الحالة الحالية أعلى اللوحة.
- Triangle: حذف آخر حرف.
- Square: تنفيذ المطابقة وبدء التنزيل المباشر.
- Circle: العودة إلى الشاشة الثلاثية.

مصدر الأكواد
------------
الكود لا يُستخرج من mods.json. التطبيق يقرأ:
https://gtxmod-ps4.pages.dev/api/mod-codes

تتم قراءة codes.static أولاً، ثم codes.community فقط إذا لم يوجد نفس معرّف المنشور داخل static. بعد ذلك يطابق التطبيق قيمة الكود مع حقل id الموجود في mods.json. إذا وجد المطابقة، يضع المنشور المطابق وحده في قائمة التثبيت ويستدعي نفس beginInstall() المستخدم في التثبيت العادي. لذلك يبدأ التنزيل والفحص والتثبيت والنقل مباشرة دون عرض صفحة الكتالوج.

إذا لم يجد التطبيق الكود، يعرض رسالة «لم يتم العثور على منشور بهذا الكود» ولا يبدأ التثبيت. يجب كتابة الكود كما يظهر في الموقع، مع أن المطابقة لا تفرق بين الحروف الإنكليزية الكبيرة والصغيرة.

وظيفة «حذف المود»
------------------
لم يتغير منطق الحذف في V14. تبقى قائمة المودات المثبتة وسجل installed.db ومسارات الحذف وإزالة أسطر dlclist.xml واستبدال vehicles.rpf كما كانت في V13.

وظيفة «اسم الموقع»
-------------------
تعرض الشاشة الرابط:
https://gtxmod-ps4.pages.dev

التعديلات المصدرية
------------------
- mod_catalog.hpp: إضافة الحقل code إلى Mod.
- mod_catalog.cpp: جلب API الأكواد وتحليل static وcommunity وربط الكود بمعرّف المود.
- main.cpp: إضافة codeMode، شاشة إدخال الكود، مطابقة الكود، خيار اسم الموقع، والخيار الثالث في شاشة البداية.
- http_client.cpp: دعم روابط صفحات MediaFire من نوع /file/.../file؛ يجلب التطبيق الصفحة، يبحث عن زر downloadButton، يستخرج رابط التنزيل المباشر، ثم يبدأ تنزيل الملف. الروابط المباشرة القديمة ما زالت مدعومة.
- mod_catalog.cpp: السماح بروابط صفحات MediaFire الآمنة داخل حقل download مع إبقاء بقية النطاقات الخارجية مرفوضة.
- Makefile وWorkflow لم يحتاجا تغييراً لأن Makefile يجمع كل ملفات installer/*.cpp والـ Workflow يراقب مجلد المصدر ويرفع artifact باسم GTXMOD-Mod-Installer-PS4.

دعم MediaFire
-------------
إذا كان رابط المنشور بهذا الشكل:
https://www.mediafire.com/file/معرّف/اسم-الملف.zip/file

فلن يحاول التطبيق حفظ صفحة HTML على أنها ZIP. بدلاً من ذلك يجلب صفحة MediaFire ويقرأ الرابط الموجود في زر Download، مثل:
https://downloadXXXX.mediafire.com/.../اسم-الملف.zip

ثم ينزل الملف المباشر إلى مجلد مؤقت، وبعد انتهاء النقل والتثبيت يحذف الملف المؤقت كالمعتاد. إذا تغير تصميم MediaFire ولم يعد زر downloadButton موجوداً، سيظهر خطأ واضح بدلاً من تثبيت ملف غير صالح.

بناء GitHub Actions من Termux
-----------------------------
بعد فك المصدر داخل نسخة المستودع:

cd ~/yahia-cloud-build
git diff --check
git add src/gtxmod-mod-installer README_AR_V14.txt
git commit -m "Add V14 direct mod code installer"
git push

ثم راقب آخر تشغيل:

RUN_ID=$(gh run list --repo appstorearbic-svg/yahia-ps4-homebrew --workflow build-mod-installer.yml --limit 1 --json databaseId --jq '.[0].databaseId')
echo "RUN_ID=$RUN_ID"
gh run watch "$RUN_ID" --repo appstorearbic-svg/yahia-ps4-homebrew --exit-status

عند نجاح البناء نزّل artifact:

rm -rf "$HOME/GTXMOD-V14-download"
mkdir -p "$HOME/GTXMOD-V14-download"
gh run download "$RUN_ID" --repo appstorearbic-svg/yahia-ps4-homebrew --name GTXMOD-Mod-Installer-PS4 --dir "$HOME/GTXMOD-V14-download"
mkdir -p /sdcard/Download/pkg/finish
find "$HOME/GTXMOD-V14-download" -type f -name 'GTXMOD-Mod-Installer.pkg' -exec cp -f {} /sdcard/Download/pkg/finish/ \;
ls -lh /sdcard/Download/pkg/finish/GTXMOD-Mod-Installer.pkg

ملاحظة مهمة: يجب استخدام RUN_ID الخاص بالبناء الحالي، وعدم نسخ PKG من مجلد قديم أو من تشغيل سابق.

الاختبار على PS4
-----------------
لم يتم تشغيل PKG داخل جهاز PS4 من هذه البيئة، لذلك لا يجوز اعتبار V14 مثبتاً أو مجرباً فعلياً قبل نجاح GitHub Actions، تثبيت PKG الجديد على PS4، فتح التطبيق، اختيار «تحميل مود»، إدخال كود أحمر حقيقي من منشور الموقع، والتأكد من بدء التنزيل والفحص والتثبيت مباشرة.

اختبار إضافي مطلوب:
- إدخال كود صحيح والتأكد من تثبيت المنشور المطابق.
- إدخال كود خاطئ والتأكد من ظهور رسالة الخطأ وعدم بدء التثبيت.
- اختيار «حذف المود» والتأكد من بقاء وظائف الحذف السابقة.
- اختيار «اسم الموقع» والتأكد من ظهور الرابط والعودة بالدائرة.

فحص محلي تم إنجازه
-------------------
تمت ترجمة mod_catalog.cpp محلياً بواسطة clang++ مع C++11 ونجح فحص الصياغة. كما تم التحقق من توازن الأقواس في main.cpp. بيئة المصدر الحالية ليست نسخة Git عاملة، لذلك يجب تنفيذ git diff --check بعد فك الحزمة داخل نسخة المستودع قبل commit.
