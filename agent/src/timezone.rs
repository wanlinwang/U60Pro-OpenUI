//! System timezone: fixed UTC offsets persisted through OpenWrt UCI.
//! Never changes the system clock or commits another process's staged changes.
use crate::process::BoundedCommand;
use serde::Deserialize;
use serde_json::{json, Value};
use std::{
    fs,
    io::{self, Write},
    os::unix::fs::{symlink, DirBuilderExt, OpenOptionsExt, PermissionsExt},
    path::{Path, PathBuf},
    process::Command,
    sync::{
        atomic::{AtomicU64, Ordering},
        Mutex,
    },
};

static LOCK: Mutex<()> = Mutex::new(());
static NEXT: AtomicU64 = AtomicU64::new(0);
const TZ_KEY: &str = "system.@system[0].timezone";
const NAME_KEY: &str = "system.@system[0].zonename";
const RUNTIME_TZ: &str = "/tmp/TZ";
const RUNTIME_LINK: &str = "/tmp/localtime";
const RUNTIME_TZIF: &str = "/tmp/openui-timezone.tzif";
fn runtime_tz() -> PathBuf {
    #[cfg(test)]
    if let Some(root) = std::env::var_os("OPENUI_TZ_TEST_ROOT") {
        return PathBuf::from(root).join("runtime/TZ");
    }
    PathBuf::from(RUNTIME_TZ)
}
fn runtime_link() -> PathBuf {
    #[cfg(test)]
    if let Some(root) = std::env::var_os("OPENUI_TZ_TEST_ROOT") {
        return PathBuf::from(root).join("runtime/localtime");
    }
    PathBuf::from(RUNTIME_LINK)
}
fn runtime_tzif() -> PathBuf {
    #[cfg(test)]
    if let Some(root) = std::env::var_os("OPENUI_TZ_TEST_ROOT") {
        return PathBuf::from(root).join("runtime/openui-timezone.tzif");
    }
    PathBuf::from(RUNTIME_TZIF)
}
fn uci_program() -> PathBuf {
    #[cfg(test)]
    if let Some(root) = std::env::var_os("OPENUI_TZ_TEST_ROOT") {
        return PathBuf::from(root).join("uci");
    }
    PathBuf::from("uci")
}

fn spec(offset: i32) -> Option<String> {
    if !(-720..=840).contains(&offset) || offset % 15 != 0 {
        return None;
    }
    if offset == 0 {
        return Some("UTC0".into());
    }
    let value = offset.abs();
    let sign = if offset > 0 { '-' } else { '+' };
    Some(if value % 60 == 0 {
        format!("UTC{sign}{}", value / 60)
    } else {
        format!("UTC{sign}{}:{:02}", value / 60, value % 60)
    })
}
fn parse_spec(s: &str) -> Option<i32> {
    if s == "UTC" || s == "UTC0" {
        return Some(0);
    }
    let rest = s.strip_prefix("UTC")?;
    let (sign, rest) = if let Some(v) = rest.strip_prefix('-') {
        (1, v)
    } else if let Some(v) = rest.strip_prefix('+') {
        (-1, v)
    } else {
        return None;
    };
    let (hours, minutes) = match rest.split_once(':') {
        Some((h, m)) if m.len() == 2 => (h.parse::<i32>().ok()?, m.parse::<i32>().ok()?),
        None => (rest.parse::<i32>().ok()?, 0),
        _ => return None,
    };
    if !(0..=14).contains(&hours) || !(0..60).contains(&minutes) {
        return None;
    }
    let offset = sign * (hours * 60 + minutes);
    spec(offset)?;
    Some(offset)
}
/// A TZif v2 file with one fixed-offset type and a POSIX footer. This firmware
/// has no zoneinfo database, while musl reads /etc/localtime rather than /etc/TZ.
fn fixed_tzif(offset: i32) -> Option<Vec<u8>> {
    let zone = spec(offset)?;
    let mut header = b"TZif2".to_vec();
    header.extend_from_slice(&[0; 15]);
    for n in [0u32, 0, 0, 0, 1, 4] {
        header.extend_from_slice(&n.to_be_bytes());
    }
    let mut block = (offset * 60).to_be_bytes().to_vec();
    block.extend_from_slice(&[0, 0]);
    block.extend_from_slice(b"UTC\0");
    let mut bytes = Vec::new();
    bytes.extend_from_slice(&header);
    bytes.extend_from_slice(&block);
    bytes.extend_from_slice(&header);
    bytes.extend_from_slice(&block);
    bytes.push(b'\n');
    bytes.extend_from_slice(zone.as_bytes());
    bytes.push(b'\n');
    Some(bytes)
}
fn uci(args: &[&str], stage: Option<&Path>) -> Result<String, String> {
    let mut cmd = Command::new(uci_program());
    cmd.arg("-q");
    if let Some(p) = stage {
        cmd.arg("-t").arg(p);
    }
    cmd.args(args);
    let out = cmd
        .bounded_output()
        .map_err(|_| "Cannot run UCI".to_string())?;
    if !out.status.success() {
        return Err("UCI operation failed".into());
    }
    Ok(String::from_utf8_lossy(&out.stdout).trim().to_string())
}
fn optional(key: &str) -> Option<String> {
    uci(&["get", key], None).ok()
}
fn no_pending() -> Result<(), String> {
    if !uci(&["changes", "system"], None)?.is_empty() {
        return Err("System configuration has pending changes; retry later".into());
    }
    Ok(())
}
struct Stage(PathBuf);
impl Stage {
    fn new() -> Result<Self, String> {
        #[cfg(test)]
        let root = std::env::var_os("OPENUI_TZ_TEST_ROOT")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("/tmp"));
        #[cfg(not(test))]
        let root = PathBuf::from("/tmp");
        let p = root.join(format!(
            "openui-timezone-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        fs::DirBuilder::new()
            .mode(0o700)
            .create(&p)
            .map_err(|_| "Cannot prepare timezone update".to_string())?;
        Ok(Self(p))
    }
    fn set(&self, key: &str, value: &str) -> Result<(), String> {
        uci(&["set", &format!("{key}={value}")], Some(&self.0)).map(|_| ())
    }
    fn delete(&self, key: &str) -> Result<(), String> {
        uci(&["delete", key], Some(&self.0)).map(|_| ())
    }
    fn commit(&self) -> Result<(), String> {
        uci(&["commit", "system"], Some(&self.0)).map(|_| ())
    }
}
impl Drop for Stage {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}
fn runtime_write(path: &Path, bytes: &[u8]) -> io::Result<()> {
    let tmp = path.with_file_name(format!(
        ".openui-tz-{}-{}",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ));
    let result = (|| {
        let mut f = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(0o644)
            .open(&tmp)?;
        f.set_permissions(fs::Permissions::from_mode(0o644))?;
        f.write_all(bytes)?;
        f.sync_all()?;
        fs::rename(&tmp, path)?;
        Ok(())
    })();
    if result.is_err() {
        let _ = fs::remove_file(tmp);
    }
    result
}
#[derive(Clone)]
struct Snapshot {
    zone: String,
    name: Option<String>,
    runtime: Option<Vec<u8>>,
    link: Option<PathBuf>,
    tzif: Option<Vec<u8>>,
}
fn snapshot() -> Result<Snapshot, String> {
    let zone = uci(&["get", TZ_KEY], None)?;
    let name = optional(NAME_KEY);
    if let Ok(meta) = fs::symlink_metadata(runtime_tz()) {
        if !meta.file_type().is_file() {
            return Err("Unsupported timezone file layout".into());
        }
    }
    let runtime = match fs::read(runtime_tz()) {
        Ok(v) if v.len() <= 256 => Some(v),
        Ok(_) => return Err("Unexpected timezone file".into()),
        Err(e) if e.kind() == io::ErrorKind::NotFound => None,
        Err(_) => return Err("Cannot read timezone file".into()),
    };
    let link = match fs::symlink_metadata(runtime_link()) {
        Ok(v) if v.file_type().is_symlink() => {
            Some(fs::read_link(runtime_link()).map_err(|_| "Cannot read timezone link")?)
        }
        Ok(_) => return Err("Unsupported timezone file layout".into()),
        Err(e) if e.kind() == io::ErrorKind::NotFound => None,
        Err(_) => return Err("Cannot inspect timezone link".into()),
    };
    if let Ok(meta) = fs::symlink_metadata(runtime_tzif()) {
        if !meta.file_type().is_file() {
            return Err("Unsupported timezone data layout".into());
        }
    }
    let tzif = match fs::read(runtime_tzif()) {
        Ok(v) if v.len() <= 4096 => Some(v),
        Ok(_) => return Err("Unexpected timezone data".into()),
        Err(e) if e.kind() == io::ErrorKind::NotFound => None,
        Err(_) => return Err("Cannot read timezone data".into()),
    };
    Ok(Snapshot {
        zone,
        name,
        runtime,
        link,
        tzif,
    })
}
fn config_matches(s: &Snapshot) -> bool {
    uci(&["get", TZ_KEY], None).ok().as_deref() == Some(&s.zone) && optional(NAME_KEY) == s.name
}
fn replace_link(target: &Path) -> io::Result<()> {
    let tmp = runtime_link().with_file_name(format!(
        ".openui-localtime-{}-{}",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ));
    let result = (|| {
        symlink(target, &tmp)?;
        fs::rename(&tmp, runtime_link())?;
        Ok(())
    })();
    if result.is_err() {
        let _ = fs::remove_file(tmp);
    }
    result
}
fn restore_runtime(
    s: &Snapshot,
    tz_changed: bool,
    link_changed: bool,
    tzif_changed: bool,
) -> Result<(), String> {
    if tz_changed {
        match &s.runtime {
            Some(v) => {
                runtime_write(&runtime_tz(), v).map_err(|_| "Cannot restore runtime timezone")?
            }
            None => match fs::remove_file(runtime_tz()) {
                Ok(()) => (),
                Err(e) if e.kind() == io::ErrorKind::NotFound => (),
                Err(_) => return Err("Cannot restore runtime timezone".into()),
            },
        }
    }
    if link_changed {
        if fs::read_link(runtime_link()).ok().as_ref() != Some(&runtime_tzif()) {
            return Err("Timezone link changed during rollback".into());
        }
        match &s.link {
            Some(target) => replace_link(target).map_err(|_| "Cannot restore timezone link")?,
            None => fs::remove_file(runtime_link()).map_err(|_| "Cannot restore timezone link")?,
        }
    }
    if tzif_changed {
        match &s.tzif {
            Some(bytes) => {
                runtime_write(&runtime_tzif(), bytes).map_err(|_| "Cannot restore timezone data")?
            }
            None => match fs::remove_file(runtime_tzif()) {
                Ok(()) => (),
                Err(e) if e.kind() == io::ErrorKind::NotFound => (),
                Err(_) => return Err("Cannot restore timezone data".into()),
            },
        }
    }
    Ok(())
}
fn runtime_offset_matches(offset: i32) -> bool {
    #[cfg(test)]
    {
        let _ = offset;
        if std::env::var_os("OPENUI_TZ_FAIL_READBACK").is_some() {
            std::env::remove_var("OPENUI_TZ_FAIL_READBACK");
            false
        } else {
            true
        }
    }
    #[cfg(not(test))]
    {
        let sign = if offset < 0 { '-' } else { '+' };
        let n = offset.abs();
        let expected = format!("{sign}{:02}{:02}", n / 60, n % 60);
        Command::new("date")
            .arg("+%z")
            .env_remove("TZ")
            .bounded_output()
            .ok()
            .is_some_and(|o| {
                o.status.success() && String::from_utf8_lossy(&o.stdout).trim() == expected
            })
    }
}
fn restore_config(before: &Snapshot, target: &str) -> Result<(), String> {
    no_pending()?;
    if uci(&["get", TZ_KEY], None)? != target || optional(NAME_KEY).is_some() {
        return Err("System configuration changed during rollback".into());
    }
    let stage = Stage::new()?;
    stage.set(TZ_KEY, &before.zone)?;
    if let Some(name) = &before.name {
        stage.set(NAME_KEY, name)?;
    }
    stage.commit()
}
fn apply_runtime(before: &Snapshot, target: &str, offset: i32) -> Result<(), String> {
    let mut runtime_touched = false;
    let mut tzif_touched = false;
    let mut link_changed = false;
    let tzif = fixed_tzif(offset).ok_or("Invalid timezone offset")?;
    let apply = (|| -> Result<(), String> {
        let current = snapshot()?;
        if current.runtime != before.runtime
            || current.link != before.link
            || current.tzif != before.tzif
        {
            return Err("Runtime timezone changed; refresh and retry".into());
        }
        runtime_write(&runtime_tzif(), &tzif).map_err(|_| "Cannot write timezone data")?;
        tzif_touched = true;
        runtime_write(&runtime_tz(), format!("{target}\n").as_bytes())
            .map_err(|_| "Cannot apply timezone")?;
        runtime_touched = true;
        replace_link(&runtime_tzif()).map_err(|_| "Cannot update timezone link")?;
        link_changed = true;
        if uci(&["get", TZ_KEY], None)? != target
            || optional(NAME_KEY).is_some()
            || fs::read(runtime_tz()).ok().as_deref() != Some(format!("{target}\n").as_bytes())
            || fs::read(runtime_tzif()).ok().as_deref() != Some(tzif.as_slice())
            || fs::read_link(runtime_link()).ok().as_ref() != Some(&runtime_tzif())
            || !runtime_offset_matches(offset)
        {
            return Err("Timezone readback differs".into());
        }
        Ok(())
    })();
    if let Err(error) = apply {
        if runtime_touched || link_changed || tzif_touched {
            if let Err(e) = restore_runtime(before, runtime_touched, link_changed, tzif_touched) {
                return Err(format!("{error}; runtime restoration needs attention: {e}"));
            }
        }
        return Err(error);
    }
    Ok(())
}
fn apply_offset(offset: i32) -> Result<(), String> {
    let target = spec(offset).ok_or("Invalid timezone offset")?;
    let _guard = LOCK.lock().unwrap_or_else(|e| e.into_inner());
    no_pending()?;
    let before = snapshot()?;
    let stage = Stage::new()?;
    stage.set(TZ_KEY, &target)?;
    if before.name.is_some() {
        stage.delete(NAME_KEY)?;
    }
    no_pending()?;
    if !config_matches(&before) {
        return Err("System timezone changed; refresh and retry".into());
    }
    if let Err(error) = stage.commit() {
        if config_matches(&before) {
            return Err(error);
        }
        return Err(match restore_config(&before, &target) {
            Ok(()) => format!("{error}; previous timezone restored"),
            Err(e) => format!("{error}; timezone restoration needs attention: {e}"),
        });
    }
    if let Err(error) = apply_runtime(&before, &target, offset) {
        let rollback = restore_config(&before, &target);
        return Err(match rollback {
            Ok(()) if error.contains("runtime restoration needs attention") => {
                format!("{error}; previous timezone configuration restored")
            }
            Ok(()) => format!("{error}; previous timezone restored"),
            Err(e) => format!("{error}; timezone restoration needs attention: {e}"),
        });
    }
    Ok(())
}
/// /tmp is volatile. Recreate our fixed-zone file on boot, but never replace a
/// named-zone file or a /tmp/localtime link owned by the firmware or user.
pub fn start() {
    let _guard = LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let restore = (|| -> Result<(), String> {
        no_pending()?;
        let before = snapshot()?;
        if before.name.is_some() {
            return Ok(());
        }
        let Some(offset) = parse_spec(&before.zone) else {
            return Ok(());
        };
        if before
            .link
            .as_ref()
            .is_some_and(|link| link != &runtime_tzif())
        {
            return Ok(());
        }
        if let Some(bytes) = &before.runtime {
            let raw = std::str::from_utf8(bytes)
                .map_err(|_| "Invalid runtime timezone")?
                .trim();
            if parse_spec(raw) != Some(offset) {
                return Ok(());
            }
        }
        apply_runtime(&before, &before.zone, offset)
    })();
    if let Err(e) = restore {
        eprintln!("[timezone] runtime restore failed: {e}");
    }
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Update {
    offset_minutes: i32,
}
pub fn get() -> (u16, Value) {
    let _guard = LOCK.lock().unwrap_or_else(|e| e.into_inner());
    match uci(&["get", TZ_KEY], None) {
        Ok(zone) => {
            let named = optional(NAME_KEY).filter(|_| fs::metadata(runtime_link()).is_ok());
            let offset = if named.is_some() {
                None
            } else {
                parse_spec(&zone)
            };
            let effective = named.unwrap_or(zone);
            (
                200,
                json!({"ok":true,"data":{"offset_minutes":offset,"timezone":effective}}),
            )
        }
        Err(e) => (503, json!({"ok":false,"error":e})),
    }
}
pub fn update(body: &[u8]) -> (u16, Value) {
    let update: Update = match serde_json::from_slice(body) {
        Ok(v) => v,
        Err(_) => return (400, json!({"ok":false,"error":"Invalid timezone request"})),
    };
    if spec(update.offset_minutes).is_none() {
        return (400, json!({"ok":false,"error":"Invalid timezone offset"}));
    }
    match apply_offset(update.offset_minutes) {
        Ok(()) => get(),
        Err(e) => (409, json!({"ok":false,"error":e})),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn offset_signs_and_roundtrips() {
        assert_eq!(spec(480).as_deref(), Some("UTC-8"));
        assert_eq!(spec(-210).as_deref(), Some("UTC+3:30"));
        for n in (-720..=840).step_by(15) {
            assert_eq!(parse_spec(&spec(n).unwrap()), Some(n));
        }
        assert_eq!(parse_spec("UTC"), Some(0));
        assert!(spec(481).is_none());
        assert!(spec(900).is_none());
        for s in [
            "UTC-15",
            "UTC+1:99",
            "UTC-2;reboot",
            "Asia/Shanghai",
            "UTC+999999999999",
        ] {
            assert!(parse_spec(s).is_none(), "{s}");
        }
    }
    #[test]
    fn isolated_uci_apply_pending_and_runtime_rollback() {
        let root =
            std::env::temp_dir().join(format!("openui-timezone-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(root.join("runtime")).unwrap();
        fs::write(root.join("zone"), "UTC\n").unwrap();
        fs::write(root.join("runtime/TZ"), "UTC\n").unwrap();
        let script = r##"#!/bin/sh
set -eu
r="$OPENUI_TZ_TEST_ROOT"
[ "$1" = -q ] && shift
stage=''
if [ "${1:-}" = -t ]; then stage="$2"; shift 2; fi
case "$1" in
  changes) [ ! -f "$r/pending" ] || printf pending ;;
  get) case "$2" in *timezone) cat "$r/zone";; *zonename) [ -f "$r/name" ] && cat "$r/name" || exit 1;; *) exit 1;; esac ;;
  set) v="${2#*=}"; case "$2" in *timezone=*) printf '%s\n' "$v" > "$stage/zone";; *zonename=*) printf '%s\n' "$v" > "$stage/name";; *) exit 1;; esac ;;
  delete) : > "$stage/delete-name" ;;
  commit) if [ -f "$r/fail-commit" ]; then rm "$r/fail-commit"; exit 1; fi
          [ ! -f "$stage/zone" ] || cp "$stage/zone" "$r/zone"
          [ ! -f "$stage/delete-name" ] || rm -f "$r/name"
          [ ! -f "$stage/name" ] || cp "$stage/name" "$r/name" ;;
  *) exit 1 ;;
esac
"##;
        let uci = root.join("uci");
        fs::write(&uci, script).unwrap();
        fs::set_permissions(&uci, fs::Permissions::from_mode(0o700)).unwrap();
        std::env::set_var("OPENUI_TZ_TEST_ROOT", &root);
        assert!(apply_offset(480).is_ok());
        assert_eq!(fs::read_to_string(root.join("zone")).unwrap(), "UTC-8\n");
        assert_eq!(
            fs::read_to_string(root.join("runtime/TZ")).unwrap(),
            "UTC-8\n"
        );
        assert_eq!(
            fs::read_link(root.join("runtime/localtime")).unwrap(),
            root.join("runtime/openui-timezone.tzif")
        );
        std::env::set_var("OPENUI_TZ_FAIL_READBACK", "1");
        let err = apply_offset(0).unwrap_err();
        assert!(err.contains("previous timezone restored"), "{err}");
        assert_eq!(fs::read_to_string(root.join("zone")).unwrap(), "UTC-8\n");
        assert_eq!(
            fs::read_to_string(root.join("runtime/TZ")).unwrap(),
            "UTC-8\n"
        );
        let date = Command::new("date")
            .args(["-d", "@1704067200", "+%z"])
            .env("TZ", root.join("runtime/openui-timezone.tzif"))
            .output()
            .unwrap();
        assert!(date.status.success());
        assert_eq!(String::from_utf8_lossy(&date.stdout).trim(), "+0800");
        let date = Command::new("date")
            .args(["-d", "@1704067200", "+%z"])
            .env("TZ", root.join("runtime/openui-timezone.tzif"))
            .output()
            .unwrap();
        assert!(date.status.success());
        assert_eq!(String::from_utf8_lossy(&date.stdout).trim(), "+0800");
        fs::remove_file(root.join("runtime/localtime")).unwrap();
        fs::remove_file(root.join("runtime/TZ")).unwrap();
        fs::remove_file(root.join("runtime/openui-timezone.tzif")).unwrap();
        start();
        assert_eq!(
            fs::read_to_string(root.join("runtime/TZ")).unwrap(),
            "UTC-8\n"
        );
        assert_eq!(
            fs::read_link(root.join("runtime/localtime")).unwrap(),
            root.join("runtime/openui-timezone.tzif")
        );
        fs::write(root.join("pending"), "pending").unwrap();
        assert!(apply_offset(0).is_err());
        fs::remove_file(root.join("pending")).unwrap();
        fs::write(root.join("fail-commit"), "").unwrap();
        assert!(apply_offset(0).is_err());
        assert_eq!(fs::read_to_string(root.join("zone")).unwrap(), "UTC-8\n");
        fs::set_permissions(root.join("runtime"), fs::Permissions::from_mode(0o500)).unwrap();
        let err = apply_offset(0).unwrap_err();
        assert!(err.contains("previous timezone restored"), "{err}");
        fs::set_permissions(root.join("runtime"), fs::Permissions::from_mode(0o700)).unwrap();
        assert_eq!(fs::read_to_string(root.join("zone")).unwrap(), "UTC-8\n");
        assert_eq!(
            fs::read_to_string(root.join("runtime/TZ")).unwrap(),
            "UTC-8\n"
        );
        assert!(apply_offset(481).is_err());
        std::env::remove_var("OPENUI_TZ_TEST_ROOT");
        fs::remove_dir_all(root).unwrap();
    }
}
