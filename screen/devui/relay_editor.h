/* Relay editor: local-only state; no credential appears in status or action IDs.
 * Passwords are never read back from the agent. GPL-3.0-or-later. */
enum { RV_STATUS, RV_SAVED, RV_SCAN, RV_DETAIL, RV_EDIT, RV_KEYBOARD };
enum { RK_SSID, RK_PASSWORD, RK_SMS_TO, RK_SMS_BODY, RK_LATENCY, RK_LATENCY_NAME };
struct relay_editor {
  char ssid[33], password[65], before[65], pinyin[9];
  int chinese, candidate_page;
  unsigned long long rev, gen, id;
  int active, security, hidden, saved, keep, key, keymode, show, overflow, from_scan, scan_index, original_security, has_secret;
};
static struct relay_editor relay_edit;
static int relay_view, relay_offset;
/* Non-secret rendering versions: never retain a second copy of the input. */
static unsigned long long relay_key_revision, relay_key_epoch;
/* Volatile writes cannot be optimized away. Also clear abandoned bytes after backspace. */
static void relay_wipe(void *p, size_t n) {
  volatile unsigned char *v = p;
  while (n--) *v++ = 0;
}
static void relay_editor_clear(void) { relay_wipe(&relay_edit,sizeof(relay_edit));relay_wipe(confirm_req,sizeof(confirm_req));relay_key_revision++;relay_key_epoch++; }
static char *relay_input(void) { return (relay_edit.key==RK_LATENCY||relay_edit.key==RK_LATENCY_NAME)?latency_ui.edit:relay_edit.key==RK_SMS_TO?sms_ui.number:relay_edit.key==RK_SMS_BODY?sms_ui.message:relay_edit.key==RK_SSID?relay_edit.ssid:relay_edit.password; }
static size_t relay_input_limit(void) { return relay_edit.key==RK_SMS_BODY?160:relay_edit.key==RK_LATENCY_NAME?72:(relay_edit.key==RK_PASSWORD||relay_edit.key==RK_LATENCY)?64:32; }
static void relay_key_open(int field) {
  relay_key_revision++;relay_key_epoch++;
  relay_edit.chinese=0;relay_edit.pinyin[0]=0;relay_edit.candidate_page=0;
  relay_edit.key=field;relay_edit.keymode=field==RK_SMS_TO?2:0;relay_edit.show=0;relay_edit.overflow=0;
  relay_wipe(relay_edit.before,sizeof(relay_edit.before));
  if(field==RK_SSID)snprintf(relay_edit.before,sizeof(relay_edit.before),"%s",relay_input());
  if(field==RK_LATENCY||field==RK_LATENCY_NAME)snprintf(latency_ui.before,sizeof(latency_ui.before),"%s",relay_input());
  if(field==RK_SMS_TO||field==RK_SMS_BODY)snprintf(sms_ui.before,sizeof(sms_ui.before),"%s",relay_input());
  relay_view=RV_KEYBOARD;
}
#include "pinyin_data.h"
/* Offline single-syllable Pinyin, available only for messages and target names. */
static int relay_ime_allowed(void) { return relay_edit.key==RK_SMS_BODY || relay_edit.key==RK_LATENCY_NAME; }
static const char *relay_candidates(void) {
  for(size_t i=0;i<sizeof(pinyin_data)/sizeof(pinyin_data[0]);i++)
    if(!strcmp(relay_edit.pinyin,pinyin_data[i].syllable))return pinyin_data[i].characters;
  return "";
}
static void relay_ime_changed(void) { relay_key_revision++;relay_key_epoch++;need_draw=1; }
static int relay_append(const char *s,size_t n) {
  char *out=relay_input();size_t used=strlen(out);
  if(used+n>relay_input_limit()){relay_edit.overflow=1;relay_ime_changed();return 0;}
  memcpy(out+used,s,n);out[used+n]=0;relay_edit.overflow=0;relay_key_revision++;return 1;
}
static int relay_ime_raw(void) {
  size_t n=strlen(relay_edit.pinyin);
  if(!n)return 1;
  if(!relay_append(relay_edit.pinyin,n))return 0;
  relay_edit.pinyin[0]=0;relay_edit.candidate_page=0;relay_ime_changed();return 1;
}
static int relay_ime_select(int number) {
  const unsigned char *s=(const unsigned char*)relay_candidates();
  int skip=relay_edit.candidate_page*4+number;
  while(*s&&skip-- > 0) {s++;while((*s&0xc0)==0x80)s++;}
  if(!*s)return 0;
  const unsigned char *end=s+1;while((*end&0xc0)==0x80)end++;
  if(!relay_append((const char*)s,(size_t)(end-s)))return 0;
  relay_edit.pinyin[0]=0;relay_edit.candidate_page=0;relay_ime_changed();return 1;
}
static int relay_candidate_count(void) {
  const unsigned char *s=(const unsigned char*)relay_candidates();int n=0;
  for(;*s;s++)if((*s&0xc0)!=0x80)n++;return n;
}
/* Wi-Fi fields remain ASCII-only; chosen UTF-8 SSIDs are preserved. */
static int relay_ascii(const char *s) {
  for(;*s;s++)if((unsigned char)*s<32||(unsigned char)*s>126)return 0;
  return 1;
}
static void relay_key_char(unsigned c) {
  char *s=relay_input();size_t n=strlen(s);
  if(c<32||c>126)return;
  if(relay_ime_allowed()&&relay_edit.chinese&&relay_edit.keymode==0&&c>='a'&&c<='z') {
    size_t n=strlen(relay_edit.pinyin);if(n<8){relay_edit.pinyin[n]=(char)c;relay_edit.pinyin[n+1]=0;relay_edit.candidate_page=0;relay_ime_changed();}return;
  }
  if(relay_edit.pinyin[0]&&!relay_ime_raw())return;
  if(relay_edit.key==RK_SMS_TO&&!((c>='0'&&c<='9')||c=='+'||c=='*'||c=='#'))return;
  relay_key_revision++;
  if(n>=relay_input_limit()){relay_edit.overflow=1;return;}
  s[n]=(char)c;s[n+1]=0;relay_edit.overflow=0;
  if(relay_edit.key==RK_PASSWORD)relay_edit.keep=0;
}
static void relay_key_backspace(void) {
  relay_key_revision++;
  if(relay_edit.pinyin[0]) {size_t n=strlen(relay_edit.pinyin);relay_edit.pinyin[n-1]=0;relay_edit.candidate_page=0;relay_ime_changed();return;}
  char *s=relay_input();size_t n=strlen(s);
  if(n) {do {n--;} while(n&&((unsigned char)s[n]&0xc0)==0x80);s[n]=0;}
  relay_edit.overflow=0;
  if(relay_edit.key==RK_PASSWORD)relay_edit.keep=0;
}
static const char *relay_key_row(int row) {
  static const char *const keys[4][3]={
    {"qwertyuiop","asdfghjkl","zxcvbnm"},
    {"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"},
    {"1234567890","!\"#$%&'()*","+,-./:;<=>"},
    {"?@[\\]^_`{|","}~","1234567890"}
  };
  return keys[relay_edit.keymode][row];
}
static int relay_password_valid(void) {
  size_t n=strlen(relay_edit.password);
  if(!relay_edit.security)return !n;
  if(relay_edit.keep&&relay_edit.saved&&relay_edit.has_secret&&relay_edit.security==relay_edit.original_security)return 1;
  if(n>=8&&n<=63)return 1;
  if(n!=64||(relay_edit.security!=1&&relay_edit.security!=3))return 0;
  for(size_t i=0;i<n;i++)if(!strchr("0123456789abcdefABCDEF",relay_edit.password[i]))return 0;
  return 1;
}
static int relay_keyboard_tap(const char *a) {
  if(relay_view!=RV_KEYBOARD)return 0;
  if(!strncmp(a,"rk",2)&&strlen(a)==4) {
    unsigned c=0;
    for(int i=2;i<4;i++) {
      const char *p=strchr("0123456789abcdef",a[i]);if(!p)return 1;
      c=c*16+(unsigned)(p-"0123456789abcdef");
    }
    relay_key_char(c);
  } else if(!strcmp(a,"rime")&&relay_ime_allowed()) {
    if(!relay_ime_raw())return 1;relay_edit.chinese=!relay_edit.chinese;relay_edit.keymode=0;relay_ime_changed();
  } else if(!strncmp(a,"rpick",5)&&a[5]>='0'&&a[5]<='3'&&!a[6])relay_ime_select(a[5]-'0');
  else if(!strcmp(a,"riprev")) {if(relay_edit.candidate_page>0)relay_edit.candidate_page--;relay_ime_changed();}
  else if(!strcmp(a,"rinext")) {if((relay_edit.candidate_page+1)*4<relay_candidate_count())relay_edit.candidate_page++;relay_ime_changed();}
  else if(!strcmp(a,"rshift")) {if(!relay_ime_raw())return 1;relay_edit.keymode=relay_edit.keymode==1?0:1;}
  else if(!strcmp(a,"rsymbol")) {if(!relay_ime_raw())return 1;relay_edit.keymode=relay_edit.keymode==2?3:2;}
  else if(!strcmp(a,"rletters"))relay_edit.keymode=0;
  else if(!strcmp(a,"rspace")) {if(relay_edit.pinyin[0]) {if(!relay_ime_select(0))relay_ime_raw();} else relay_key_char(' ');}
  else if(!strcmp(a,"rbs"))relay_key_backspace();
  else if(!strcmp(a,"rclear")) {
    relay_key_revision++;
    relay_wipe(relay_input(),relay_input_limit()+1);if(relay_edit.pinyin[0]){relay_edit.pinyin[0]=0;relay_edit.candidate_page=0;relay_ime_changed();}relay_edit.overflow=0;
    if(relay_edit.key==RK_PASSWORD)relay_edit.keep=0;
  } else if(!strcmp(a,"rshow"))relay_edit.show=!relay_edit.show;
  else if(!strcmp(a,"rkdone")) {
    if(!relay_ime_raw())return 1;
    relay_wipe(relay_edit.before,sizeof(relay_edit.before));relay_wipe(sms_ui.before,sizeof(sms_ui.before));relay_edit.show=0;relay_view=RV_EDIT;
  } else if(!strcmp(a,"rkcancel")||!strcmp(a,"back")) {
    /* Cancel never restores a password into memory: keeping an existing saved secret
     * is explicit on the form. Cancelling an SSID field restores its prior bytes. */
    relay_wipe(relay_input(),relay_input_limit()+1);
    if(relay_edit.key==RK_SSID)snprintf(relay_edit.ssid,sizeof(relay_edit.ssid),"%s",relay_edit.before);
    else if(relay_edit.key==RK_SMS_TO||relay_edit.key==RK_SMS_BODY)snprintf(relay_input(),relay_input_limit()+1,"%s",sms_ui.before);
    else if((relay_edit.key==RK_LATENCY||relay_edit.key==RK_LATENCY_NAME))snprintf(relay_input(),relay_input_limit()+1,"%s",latency_ui.before);
    else relay_edit.keep=relay_edit.saved&&relay_edit.has_secret&&relay_edit.security==relay_edit.original_security;
    relay_wipe(relay_edit.before,sizeof(relay_edit.before));relay_wipe(sms_ui.before,sizeof(sms_ui.before));relay_edit.show=0;relay_view=RV_EDIT;
  }
  return 1;
}
