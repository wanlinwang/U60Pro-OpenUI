/* Six compact editable targets; measurements are from the router itself. */
static void render_latency(void) {
  if(relay_view==RV_KEYBOARD) { render_relay_keyboard(); return; }
  if(latency_ui.details) {
    header(L("Edit target","编辑目标"));
    nav_row(12,74,296,64,"latname",L("Name","名称"),latency_ui.names[latency_ui.slot][0]?latency_ui.names[latency_ui.slot]:L("Tap to set","点击设置"),"",1);
    nav_row(12,150,296,64,"lataddress",L("Address","目标地址"),latency_ui.targets[latency_ui.slot],"",1);
    return;
  }
  header(L("Latency","测延时"));
  int start=latency_ui.scroll/LATENCY_ROW_STEP,shift=latency_ui.scroll%LATENCY_ROW_STEP;
  add("<div class=\"box\" style=\"left:0px;top:%dpx;width:320px;height:%dpx;overflow:hidden\">",LATENCY_LIST_TOP,LATENCY_LIST_HEIGHT);
  for(int row=0;row<=LATENCY_PAGE_SIZE;row++) {
    int pos=start+row,i=latency_visible_slot(pos);if(!latency_ui.loaded||i<0)continue;
    char id[12],title[160],detail[48]="";
    snprintf(id,sizeof(id),"lt%d",row);
    const char *target=latency_ui.targets[i];
    const char *shown=!strncmp(target,"https://",8)?target+8:target;
    if(*shown&&latency_ui.names[i][0])snprintf(title,sizeof(title),"%s%d. %s %s",latency_ui.sorted&&latency_ui.moving==i?"> ":"",pos+1,latency_ui.names[i],shown);
    else if(*shown)snprintf(title,sizeof(title),"%s%d. %s",latency_ui.sorted&&latency_ui.moving==i?"> ":"",pos+1,shown);
    else snprintf(title,sizeof(title),"%s",L("Add target","添加目标"));
    const char *result=latency_ui.results[i],*kind=!strncmp(target,"https://",8)?"HTTPS":"Ping";
    if(pending_kind==JOB_LATENCY&&*target)snprintf(detail,sizeof(detail),"%s",L("Testing...","测试中..."));
    else if(!strcmp(result,"timeout"))snprintf(detail,sizeof(detail),"%s %s",kind,L("No reply","无响应"));
    else if(!strcmp(result,"error"))snprintf(detail,sizeof(detail),"%s %s",kind,L("Unavailable","不可用"));
    else if(*result)snprintf(detail,sizeof(detail),"%s%s%s",!strncmp(result,"TS ",3)?"":kind,!strncmp(result,"TS ",3)?"":" ",result);
    int result_width=*detail?html_view_text_width_px(detail,16):0;
    if(result_width>170)result_width=170;
    int title_width=270-(result_width?result_width+8:0);
    int used=html_view_text_width_px(title,16);if(used>title_width)used=title_width;
    open_row(12,row*LATENCY_ROW_STEP-shift,296,48,id,latency_ui.loaded&&i<latency_ui.slots&&!blocked(),"");
    add("<div class=\"box head\" style=\"left:10px;top:13px;width:%dpx;height:20px\">",title_width);shortened(title,title_width,16);add("</div>");
    if(result_width) {add("<div class=\"box muted\" style=\"left:%dpx;top:13px;width:%dpx;height:20px\">",18+used,result_width);shortened(detail,result_width,16);add("</div>");}
    add("</a>");
  }
  add("</div>");
  int count=latency_target_count();
  if(!latency_ui.loaded)linkbox(12,372,296,48,"latreload",L("Retry loading","重试加载"),!blocked(),PLAIN);
  else if(latency_ui.sorted)box(12,372,296,48,"muted",latency_ui.moving<0?L("Choose a target","选择要移动的目标"):L("Choose destination","选择目标位置"));
  else linkbox(12,372,296,48,"latrun",pending_kind==JOB_LATENCY?L("Testing...","测试中..."):L("Test all","全部测试"),count&&!blocked(),PRIMARY);
  linkbox(12,425,75,48,"latprev",L("Up","向上"),latency_ui.loaded&&latency_ui.scroll>0,PLAIN);
  char page_label[24];snprintf(page_label,sizeof(page_label),"%d/%d",count?start+1:0,count);
  box(91,435,43,22,"muted",page_label);
  linkbox(140,425,75,48,"latnext",L("Down","向下"),latency_ui.loaded&&latency_ui.scroll<latency_max_scroll(),PLAIN);
  linkbox(220,425,88,48,"latsort",latency_ui.sorted?L("Done","完成"):L("Sort","排序"),latency_ui.loaded&&latency_ui.slots==LATENCY_SLOTS&&!blocked(),PLAIN);
  if(latency_ui.result_error)box(16,348,288,20,"bad",L("Results unavailable. Retry.","结果读取失败，请重试。"));
}
