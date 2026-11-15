/* The screen's SMS view: all message text is escaped before rendering. */
static const char *sms_tag(int tag) {
  return tag==0?L("Received","已接收"):tag==1?L("Unread","未读"):tag==2?L("Sent","已发送"):tag==3?L("Failed","发送失败"):L("Message","短信");
}
/* WMS and ISO dates are converted only when the source offset is known. */
static int sms_two_digits(const char *p) {return p[0]>='0'&&p[0]<='9'&&p[1]>='0'&&p[1]<='9';}
static int sms_pair(const char *p) {return (p[0]-'0')*10+p[1]-'0';}
static int sms_valid_date(int year,int month,int day,int hour,int minute,int second) {
  static const int days[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
  if(year<1900||year>9999||month<1||month>12||day<1||hour>23||minute>59||second>59)return 0;
  int max=days[month]+(month==2&&year%4==0&&(year%100!=0||year%400==0));return day<=max;
}
static int sms_zone_minutes(const char *s,int quarter_hours,int *minutes) {
  if(!strcmp(s,"Z")){*minutes=0;return 1;}
  int sign=1;if(*s=='+'||*s=='-'){if(*s=='-')sign=-1;s++;}
  if(!*s)return 0;
  if(quarter_hours) {
    int q=0,n=0;while(*s>='0'&&*s<='9'&&n<2){q=q*10+(*s++-'0');n++;}
    if(*s||!n||q>56)return 0;*minutes=sign*q*15;return 1;
  }
  int hours=0,n=0,m=0;
  while(*s>='0'&&*s<='9'&&n<2){hours=hours*10+(*s++-'0');n++;}
  if(!n||hours>14)return 0;
  if(*s==':'||(n==2&&strlen(s)==2&&sms_two_digits(s))) {
    if(*s==':')s++;
    if(strlen(s)!=2||!sms_two_digits(s)||(m=sms_pair(s))>=60)return 0;s+=2;
  } else if(*s=='.') {
    s++;int fraction=0,scale=1,digits=0;
    while(*s>='0'&&*s<='9'&&digits<6){fraction=fraction*10+(*s++-'0');scale*=10;digits++;}
    if(!digits)return 0;m=(fraction*60+scale/2)/scale;
  }
  if(*s||m>=60||(hours==14&&m))return 0;
  *minutes=sign*(hours*60+m);return 1;
}
static void sms_format_date_at(char *out,size_t cap,const char *raw,int display_offset) {
  size_t n=strlen(raw);int year=0,month=0,day=0,hour=0,minute=0,second=0,quarter=0;const char *zone="";
  if(n>=17&&(raw[2]==';'||raw[2]==',')&&raw[5]==raw[2]&&raw[8]==raw[2]&&raw[11]==raw[2]&&raw[14]==raw[2]&&
     sms_two_digits(raw)&&sms_two_digits(raw+3)&&sms_two_digits(raw+6)&&sms_two_digits(raw+9)&&sms_two_digits(raw+12)&&sms_two_digits(raw+15)&&
     (n==17||(raw[17]==raw[2]&&raw[18]))) {
    year=2000+sms_pair(raw);month=sms_pair(raw+3);day=sms_pair(raw+6);hour=sms_pair(raw+9);minute=sms_pair(raw+12);second=sms_pair(raw+15);
    if(n>17){zone=raw+18;quarter=raw[2]==',';}
  } else if(n>=17&&raw[2]=='/'&&raw[5]=='/'&&(raw[8]==','||raw[8]==' ')&&raw[11]==':'&&raw[14]==':'&&
            sms_two_digits(raw)&&sms_two_digits(raw+3)&&sms_two_digits(raw+6)&&sms_two_digits(raw+9)&&sms_two_digits(raw+12)&&sms_two_digits(raw+15)) {
    year=2000+sms_pair(raw);month=sms_pair(raw+3);day=sms_pair(raw+6);hour=sms_pair(raw+9);minute=sms_pair(raw+12);second=sms_pair(raw+15);zone=raw+17;quarter=1;
  } else if(n>=16&&sms_two_digits(raw)&&sms_two_digits(raw+2)&&raw[4]=='-'&&raw[7]=='-'&&
            (raw[10]==' '||raw[10]=='T')&&raw[13]==':'&&sms_two_digits(raw+5)&&sms_two_digits(raw+8)&&sms_two_digits(raw+11)&&sms_two_digits(raw+14)) {
    year=sms_pair(raw)*100+sms_pair(raw+2);month=sms_pair(raw+5);day=sms_pair(raw+8);hour=sms_pair(raw+11);minute=sms_pair(raw+14);
    zone=raw+16;
    if(*zone==':') {if(strlen(zone)<3||!sms_two_digits(zone+1))goto unchanged;second=sms_pair(zone+1);zone+=3;}
  } else goto unchanged;
  if(!sms_valid_date(year,month,day,hour,minute,second))goto unchanged;
  if(*zone) {
    int source_minutes;if(!sms_zone_minutes(zone,quarter,&source_minutes))goto unchanged;
    struct tm date={.tm_year=year-1900,.tm_mon=month-1,.tm_mday=day,.tm_hour=hour,.tm_min=minute,.tm_sec=second};
    errno=0;time_t instant=timegm(&date);if(instant==(time_t)-1&&errno==EOVERFLOW)goto unchanged;
    instant-=(time_t)source_minutes*60;
    struct tm local,*ok;
    if(display_offset==10000)ok=localtime_r(&instant,&local);
    else {instant+=(time_t)display_offset*60;ok=gmtime_r(&instant,&local);}
    if(!ok||local.tm_year+1900<1900||local.tm_year+1900>9999)goto unchanged;
    year=local.tm_year+1900;month=local.tm_mon+1;day=local.tm_mday;hour=local.tm_hour;minute=local.tm_min;
  }
  snprintf(out,cap,"%04d-%02d-%02d %02d:%02d",year,month,day,hour,minute);return;
unchanged:
  snprintf(out,cap,"%s",raw);
}
static void sms_format_date(char *out,size_t cap,const char *raw) {screen_system_timezone();sms_format_date_at(out,cap,raw,10000);}
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
static int sms_chat_max_pan(void) {
  int total=0;for(int i=0;i<sms_ui.n;i++)total+=sms_chat_height(&sms_ui.rows[i]);
  int max=total-(sms_keyboard_inline()?160:310);return max>0?max:0;
}
static void sms_bubble(const struct sms_row *r,int y) {
  int outgoing=r->tag==2||r->tag==3||r->tag==4;
  int x=outgoing?66:12,h=sms_chat_height(r);char title[120],date[80];
  rect(x,y,242,h-6,outgoing?"sms-out":"sms-in",NULL);
  sms_format_date(date,sizeof(date),r->date);
  if(r->parts>1)snprintf(title,sizeof(title),"%s%s%s  %d/%d",date,*date?" ":"",sms_tag(r->tag),r->part,r->parts);
  else snprintf(title,sizeof(title),"%s%s%s",date,*date?" ":"",sms_tag(r->tag));
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
    box(16,142,288,20,"muted",L("Message","短信正文"));
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
    int height=0,limit=typing?160:310;for(int i=0;i<sms_ui.n;i++)height+=sms_chat_height(&sms_ui.rows[i]);
    int pan=sms_ui.pan,max=sms_chat_max_pan();if(pan<0)pan=0;if(pan>max)pan=max;
    add("<div class=\"box\" style=\"left:0px;top:50px;width:320px;height:%dpx;overflow:hidden\">",limit);
    int y=limit-height+pan+sms_ui.drag_shift;for(int i=0;i<sms_ui.n;i++){sms_bubble(&sms_ui.rows[i],y);y+=sms_chat_height(&sms_ui.rows[i]);}
    add("</div>");
    if(sms_ui.load_error)box(16,120,288,44,"wrap muted",L("Unable to load conversation.","无法读取对话，请返回刷新。"));
    if(typing) {
      /* Input is painted separately, so repeated keypresses reuse the same hit document. */
      relay_keybox(190,214,56,"rkdone",L("Hide","收起"));
      relay_keybox(252,214,60,"sms_reply_send",L("Send","发送"));
      int py=relay_edit.chinese;
      if(py)render_pinyin_candidates(260);
      for(int row=0;row<3;row++) {
        const char *keys=relay_key_row(row);int len=(int)strlen(keys),start=(320-len*31)/2;
        for(int i=0;i<len;i++){char id[8],label[2]={keys[i],0};snprintf(id,sizeof(id),"rk%02x",(unsigned char)keys[i]);
          if(py)compact_linkbox(start+i*31,298+row*38,30,36,id,label,1,PLAIN,3);
          else relay_keybox(start+i*31,264+row*46,30,id,label);}
      }
      int bottom=py?414:402;
      relay_keybox(8,bottom,48,"rshift","Aa");relay_keybox(60,bottom,58,"rsymbol",relay_edit.keymode==2?"#+=":"123!?");
      relay_keybox(122,bottom,48,"rime",relay_edit.chinese?L("中","中"):L("EN","英"));relay_keybox(174,bottom,78,"rspace",L("Space","空格"));relay_keybox(256,bottom,56,"rbs","⌫");
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
    char date[80];sms_format_date(date,sizeof(date),r->date);
    header(L("Message","短信详情"));sbox(16,72,288,24,"head",r->number);sbox(16,100,288,22,"muted",date);
    int more=sms_chunk(r->content,sms_ui.text_offset,chunk,sizeof(chunk));
    sms_lines(chunk,132,18,6);
    linkbox(12,350,142,48,"sms_text_prev",L("Previous","上一页"),sms_ui.text_offset>0,PLAIN);
    linkbox(166,350,142,48,"sms_text_next",L("Next","下一页"),more,PLAIN);
    relay_wipe(chunk,sizeof(chunk));return;
  }
  header(L("SMS","短信"));char tmp[80];snprintf(tmp,sizeof(tmp),L("Conversations: %d","短信会话：%d"),sms_ui.total);
  sbox(16,70,288,22,"muted",pending_kind==JOB_SMS_SEND?L("Sending…","正在发送…"):notice[0]?notice:tmp);
  add("<div class=\"box\" style=\"left:0px;top:100px;width:320px;height:228px;overflow:hidden\">");
  for(int i=0;i<sms_ui.n;i++) {char id[16];snprintf(id,sizeof(id),"sms_row%d",i);nav_row(12,i*57+sms_ui.drag_shift,296,52,id,sms_ui.rows[i].number[0]?sms_ui.rows[i].number:L("Unknown sender","未知号码"),sms_ui.rows[i].content,"",1);}
  add("</div>");
  if(!sms_ui.n)box(16,142,288,48,"wrap muted",pending_kind==JOB_SMS_LIST?L("Loading messages…","正在读取短信…"):sms_ui.load_error?L("Unable to read SMS. Tap Refresh.","无法读取短信，请点击刷新。") : L("No messages. Tap Refresh.","暂无短信，可点击刷新。"));
  linkbox(12,338,92,44,"sms_prev",L("Previous","上一页"),sms_ui.offset>0,PLAIN);
  linkbox(114,338,92,44,"sms_refresh",L("Refresh","刷新"),!blocked(),PLAIN);
  linkbox(216,338,92,44,"sms_next",L("Next","下一页"),sms_ui.offset+sms_ui.n<sms_ui.total,PLAIN);
  linkbox(12,397,296,52,"sms_new",L("New SMS","新建短信"),!blocked(),PRIMARY);
}
