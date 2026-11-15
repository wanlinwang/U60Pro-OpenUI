/* Strictly bounded records from the local agent. Targets are never shell text. */
static int latency_wire_target(const char *s) {
  size_t n=strlen(s);if(!n||n>64)return 0;
  int https=!strncmp(s,"https://",8);if(https)s+=8;
  if(!*s||*s=='-')return 0;
  if(*s=='[') {if(!https)return 0;s++;while(*s&&*s!=']') {if(!isxdigit((unsigned char)*s)&&*s!=':'&&*s!='.')return 0;s++;}return *s==']'&&!s[1];}
  for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='.'||*s=='-'||*s==':'))return 0;
  return 1;
}
static int latency_fetch_names(void) {
  char reply[6144],*save,*line;char names[LATENCY_SLOTS][73]={{0}};
  latency_ui.name_support=0;
  if(exchange("latency names\n",reply,sizeof(reply))||!strcmp(reply,"ERR Unsupported action\n")||!strcmp(reply,"OK 1\n")) {memset(latency_ui.names,0,sizeof(latency_ui.names));return 1;}
  if(strncmp(reply,"NAMES 24\n",9))return 0;
  line=strtok_r(reply,"\n",&save);if(!line||strcmp(line,"NAMES 24"))return 0;
  for(int i=0;i<LATENCY_SLOTS;i++) {
    int slot;char target[65],raw[145],extra;line=strtok_r(NULL,"\n",&save);
    if(!line||sscanf(line,"%d %64s %144s %c",&slot,target,raw,&extra)!=3||slot!=i||strcmp(target,latency_ui.targets[i][0]?latency_ui.targets[i]:"-"))return 0;
    if(strcmp(raw,"-")) {
      size_t n=strlen(raw);if(!n||n>144||n%2)return 0;
      for(size_t j=0;j<n;j+=2){unsigned int v;if(sscanf(raw+j,"%2x",&v)!=1||!isxdigit((unsigned char)raw[j])||!isxdigit((unsigned char)raw[j+1])||!v)return 0;names[i][j/2]=(char)v;}
      names[i][n/2]=0;
    }
  }
  line=strtok_r(NULL,"\n",&save);if(!line||strcmp(line,".")||strtok_r(NULL,"\n",&save))return 0;
  memcpy(latency_ui.names,names,sizeof(names));latency_ui.name_support=1;return 1;
}
static int latency_fetch_targets(void) {
  char reply[2048],targets[LATENCY_SLOTS][65]={{0}},*save,*line;int slots=LATENCY_SLOTS;
  if(exchange("latency targets3\n",reply,sizeof(reply))||!strcmp(reply,"ERR Unsupported action\n")) {
    slots=6;
    if(exchange("latency targets2\n",reply,sizeof(reply))||!strcmp(reply,"ERR Unsupported action\n")) {
      slots=4;if(exchange("latency targets\n",reply,sizeof(reply)))goto bad;
    }
  }
  line=strtok_r(reply,"\n",&save);if(!line||strcmp(line,slots==4?"TARGETS 4":slots==6?"TARGETS 6":"TARGETS 24"))goto bad;
  for(int i=0;i<slots;i++) {
    int slot;char raw[65],extra;
    line=strtok_r(NULL,"\n",&save);
    if(!line||sscanf(line,"%d %64s %c",&slot,raw,&extra)!=2||slot!=i)goto bad;
    if(strcmp(raw,"-")) {if(!latency_wire_target(raw))goto bad;snprintf(targets[i],sizeof(targets[i]),"%s",raw);}
  }
  line=strtok_r(NULL,"\n",&save);if(!line||strcmp(line,".")||strtok_r(NULL,"\n",&save))goto bad;
  memcpy(latency_ui.targets,targets,sizeof(targets));memset(latency_ui.results,0,sizeof(latency_ui.results));
  latency_ui.slots=slots;
  if(slots==LATENCY_SLOTS){if(!latency_fetch_names())goto bad;}else {latency_ui.name_support=0;memset(latency_ui.names,0,sizeof(latency_ui.names));}
  latency_clamp_page();latency_ui.loaded=1;latency_ui.result_error=0;need_draw=1;return 1;
bad:
  latency_ui.loaded=0;need_draw=1;return 0;
}
static void latency_fetch_results(void) {
  char reply[3072],results[LATENCY_SLOTS][32]={{0}},*save,*line;int count=latency_target_count();
  if(exchange("latency results\n",reply,sizeof(reply)))goto bad;
  line=strtok_r(reply,"\n",&save);int n;char extra;
  if(!line||sscanf(line,"LATENCY %d %c",&n,&extra)!=1||n!=count)goto bad;
  for(int pos=0;pos<count;pos++){int i=latency_visible_slot(pos);if(i>=0&&latency_ui.targets[i][0]) {
    char target[65],value[32];line=strtok_r(NULL,"\n",&save);
    if(!line||sscanf(line,"%64s %31s %c",target,value,&extra)!=2||strcmp(target,latency_ui.targets[i]))goto bad;
    if(!strcmp(value,"timeout")||!strcmp(value,"error"))snprintf(results[i],sizeof(results[i]),"%s",value);
    else if(!strncmp(value,"tsd:",4)||!strncmp(value,"tsr:",4)||!strncmp(value,"tsu:",4)) {
      char *end;double ms=strtod(value+4,&end);if(*end||!(ms>=0&&ms<=60000))goto bad;
      snprintf(results[i],sizeof(results[i]),"TS %.1fms %s",ms,value[2]=='d'?L("direct","直连"):value[2]=='r'?L("relay","中继"):"");
    } else {char *end;double ms=strtod(value,&end);if(*end||!(ms>=0&&ms<=60000))goto bad;snprintf(results[i],sizeof(results[i]),"%.1f ms",ms);}
  }}
  line=strtok_r(NULL,"\n",&save);if(!line||strcmp(line,".")||strtok_r(NULL,"\n",&save))goto bad;
  memcpy(latency_ui.results,results,sizeof(results));latency_ui.result_error=0;need_draw=1;return;
bad:
  memset(latency_ui.results,0,sizeof(latency_ui.results));latency_ui.result_error=1;need_draw=1;
}
static int latency_tap(const char *a) {
  if(page!=LATENCY_PAGE)return 0;
  if(relay_view==RV_KEYBOARD&&(relay_edit.key==RK_LATENCY||relay_edit.key==RK_LATENCY_NAME)) {
    if(!strcmp(a,"rkdone")&&relay_edit.key==RK_LATENCY_NAME) {
      if(!relay_ime_raw())return 1;
      char req[240],reply[64],hex[145];const char *h="0123456789abcdef";size_t n=strlen(latency_ui.edit);
      for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)latency_ui.edit[i];hex[2*i]=h[c>>4];hex[2*i+1]=h[c&15];}hex[n*2]=0;
      snprintf(req,sizeof(req),"latency name %d %s %s\n",latency_ui.slot,latency_ui.targets[latency_ui.slot],n?hex:"-");
      if(exchange(req,reply,sizeof(reply))||strcmp(reply,"OK\n")) {relay_editor_clear();relay_view=RV_STATUS;latency_ui.details=0;(void)latency_fetch_targets();snprintf(notice,sizeof(notice),"%s",L("Could not save name.","无法保存名称，请重试。"));dialog=3;return 1;}
      snprintf(latency_ui.names[latency_ui.slot],sizeof(latency_ui.names[0]),"%s",latency_ui.edit);
      relay_editor_clear();relay_view=RV_STATUS;return 1;
    }
    if(!strcmp(a,"rkdone")) {
      char req[100],reply[64];
      if(latency_ui.edit[0]&&!latency_wire_target(latency_ui.edit)) {
        snprintf(notice,sizeof(notice),"%s",L("Enter a hostname or IP address.","请输入域名或 IP 地址。"));dialog=3;return 1;
      }
      int was_empty=!latency_ui.targets[latency_ui.slot][0],last=latency_last_target();
      snprintf(req,sizeof(req),"latency %s %d %s\n",latency_ui.slots==LATENCY_SLOTS?"set3":"set",latency_ui.slot,latency_ui.edit[0]?latency_ui.edit:"-");
      if(exchange(req,reply,sizeof(reply))||strcmp(reply,"OK\n")) {
        relay_editor_clear();relay_view=RV_STATUS;(void)latency_fetch_targets();
        snprintf(notice,sizeof(notice),"%s",L("Could not confirm save. Reload and retry.","无法确认保存，请重新加载后重试。"));dialog=3;return 1;
      }
      if(strcmp(latency_ui.before,latency_ui.edit))latency_ui.names[latency_ui.slot][0]=0;
      snprintf(latency_ui.targets[latency_ui.slot],sizeof(latency_ui.targets[0]),"%s",latency_ui.edit);
      if(was_empty&&latency_ui.edit[0]&&latency_ui.slot<last&&latency_ui.slots==LATENCY_SLOTS) {
        snprintf(req,sizeof(req),"latency move %d %d\n",latency_ui.slot,last);
        if(exchange(req,reply,sizeof(reply))||strcmp(reply,"OK\n")) {
          relay_editor_clear();relay_view=RV_STATUS;(void)latency_fetch_targets();
          snprintf(notice,sizeof(notice),"%s",L("Saved; check the target order.","已保存，请检查目标顺序。"));dialog=3;return 1;
        }
        (void)latency_fetch_targets();
      }
      latency_clamp_page();
      memset(latency_ui.results,0,sizeof(latency_ui.results));latency_ui.result_error=0;
      relay_editor_clear();relay_view=RV_STATUS;latency_ui.details=0;return 1;
    }
    if(!strcmp(a,"rkcancel")||!strcmp(a,"back")) {relay_keyboard_tap(a);relay_editor_clear();relay_view=RV_STATUS;return 1;}
    return relay_keyboard_tap(a);
  }
  if(latency_ui.details) {
    if(!strcmp(a,"back")){latency_ui.details=0;return 1;}
    if(!strcmp(a,"latname")||!strcmp(a,"lataddress")) {
      int name=!strcmp(a,"latname");
      snprintf(latency_ui.edit,sizeof(latency_ui.edit),"%s",name?latency_ui.names[latency_ui.slot]:latency_ui.targets[latency_ui.slot]);
      relay_editor_clear();relay_edit.active=1;relay_key_open(name?RK_LATENCY_NAME:RK_LATENCY);return 1;
    }
    return 1;
  }
  if(!strcmp(a,"latreload")){(void)latency_fetch_targets();return 1;}
  if(!strcmp(a,"latprev")||!strcmp(a,"latnext")) {latency_scroll_to(latency_ui.scroll+(!strcmp(a,"latprev")?-LATENCY_LIST_HEIGHT:LATENCY_LIST_HEIGHT));return 1;}
  if(!strcmp(a,"latsort")) {if(blocked()||latency_ui.slots!=LATENCY_SLOTS)return 1;latency_ui.sorted=!latency_ui.sorted;latency_ui.moving=-1;return 1;}
  if(a[0]=='l'&&a[1]=='t'&&a[2]>='0'&&a[2]<='0'+LATENCY_PAGE_SIZE&&!a[3]) {
    if(!latency_ui.loaded||blocked())return 1;
    latency_ui.slot=latency_visible_slot(latency_ui.scroll/LATENCY_ROW_STEP+a[2]-'0');if(latency_ui.slot<0)return 1;
    if(latency_ui.sorted) {
      if(latency_ui.moving<0) {if(latency_ui.targets[latency_ui.slot][0])latency_ui.moving=latency_ui.slot;return 1;}
      if(latency_ui.moving==latency_ui.slot) {latency_ui.moving=-1;return 1;}
      int dest=latency_ui.targets[latency_ui.slot][0]?latency_ui.slot:latency_last_target();
      char req[64],reply[64];snprintf(req,sizeof(req),"latency move %d %d\n",latency_ui.moving,dest);
      if(exchange(req,reply,sizeof(reply))||strcmp(reply,"OK\n")) {latency_ui.moving=-1;(void)latency_fetch_targets();snprintf(notice,sizeof(notice),"%s",L("Could not confirm order. Retry.","无法确认排序，请重试。"));dialog=3;return 1;}
      latency_ui.moving=-1;(void)latency_fetch_targets();return 1;
    }
    if(latency_ui.name_support&&latency_ui.targets[latency_ui.slot][0]) {latency_ui.details=1;return 1;}
    snprintf(latency_ui.edit,sizeof(latency_ui.edit),"%s",latency_ui.targets[latency_ui.slot]);
    relay_editor_clear();relay_edit.active=1;relay_key_open(RK_LATENCY);return 1;
  }
  if(!strcmp(a,"latrun")) {
    if(!latency_ui.loaded||blocked())return 1;
    char req[2048]="latency run";int count=0;
    for(int pos=0;pos<latency_target_count();pos++){int i=latency_visible_slot(pos);if(i>=0&&latency_ui.targets[i][0]) {
      size_t used=strlen(req),len=strlen(latency_ui.targets[i]);
      if(used+len+2>=sizeof(req))return 1;
      req[used]=' ';memcpy(req+used+1,latency_ui.targets[i],len+1);count++;
    }}
    if(count) {strcat(req,"\n");memset(latency_ui.results,0,sizeof(latency_ui.results));latency_ui.result_error=0;send_job(req,JOB_LATENCY,0);}
    return 1;
  }
  return 0;
}
