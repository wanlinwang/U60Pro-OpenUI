/* 320x480 local touchscreen keyboard. Every visible key is an HTML hit action.
 * Narrow character keys are 30x44; editing/action controls are >=44 pixels high. */
static void relay_keybox(int x,int y,int w,const char *id,const char *label) {
  compact_linkbox(x,y,w,44,id,label,1,PLAIN,4);
}
static int sms_keyboard_inline(void) {
  return page==SMS_PAGE&&sms_ui.view==SMS_THREAD&&relay_view==RV_KEYBOARD;
}
static void render_pinyin_candidates(int y) {
  if(!relay_ime_allowed()||!relay_edit.chinese)return;
  char syllable[16];snprintf(syllable,sizeof(syllable),"‹%s",relay_edit.pinyin[0]?relay_edit.pinyin:"拼音");
  compact_linkbox(4,y,58,34,"riprev",syllable,1,PLAIN,4);
  const unsigned char *p=(const unsigned char*)relay_candidates();int skip=relay_edit.candidate_page*4;
  while(*p&&skip-- > 0){p++;while((*p&0xc0)==0x80)p++;}
  for(int i=0;i<4;i++) {
    char label[8]="",id[12];snprintf(id,sizeof(id),"rpick%d",i);
    if(*p){const unsigned char *end=p+1;while((*end&0xc0)==0x80)end++;size_t n=(size_t)(end-p);if(n<sizeof(label)){memcpy(label,p,n);label[n]=0;}p=end;}
    compact_linkbox(64+i*48,y,46,34,id,label,label[0]!=0,PLAIN,4);
  }
  compact_linkbox(260,y,55,34,"rinext","›",1,PLAIN,4);
}
static void render_relay_keyboard(void) {
  header(relay_edit.key==RK_LATENCY_NAME?L("Target name","目标名称"):relay_edit.key==RK_LATENCY?L("Target address","目标地址"):relay_edit.key==RK_SMS_TO?L("Recipient","收件号码"):relay_edit.key==RK_SMS_BODY?L("Message","短信正文"):relay_edit.key==RK_SSID?L("SSID (ASCII)","SSID (ASCII)"):L("Wi-Fi password","Wi-Fi 密码"));
  /* The HTML scaffold contains no input, even when Show is selected. Input and
   * count are painted into this reserved 296x64 rectangle without replacing g_doc. */
  relay_keybox(8,136,94,"rclear",L("Clear","清空"));
  if(relay_ime_allowed())relay_keybox(110,136,94,"rime",relay_edit.chinese?L("Chinese","中文"):L("English","英文"));
  else relay_keybox(110,136,94,"rshow",relay_edit.key==RK_PASSWORD?(relay_edit.show?L("Hide","隐藏"):L("Show","显示")):L("ASCII","ASCII"));
  int py=relay_ime_allowed()&&relay_edit.chinese;
  if(py)render_pinyin_candidates(182);
  relay_keybox(212,136,100,"rbs",L("⌫ Delete","⌫ 退格"));
  for(int row=0;row<3;row++) {
    const char *keys=relay_key_row(row);int len=(int)strlen(keys),start=(320-len*31)/2;
    for(int i=0;i<len;i++) {
      char id[8],label[2]={keys[i],0};snprintf(id,sizeof(id),"rk%02x",(unsigned char)keys[i]);
      relay_keybox(start+i*31,(py?220:190)+row*48,30,id,label);
    }
  }
  int bottom=py?366:336;
  relay_keybox(8,bottom,70,"rshift",L("Aa","大小写"));
  relay_keybox(86,bottom,70,"rsymbol",relay_edit.keymode==2?"#+=":"123!?");
  relay_keybox(164,bottom,70,"rletters","ABC");
  relay_keybox(242,bottom,70,"rspace",L("Space","空格"));
  linkbox(8,py?418:396,148,52,"rkcancel",L("Cancel","取消"),1,PLAIN);
  linkbox(164,py?418:396,148,52,"rkdone",L("Done","完成"),1,PRIMARY);
}
/* Cache only public, fixed-font ASCII metrics once; no per-key FreeType measuring
 * and no credential-derived cache. */
static struct {int advance,left,right;} relay_keyboard_glyph[95];
static int relay_keyboard_glyph_ready;
static void relay_keyboard_glyph_init(void) {
  if(relay_keyboard_glyph_ready)return;
  for(int c=32;c<=126;c++) {
    char glyph[2]={(char)c,0};int x0,y0,x1,y1;
    relay_keyboard_glyph[c-32].advance=html_view_text_width_px(glyph,16);
    html_view_text_bounds_px(glyph,16,&x0,&y0,&x1,&y1);
    relay_keyboard_glyph[c-32].left=x0;relay_keyboard_glyph[c-32].right=x1;
  }
  relay_keyboard_glyph_ready=1;
}
static int relay_keyboard_text_extent(const char *value,int *left) {
  for(const unsigned char *s=(const unsigned char*)value;*s;s++)if(*s>=128) {
    int x0,y0,x1,y1;html_view_text_bounds_px(value,16,&x0,&y0,&x1,&y1);
    int start=x0<0?x0:0,advance=html_view_text_width_px(value,16);
    if(left)*left=start;
    return (x1>advance?x1:advance)-start;
  }
  int pen=0,min=0,max=0;relay_keyboard_glyph_init();
  for(const unsigned char *s=(const unsigned char *)value;*s;s++) {
    if(*s<32||*s>126)continue;
    int i=*s-32,a=pen+relay_keyboard_glyph[i].left,b=pen+relay_keyboard_glyph[i].right;
    if(a<min)min=a;
    if(b>max)max=b;
    pen+=relay_keyboard_glyph[i].advance;if(pen>max)max=pen;
  }
  if(left)*left=min;
  return max-min;
}
/* Bounded ASCII display tail, fitted to the reserved rectangle; caller wipes scratch. */
static void relay_keyboard_display(char value[35],char count[64]) {
  const char *s=relay_input();size_t n=strlen(s),visible=n;
  if(relay_edit.key==RK_PASSWORD&&!relay_edit.show) {if(visible>34)visible=34;memset(value,'*',visible);value[visible]=0;}
  else {size_t start=n>34?n-34:0;while(start<n&&((unsigned char)s[start]&0xc0)==0x80)start++;snprintf(value,35,"%s",s+start);}
  int width=sms_keyboard_inline()?168:296;
  while(*value&&relay_keyboard_text_extent(value,NULL)>width) {
    size_t step=1;while(((unsigned char)value[step]&0xc0)==0x80)step++;
    memmove(value,value+step,strlen(value+step)+1);
  }
  snprintf(count,64,L("%zu / %zu bytes%s","%zu / %zu 字节%s"),n,relay_input_limit(),relay_edit.overflow?L(" · FULL"," · 已满"):"");
}
static void relay_keyboard_paint(uint16_t *target) {
  char value[35]={0},count[64]={0};relay_keyboard_display(value,count);
  html_view_target_begin(target,0);
  /* Clear the complete painted input, including abandoned bytes after backspace. */
  int inline_sms=sms_keyboard_inline(),x=12,y=inline_sms?214:72,width=inline_sms?168:296;
  html_view_fill_rect(x,y,width,inline_sms?44:64,11,13,17,255);
  int left=0;relay_keyboard_text_extent(value,&left);
  html_view_draw_text_px(x-left,y,value,16,0,232,234,238,255);
  int count_y=inline_sms?y+22:106;
  if(relay_edit.overflow)html_view_draw_text_px(x,count_y,count,16,0,240,106,106,255);
  else html_view_draw_text_px(x,count_y,count,16,0,154,161,172,255);
  html_view_target_end();
  relay_wipe(value,sizeof(value));relay_wipe(count,sizeof(count));
}
