#include "kof98/lspc_ir.h"
static const uint16_t masks[16]={0x0100u,0x0110u,0x1110u,0x1114u,0x5114u,0x5154u,0x5554u,0x5555u,0x5755u,0x575du,0xd75du,0xd7ddu,0xf7ddu,0xf7dfu,0xffdfu,0xffffu};
static int visible(int line,int y,int rows){if(rows>32)rows=32;int end=(y+rows*16-1)&511;return end>=y?(line>=y&&line<=end):(line>=y||line<=end);}
uint16_t kof_lspc_rgb555(uint16_t w){unsigned r=((w>>7)&30u)|((w>>14)&1u),g=((w>>3)&30u)|((w>>13)&1u),b=((w<<1)&30u)|((w>>12)&1u);return (uint16_t)(0x8000u|r|(g<<5)|(b<<10));}
int kof_lspc_line(const uint16_t *v,uint32_t words,const uint8_t *z,uint32_t nz,unsigned anim,unsigned line,int bios,kof_line_ir *o){
 if(!v||words<KOF_LSPC_WORDS||!z||nz!=KOF_ZOOM_BYTES||line>=224||!o||(bios!=0&&bios!=1))return -1;
 o->count=0;o->evaluated=0;o->fix_bios=(uint8_t)bios;
 int scan=(int)line+16,x=0,y=0,rows=0,zy=0,zx=0;
 for(unsigned n=0;n<381;n++){
  unsigned ctl=v[0x8200+n],zoom=v[0x8000+n];
  if(ctl&64u){x=(x+zx+1)&511;zx=(zoom>>8)&15;}
  else{y=512-(int)(ctl>>7);x=v[0x8400+n]>>7;zy=zoom&255;zx=(zoom>>8)&15;rows=ctl&63;}
  if(!rows||!visible(scan,y,rows))continue;
  if(o->evaluated==KOF_LINE_SPRITES)break;
  ++o->evaluated;
  unsigned px=(unsigned)x, mask=0, first=0,width=0;
  for(unsigned i=0;i<16;i++)if(masks[zx]&(1u<<i)){if(px<320){if(!width)first=px;mask|=1u<<i;++width;}px=(px+1u)&511u;}
  if(!width)continue;
  int sl=(scan-y)&511, zl=sl&255, invert=(sl&256)!=0;
  if(invert)zl^=255;
  if(rows>32){int period=(zy+1)*2;zl%=period;if(zl>zy){zl=period-1-zl;invert=!invert;}}
  unsigned yt=z[(zy<<8)|zl],tile=yt>>4,row=yt&15;
  if(invert){row^=15;tile^=31;}
  unsigned tm=n*64u+tile*2u,attr=v[tm+1];uint32_t code=v[tm]|((attr&0xf0u)<<12);
  if(attr&8u)code=(code&~7u)|(anim&7u);else if(attr&4u)code=(code&~3u)|(anim&3u);
  if(attr&2u)row^=15;
  o->sprites[o->count++]=(kof_sprite_span){.tile=code&0x7ffffu,.mask=(uint16_t)mask,.x=(uint16_t)first,.slot=(uint16_t)n,.row=(uint8_t)row,.palette=(uint8_t)(attr>>8),.flip=(uint8_t)(attr&1u),.width=(uint8_t)width};
 }
 for(unsigned col=0;col<40;col++){uint16_t e=v[0x7000+col*32u+(unsigned)scan/8u];o->fix[col]=(kof_fix_span){.tile=e&4095u,.column=(uint8_t)col,.row=(uint8_t)(scan&7),.palette=(uint8_t)(e>>12)};}
 return 0;
}
