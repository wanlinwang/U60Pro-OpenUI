static int sms_unhex(char *dst,size_t cap,const char *hex) {
  if(!strcmp(hex,"-")){dst[0]=0;return 1;}
  size_t len=strlen(hex);if(!len||(len&1)||len/2>=cap)return 0;
  for(size_t i=0;i<len/2;i++) {char pair[3]={hex[i*2],hex[i*2+1],0};if(!isxdigit((unsigned char)pair[0])||!isxdigit((unsigned char)pair[1]))return 0;char *end;long n=strtol(pair,&end,16);if(*end||n<32||n>255)return 0;dst[i]=(char)n;}
  dst[len/2]=0;return 1;
}
static void sms_fetch(void) {
  char req[240],reply[32768],key[197];
  int chat=sms_ui.view==SMS_THREAD||sms_ui.view==SMS_DETAIL;
  if(chat) {
    relay_hex(key,sms_ui.thread_key);snprintf(req,sizeof(req),"sms chat3 %s %d\n",key,sms_ui.thread_offset);relay_wipe(key,sizeof(key));
  } else snprintf(req,sizeof(req),"sms conversations %d\n",sms_ui.offset);
  int result=exchange(req,reply,sizeof(reply));
  /* Older agents only know chat2. Fall back without changing their protocol. */
  if(chat&&(result!=0||strncmp(reply,"CHAT ",5))) {req[8]='2';result=exchange(req,reply,sizeof(reply));}
  if(result!=0){sms_ui.n=0;sms_ui.load_error=1;relay_wipe(req,sizeof(req));return;}
  relay_wipe(req,sizeof(req));
  char *save,*line=strtok_r(reply,"\n",&save);int total,offset,n=0;
  const char *format=chat?"CHAT %d %d":"SMS %d %d";
  if(!line||sscanf(line,format,&total,&offset)!=2||total<0||total>(chat?100000:500)||offset<0||offset>total||(chat&&(total==0||offset==total))){sms_ui.n=0;sms_ui.load_error=1;relay_wipe(reply,sizeof(reply));return;}
  struct sms_row rows[6];memset(rows,0,sizeof(rows));int valid=1,height=0;
  while((line=strtok_r(NULL,"\n",&save))&&strcmp(line,".")) {
    char *fields[7];int count=split_tabs(line,fields,7);int tag;
    if(count!=6||n>=6||sscanf(fields[1],"%d",&tag)!=1||tag<0||tag>255){valid=0;break;}
    if(chat) {
      int part,parts;
      if(strcmp(fields[0],"b")||sscanf(fields[2],"%d",&part)!=1||sscanf(fields[3],"%d",&parts)!=1||part<1||parts<part||parts>100||
         !sms_unhex(rows[n].date,sizeof(rows[n].date),fields[4])||!sms_unhex(rows[n].content,sizeof(rows[n].content),fields[5])){valid=0;break;}
      int chars=0;for(const unsigned char *p=(const unsigned char*)rows[n].content;*p;p++)if((*p&0xc0)!=0x80)chars++;
      if(chars>42){valid=0;break;}height+=30+20*(chars?(chars+13)/14:1);
      rows[n].part=part;rows[n].parts=parts;
    } else if(strcmp(fields[0],"m")||!sms_unhex(rows[n].key,sizeof(rows[n].key),fields[2])||!sms_unhex(rows[n].number,sizeof(rows[n].number),fields[3])||
       !sms_unhex(rows[n].date,sizeof(rows[n].date),fields[4])||!sms_unhex(rows[n].content,sizeof(rows[n].content),fields[5])){valid=0;break;}
    rows[n++].tag=tag;
  }
  if(!line||strcmp(line,".")||(chat?(n<1||height>310):(n>4||n>total-offset)))valid=0;
  if(!valid){sms_ui.n=0;sms_ui.load_error=1;relay_wipe(rows,sizeof(rows));relay_wipe(reply,sizeof(reply));return;}
  sms_ui.load_error=0;relay_wipe(sms_ui.rows,sizeof(sms_ui.rows));memcpy(sms_ui.rows,rows,sizeof(rows));sms_ui.n=n;sms_ui.total=total;
  if(sms_ui.view==SMS_THREAD||sms_ui.view==SMS_DETAIL)sms_ui.thread_offset=offset;else sms_ui.offset=offset;
  relay_wipe(rows,sizeof(rows));relay_wipe(reply,sizeof(reply));need_draw=1;
}
static void sms_leave(void) {
  char reply[32];exchange("sms clear\n",reply,sizeof(reply));relay_wipe(reply,sizeof(reply));
  relay_editor_clear();relay_wipe(&sms_ui,sizeof(sms_ui));
  relay_wipe(html,sizeof(html));relay_wipe(drawn_html,sizeof(drawn_html));drawn_valid=0;
}
static int sms_reply_number(void) {
  if(strncmp(sms_ui.thread_key,"n:",2))return 0;
  const char *p=sms_ui.thread_key+2;size_t n=strlen(p);if(!n||n>32)return 0;
  for(;*p;p++)if(!((*p>='0'&&*p<='9')||*p=='+'||*p=='*'||*p=='#'))return 0;
  return 1;
}
static void sms_return_thread(void) {sms_ui.view=SMS_THREAD;sms_fetch();}
/* Follow a finger within the visible batch. Pull past an edge to load the
   neighbouring batch on release; no SMS request is made during motion. */
static void sms_drag_redraw(void) {relay_keyboard_cache.valid=0;need_draw=1;}
static void sms_drag_cancel(int startpan) {
  if(sms_ui.pan!=startpan||sms_ui.drag_shift){sms_ui.pan=startpan;sms_ui.drag_shift=0;sms_drag_redraw();}
}
static void sms_drag_update(int view,int startpan,int dy) {
  int pan=startpan,shift=0;
  if(view==SMS_LIST) {
    if((dy>0&&sms_ui.offset>0)||(dy<0&&sms_ui.offset+sms_ui.n<sms_ui.total))shift=dy;
  } else if(view==SMS_THREAD) {
    int max=sms_chat_max_pan(),raw=startpan+dy;pan=raw<0?0:raw>max?max:raw;
    int extra=raw-pan;
    if((extra>0&&sms_ui.thread_offset+1<sms_ui.total)||(extra<0&&sms_ui.thread_offset>0))shift=extra;
  }
  if(shift>64)shift=64;if(shift< -64)shift= -64;
  if(sms_ui.pan!=pan||sms_ui.drag_shift!=shift){sms_ui.pan=pan;sms_ui.drag_shift=shift;sms_drag_redraw();}
}
static void sms_drag_finish(int view) {
  int shift=sms_ui.drag_shift;sms_ui.drag_shift=0;
  if(view==SMS_LIST) {
    if(shift<= -36&&sms_ui.offset+sms_ui.n<sms_ui.total){sms_ui.offset+=4;sms_fetch();}
    else if(shift>=36&&sms_ui.offset>0){sms_ui.offset=sms_ui.offset>=4?sms_ui.offset-4:0;sms_fetch();}
  } else if(view==SMS_THREAD) {
    if(shift>=36&&sms_ui.thread_offset+1<sms_ui.total){sms_ui.thread_offset++;sms_fetch();sms_ui.pan=0;}
    else if(shift<= -36&&sms_ui.thread_offset>0){sms_ui.thread_offset--;sms_fetch();sms_ui.pan=sms_chat_max_pan();}
  }
  sms_drag_redraw();
}
static int sms_inline_key(const char *action) {
  int result=relay_keyboard_tap(action);
  if(relay_view!=RV_KEYBOARD&&sms_ui.pan>sms_chat_max_pan())sms_ui.pan=sms_chat_max_pan();
  return result;
}
static int sms_tap(const char *a) {
  if(page!=SMS_PAGE)return 0;
  if(relay_view==RV_KEYBOARD) {
    if(sms_ui.view!=SMS_THREAD)return relay_keyboard_tap(a);
    if(!strcmp(a,"back"))return sms_inline_key("rkdone");
    if(strcmp(a,"sms_reply_send"))return sms_inline_key(a);
    if(!sms_reply_number()||blocked())return 1;
    if(!relay_ime_raw()||!sms_ui.message[0])return 1;
    sms_inline_key("rkdone");
  }
  if(!strcmp(a,"back")) {
    if(sms_ui.view==SMS_DETAIL){sms_ui.view=SMS_THREAD;sms_ui.text_offset=0;}
    else if(sms_ui.view==SMS_CONFIRM&&sms_ui.from_thread){sms_ui.view=SMS_THREAD;}
    else if(sms_ui.view==SMS_THREAD||sms_ui.view==SMS_COMPOSE||sms_ui.view==SMS_CONFIRM){sms_ui.view=SMS_LIST;relay_wipe(sms_ui.number,sizeof(sms_ui.number));relay_wipe(sms_ui.message,sizeof(sms_ui.message));sms_ui.from_thread=0;sms_fetch();}
    else go(MENU);
    return 1;
  }
  if(sms_ui.view==SMS_LIST) {
    if(!strcmp(a,"sms_refresh")&&!blocked())send_job("sms refresh\n",JOB_SMS_LIST,0);
    else if(!strcmp(a,"sms_prev")&&sms_ui.offset>0){sms_ui.offset-=4;sms_ui.drag_shift=0;sms_fetch();}
    else if(!strcmp(a,"sms_next")&&sms_ui.offset+sms_ui.n<sms_ui.total){sms_ui.offset+=4;sms_ui.drag_shift=0;sms_fetch();}
    else if(!strcmp(a,"sms_new")&&!blocked()){sms_ui.from_thread=0;sms_ui.view=SMS_COMPOSE;relay_edit.active=1;}
    else if(!strncmp(a,"sms_row",7)&&a[7]>='0'&&a[7]<='3'&&!a[8]&&a[7]-'0'<sms_ui.n){
      int i=a[7]-'0';snprintf(sms_ui.thread_key,sizeof(sms_ui.thread_key),"%s",sms_ui.rows[i].key);
      snprintf(sms_ui.thread_number,sizeof(sms_ui.thread_number),"%s",sms_ui.rows[i].number);
      relay_wipe(sms_ui.message,sizeof(sms_ui.message));sms_ui.thread_offset=0;sms_ui.pan=sms_ui.drag_shift=0;relay_edit.active=1;sms_return_thread();
    }
  } else if(sms_ui.view==SMS_THREAD) {
    if(!strcmp(a,"sms_older")&&sms_ui.thread_offset+1<sms_ui.total){sms_ui.thread_offset++;sms_fetch();sms_ui.pan=0;}
    else if(!strcmp(a,"sms_newer")&&sms_ui.thread_offset>0){sms_ui.thread_offset--;sms_fetch();sms_ui.pan=sms_chat_max_pan();}
    else if(!strcmp(a,"sms_thread_refresh")&&!blocked())send_job("sms refresh\n",JOB_SMS_LIST,0);
    else if(!strcmp(a,"sms_reply")&&sms_reply_number()&&!blocked())relay_key_open(RK_SMS_BODY);
    else if(!strcmp(a,"sms_reply_send")&&sms_reply_number()&&sms_ui.message[0]&&!blocked()){
      snprintf(sms_ui.number,sizeof(sms_ui.number),"%s",sms_ui.thread_key+2);sms_ui.from_thread=1;sms_ui.view=SMS_CONFIRM;
    }
  } else if(sms_ui.view==SMS_DETAIL) {
    if(!strcmp(a,"sms_text_prev")&&sms_ui.text_offset>0)sms_ui.text_offset--;
    else if(!strcmp(a,"sms_text_next")){char chunk[512];if(sms_chunk(sms_ui.rows[sms_ui.chosen].content,sms_ui.text_offset,chunk,sizeof(chunk)))sms_ui.text_offset++;relay_wipe(chunk,sizeof(chunk));}
  } else if(sms_ui.view==SMS_COMPOSE) {
    if(!strcmp(a,"sms_to"))relay_key_open(RK_SMS_TO);
    else if(!strcmp(a,"sms_body"))relay_key_open(RK_SMS_BODY);
    else if(!strcmp(a,"sms_preview")&&sms_ui.number[0]&&sms_ui.message[0]&&!blocked())sms_ui.view=SMS_CONFIRM;
    else if(!strcmp(a,"sms_cancel")){relay_wipe(sms_ui.number,sizeof(sms_ui.number));relay_wipe(sms_ui.message,sizeof(sms_ui.message));sms_ui.view=SMS_LIST;sms_fetch();}
  } else if(sms_ui.view==SMS_CONFIRM) {
    if(!strcmp(a,"sms_edit"))sms_ui.view=sms_ui.from_thread?SMS_THREAD:SMS_COMPOSE;
    else if(!strcmp(a,"sms_send")&&!blocked()) {
      char number[65],message[321],req[400];relay_hex(number,sms_ui.number);relay_hex(message,sms_ui.message);
      snprintf(req,sizeof(req),"sms send %s %s\n",number,message);
      int result=send_job(req,JOB_SMS_SEND,0);
      relay_wipe(number,sizeof(number));relay_wipe(message,sizeof(message));relay_wipe(req,sizeof(req));
      relay_wipe(sms_ui.number,sizeof(sms_ui.number));relay_wipe(sms_ui.message,sizeof(sms_ui.message));sms_ui.view=sms_ui.from_thread?SMS_THREAD:SMS_LIST;
      if(result){snprintf(notice,sizeof(notice),"%s",L("Send outcome unknown. Check Sent before retrying.","发送结果未知，重试前请检查已发送短信。"));dialog=3;}
    }
  }
  return 1;
}
