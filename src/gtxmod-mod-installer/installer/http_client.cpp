#include "http_client.hpp"
#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>
#include <orbis/_types/http.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

namespace gtxhttp {
static int netPool = -1, sslContext = -1, httpContext = -1;
static const char *UA = "GTXMOD-Mod-Installer/1.00 (PS4 Homebrew)";
static bool begins(const std::string &s, const char *p) { return s.compare(0, strlen(p), p) == 0; }
static bool mediafirePage(const std::string &url) { return begins(url, "https://www.mediafire.com/file/"); }
static bool mediafireDownload(const std::string &url) { return begins(url, "https://download") && url.find(".mediafire.com/") != std::string::npos; }
static bool trustedUrl(const std::string &url) {
    if (!begins(url, "https://gtxmod-ps4.pages.dev/") && !mediafirePage(url) && !mediafireDownload(url)) return false;
    return url.find("\r") == std::string::npos && url.find("\n") == std::string::npos;
}
static std::string mediafireDirectUrl(const std::string &html) {
    // MediaFire قد يغيّر ترتيب خصائص الزر أو يرسل نسخة mobile؛ لذلك نبحث
    // عن رابط CDN المباشر نفسه، ولا نعتمد على مكان downloadButton فقط.
    const char *needles[] = {"https://download", "https:\\/\\/download"};
    for (size_t n=0;n<sizeof(needles)/sizeof(needles[0]);++n) {
        size_t p=0;
        while ((p=html.find(needles[n],p))!=std::string::npos) {
            size_t begin=p, end=p;
            while(end<html.size() && html[end]!='"' && html[end]!='\'' && html[end]!='<' && html[end]!=' ' && html[end]!='\t' && html[end]!='\r' && html[end]!='\n')++end;
            std::string url=html.substr(begin,end-begin);
            for(size_t q=0;(q=url.find("\\/",q))!=std::string::npos;)url.replace(q,2,"/");
            for(size_t q=0;(q=url.find("&amp;",q))!=std::string::npos;)url.replace(q,5,"&");
            if(mediafireDownload(url))return url;
            p=end;
        }
    }
    return std::string();
}
static bool requestOpen(const std::string &url, int &tpl, int &conn, int &req, std::string *error) {
    tpl = conn = req = -1;
    if (!trustedUrl(url)) { if (error) *error = "رابط غير آمن أو خارج موقع GTXMOD"; return false; }
    tpl = sceHttpCreateTemplate(httpContext, UA, ORBIS_HTTP_VERSION_1_1, 1);
    if (tpl < 0) { if (error) *error = "تعذر إنشاء قالب HTTP"; return false; }
    conn = sceHttpCreateConnectionWithURL(tpl, url.c_str(), true);
    if (conn < 0) { if (error) *error = "تعذر الاتصال بموقع GTXMOD"; return false; }
    req = sceHttpCreateRequestWithURL(conn, ORBIS_METHOD_GET, url.c_str(), 0);
    if (req < 0) { if (error) *error = "تعذر إنشاء طلب التنزيل"; return false; }
    // اطلب HTML غير مضغوط؛ بعض نسخ SceHttp لا تفك gzip تلقائياً، فيظهر
    // للمحلل جسم الصفحة بلا downloadButton.
    sceHttpAddRequestHeader(req, "Accept-Encoding", "identity", 0);
    sceHttpAddRequestHeader(req, "Accept", "text/html,application/xhtml+xml,*/*", 0);
    if (sceHttpSendRequest(req, nullptr, 0) < 0) { if (error) *error = "فشل إرسال الطلب"; return false; }
    int status = 0;
    if (sceHttpGetStatusCode(req, &status) < 0 || status < 200 || status >= 300) {
        if (error) { char b[96]; snprintf(b, sizeof(b), "استجابة الموقع HTTP %d", status); *error=b; }
        return false;
    }
    return true;
}
static void requestClose(int tpl, int conn, int req) {
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    if (tpl >= 0) sceHttpDeleteTemplate(tpl);
}
bool initialize(std::string *error) {
    if (httpContext >= 0) return true;
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) < 0 ||
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP) < 0 ||
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL) < 0) {
        if (error) *error = "تعذر تحميل وحدات الشبكة وHTTPS"; return false;
    }
    if (sceNetInit() < 0) { if (error) *error="تعذر تهيئة الشبكة"; return false; }
    netPool = sceNetPoolCreate("gtxmod_net", 256 * 1024, 0);
    if (netPool < 0) { if (error) *error="تعذر إنشاء مساحة شبكة"; return false; }
    sslContext = sceSslInit(SSL_POOLSIZE);
    if (sslContext < 0) { if (error) *error="تعذر تهيئة SSL"; return false; }
    httpContext = sceHttpInit(netPool, sslContext, LIBHTTP_POOLSIZE);
    if (httpContext < 0) { if (error) *error="تعذر تهيئة HTTPS"; return false; }
    return true;
}
void shutdown() {
    if (httpContext >= 0) { sceHttpTerm(httpContext); httpContext=-1; }
    if (sslContext >= 0) { sceSslTerm(); sslContext=-1; }
    if (netPool >= 0) { sceNetPoolDestroy(netPool); netPool=-1; }
}
bool getText(const std::string &url, std::string &body, std::string *error) {
    if (httpContext < 0 && !initialize(error)) return false;
    int tpl,conn,req;
    if (!requestOpen(url,tpl,conn,req,error)) { requestClose(tpl,conn,req); return false; }
    body.clear(); char buffer[8192]; bool ok=true;
    for (;;) {
        int n=sceHttpReadData(req,buffer,sizeof(buffer));
        if (n<0) { if(error)*error="فشل قراءة رد الموقع"; ok=false; break; }
        if (!n) break;
        if (body.size()+(size_t)n>4*1024*1024) { if(error)*error="قائمة المودات أكبر من الحد الآمن"; ok=false; break; }
        body.append(buffer,(size_t)n);
    }
    requestClose(tpl,conn,req); return ok;
}
bool download(const std::string &url, const std::string &destination, ProgressCallback callback, void *context, std::string *error) {
    if (httpContext < 0 && !initialize(error)) return false;
    std::string target = url;
    if (mediafirePage(url)) {
        std::string html;
        if (!getText(url, html, error)) return false;
        target = mediafireDirectUrl(html);
        if (target.empty()) { if (error) *error = "تعذر العثور على زر Download في صفحة MediaFire"; return false; }
    }
    int tpl,conn,req;
    if (!requestOpen(target,tpl,conn,req,error)) { requestClose(tpl,conn,req); return false; }
    int lengthType=ORBIS_HTTP_CONTENTLEN_NOT_FOUND; size_t total=0;
    if (sceHttpGetResponseContentLength(req,&lengthType,&total)<0 || lengthType!=ORBIS_HTTP_CONTENTLEN_EXIST) total=0;
    std::string temp=destination+".tmp"; unlink(temp.c_str());
    FILE *fp=fopen(temp.c_str(),"wb");
    if (!fp) { if(error)*error="تعذر فتح ملف التنزيل المؤقت"; requestClose(tpl,conn,req); return false; }
    uint64_t received=0; char buffer[64*1024]; bool ok=true;
    if (callback && !callback(0,total,context)) { if(error)*error="تم إلغاء التنزيل"; ok=false; }
    while (ok) {
        int n=sceHttpReadData(req,buffer,sizeof(buffer));
        if (n<0) { if(error)*error="انقطع الاتصال أثناء التنزيل"; ok=false; break; }
        if (!n) break;
        if (fwrite(buffer,1,(size_t)n,fp)!=(size_t)n) { if(error)*error="تعذر حفظ بيانات التنزيل"; ok=false; break; }
        received+=(uint64_t)n;
        if (callback && !callback(received,total,context)) { if(error)*error="تم إلغاء التنزيل"; ok=false; break; }
    }
    if (ok && total && received!=total) { if(error)*error="حجم الملف لا يطابق استجابة الموقع"; ok=false; }
    if (ok && fflush(fp)!=0) { if(error)*error="تعذر إنهاء ملف التنزيل"; ok=false; }
    if (ok && fsync(fileno(fp))<0) { if(error)*error="تعذر حفظ ملف التنزيل"; ok=false; }
    fclose(fp); requestClose(tpl,conn,req);
    if (ok && rename(temp.c_str(),destination.c_str())<0) { if(error)*error="تعذر اعتماد الملف بعد اكتمال التنزيل"; ok=false; }
    if (!ok) unlink(temp.c_str());
    return ok;
}
}
