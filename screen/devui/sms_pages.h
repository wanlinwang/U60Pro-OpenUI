/* The screen's SMS view: all message text is escaped before rendering. */
static const char *sms_tag(int tag) {
  return tag==0?L("Received","已接收"):tag==1?L("Unread","未读"):tag==2?L("Sent","已发送"):tag==3?L("Failed","发送失败"):L("Message","短信");
}
/* At most 100 Unicode codepoints per page, without cutting a UTF-8 sequence. */
static int sms_chunk(const char *text,int page,char *out,size_t cap) {
  const unsigned char *p=(const unsigned char*)text;int count=0, index=0;size_t n=0;
  while(*p) {
    if(index >= (page+1)*100) { out[n]=0; return 1; }
    const unsigned char *start=p++;while((*p&0xc0)==0x80)p++;
    if(index++ < page*100)continue;
    count++;
    size_t len=(size_t)(p-start);if(n+len>=cap){out[n]=0;return 1;}
    memcpy(out+n,start,len);n+=len;
  }
  out[n]=0;(void)count;return 0;
}
/* Explicitly break long, unspaced text: the renderer does not wrap long words. */
static void sms_lines(const char *text,int y,int columns,int lines) {
  const unsigned char *p=(const unsigned char *)text;
  for(int row=0;row<lines&&*p;row++) {
    const unsigned char *start=p;int count=0;
    while(*p&&count<columns) {p++;while((*p&0xc0)==0x80)p++;count++;}
    char line[129];size_t n=(size_t)(p-start);if(n>=sizeof(line))n=sizeof(line)-1;
    memcpy(line,start,n);line[n]=0;box(16,y+row*22,288,22,"head",line);relay_wipe(line,sizeof(line));
  }
}
/* Render each fragment in place; outgoing and incoming bubbles use opposite sides. */
static int sms_chat_height(const struct sms_row *r) {
  int chars=0;for(const unsigned char *p=(const unsigned char *)r->content;*p;p++)if((*p&0xc0)!=0x80)chars++;
  return 30+20*(chars?(chars+13)/14:1);
}
static void sms_bubble(const struct sms_row *r,int y) {
  int outgoing=r->tag==2||r->tag==3||r->tag==4;
  int x=outgoing?66:12,h=sms_chat_height(r);char title[120];
  rect(x,y,242,h-6,outgoing?"sms-out":"sms-in",NULL);
  if(r->parts>1)snprintf(title,sizeof(title),"%s %s  %d/%d",sms_tag(r->tag),r->date,r->part,r->parts);
  else snprintf(title,sizeof(title),"%s %s",sms_tag(r->tag),r->date);
  sbox(x+8,y+4,226,20,"muted",title);
  const unsigned char *p=(const unsigned char *)r->content;int row=0;
  if(!*p)box(x+8,y+24,224,20,"",L("(empty)","（空短信）"));
  while(*p) {
    const unsigned char *start=p;int count=0;while(*p&&count<14){p++;while((*p&0xc0)==0x80)p++;count++;}
    char part[64];size_t n=(size_t)(p-start);memcpy(part,start,n);part[n]=0;
    add("<div class=\"box\" style=\"left:%dpx;top:%dpx;width:224px;height:20px\">",x+8,y+24+row*20);
    /* Non-breaking spaces keep consecutive and leading spaces visible. */
    for(const char *c=part;*c;c++) {if(*c==' ')add("&#160;");else {char one[2]={*c,0};escaped(one);}}
    add("</div>");relay_wipe(part,sizeof(part));row++;
  }
}
static void render_sms(void) {
  if(relay_view==RV_KEYBOARD&&sms_ui.view!=SMS_THREAD){render_relay_keyboard();return;}
  if(sms_ui.view==SMS_COMPOSE||sms_ui.view==SMS_CONFIRM) {
    header(sms_ui.view==SMS_CONFIRM?L("Confirm SMS","确认发送短信"):L("New SMS","新建短信"));
    if(sms_ui.view==SMS_COMPOSE) nav_row(12,72,296,55,"sms_to",L("Recipient","收件号码"),sms_ui.number[0]?sms_ui.number:L("Tap to enter number","点击输入号码"),"",1);
    else {box(16,72,288,20,"muted",L("Recipient","收件号码"));box(16,96,288,22,"head",sms_ui.number);}
    box(16,142,288,20,"muted",L("Message (English, digits, symbols)","短信正文（英文、数字、符号）"));
    if(sms_ui.message[0])sms_lines(sms_ui.message,168,32,5);
    else box(16,168,288,22,"head",L("Tap Edit to enter message","点击编辑输入正文"));
    if(sms_ui.view==SMS_COMPOSE) {
      linkbox(12,316,142,48,"sms_body",L("Edit message","编辑正文"),1,PLAIN);
      linkbox(166,316,142,48,"sms_preview",L("Review","预览发送"),sms_ui.number[0]&&sms_ui.message[0]&&!blocked(),PRIMARY);
      linkbox(12,382,296,48,"sms_cancel",L("Cancel","取消"),1,PLAIN);
    } else {
      box(16,306,288,38,"wrap warn",L("Confirm recipient and message. Carrier charges may apply.","请核对号码和正文，运营商可能收取短信费。"));
      linkbox(12,366,142,52,"sms_edit",L("Edit","返回修改"),1,PLAIN);
      linkbox(166,366,142,52,"sms_send",L("Send SMS","确认发送"),!blocked(),PRIMARY);
    }
    return;
  }
  if(sms_ui.view==SMS_THREAD) {
    int typing=sms_keyboard_inline();
    rect(0,0,W,48,"bar",NULL);linkbox(4,2,62,44,"back",L("‹","‹"),1,PLAIN);
    sbox(74,12,238,22,"head",sms_ui.thread_number[0]?sms_ui.thread_number:L("Unknown sender","未知号码"));
    /* When typing, keep as much of the newest conversation as fits above the keyboard. */
    int first=0,height=0,limit=typing?160:310,bottom=typing?210:360;
    for(int i=sms_ui.n-1;i>=0;i--){int h=sms_chat_height(&sms_ui.rows[i]);if(height+h>limit){first=i+1;break;}height+=h;}
    int y=bottom-height;for(int i=first;i<sms_ui.n;i++){sms_bubble(&sms_ui.rows[i],y);y+=sms_chat_height(&sms_ui.rows[i]);}
    if(sms_ui.load_error)box(16,120,288,44,"wrap muted",L("Unable to load conversation.","无法读取对话，请返回刷新。"));
    if(typing) {
      /* Input is painted separately, so repeated keypresses reuse the same hit document. */
      relay_keybox(190,214,56,"rkdone",L("Hide","收起"));
      relay_keybox(252,214,60,"sms_reply_send",L("Send","发送"));
      for(int row=0;row<3;row++) {
        const char *keys=relay_key_row(row);int len=(int)strlen(keys),start=(320-len*31)/2;
        for(int i=0;i<len;i++){char id[8],label[2]={keys[i],0};snprintf(id,sizeof(id),"rk%02x",(unsigned char)keys[i]);relay_keybox(start+i*31,264+row*46,30,id,label);}
      }
      relay_keybox(8,402,48,"rshift","Aa");relay_keybox(60,402,58,"rsymbol",relay_edit.keymode==2?"#+=":"123!?");
      relay_keybox(122,402,48,"rletters","ABC");relay_keybox(174,402,78,"rspace",L("Space","空格"));relay_keybox(256,402,56,"rbs","⌫");
    } else {
      open_row(8,366,208,48,"sms_reply",sms_reply_number()&&!blocked(),"");
      add("<div class=\"name\" style=\"top:12px\">");
      shortened(sms_reply_number()?(sms_ui.message[0]?sms_ui.message:L("Type a message…","点击输入短信…")):L("Cannot reply","无法回复此号码"),170,16);add("</div></a>");
      linkbox(224,366,88,48,"sms_reply_send",L("Send","发送"),sms_reply_number()&&sms_ui.message[0]&&!blocked(),PRIMARY);
      linkbox(8,420,96,48,"sms_older",L("Earlier","更早"),sms_ui.thread_offset+1<sms_ui.total,PLAIN);
      linkbox(112,420,96,48,"sms_newer",L("Later","更新"),sms_ui.thread_offset>0,PLAIN);
      linkbox(216,420,96,48,"sms_thread_refresh",pending_kind==JOB_SMS_SEND||pending_kind==JOB_SMS_LIST?L("Updating","刷新中"):L("Refresh","刷新"),!blocked(),PLAIN);
    }
    return;
  }
  if(sms_ui.view==SMS_DETAIL&&sms_ui.chosen>=0&&sms_ui.chosen<sms_ui.n) {
    const struct sms_row *r=&sms_ui.rows[sms_ui.chosen];char chunk[512];
    header(L("Message","短信详情"));sbox(16,72,288,24,"head",r->number);sbox(16,100,288,22,"muted",r->date);
    int more=sms_chunk(r->content,sms_ui.text_offset,chunk,sizeof(chunk));
    sms_lines(chunk,132,18,6);
    linkbox(12,350,142,48,"sms_text_prev",L("Previous","上一页"),sms_ui.text_offset>0,PLAIN);
    linkbox(166,350,142,48,"sms_text_next",L("Next","下一页"),more,PLAIN);
    relay_wipe(chunk,sizeof(chunk));return;
  }
  header(L("SMS","短信"));char tmp[80];snprintf(tmp,sizeof(tmp),L("Conversations: %d","短信会话：%d"),sms_ui.total);
  sbox(16,70,288,22,"muted",pending_kind==JOB_SMS_SEND?L("Sending…","正在发送…"):notice[0]?notice:tmp);
  for(int i=0;i<sms_ui.n;i++) {char id[16];snprintf(id,sizeof(id),"sms_row%d",i);nav_row(12,100+i*57,296,52,id,sms_ui.rows[i].number[0]?sms_ui.rows[i].number:L("Unknown sender","未知号码"),sms_ui.rows[i].content,"",1);}
  if(!sms_ui.n)box(16,142,288,48,"wrap muted",pending_kind==JOB_SMS_LIST?L("Loading messages…","正在读取短信…"):sms_ui.load_error?L("Unable to read SMS. Tap Refresh.","无法读取短信，请点击刷新。") : L("No messages. Tap Refresh.","暂无短信，可点击刷新。"));
  linkbox(12,338,92,44,"sms_prev",L("Previous","上一页"),sms_ui.offset>0,PLAIN);
  linkbox(114,338,92,44,"sms_refresh",L("Refresh","刷新"),!blocked(),PLAIN);
  linkbox(216,338,92,44,"sms_next",L("Next","下一页"),sms_ui.offset+sms_ui.n<sms_ui.total,PLAIN);
  linkbox(12,397,296,52,"sms_new",L("New SMS","新建短信"),!blocked(),PRIMARY);
}
