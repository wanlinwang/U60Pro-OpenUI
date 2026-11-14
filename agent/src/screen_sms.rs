//! Private, bounded SMS projection for the local screen socket. Never used by status/logs.
use serde_json::Value;
use std::sync::{
    atomic::{AtomicU64, Ordering},
    Mutex,
};

#[derive(PartialEq)]
pub struct Draft {
    pub number: String,
    pub message: String,
}
impl std::fmt::Debug for Draft {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("SmsDraft([redacted])")
    }
}

const MAX_ITEMS: usize = 500;
const PAGE: usize = 4;
const THREAD_PAGE: usize = 2;
#[derive(PartialEq)]
pub struct ThreadKey(pub String);
impl std::fmt::Debug for ThreadKey {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("SmsThreadKey([redacted])")
    }
}
#[derive(Clone)]
struct Item {
    key: String,
    tag: u8,
    number: String,
    date: String,
    content: String,
}
static MESSAGES: Mutex<Option<Vec<Item>>> = Mutex::new(None);
static EPOCH: AtomicU64 = AtomicU64::new(0);

pub fn clear() {
    let mut messages = MESSAGES.lock().unwrap();
    EPOCH.fetch_add(1, Ordering::Relaxed);
    *messages = None;
}

fn value(v: &Value) -> String {
    let Some(s) = v.as_str() else {
        return v.as_i64().map(|n| n.to_string()).unwrap_or_default();
    };
    if !s.is_empty() && s.len() % 4 == 0 && s.bytes().all(|b| b.is_ascii_hexdigit()) {
        let units: Option<Vec<u16>> = s
            .as_bytes()
            .chunks_exact(4)
            .map(|chunk| {
                std::str::from_utf8(chunk)
                    .ok()
                    .and_then(|n| u16::from_str_radix(n, 16).ok())
            })
            .collect();
        if let Some(decoded) = units.and_then(|u| String::from_utf16(&u).ok()) {
            return decoded;
        }
    }
    s.to_owned()
}
fn clean(s: &str, limit: usize) -> String {
    let mut out = String::new();
    for c in s.chars() {
        let c = if c.is_control() { ' ' } else { c };
        if out.len() + c.len_utf8() > limit {
            while out.len() + 3 > limit {
                out.pop();
            }
            out.push_str("...");
            break;
        }
        out.push(c);
    }
    out
}
fn encode(s: &str) -> String {
    if s.is_empty() {
        return "-".into();
    }
    s.as_bytes().iter().map(|b| format!("{b:02x}")).collect()
}
fn parse_items(v: &Value) -> Result<Vec<Item>, &'static str> {
    let rows = v
        .as_array()
        .or_else(|| v.get("messages").and_then(Value::as_array))
        .ok_or("SMS list unavailable")?;
    Ok(rows
        .iter()
        .take(MAX_ITEMS)
        .enumerate()
        .map(|(index, v)| {
            let original = value(&v["number"]);
            let number = clean(&original, 96);
            let numeric: String = original
                .chars()
                .filter(|c| !matches!(c, ' ' | '-' | '(' | ')'))
                .collect();
            let key = if original.is_empty()
                || original.len() > 96
                || original.chars().any(char::is_control)
            {
                let id = v["id"]
                    .as_u64()
                    .map(|n| n.to_string())
                    .unwrap_or_else(|| format!("index{index}"));
                format!("u:{id}")
            } else if !numeric.is_empty()
                && numeric.len() <= 96
                && numeric
                    .bytes()
                    .all(|b| b.is_ascii_digit() || matches!(b, b'+' | b'*' | b'#'))
            {
                format!("n:{numeric}")
            } else {
                format!("a:{original}")
            };
            Item {
                key,
                tag: v["tag"]
                    .as_u64()
                    .or_else(|| v["tag"].as_str().and_then(|x| x.parse().ok()))
                    .and_then(|n| u8::try_from(n).ok())
                    .unwrap_or(0),
                number,
                date: clean(v["date"].as_str().unwrap_or(""), 64),
                content: clean(&value(&v["content"]), 2048),
            }
        })
        .collect())
}
pub fn refresh(state: &crate::handlers::AppState) -> (u16, Value) {
    let epoch = EPOCH.load(Ordering::Relaxed);
    let (_, cap) = crate::sms::sms_capabilities(state);
    if cap["data"]["available"] != true || cap["data"]["ready"] != true {
        let mut messages = MESSAGES.lock().unwrap();
        if EPOCH.load(Ordering::Relaxed) == epoch {
            *messages = None;
        }
        return (
            503,
            serde_json::json!({"ok":false,"error":"SMS service unavailable"}),
        );
    }
    let (code, data) = crate::sms::sms_list(state, br#"{"page":0,"per_page":500}"#);
    let parsed = if code == 200 {
        parse_items(&data["data"]).ok()
    } else {
        None
    };
    let ok = parsed.is_some();
    let mut messages = MESSAGES.lock().unwrap();
    if EPOCH.load(Ordering::Relaxed) != epoch {
        return (
            409,
            serde_json::json!({"ok":false,"error":"SMS view closed"}),
        );
    }
    *messages = parsed;
    if ok {
        (200, serde_json::json!({"ok":true}))
    } else {
        (
            503,
            serde_json::json!({"ok":false,"error":"SMS list unavailable"}),
        )
    }
}
fn conversations(items: &[Item]) -> Vec<&Item> {
    let mut seen = std::collections::HashSet::new();
    items
        .iter()
        .filter(|item| seen.insert(item.key.as_str()))
        .collect()
}
fn row(out: &mut String, item: &Item) {
    out.push_str(&format!(
        "m\t{}\t{}\t{}\t{}\t{}\n",
        item.tag,
        encode(&item.key),
        encode(&item.number),
        encode(&item.date),
        encode(&item.content)
    ));
}
fn list_page(items: &[Item], offset: usize) -> String {
    let groups = conversations(items);
    let start = offset.min(groups.len());
    let mut out = format!("SMS {} {}\n", groups.len(), start);
    for item in groups.into_iter().skip(start).take(PAGE) {
        row(&mut out, item);
    }
    out.push_str(".\n");
    out
}
fn thread_page(items: &[Item], key: &str, offset: usize) -> String {
    let matching: Vec<_> = items.iter().filter(|item| item.key == key).collect();
    if matching.is_empty() {
        return "SMS unavailable\n".into();
    }
    let start = offset.min(matching.len().saturating_sub(1) / THREAD_PAGE * THREAD_PAGE);
    let mut out = format!("SMS {} {}\n", matching.len(), start);
    for item in matching.into_iter().skip(start).take(THREAD_PAGE).rev() {
        row(&mut out, item);
    }
    out.push_str(".\n");
    out
}
// Pages are packed newest first, but each page is drawn oldest to newest. Long
// messages are divided at Unicode boundaries so every part appears in a bubble.
const CHAT_COLUMNS: usize = 14;
const CHAT_LINES: usize = 3;
const CHAT_HEIGHT: usize = 190;
const CHAT2_HEIGHT: usize = 280;
const CHAT3_HEIGHT: usize = 310;
struct ChatPart<'a> {
    item: &'a Item,
    text: String,
    part: usize,
    parts: usize,
}
fn chat_pages<'a>(items: &'a [Item], key: &str, height_limit: usize) -> Vec<Vec<ChatPart<'a>>> {
    let mut pages: Vec<Vec<ChatPart<'a>>> = Vec::new();
    let mut used = 0;
    for item in items.iter().filter(|item| item.key == key) {
        let chars: Vec<char> = item.content.chars().collect();
        let chunk_size = CHAT_COLUMNS * CHAT_LINES;
        let count = chars.len().max(1).div_ceil(chunk_size);
        for part in (0..count).rev() {
            let start = part * chunk_size;
            let end = (start + chunk_size).min(chars.len());
            let text: String = chars[start..end].iter().collect();
            let lines = (end - start).max(1).div_ceil(CHAT_COLUMNS);
            let height = 30 + lines * 20;
            if pages.is_empty() || used + height > height_limit {
                pages.push(Vec::new());
                used = 0;
            }
            pages.last_mut().unwrap().push(ChatPart {
                item,
                text,
                part: part + 1,
                parts: count,
            });
            used += height;
        }
    }
    pages
}
fn chat_page(items: &[Item], key: &str, offset: usize, height_limit: usize) -> String {
    let pages = chat_pages(items, key, height_limit);
    if pages.is_empty() {
        return "CHAT unavailable\n".into();
    }
    let page = offset.min(pages.len() - 1);
    let mut out = format!("CHAT {} {}\n", pages.len(), page);
    for row in pages[page].iter().rev() {
        out.push_str(&format!(
            "b\t{}\t{}\t{}\t{}\t{}\n",
            row.item.tag,
            row.part,
            row.parts,
            encode(&row.item.date),
            encode(&row.text)
        ));
    }
    out.push_str(".\n");
    out
}
pub fn chat(key: &str, offset: usize) -> String {
    let guard = MESSAGES.lock().unwrap();
    guard.as_ref().map_or_else(
        || "CHAT unavailable\n".into(),
        |items| chat_page(items, key, offset, CHAT_HEIGHT),
    )
}
pub fn chat2(key: &str, offset: usize) -> String {
    let guard = MESSAGES.lock().unwrap();
    guard.as_ref().map_or_else(
        || "CHAT unavailable\n".into(),
        |items| chat_page(items, key, offset, CHAT2_HEIGHT),
    )
}
pub fn chat3(key: &str, offset: usize) -> String {
    let guard = MESSAGES.lock().unwrap();
    guard.as_ref().map_or_else(
        || "CHAT unavailable\n".into(),
        |items| chat_page(items, key, offset, CHAT3_HEIGHT),
    )
}
// Keep the first screen release's five-field per-message protocol intact.
pub fn legacy_page(offset: usize) -> String {
    let guard = MESSAGES.lock().unwrap();
    let Some(items) = guard.as_ref() else {
        return "SMS unavailable\n".into();
    };
    let start = offset.min(items.len());
    let mut out = format!("SMS {} {}\n", items.len(), start);
    for item in items.iter().skip(start).take(PAGE) {
        out.push_str(&format!(
            "m\t{}\t{}\t{}\t{}\n",
            item.tag,
            encode(&item.number),
            encode(&item.date),
            encode(&item.content)
        ));
    }
    out.push_str(".\n");
    out
}
pub fn page(offset: usize) -> String {
    let guard = MESSAGES.lock().unwrap();
    guard.as_ref().map_or_else(
        || "SMS unavailable\n".into(),
        |items| list_page(items, offset),
    )
}
pub fn thread(key: &str, offset: usize) -> String {
    let guard = MESSAGES.lock().unwrap();
    guard.as_ref().map_or_else(
        || "SMS unavailable\n".into(),
        |items| thread_page(items, key, offset),
    )
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn decodes_chinese_and_sanitizes_and_bounds() {
        let rows=parse_items(&serde_json::json!({"messages":[{"number":"002B0031","content":"4F60597D","date":"today","tag":1},{"number":"Alice","content":"A\nB","tag":"2"}]})).unwrap();
        assert_eq!(rows[0].number, "+1");
        assert_eq!(rows[0].content, "你好");
        assert_eq!(rows[1].content, "A B");
        assert_eq!(rows[1].tag, 2);
        assert!(clean(&"中".repeat(400), 1024).ends_with("..."));
        assert!(parse_items(&serde_json::json!({})).is_err());
    }
    #[test]
    fn groups_numbers_and_paginates_conversations_chronologically() {
        let rows = parse_items(&serde_json::json!({"messages":[
            {"id":5,"number":"+1 (23)","content":"new","tag":2},
            {"id":4,"number":"555","content":"other","tag":1},
            {"id":3,"number":"+123","content":"middle","tag":1},
            {"id":2,"number":"+123","content":"old","tag":0},
            {"id":1,"number":"","content":"unknown","tag":0},
            {"id":0,"number":"","content":"another unknown","tag":0}
        ]}))
        .unwrap();
        let list = list_page(&rows, 0);
        assert!(list.starts_with("SMS 4 0\n"));
        assert_eq!(list.matches("\nm\t").count(), 4);
        let recent = thread_page(&rows, "n:+123", 0);
        assert!(recent.starts_with("SMS 3 0\n"));
        assert!(recent.find(&encode("middle")).unwrap() < recent.find(&encode("new")).unwrap());
        let older = thread_page(&rows, "n:+123", 2);
        assert!(older.starts_with("SMS 3 2\n") && older.contains(&encode("old")));
        assert_eq!(thread_page(&rows, "n:999", 0), "SMS unavailable\n");
        assert!(!format!("{:?}", ThreadKey("n:+123".into())).contains("123"));
    }
    #[test]
    fn legacy_screen_projection_remains_compatible() {
        let rows = parse_items(
            &serde_json::json!({"messages":[{"id":1,"number":"+123","content":"hello","tag":1}]}),
        )
        .unwrap();
        *MESSAGES.lock().unwrap() = Some(rows);
        let old = legacy_page(0);
        assert!(old.starts_with("SMS 1 0\n"));
        let line = old.lines().nth(1).unwrap();
        assert_eq!(line.split('\t').count(), 5);
        assert_eq!(line.split('\t').nth(2), Some("2b313233"));
        clear();
    }
    #[test]
    fn chat_bubbles_preserve_long_unicode_messages_and_chronology() {
        let long = format!("头{}  {}尾", "中".repeat(84), "🌟".repeat(20));
        let rows = parse_items(&serde_json::json!({"messages":[
            {"id":4,"number":"123","content":"Newest", "tag":2},
            {"id":3,"number":"123","content":long, "tag":1},
            {"id":2,"number":"123","content":"Oldest", "tag":0},
            {"id":1,"number":"999","content":"Other", "tag":1}
        ]}))
        .unwrap();
        let pages = chat_pages(&rows, "n:123", CHAT2_HEIGHT);
        assert!(pages.len() >= 2);
        for page in &pages {
            assert!(page.len() <= 5);
            assert!(
                page.iter()
                    .map(|p| 30 + 20 * p.text.chars().count().max(1).div_ceil(14))
                    .sum::<usize>()
                    <= CHAT2_HEIGHT
            );
        }
        let display: Vec<_> = pages
            .iter()
            .rev()
            .flat_map(|page| page.iter().rev())
            .collect();
        assert_eq!(display.first().unwrap().text, "Oldest");
        assert_eq!(display.last().unwrap().text, "Newest");
        let joined: String = display
            .iter()
            .filter(|p| p.item.tag == 1)
            .map(|p| p.text.as_str())
            .collect();
        assert_eq!(joined, long);
        assert!(display
            .iter()
            .filter(|p| p.item.tag == 1)
            .enumerate()
            .all(|(i, p)| p.part == i + 1));
        assert!(chat_page(&rows, "n:123", 0, CHAT2_HEIGHT)
            .starts_with(&format!("CHAT {} 0\n", pages.len())));
        assert_eq!(
            chat_page(&rows, "n:missing", 0, CHAT2_HEIGHT),
            "CHAT unavailable\n"
        );
    }
    #[test]
    fn larger_view_keeps_the_previous_chat_protocol_unchanged() {
        let messages: Vec<_> = (0..5)
            .rev()
            .map(|i| serde_json::json!({"id":i,"number":"123","content":"hello","tag":1}))
            .collect();
        let rows = parse_items(&serde_json::json!({"messages":messages})).unwrap();
        assert_eq!(chat_pages(&rows, "n:123", CHAT_HEIGHT).len(), 2);
        assert_eq!(chat_pages(&rows, "n:123", CHAT2_HEIGHT).len(), 1);
        assert!(chat_page(&rows, "n:123", 0, CHAT_HEIGHT).starts_with("CHAT 2 0\n"));
        assert!(chat_page(&rows, "n:123", 0, CHAT2_HEIGHT).starts_with("CHAT 1 0\n"));
    }
    #[test]
    fn chat3_fits_six_short_messages_without_changing_older_protocols() {
        let messages: Vec<_> = (0..6)
            .rev()
            .map(|i| serde_json::json!({"id":i,"number":"123","content":format!("m{i}"),"tag":1}))
            .collect();
        let rows = parse_items(&serde_json::json!({"messages":messages})).unwrap();
        assert_eq!(chat_pages(&rows, "n:123", CHAT_HEIGHT).len(), 2);
        assert_eq!(chat_pages(&rows, "n:123", CHAT2_HEIGHT).len(), 2);
        let pages = chat_pages(&rows, "n:123", CHAT3_HEIGHT);
        assert_eq!(pages.len(), 1);
        assert_eq!(pages[0].len(), 6);
        assert!(chat_page(&rows, "n:123", 0, CHAT3_HEIGHT).starts_with("CHAT 1 0\n"));
    }
}
