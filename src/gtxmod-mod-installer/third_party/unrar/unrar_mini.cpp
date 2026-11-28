#include "unrar_mini.h"
#include "rar.hpp"
#include <string>
int unrar_mini_extract(const char *rarPath,const char *outDir){
    Archive Arc(NULL);
    wchar_t wp[2048]; CharToWide(rarPath,wp,2048);
    if(!Arc.Open(wp,0)||!Arc.IsArchive(true)) return 1;
    wchar_t wd[2048]; CharToWide(outDir,wd,2048);
    Arc.SetExtrDir(wd);
    int rc=0;
    while(Arc.ReadHeader()>0){
        if(Arc.GetHeaderType()==HEAD_FILE){
            if(!Arc.ExtractCurrentFile()){rc=2;break;}
        }
        Arc.SeekToNext();
    }
    Arc.Close();
    return rc;
}
