/* SCREEN_PREVIEW only. Overview USB LAN switch: synthetic H records + captured IPC. */
static void usblan_fixture(const char *lan) {
  char text[1536];unsigned long long now=(unsigned long long)time(NULL);
  snprintf(text,sizeof(text),"S 7 1 0 0 0 0 0 0 1 %llu\nO 83 320 18372 34.5 12.4\nA\t1\t0\t%llu\t0\t1\t1\t1\t1\t0\t1\t1\n"
    "H\t-65\t-88\t-10\t19\t5G SA\tN78\tWL_AND_5G\tB1,B3\tN78,N41\t502\t256000\t42000\t12\t35\t34200\t2\t1\t1\t5\tU60Pro_5G\trndis\t0\t64\t1\t1\t80\t80\t70\t4120\t450\tGood%s\n"
    "J 0 0 0 0\n.\n",now,now,lan);
  assert(parse_snapshot(text,&data));
  reset_job();page=OVERVIEW;
}
static int overview_contains(const char *start,const char *end,const char *text) {
  const char *found=strstr(start,text);
  return found&&found<end;
}
static void usblan_tests(struct drm_buf *b,const char *dir) {
  /* An older agent's 32-field H: still parsed, USB LAN unknown. */
  usblan_fixture("");assert(data.toolbox.has&&!data.toolbox.has_usb_lan);
  capture_both(b,dir,"usblan-unknown");press_id(b,"ulan");assert(!test_request[0]);
  usblan_fixture("\t1\t2\trndis0");
  assert(data.toolbox.has_usb_lan&&data.toolbox.usb_lan_on&&data.toolbox.usb_lan_state==2&&!strcmp(data.toolbox.usb_lan_iface,"rndis0"));
  capture_both(b,dir,"usblan-overview-on");
  usblan_fixture("\t1\t1\trndis0");capture_both(b,dir,"usblan-waiting");
  render(b);assert(strstr(html,"waiting for USB host"));
  usblan_fixture("\t1\t2\trndis0");
  /* Between the stats card and the first admin row, a full 48px target. */
  int x,y,w,h,ax,ay,aw,ah;
  render(b);
  assert(html_view_rect("#ulan",&x,&y,&w,&h)&&html_view_rect("#admin0",&ax,&ay,&aw,&ah));
  if(!(h>=48&&w>=48&&y>=208&&y+h<=ay))fprintf(stderr,"usblan geometry: x=%d y=%d w=%d h=%d admin0_y=%d\n",x,y,w,h,ay);
  assert(h>=48&&w>=48&&y>=208&&y+h<=ay);
  press_id(b,"ulan");expect_request("tool usb_lan 0\n");assert(pending_kind==JOB_TOOL);
  /* Pending: a second tap sends nothing new. */
  test_request[0]=0;press_id(b,"ulan");assert(!test_request[0]);
  usblan_fixture("\t0\t0\trndis0");capture_both(b,dir,"usblan-overview-off");
  press_id(b,"ulan");expect_request("tool usb_lan 1\n");
  usblan_fixture("\t1\t0\t-");capture_both(b,dir,"usblan-no-adapter");
  render(b);assert(strstr(html,"no USB adapter"));settings_zh=1;render(b);assert(strstr(html,"未检测到 USB 网卡"));settings_zh=0;
  usblan_fixture("\t1\t0\tecm0");render(b);assert(strstr(html,"ecm0"));
  /* Stale status: a label, never a switch. */
  usblan_fixture("\t1\t1\trndis0");data.stamp-=11;press_id(b,"ulan");assert(!test_request[0]);
  usblan_fixture("\t1\t1\trndis0");data.busy=1;press_id(b,"ulan");assert(!test_request[0]);
  usblan_fixture("\t1\t1\trndis0");
  /* The menu has no carrier card; signal bars remain (-88 dBm is three bars).
   * Select only Overview so the Signal tile cannot itself display No SIM. */
  int saved_menu=settings_menu,saved_order_count=settings_order_count,saved_zh=settings_zh,saved_order[18];
  memcpy(saved_order,settings_order,sizeof(saved_order));settings_order_count=0;
  settings_menu=1<<OVERVIEW;
  usblan_fixture("\t1\t2\trndis0\t100\t100\t19\t18\tT-Mobile");page=MENU;
  assert(!strcmp(data.toolbox.carrier,"T-Mobile"));
  render(b);assert(!strstr(html,"T-Mobile")&&!strstr(html,"dBm")&&!strstr(html,"5G SA"));
  assert(!strstr(html,"left:10px;top:64px;width:300px;height:84px"));
  assert(strstr(html,"left:212px;top:13px;width:5px;height:14px;border-radius:1px;background:#ffffff")&&strstr(html,"left:219px;top:11px;width:3px;height:16px;border-radius:1px;background:#3a414b"));
  capture_both(b,dir,"menu-carrier");
  /* An older H39 or missing carrier still parses without restoring the card. */
  usblan_fixture("\t1\t2\trndis0\t100\t100\t19\t18");page=MENU;render(b);
  assert(!data.toolbox.carrier[0]&&!strstr(html,"5G SA")&&!strstr(html,"dBm"));
  usblan_fixture("\t1\t2\trndis0\t100\t100\t19\t18\t-");page=MENU;render(b);assert(!data.toolbox.carrier[0]&&!strstr(html,"5G SA"));
  assert(data.toolbox.sim==-1);
  /* No SIM: `-` figures and sim 0, so nothing shows the emergency-camping level. */
  {
    char text[1536];unsigned long long now=(unsigned long long)time(NULL);
    snprintf(text,sizeof(text),"S 7 1 0 0 0 0 0 0 1 %llu\nO 83 320 18372 34.5 12.4\nA\t1\t0\t%llu\t0\t1\t1\t1\t1\t0\t1\t1\n"
      "H\t-\t-\t-\t-\tLIMITED_SERVICE_SA\t-\tWL_AND_5G\t-\t-\t-\t0\t0\t12\t35\t34200\t2\t1\t1\t5\tU60Pro_5G\trndis\t0\t64\t1\t1\t80\t80\t70\t4120\t450\tGood\t1\t2\trndis0\t100\t100\t19\t18\t-\t0\n"
      "J 0 0 0 0\n.\n",now,now);
    assert(parse_snapshot(text,&data));reset_job();
  }
  assert(data.toolbox.sim==0&&data.toolbox.rsrp==NO_FIGURE&&data.toolbox.sinr==NO_FIGURE&&data.toolbox.rsrq==NO_FIGURE);
  page=MENU;render(b);assert(!strstr(html,"No SIM")&&!strstr(html,"dBm")&&!strstr(html,"LIMITED"));
  assert(strstr(html,"left:198px;top:21px;width:5px;height:6px;border-radius:1px;background:#3a414b"));
  capture_both(b,dir,"menu-no-sim");
  page=SIGNAL_PAGE;render(b);assert(strstr(html,"No SIM")&&!strstr(html," dBm")&&!strstr(html,"LIMITED"));
  capture_both(b,dir,"signal-no-sim");

  /* Live rates and CPU remain within Overview for both row and grid layouts. */
  usblan_fixture("\t1\t1\trndis0");
  data.toolbox.kbps_down=125;data.toolbox.kbps_up=2500;data.cpu=12.4;
  settings_menu=1<<OVERVIEW;settings_order_count=0;page=MENU;
  for(int lang=0;lang<=1;lang++) {
    settings_zh=lang;render(b);
    const char *a=strstr(html,"id=\"overview\""),*end=a?strstr(a,"</a>"):NULL;
    assert(a&&end&&overview_contains(a,end,"CPU12%")&&overview_contains(a,end,"↓125K")&&overview_contains(a,end,"↑2.5M"));
    int x,y,w,h;assert(html_view_rect("#overview",&x,&y,&w,&h));
    assert(y>=52&&w>=48&&h>=48&&x+w<=W&&y+h<=H);
    press_id(b,"overview");assert(page==OVERVIEW);page=MENU;
  }
  capture_both(b,dir,"menu-overview-rates-row");
  settings_menu=(1<<OVERVIEW)|(1<<WG)|(1<<PROXY)|(1<<CLASH)|(1<<SIGNAL_PAGE);
  settings_order_count=5;settings_order[0]=WG;settings_order[1]=PROXY;
  settings_order[2]=SIGNAL_PAGE;settings_order[3]=OVERVIEW;settings_order[4]=CLASH;
  for(int lang=0;lang<=1;lang++) {
    settings_zh=lang;render(b);
    const char *wg=strstr(html,"id=\"wireguard\""),*a=strstr(html,"id=\"overview\""),*end=a?strstr(a,"</a>"):NULL;
    const char *clash=strstr(html,"id=\"clash\"");
    assert(wg&&a&&end&&clash&&wg<a&&a<clash);
    assert(overview_contains(a,end,"CPU12%")&&overview_contains(a,end,"↓125K")&&overview_contains(a,end,"↑2.5M"));
    int x,y,w,h;assert(html_view_rect("#overview",&x,&y,&w,&h));
    assert(y>=52&&w>=48&&h>=48&&x+w<=W&&y+h<=H);
    press_id(b,"overview");assert(page==OVERVIEW);page=MENU;
  }
  capture_both(b,dir,"menu-overview-rates-grid");
  /* Actual rendered text width must fit: shortening would hide the rate. */
  {
    const unsigned long long rates[]={0,125,999,1000,9999,10000,999999,1000000,9999999,
      10000000,999999999,1000000000,9999999999ULL,~0ULL};
    char compact[24],label[32];
    for(size_t i=0;i<sizeof(rates)/sizeof(rates[0]);i++) {
      menu_rate(compact,sizeof(compact),rates[i]);
      for(int direction=0;direction<2;direction++) {
        snprintf(label,sizeof(label),"%s%s",direction?"↑":"↓",compact);
        int width=html_view_text_width_px(label,12);
        if(width>39)fprintf(stderr,"overview rate too wide: %s (%d px)\n",label,width);
        assert(width<=39);
      }
    }
    assert(html_view_text_width_px("CPU100%",10)<=38);
  }
  data.toolbox.has=0;data.stamp=0;render(b);
  {const char *a=strstr(html,"id=\"overview\""),*end=a?strstr(a,"</a>"):NULL;
   assert(a&&end&&overview_contains(a,end,"CPU--")&&overview_contains(a,end,"↓--")&&overview_contains(a,end,"↑--"));}
  capture_both(b,dir,"menu-overview-rates-unknown");
  usblan_fixture("\t1\t1\trndis0");
  settings_menu=saved_menu;settings_order_count=saved_order_count;settings_zh=saved_zh;memcpy(settings_order,saved_order,sizeof(saved_order));
  puts("PASS USB LAN H35 parsing (old H32 unknown), Overview 48px row above admin rows (zh/en), exact on/off commands, pending/stale/busy guards; menu signal bars without carrier card, no-SIM H41 without figures");
}
