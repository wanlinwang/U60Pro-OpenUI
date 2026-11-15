/* Synthetic agent records and real touchscreen hit tests. */
static void latency_tests(struct drm_buf *b,const char *dir) {
  char snapshot[512];
  snprintf(snapshot,sizeof(snapshot),"S 7 1 0 0 0 0 0 0 1 %llu\nC 7 120 128 2 16 zh\nv 7 120 128 2 %d zh 1 480\nn 7 2 4 25\n",(unsigned long long)time(NULL),(1<<OVERVIEW)|(1<<LATENCY_PAGE));
  struct snapshot parsed;assert(parse_snapshot(snapshot,&parsed));
  assert(parsed.native_menu_record&&parsed.menu_order_count==2&&parsed.menu_order[1]==LATENCY_PAGE);
  settings_menu=(1<<LATENCY_PAGE);settings_native_menu=1;settings_order_count=1;settings_order[0]=LATENCY_PAGE;
  settings_zh=1;page=MENU;dialog=0;reset_job();data.busy=0;data.stamp=(unsigned long long)time(NULL);
  stub_latency_targets2="TARGETS 6\n0 https://google.com\n1 -\n2 100.64.0.1\n3 -\n4 very-long-subdomain.example.com\n5 -\n.\n";
  stub_latency_set="OK\n";stub_job="OK 1\n";
  press_id(b,"latency");expect_request("latency targets2\n");assert(page==LATENCY_PAGE&&latency_ui.loaded&&latency_ui.slots==6);
  assert(latency_visible_count()==4&&latency_visible_slot(1)==2&&latency_visible_slot(3)==5);
  render(b);char *add=strstr(html,"添加目标");assert(add&&!strstr(add+strlen("添加目标"),"添加目标"));assert(!strstr(html,"act:lt4"));
  capture_both(b,dir,"latency-single-add");
  press_id(b,"lt0");relay_press(b,"rclear");relay_press(b,"rkcancel");assert(!strcmp(latency_ui.targets[0],"https://google.com"));
  test_log[0]=0;press_id(b,"lt3");relay_type(b,"bad;value");relay_press(b,"rkdone");assert(dialog==3&&!strstr(test_log,"latency set"));press_id(b,"refresh");
  press_id(b,"lt3");relay_type(b,"example.net");relay_press(b,"rkdone");expect_request("latency set 5 example.net\n");
  assert(latency_visible_count()==5&&latency_visible_slot(4)==1);
  press_id(b,"latrun");expect_request("latency run https://google.com 100.64.0.1 very-long-subdomain.example.com example.net\n");
  stub_latency_results="LATENCY 4\nhttps://google.com 837.2\n100.64.0.1 tsd:82.0\nvery-long-subdomain.example.com 21.0\nexample.net 13.0\n.\n";
  data.job=data.done=data.fresh=1;data.has_fresh=1;data.result=0;data.busy=0;settle_jobs();
  assert(!pending&&!strcmp(latency_ui.results[0],"837.2 ms")&&!strcmp(latency_ui.results[2],"TS 82.0ms 直连"));
  capture_both(b,dir,"latency-results");assert(!strstr(html,"very-long-subdomain.example.com")&&strstr(html,"..."));
  stub_latency_results="LATENCY 1\nwrong.example 2.1\n.\n";latency_fetch_results();assert(latency_ui.result_error&&!latency_ui.results[0][0]);
  stub_latency_targets2="ERR Unsupported action\n";stub_latency_targets="TARGETS 4\n0 https://google.com\n1 -\n2 100.64.0.1\n3 -\n.\n";
  assert(latency_fetch_targets()&&latency_ui.slots==4&&latency_visible_count()==3);
  stub_latency_targets2="ERR Cannot read targets\n";assert(!latency_fetch_targets()&&!latency_ui.loaded);
  stub_latency_targets2="ERR Unsupported action\n";stub_latency_targets="ERR unsupported\n";assert(!latency_fetch_targets()&&!latency_ui.loaded);
  render(b);assert(strstr(html,"latreload")&&!strstr(html,"act:latrun"));
  stub_latency_targets3="TARGETS 24\n0 first.example\n1 second.example\n2 -\n3 three.example\n4 four.example\n5 five.example\n6 six.example\n7 seven.example\n8 -\n9 -\n10 -\n11 -\n12 -\n13 -\n14 -\n15 -\n16 -\n17 -\n18 -\n19 -\n20 -\n21 -\n22 -\n23 -\n.\n";
  assert(latency_fetch_targets()&&latency_ui.slots==24&&latency_visible_count()==8&&latency_max_scroll()==102);
  latency_scroll_to(25);capture_both(b,dir,"latency-scroll-partial");
  assert(!strcmp(html_view_click(100,60),"act:lt0"));assert(!strcmp(html_view_click(100,90),"act:lt1"));
  assert(strcmp(html_view_click(100,45),"act:lt0"));assert(strncmp(html_view_click(100,365),"act:lt",6));
  latency_scroll_to(0);
  press_id(b,"latnext");assert(latency_ui.scroll==102&&latency_visible_slot(6)==7&&latency_visible_slot(7)==8);
  render(b);add=strstr(html,"添加目标");assert(add&&!strstr(add+strlen("添加目标"),"添加目标"));
  press_id(b,"lt5");relay_type(b,"eight.example");relay_press(b,"rkdone");expect_request("latency set3 8 eight.example\n");
  press_id(b,"latrun");expect_request("latency run first.example second.example three.example four.example five.example six.example seven.example eight.example\n");
  stub_latency_results="LATENCY 8\nfirst.example 1.0\nsecond.example 2.0\nthree.example 3.0\nfour.example 4.0\nfive.example 5.0\nsix.example 6.0\nseven.example 20.0\neight.example 30.0\n.\n";
  data.job=data.done=data.fresh=1;data.has_fresh=1;data.result=0;data.busy=0;settle_jobs();assert(!strcmp(latency_ui.results[7],"20.0 ms")&&!strcmp(latency_ui.results[0],"1.0 ms"));
  press_id(b,"latsort");press_id(b,"lt4");assert(latency_ui.moving==7);press_id(b,"latprev");
  test_log[0]=0;press_id(b,"lt0");assert(strstr(test_log,"latency move 7 0\n"));assert(latency_ui.moving<0);press_id(b,"latsort");
  /* A full last slot with earlier gaps appends a new entry after the last target. */
  memset(latency_ui.targets,0,sizeof(latency_ui.targets));snprintf(latency_ui.targets[0],65,"first.example");snprintf(latency_ui.targets[23],65,"last.example");latency_ui.scroll=0;
  assert(latency_visible_slot(2)==1);press_id(b,"lt2");relay_type(b,"new.example");test_log[0]=0;relay_press(b,"rkdone");
  assert(strstr(test_log,"latency set3 1 new.example\n")&&strstr(test_log,"latency move 1 23\n"));
  /* Deleting a target hides its hole and leaves one add row. */
  memset(latency_ui.targets,0,sizeof(latency_ui.targets));snprintf(latency_ui.targets[0],65,"first.example");snprintf(latency_ui.targets[8],65,"last.example");
  press_id(b,"lt0");relay_press(b,"rclear");relay_press(b,"rkdone");assert(latency_target_count()==1&&latency_visible_slot(0)==8&&latency_visible_count()==2);
  memset(latency_ui.targets,0,sizeof(latency_ui.targets));assert(latency_visible_count()==1&&latency_visible_slot(0)==0&&latency_max_scroll()==0);
  for(int i=0;i<6;i++)snprintf(latency_ui.targets[i],65,"host%d.example",i);
  assert(latency_visible_count()==7&&latency_max_scroll()==51&&latency_visible_slot(6)==6);
  for(int i=0;i<LATENCY_SLOTS;i++)snprintf(latency_ui.targets[i],65,"host%d.example",i);
  assert(latency_visible_count()==24&&latency_max_scroll()==18*51&&latency_visible_slot(23)==23&&latency_visible_slot(24)<0);
  latency_scroll_to(latency_max_scroll());render(b);assert(!strstr(html,"添加目标"));
  char all_results[1024]="LATENCY 24\n";
  for(int i=0;i<LATENCY_SLOTS;i++){char line[48];snprintf(line,sizeof(line),"host%d.example %d.0\n",i,i);strcat(all_results,line);}
  strcat(all_results,".\n");stub_latency_results=all_results;
  press_id(b,"latrun");assert(strstr(test_request,"host0.example")&&strstr(test_request,"host23.example"));
  data.job=data.done=data.fresh=1;data.has_fresh=1;data.result=0;data.busy=0;settle_jobs();
  assert(!strcmp(latency_ui.results[0],"0.0 ms")&&!strcmp(latency_ui.results[23],"23.0 ms"));
  /* Names are optional and old agents keep working; the address is never replaced. */
  stub_latency_names="NAMES 24\n0 first.example e5aeb6\n1 second.example -\n2 - -\n3 three.example -\n4 four.example -\n5 five.example -\n6 six.example -\n7 seven.example -\n8 - -\n9 - -\n10 - -\n11 - -\n12 - -\n13 - -\n14 - -\n15 - -\n16 - -\n17 - -\n18 - -\n19 - -\n20 - -\n21 - -\n22 - -\n23 - -\n.\n";
  assert(latency_fetch_targets()&&latency_ui.name_support&&!strcmp(latency_ui.names[0],"家"));
  latency_ui.scroll=0;press_id(b,"lt0");assert(latency_ui.details);press_id(b,"latname");
  ime_press(b,"rclear");ime_press(b,"rime");ime_press(b,"rk6e");ime_press(b,"rk69");ime_press(b,"rpick0");
  assert(!strcmp(latency_ui.edit,"你"));ime_press(b,"rkdone");
  assert(strstr(test_log,"latency name 0 first.example e4bda0\n")&&!strcmp(latency_ui.names[0],"你"));
  press_id(b,"back");assert(!latency_ui.details);render(b);assert(strstr(html,"你"));
  stub_latency_names=NULL;
  stub_latency_targets=stub_latency_targets2=stub_latency_targets3=stub_latency_results=stub_latency_set=NULL;reset_job();relay_editor_clear();relay_view=RV_STATUS;
  puts("PASS latency single-add, gaps, pagination, cross-page sorting, tests and old-agent fallback");
}
