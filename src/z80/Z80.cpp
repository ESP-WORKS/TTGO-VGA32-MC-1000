/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 MC-1000 emulator contributors
 *
 * Independent instruction decoder based on the published Z80 instruction set.
 * No source code from Marat Fayzullin's emulator is included in this file.
 */
#include "Z80.h"
#include <string.h>

#if defined(__GNUC__) && defined(ESP32)
#pragma GCC optimize ("O3")
#endif

namespace {
using U8 = uint8_t; using U16 = uint16_t;
static inline U8 rd(U16 a) { return RdZ80(a); }
static inline void wr(U16 a,U8 v) { WrZ80(a,v); }
static inline U8 imm(Z80 &s) { const U8 v=rd(s.PC.W); s.PC.W++; return v; }
static inline U16 imm16(Z80 &s) { U8 l=imm(s); return (U16)(l|((U16)imm(s)<<8)); }
static inline U16 word(U16 a) { return (U16)(rd(a)|((U16)rd((U16)(a+1))<<8)); }
static inline void store(U16 a,U16 v) { wr(a,(U8)v); wr((U16)(a+1),(U8)(v>>8)); }
static inline void push(Z80 &s,U16 v) { s.SP.W-=2;store(s.SP.W,v); }
static inline U16 pop(Z80 &s) { U16 v=word(s.SP.W);s.SP.W+=2;return v; }
static inline U8 parity(U8 v) { v ^= v>>4;v ^= v>>2;v ^= v>>1;return (U8)((~v)&1); }
static inline U8 sz(U8 v) { return (U8)(v&(S_FLAG|0x28)) | (v==0 ? Z_FLAG : 0); }
static inline U8 szp(U8 v) { return (U8)(sz(v)|(parity(v)?P_FLAG:0)); }
static inline U8 carry(Z80 &s) { return s.AF.B.l & C_FLAG; }
static inline void incR(Z80 &s) { s.R=(U8)((s.R&0x80)|((s.R+1)&0x7f)); }
static inline U8 fetch(Z80 &s) { incR(s);return imm(s); }
static inline U16 getHL(Z80 &s,int idx) { return idx==1?s.IX.W:idx==2?s.IY.W:s.HL.W; }
static inline void setHL(Z80 &s,int idx,U16 v) { if(idx==1)s.IX.W=v;else if(idx==2)s.IY.W=v;else s.HL.W=v; }
static inline U16 addrHL(Z80 &s,int idx) { return (U16)(getHL(s,idx)+(idx?(int8_t)imm(s):0)); }
static inline U8 getr(Z80 &s,int r,int idx,U16 mem) {
  switch(r) {
    case 0:return s.BC.B.h;case 1:return s.BC.B.l;
    case 2:return s.DE.B.h;case 3:return s.DE.B.l;
    case 4:return idx? (U8)(getHL(s,idx)>>8):s.HL.B.h;
    case 5:return idx? (U8)getHL(s,idx):s.HL.B.l;
    case 6:return rd(mem);
    default:return s.AF.B.h;
  }
}
static inline void setr(Z80 &s,int r,int idx,U16 mem,U8 v) {
  switch(r) {
    case 0:s.BC.B.h=v;break;case 1:s.BC.B.l=v;break;
    case 2:s.DE.B.h=v;break;case 3:s.DE.B.l=v;break;
    case 4:if(idx)setHL(s,idx,(U16)((getHL(s,idx)&255)|(v<<8)));else s.HL.B.h=v;break;
    case 5:if(idx)setHL(s,idx,(U16)((getHL(s,idx)&0xff00)|v));else s.HL.B.l=v;break;
    case 6:wr(mem,v);break;
    default:s.AF.B.h=v;break;
  }
}
static inline U16 pair(Z80 &s,int p,int idx) {
  return p==0?s.BC.W:p==1?s.DE.W:p==2?getHL(s,idx):s.SP.W;
}
static inline void setpair(Z80 &s,int p,int idx,U16 v) {
  if(p==0)s.BC.W=v;else if(p==1)s.DE.W=v;else if(p==2)setHL(s,idx,v);else s.SP.W=v;
}
static inline U8 add8(Z80 &s,U8 a,U8 b,U8 c,bool subtraction) {
  const U16 result=subtraction ? (U16)(a-b-c):(U16)(a+b+c);
  const U8 v=(U8)result;
  const U8 half=subtraction?((a^b^v)&0x10):((a^b^v)&0x10);
  const U8 overflow=subtraction?((a^b)&(a^v)&0x80):((~(a^b))&(a^v)&0x80);
  s.AF.B.l=(U8)(sz(v)|(half?H_FLAG:0)|(overflow?P_FLAG:0)|
      (subtraction?N_FLAG:0)|((result>255)?C_FLAG:0));
  return v;
}
static inline U8 incdec(Z80 &s,U8 v,bool dec) {
  const U8 o=v,r=(U8)(v+(dec?-1:1));
  s.AF.B.l=(U8)((s.AF.B.l&C_FLAG)|sz(r)|
      (((o^r)&0x10)?H_FLAG:0)|
      ((dec?o==0x80:o==0x7f)?P_FLAG:0)|(dec?N_FLAG:0));
  return r;
}
static inline void alu(Z80 &s,int op,U8 v) {
  const U8 a=s.AF.B.h;
  switch(op){
    case 0:s.AF.B.h=add8(s,a,v,0,false);break;
    case 1:s.AF.B.h=add8(s,a,v,carry(s),false);break;
    case 2:s.AF.B.h=add8(s,a,v,0,true);break;
    case 3:s.AF.B.h=add8(s,a,v,carry(s),true);break;
    case 4:s.AF.B.h=(U8)(a&v);s.AF.B.l=(U8)(szp(s.AF.B.h)|H_FLAG);break;
    case 5:s.AF.B.h=(U8)(a^v);s.AF.B.l=szp(s.AF.B.h);break;
    case 6:s.AF.B.h=(U8)(a|v);s.AF.B.l=szp(s.AF.B.h);break;
    case 7:(void)add8(s,a,v,0,true);s.AF.B.l=(U8)((s.AF.B.l&~0x28)|(v&0x28));break;
  }
}
static inline bool cond(Z80 &s,int n) {
  U8 f=s.AF.B.l;
  switch(n){case 0:return !(f&Z_FLAG);case 1:return f&Z_FLAG;
  case 2:return !(f&C_FLAG);case 3:return f&C_FLAG;
  case 4:return !(f&P_FLAG);case 5:return f&P_FLAG;
  case 6:return !(f&S_FLAG);default:return f&S_FLAG;}
}
static inline U8 rot(Z80 &s,int op,U8 v,bool cb) {
  U8 r=v,c=0; U8 oldCarry=carry(s);
  switch(op){
    case 0:c=v>>7;r=(U8)((v<<1)|c);break;
    case 1:c=v&1;r=(U8)((v>>1)|(c<<7));break;
    case 2:c=v>>7;r=(U8)((v<<1)|oldCarry);break;
    case 3:c=v&1;r=(U8)((v>>1)|(oldCarry<<7));break;
    case 4:c=v>>7;r=(U8)(v<<1);break;
    case 5:c=v&1;r=(U8)((v>>1)|(v&0x80));break;
    case 6:c=v>>7;r=(U8)((v<<1)|1);break; // undocumented SLL
    case 7:c=v&1;r=(U8)(v>>1);break;
  }
  s.AF.B.l=(U8)((cb?szp(r):(s.AF.B.l&(S_FLAG|Z_FLAG|P_FLAG)))|
                 (r&0x28)|(c?C_FLAG:0));
  return r;
}
static inline void cbop(Z80 &s,U8 op,int idx,U16 address,bool indexed) {
  const int x=op>>6,y=(op>>3)&7,z=op&7;
  const U8 old=getr(s,indexed?6:z,indexed?0:idx,address);
  U8 res=old;
  if(x==0)res=rot(s,y,old,true);
  else if(x==1) {
    U8 mask=(U8)(1<<y);
    s.AF.B.l=(U8)((s.AF.B.l&C_FLAG)|H_FLAG|(old&mask?0:(Z_FLAG|P_FLAG))|
                     (y==7&&(old&mask)?S_FLAG:0)|(old&0x28));
  } else if(x==2)res=(U8)(old&~(1<<y));
  else res=(U8)(old|(1<<y));
  if(x!=1){
    if(indexed){wr(address,res);if(z!=6)setr(s,z,0,address,res);}
    else setr(s,z,idx,address,res);
  }
}
static inline void add16(Z80 &s,U16 a,U16 b,int op,int idx) {
  if(op==0){
    uint32_t v=(uint32_t)a+b;
    s.AF.B.l=(U8)((s.AF.B.l&(S_FLAG|Z_FLAG|P_FLAG))|
      (((a&0x0fff)+(b&0x0fff))>0x0fff?H_FLAG:0)|
      (v>0xffff?C_FLAG:0)|((v>>8)&0x28));
    setHL(s,idx,(U16)v);
  } else {
    uint32_t v=op==1 ? (uint32_t)a+b+carry(s) : (uint32_t)a-b-carry(s);
    U16 r=(U16)v;
    bool h=((a^b^r)&0x1000)!=0;
    bool over=op==1 ? ((~(a^b)&(a^r)&0x8000)!=0) : (((a^b)&(a^r)&0x8000)!=0);
    s.AF.B.l=(U8)((r>>8&0xA8)|(r==0?Z_FLAG:0)|(h?H_FLAG:0)|
      (over?P_FLAG:0)|(op==2?N_FLAG:0)|(v>0xffff?C_FLAG:0));
    setHL(s,0,r);
  }
}
}
namespace {
static inline void edop(Z80 &s,U8 op,int &t) {
  const int x=op>>6,y=(op>>3)&7,z=op&7,p=y>>1,q=y&1;
  t=8;
  if(x==1){
    switch(z){
      case 0: { // IN r,(C)
        U8 v=InZ80(s.BC.W);
        if(y!=6)setr(s,y,0,s.HL.W,v);
        s.AF.B.l=(U8)((s.AF.B.l&C_FLAG)|szp(v));t=12;break;
      }
      case 1:OutZ80(s.BC.W,y==6?0:getr(s,y,0,s.HL.W));t=12;break;
      case 2:add16(s,s.HL.W,pair(s,p,0),q?1:2,0);t=15;break;
      case 3: { U16 a=imm16(s);if(q)setpair(s,p,0,word(a));else store(a,pair(s,p,0));t=20;break; }
      case 4:s.AF.B.h=add8(s,0,s.AF.B.h,0,true);t=8;break; // NEG
      case 5:s.PC.W=pop(s);s.IFF=(U8)((s.IFF&~IFF_1)|((s.IFF&IFF_2)?IFF_1:0));t=14;break;
      case 6:s.im=(y==2||y==6)?1:(y==3||y==7)?2:0;
             s.IFF=(U8)((s.IFF&~(IFF_IM1|IFF_IM2))|(s.im==1?IFF_IM1:s.im==2?IFF_IM2:0));t=8;break;
      case 7:
        switch(y){
          case 0:s.I=s.AF.B.h; t=9;break;
          case 1:s.R=s.AF.B.h;t=9;break;
          case 2:case 3: { U8 v=y==2?s.I:s.R; s.AF.B.h=v;
            s.AF.B.l=(U8)((s.AF.B.l&C_FLAG)|sz(v)|((s.IFF&IFF_2)?P_FLAG:0));t=9;break;}
          case 4:case 5:{U8 a=s.AF.B.h,m=rd(s.HL.W),n;
            if(y==4){n=(U8)((m>>4)|(a<<4));s.AF.B.h=(U8)((a&0xF0)|(m&0x0F));}
            else {n=(U8)((m<<4)|(a&0x0F));s.AF.B.h=(U8)((a&0xF0)|(m>>4));}
            wr(s.HL.W,n);s.AF.B.l=(U8)((s.AF.B.l&C_FLAG)|szp(s.AF.B.h));t=18;break; }
          default:t=8;break;
        }
      default:break;
    }
    return;
  }
  if(x!=2 || y<4 || z>3)return;
  const bool dec=(y&1)!=0;
  const bool repeat=(y&2)!=0;
  const int delta=dec?-1:1;
  const U8 data=rd(s.HL.W);
  if(z==0){ // LDI/LDIR/LDD/LDDR
    wr(s.DE.W,data);s.DE.W+=delta;s.HL.W+=delta;s.BC.W--;
    U8 r=(U8)(s.AF.B.h+data);
    s.AF.B.l=(U8)((s.AF.B.l&(S_FLAG|Z_FLAG|C_FLAG))|
      (s.BC.W?P_FLAG:0)|(r&8)|((r&2)<<4));
  } else if(z==1){ // CPI/CPIR/CPD/CPDR
    const U8 a=s.AF.B.h;
    const U8 n=(U8)(a-data);
    const bool half=(a&15)<(data&15);
    s.HL.W+=delta;s.BC.W--;
    U8 adj=(U8)(n-(half?1:0));
    s.AF.B.l=(U8)((s.AF.B.l&C_FLAG)|sz(n)|(half?H_FLAG:0)|N_FLAG|
      (s.BC.W?P_FLAG:0)|(adj&8)|((adj&2)<<4));
  } else if(z==2){ // INI/IND/INIR/INDR
    U8 v=InZ80(s.BC.W);wr(s.HL.W,v);s.HL.W+=delta;s.BC.B.h--;
    s.AF.B.l=(U8)((s.BC.B.h==0?Z_FLAG:0)|N_FLAG);
  } else { // OUTI/OUTD/OTIR/OTDR
    OutZ80(s.BC.W,data);s.HL.W+=delta;s.BC.B.h--;
    s.AF.B.l=(U8)((s.BC.B.h==0?Z_FLAG:0)|N_FLAG);
  }
  bool again=repeat&&(z==0?s.BC.W!=0:z==1?(s.BC.W!=0 && !(s.AF.B.l&Z_FLAG)):s.BC.B.h!=0);
  if(again)s.PC.W-=2;
  t=again?21:16;
}

static inline void daa(Z80 &s){
  const U8 a=s.AF.B.h, f=s.AF.B.l;
  U8 adjust=0;
  bool c=(f&C_FLAG)!=0;
  if(!(f&N_FLAG)){
    if((f&H_FLAG)||(a&0x0f)>9)adjust|=6;
    if(c||a>0x99){adjust|=0x60;c=true;}
    s.AF.B.h=(U8)(a+adjust);
  } else {
    if(f&H_FLAG)adjust|=6;
    if(c)adjust|=0x60;
    s.AF.B.h=(U8)(a-adjust);
  }
  s.AF.B.l=(U8)(szp(s.AF.B.h)|(f&N_FLAG)|
      (((a^s.AF.B.h)&0x10)?H_FLAG:0)|(c?C_FLAG:0));
}
}

extern "C" void ResetZ80(Z80 *cpu) {
  memset(cpu,0,sizeof(*cpu));
  cpu->SP.W=0xffff;cpu->AF.W=0xffff;
}

extern "C" void IntZ80(Z80 *cpu,uint16 data) {
  if(!(cpu->IFF&IFF_1) || cpu->ei_delay)return;
  cpu->IFF=(U8)(cpu->IFF&~(IFF_1|IFF_2|IFF_HALT));
  push(*cpu,cpu->PC.W);
  if(cpu->im==2)cpu->PC.W=word((U16)(((U16)cpu->I<<8)|(data&255)));
  else cpu->PC.W=0x0038;
  cpu->ICount-=cpu->im==2?19:13;
}

extern "C" void ExecZ80(Z80 *cpu) {
  Z80 &s=*cpu;
  if(s.IFF&IFF_HALT){s.ICount-=4;incR(s);return;}
  U8 op=fetch(s);
  int idx=0, extra=0;
  while(op==0xDD||op==0xFD){idx=op==0xDD?1:2;extra+=4;op=fetch(s);}
  int t=4;
  if(op==0xCB){
    U16 a;
    if(idx){a=addrHL(s,idx);op=fetch(s);t=(op>>6)==1?16:19;}
    else{op=fetch(s);a=s.HL.W;t=(op&7)==6?15:8;}
    cbop(s,op,idx,a,idx!=0);
  } else if(op==0xED){
    edop(s,fetch(s),t);
  } else {
    const int x=op>>6,y=(op>>3)&7,z=op&7,p=y>>1,q=y&1;
    U16 a=getHL(s,idx);
    if(idx && ((x==0&&(y==6&&(z==4||z==5||z==6))) ||
                (x==1&&op!=0x76&&(y==6||z==6)) || (x==2&&z==6))){
      a=addrHL(s,idx);extra+=(x==0 && y==6 && z==6) ? 5 : 8;
    }
    if(x==0){
      switch(z){
        case 0:
          switch(y){
            case 0:t=4;break; // NOP
            case 1:{U16 temp=s.AF.W;s.AF.W=s.AF1.W;s.AF1.W=temp;t=4;break;}
            case 2:{(void)imm(s);s.BC.B.h--;t=s.BC.B.h?13:8;if(s.BC.B.h)s.PC.W+=(int8_t)rd((U16)(s.PC.W-1));break;}
            case 3:{int8_t d=(int8_t)imm(s);s.PC.W+=d;t=12;break;}
            default:{int8_t d=(int8_t)imm(s);bool jump=cond(s,y-4);
              if(jump) s.PC.W+=d;
              t=jump?12:7;break;}
          }break;
        case 1:
          if(!q){setpair(s,p,idx,imm16(s));t=10;}
          else{add16(s,getHL(s,idx),pair(s,p,idx),0,idx);t=11;}
          break;
        case 2:
          if(!q){
            if(p==0)wr(s.BC.W,s.AF.B.h);
            else if(p==1)wr(s.DE.W,s.AF.B.h);
            else if(p==2)store(imm16(s),getHL(s,idx));
            else wr(imm16(s),s.AF.B.h);
          } else {
            if(p==0)s.AF.B.h=rd(s.BC.W);
            else if(p==1)s.AF.B.h=rd(s.DE.W);
            else if(p==2)setHL(s,idx,word(imm16(s)));
            else s.AF.B.h=rd(imm16(s));
          }
          t=p<2?7:(p==2?16:13);break;
        case 3:setpair(s,p,idx,(U16)(pair(s,p,idx)+(q?-1:1)));t=6;break;
        case 4:setr(s,y,idx,a,incdec(s,getr(s,y,idx,a),false));t=y==6?11:4;break;
        case 5:setr(s,y,idx,a,incdec(s,getr(s,y,idx,a),true));t=y==6?11:4;break;
        case 6:setr(s,y,idx,a,imm(s));t=y==6?10:7;break;
        case 7:
          switch(y){
            case 0:s.AF.B.h=rot(s,0,s.AF.B.h,false);break;
            case 1:s.AF.B.h=rot(s,1,s.AF.B.h,false);break;
            case 2:s.AF.B.h=rot(s,2,s.AF.B.h,false);break;
            case 3:s.AF.B.h=rot(s,3,s.AF.B.h,false);break;
            case 4:daa(s);break;
            case 5:s.AF.B.h=~s.AF.B.h;s.AF.B.l=(U8)((s.AF.B.l&(S_FLAG|Z_FLAG|P_FLAG|C_FLAG))|H_FLAG|N_FLAG|(s.AF.B.h&0x28));break;
            case 6:s.AF.B.l=(U8)((s.AF.B.l&(S_FLAG|Z_FLAG|P_FLAG))|(s.AF.B.h&0x28)|C_FLAG);break;
            case 7:{U8 c=carry(s);s.AF.B.l=(U8)((s.AF.B.l&(S_FLAG|Z_FLAG|P_FLAG))|(s.AF.B.h&0x28)|(c?H_FLAG:0)|(!c?C_FLAG:0));break;}
          }
          t=4;break;
      }
    } else if(x==1){
      if(op==0x76){s.IFF|=IFF_HALT;t=4;}
      else{setr(s,y,idx,a,getr(s,z,idx,a));t=(y==6||z==6)?7:4;}
    } else if(x==2){alu(s,y,getr(s,z,idx,a));t=z==6?7:4;}
    else { // control-flow, stack and I/O
      switch(z){
        case 0:{bool b=cond(s,y);if(b)s.PC.W=pop(s);t=b?11:5;break;}
        case 1:
          if(!q){U16 v=pop(s);if(p==3)s.AF.W=v;else setpair(s,p,idx,v);t=10;}
          else if(p==0){s.PC.W=pop(s);t=10;}
          else if(p==1){ // EXX
            U16 v=s.BC.W;s.BC.W=s.BC1.W;s.BC1.W=v;
            v=s.DE.W;s.DE.W=s.DE1.W;s.DE1.W=v;
            v=s.HL.W;s.HL.W=s.HL1.W;s.HL1.W=v;t=4;
          }else if(p==2){s.PC.W=getHL(s,idx);t=4;}
          else{s.SP.W=getHL(s,idx);t=6;}
          break;
        case 2:{U16 target=imm16(s);if(cond(s,y))s.PC.W=target;t=10;break;}
        case 3:
          switch(y){
            case 0:s.PC.W=imm16(s);t=10;break;
            case 2:{U8 port=imm(s);OutZ80((U16)((s.AF.B.h<<8)|port),s.AF.B.h);t=11;break;}
            case 3:{U8 port=imm(s);s.AF.B.h=InZ80((U16)((s.AF.B.h<<8)|port));t=11;break;}
            case 4:{U16 v=word(s.SP.W);store(s.SP.W,getHL(s,idx));setHL(s,idx,v);t=19;break;}
            case 5:{U16 v=s.DE.W;s.DE.W=s.HL.W;s.HL.W=v;t=4;break;}
            case 6:s.IFF=(U8)(s.IFF&~(IFF_1|IFF_2));s.ei_delay=0;t=4;break;
            case 7:s.ei_delay=2;t=4;break;
            default:t=4;break;
          }break;
        case 4:{U16 target=imm16(s);bool b=cond(s,y);
          if(b){push(s,s.PC.W);s.PC.W=target;}t=b?17:10;break;}
        case 5:
          if(!q){push(s,p==3?s.AF.W:pair(s,p,idx));t=11;}
          else if(p==0){U16 dest=imm16(s);push(s,s.PC.W);s.PC.W=dest;t=17;}
          else t=4;
          break;
        case 6:alu(s,y,imm(s));t=7;break;
        case 7:push(s,s.PC.W);s.PC.W=(U16)(y*8);t=11;break;
      }
    }
  }
  s.ICount-=(t+extra);
  if(s.ei_delay && --s.ei_delay==0)s.IFF=(U8)(s.IFF|IFF_1|IFF_2);
}
