/* SPDX-License-Identifier: MIT
 * Fail-closed BIN transaction for TSAVE. Never remove the previous BIN until
 * the replacement is completely flushed, verified, and ready for rename.
 */
#pragma once
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdint.h>
#include "bin_game_format.h"

namespace MC1000TSAVE {
inline bool filePresent(const char *path) { struct stat st; return stat(path,&st)==0; }
inline bool path(const char *dir, const char *name, char *dst,size_t cap) {
  const int n=snprintf(dst,cap,"%s/%s",dir,name);
  return n>0 && (size_t)n<cap;
}
inline bool verify(const char *filename, const uint8_t *bytes, size_t len) {
  FILE *f=fopen(filename,"rb");
  if(!f) return false;
  bool ok=fseek(f,0,SEEK_END)==0 && ftell(f)==(long)len && fseek(f,0,SEEK_SET)==0;
  uint8_t buf[512];
  size_t pos=0;
  while(ok && pos<len) {
    const size_t n=len-pos<sizeof(buf)?len-pos:sizeof(buf);
    if(fread(buf,1,n,f)!=n || memcmp(buf,bytes+pos,n)!=0) ok=false;
    pos+=n;
  }
  if(fclose(f)!=0) ok=false;
  return ok;
}
inline bool commit(const char *directory, const char *filename,
                   const uint8_t *bytes,size_t len,bool allowReplace) {
  if(!directory || !filename || !bytes || len<=MC1000BIN::kCodeOffset ||
     len>MC1000BIN::kMaxLength || strchr(filename,'/') || strchr(filename,'\\') ||
     strlen(filename)>60) return false;
  MC1000BIN::Image img={};
  if(!MC1000BIN::identify(bytes,len,&img) || img.offset!=0 || img.length!=len) return false;
  char target[256], tmp[256], backup[256], stem[80];
  if(!path(directory,filename,target,sizeof(target))) return false;
  const size_t n=strlen(filename);
  if(n<5 || n>sizeof(stem)-5 || strcmp(filename+n-4,".BIN")!=0) return false;
  memcpy(stem,filename,n-4);
  stem[n-4]=0;
  char tempName[80];
  if(snprintf(tempName,sizeof(tempName),"%s.TMP",stem)<0 ||
     !path(directory,tempName,tmp,sizeof(tmp))) return false;
  if(filePresent(tmp)) return false; // keep any interrupted save for recovery
  const bool old=filePresent(target);
  if(old && !allowReplace) return false;
  FILE *f=fopen(tmp,"wb");
  if(!f) return false;
  bool ok=fwrite(bytes,1,len,f)==len;
  if(fflush(f)!=0) ok=false;
  if(fclose(f)!=0) ok=false;
  if(!ok || !verify(tmp,bytes,len)) { remove(tmp); return false; }
  bool backed=false;
  if(old) {
    bool found=false;
    for(int i=0;i<10;++i) {
      char name[80];
      if(snprintf(name,sizeof(name),"%s.BA%d",stem,i)<0 ||
         !path(directory,name,backup,sizeof(backup))) return false;
      if(!filePresent(backup)) {found=true;break;}
    }
    if(!found || rename(target,backup)!=0) return false; // keep .TMP
    backed=true;
  }
  ok=rename(tmp,target)==0;
  if(ok) ok=verify(target,bytes,len);
  if(!ok) {
    if(backed) {
      if(filePresent(target)) remove(target);
      rename(backup,target);
    }
    return false; // retain temp if rename failed for forensic recovery
  }
  if(backed) remove(backup);
  return true;
}
} // namespace MC1000TSAVE
