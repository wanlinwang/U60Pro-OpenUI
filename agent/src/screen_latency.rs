//! Screen ICMP and explicitly requested Clash HTTPS probes; no shell commands.
use std::{
    fs,
    io::{self, Read},
    os::unix::fs::DirBuilderExt,
    path::Path,
    sync::Mutex,
};

const TARGET_FILE: &str = "/data/local/tmp/openui-devui/latency-targets.json";
const EXTRA_TARGET_FILE: &str = "/data/local/tmp/openui-devui/latency-targets-extra.json";
const MORE_TARGET_FILE: &str = "/data/local/tmp/openui-devui/latency-targets-more.json";
const NAME_FILE: &str = "/data/local/tmp/openui-devui/latency-names.json";
const ORDER_FILE: &str = "/data/local/tmp/openui-devui/latency-order.json";
static TARGET_LOCK: Mutex<()> = Mutex::new(());

/// Accept a host for ICMP, or `https://` followed by a host for the Clash probe.
/// Paths, user info, ports and shell syntax are deliberately not supported.
pub(crate) fn valid_target(target: &str) -> bool {
    if target.is_empty() || target.len() > 64 || !target.is_ascii() {
        return false;
    }
    let https = target.starts_with("https://");
    let target = target.strip_prefix("https://").unwrap_or(target);
    let target = if target.starts_with('[') && target.ends_with(']') {
        let inner = &target[1..target.len() - 1];
        return https && inner.parse::<std::net::Ipv6Addr>().is_ok();
    } else {
        target
    };
    if target.parse::<std::net::IpAddr>().is_ok() {
        return true;
    }
    let name = target.strip_suffix('.').unwrap_or(target);
    !name.is_empty()
        && name.split('.').all(|label| {
            !label.is_empty()
                && label.len() <= 63
                && !label.starts_with('-')
                && !label.ends_with('-')
                && label
                    .bytes()
                    .all(|b| b.is_ascii_alphanumeric() || b == b'-')
        })
}

/// A bounded ICMP probe; timeout, DNS failure and filtered ICMP are all no reply.
/// This does not establish whether an HTTPS site is reachable through Clash.
#[derive(Debug, PartialEq)]
pub(crate) enum Probe {
    Reply(f64),
    Tailscale { ms: f64, path: TailPath },
    NoReply,
    Unavailable,
}

#[derive(Debug, PartialEq)]
pub(crate) enum TailPath {
    Direct,
    Relay,
    Unknown,
}

fn tailnet_candidate(target: &str) -> bool {
    target.parse::<std::net::Ipv4Addr>().is_ok_and(|ip| {
        let octets = ip.octets();
        octets[0] == 100 && (64..=127).contains(&octets[1])
    }) || target.to_ascii_lowercase().ends_with(".ts.net")
}

fn parse_tailnet_ping(output: &str, target: &str) -> Option<(f64, TailPath)> {
    let line = output.lines().find(|line| line.starts_with("pong from "))?;
    // A subnet ping can report the subnet router rather than the target. Only
    // use this path for a peer IP or a tailnet name; never mislabel LAN hosts.
    if target.parse::<std::net::Ipv4Addr>().is_ok() && !line.contains(&format!("({target})")) {
        return None;
    }
    let (route, duration) = line.split_once(" via ")?.1.rsplit_once(" in ")?;
    let duration = duration.trim();
    let ms = if let Some(value) = duration.strip_suffix("ms") {
        value.parse::<f64>().ok()?
    } else if let Some(value) = duration
        .strip_suffix("µs")
        .or_else(|| duration.strip_suffix("us"))
    {
        value.parse::<f64>().ok()? / 1000.0
    } else if let Some(value) = duration.strip_suffix('s') {
        value.parse::<f64>().ok()? * 1000.0
    } else {
        return None;
    };
    if !ms.is_finite() || !(0.0..=60_000.0).contains(&ms) {
        return None;
    }
    let lower = route.to_ascii_lowercase();
    let path = if lower.contains("derp") || lower.contains("relay") {
        TailPath::Relay
    } else if route.parse::<std::net::SocketAddr>().is_ok() {
        TailPath::Direct
    } else {
        TailPath::Unknown
    };
    Some((ms, path))
}

fn parse_rtt(output: &str) -> Option<f64> {
    let value = output.lines().find_map(|line| {
        line.split("time=")
            .nth(1)
            .and_then(|v| v.split_whitespace().next())
    })?;
    let ms: f64 = value.parse().ok()?;
    (ms.is_finite() && (0.0..=60_000.0).contains(&ms)).then_some(ms)
}

pub(crate) fn probe(target: &str) -> Probe {
    if !valid_target(target) {
        return Probe::Unavailable;
    }
    if let Some(host) = target.strip_prefix("https://") {
        // Use the existing Clash mixed listener deliberately; never silently
        // substitute a direct request when the user selected the proxy test.
        if !crate::clash::core_running() {
            return Probe::Unavailable;
        }
        let host = host
            .strip_prefix('[')
            .and_then(|s| s.strip_suffix(']'))
            .unwrap_or(host);
        let host = if host.parse::<std::net::Ipv6Addr>().is_ok() {
            format!("[{host}]")
        } else {
            host.to_owned()
        };
        let url = format!("https://{host}/");
        let mut cmd = std::process::Command::new("/usr/bin/curl");
        cmd.args([
            "-sS",
            "--head",
            "--output",
            "/dev/null",
            "--write-out",
            "%{time_starttransfer}",
            "--connect-timeout",
            "4",
            "--max-time",
            "8",
            "--noproxy",
            "",
            "--proxy",
            crate::clash::PROXY,
            &url,
        ]);
        return match process_runner::output(
            &mut cmd,
            None,
            std::time::Duration::from_secs(10),
            8192,
        ) {
            Ok(out) if out.status.success() => {
                parse_https_time(&String::from_utf8_lossy(&out.stdout))
                    .map_or(Probe::NoReply, Probe::Reply)
            }
            Ok(_) => Probe::NoReply,
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => Probe::NoReply,
            Err(_) => Probe::Unavailable,
        };
    }
    if tailnet_candidate(target) {
        let mut cmd = std::process::Command::new("/data/bin/tailscale");
        cmd.args([
            "--socket=/var/run/tailscale/tailscaled.sock",
            "ping",
            "--c",
            "1",
            "--timeout",
            "4s",
            "--until-direct=false",
            target,
        ]);
        if let Ok(out) =
            process_runner::output(&mut cmd, None, std::time::Duration::from_secs(6), 8192)
        {
            if out.status.success() {
                if let Some((ms, path)) =
                    parse_tailnet_ping(&String::from_utf8_lossy(&out.stdout), target)
                {
                    return Probe::Tailscale { ms, path };
                }
            }
        }
        // A non-tailnet address in the shared 100.64/10 range still gets ICMP.
    }
    let mut cmd = std::process::Command::new("/usr/bin/ping");
    cmd.args(["-n", "-c", "1", "-W", "2", target]);
    cmd.env("LC_ALL", "C");
    match process_runner::output(&mut cmd, None, std::time::Duration::from_secs(5), 8192) {
        Ok(out) if out.status.success() => {
            parse_rtt(&String::from_utf8_lossy(&out.stdout)).map_or(Probe::NoReply, Probe::Reply)
        }
        Ok(_) => Probe::NoReply,
        Err(e) if e.kind() == std::io::ErrorKind::TimedOut => Probe::NoReply,
        Err(_) => Probe::Unavailable,
    }
}

fn parse_https_time(output: &str) -> Option<f64> {
    let seconds: f64 = output.trim().parse().ok()?;
    (seconds.is_finite() && seconds > 0.0 && seconds <= 10.0).then_some(seconds * 1000.0)
}

/// A deliberately small batch bounds both the runtime and the panel response.
pub(crate) const LEGACY_TARGETS: usize = 4;
pub(crate) const MAX_TARGETS: usize = 6;
pub(crate) const ALL_TARGETS: usize = 24;

fn read_order(path: &Path) -> io::Result<Vec<usize>> {
    let file = match fs::File::open(path) {
        Ok(file) => file,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok((0..ALL_TARGETS).collect()),
        Err(e) => return Err(e),
    };
    let mut bytes = Vec::new();
    file.take(1025).read_to_end(&mut bytes)?;
    if bytes.len() > 1024 { return Err(io::Error::other("order file too large")); }
    let order: Vec<usize> = serde_json::from_slice(&bytes).map_err(io::Error::other)?;
    let mut sorted = order.clone();
    sorted.sort_unstable();
    if sorted != (0..ALL_TARGETS).collect::<Vec<_>>() { return Err(io::Error::other("invalid target order")); }
    Ok(order)
}

fn physical_targets() -> io::Result<Vec<String>> {
    let mut all = read_targets(Path::new(TARGET_FILE), LEGACY_TARGETS)?;
    all.extend(read_targets(Path::new(EXTRA_TARGET_FILE), MAX_TARGETS - LEGACY_TARGETS)?);
    all.extend(read_targets(Path::new(MORE_TARGET_FILE), ALL_TARGETS - MAX_TARGETS)?);
    Ok(all)
}

pub(crate) fn ordered_targets() -> io::Result<Vec<String>> {
    let _guard = TARGET_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let physical = physical_targets()?;
    Ok(read_order(Path::new(ORDER_FILE))?.into_iter().map(|i| physical[i].clone()).collect())
}

#[derive(Clone, Default, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
struct TargetName { target: String, name: String }

fn valid_name(name: &str) -> bool {
    name.len() <= 72 && !name.chars().any(|c| c.is_control())
}
fn read_names(path: &Path) -> io::Result<Vec<TargetName>> {
    let file = match fs::File::open(path) {
        Ok(file) => file,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(vec![TargetName::default(); ALL_TARGETS]),
        Err(e) => return Err(e),
    };
    let mut bytes = Vec::new();
    file.take(8193).read_to_end(&mut bytes)?;
    if bytes.len() > 8192 { return Err(io::Error::other("names file too large")); }
    let names: Vec<TargetName> = serde_json::from_slice(&bytes).map_err(io::Error::other)?;
    if names.len() != ALL_TARGETS || !names.iter().all(|n| (n.target.is_empty() || valid_target(&n.target)) && valid_name(&n.name)) {
        return Err(io::Error::other("invalid saved names"));
    }
    Ok(names)
}
pub(crate) fn ordered_names() -> io::Result<Vec<(String, String)>> {
    let _guard = TARGET_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let targets = physical_targets()?;
    let names = read_names(Path::new(NAME_FILE))?;
    let order = read_order(Path::new(ORDER_FILE))?;
    let visible_names = names_in_order(&targets, &names, &order);
    Ok(order.into_iter().zip(visible_names).map(|(i, name)| (targets[i].clone(), name)).collect())
}
fn names_in_order(targets: &[String], names: &[TargetName], order: &[usize]) -> Vec<String> {
    order.iter().map(|&i| {
        if !targets[i].is_empty() && names[i].target == targets[i] { names[i].name.clone() } else { String::new() }
    }).collect()
}
pub(crate) fn set_name(index: usize, target: &str, name: &str) -> io::Result<()> {
    let _guard = TARGET_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    if !valid_name(name) { return Err(io::Error::other("invalid name")); }
    let targets = physical_targets()?;
    let order = read_order(Path::new(ORDER_FILE))?;
    let physical = *order.get(index).ok_or_else(|| io::Error::other("invalid slot"))?;
    if target.is_empty() || targets[physical] != target { return Err(io::Error::other("target changed")); }
    let mut names = read_names(Path::new(NAME_FILE))?;
    names[physical] = TargetName { target: target.to_owned(), name: name.to_owned() };
    let path = Path::new(NAME_FILE);
    fs::DirBuilder::new().recursive(true).mode(0o700).create(path.parent().unwrap())?;
    crate::storage::atomic_write(path, &serde_json::to_vec(&names).map_err(io::Error::other)?)
}

pub(crate) fn set_ordered(index: usize, value: &str) -> io::Result<()> {
    let _guard = TARGET_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let order = read_order(Path::new(ORDER_FILE))?;
    let physical = *order.get(index).ok_or_else(|| io::Error::other("invalid slot"))?;
    // Fail closed if any portion of the list is corrupt.
    let previous = physical_targets()?;
    let mut names = read_names(Path::new(NAME_FILE))?;
    let result = if physical < LEGACY_TARGETS { update_targets(Path::new(TARGET_FILE), LEGACY_TARGETS, physical, value) }
    else if physical < MAX_TARGETS { update_targets(Path::new(EXTRA_TARGET_FILE), MAX_TARGETS - LEGACY_TARGETS, physical - LEGACY_TARGETS, value) }
    else { update_targets(Path::new(MORE_TARGET_FILE), ALL_TARGETS - MAX_TARGETS, physical - MAX_TARGETS, value) };
    result?;
    if previous[physical] != value && !names[physical].name.is_empty() {
        names[physical] = TargetName::default();
        crate::storage::atomic_write(Path::new(NAME_FILE), &serde_json::to_vec(&names).map_err(io::Error::other)?)?;
    }
    Ok(())
}

pub(crate) fn move_ordered(from: usize, to: usize) -> io::Result<()> {
    let _guard = TARGET_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    if from >= ALL_TARGETS || to >= ALL_TARGETS { return Err(io::Error::other("invalid slot")); }
    let mut order = read_order(Path::new(ORDER_FILE))?;
    let physical = physical_targets()?;
    if physical[order[from]].is_empty() { return Err(io::Error::other("empty target")); }
    let item = order.remove(from);
    order.insert(to, item);
    let path = Path::new(ORDER_FILE);
    fs::DirBuilder::new().recursive(true).mode(0o700).create(path.parent().unwrap())?;
    crate::storage::atomic_write(path, &serde_json::to_vec(&order).map_err(io::Error::other)?)
}

fn read_targets(path: &Path, count: usize) -> io::Result<Vec<String>> {
    let file = match fs::File::open(path) {
        Ok(file) => file,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(vec![String::new(); count]),
        Err(e) => return Err(e),
    };
    let mut bytes = Vec::new();
    file.take(2049).read_to_end(&mut bytes)?;
    if bytes.len() > 2048 {
        return Err(io::Error::other("target file too large"));
    }
    let targets: Vec<String> = serde_json::from_slice(&bytes).map_err(io::Error::other)?;
    if targets.len() != count || !targets.iter().all(|s| s.is_empty() || valid_target(s)) {
        return Err(io::Error::other("invalid saved targets"));
    }
    Ok(targets)
}

pub(crate) fn targets(extended: bool) -> io::Result<Vec<String>> {
    let _guard = TARGET_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let mut targets = read_targets(Path::new(TARGET_FILE), LEGACY_TARGETS)?;
    if extended {
        targets.extend(read_targets(
            Path::new(EXTRA_TARGET_FILE),
            MAX_TARGETS - LEGACY_TARGETS,
        )?);
    }
    Ok(targets)
}

fn update_targets(path: &Path, count: usize, index: usize, value: &str) -> io::Result<()> {
    if index >= count || (!value.is_empty() && !valid_target(value)) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "invalid target",
        ));
    }
    let mut targets = read_targets(path, count)?;
    targets[index] = value.to_owned();
    let parent = path
        .parent()
        .ok_or_else(|| io::Error::other("missing parent"))?;
    fs::DirBuilder::new()
        .recursive(true)
        .mode(0o700)
        .create(parent)?;
    crate::storage::atomic_write(
        path,
        &serde_json::to_vec(&targets).map_err(io::Error::other)?,
    )
}

pub(crate) fn set_target(index: usize, value: &str) -> io::Result<()> {
    let _guard = TARGET_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    if index < LEGACY_TARGETS {
        update_targets(Path::new(TARGET_FILE), LEGACY_TARGETS, index, value)
    } else if index < MAX_TARGETS {
        update_targets(
            Path::new(EXTRA_TARGET_FILE),
            MAX_TARGETS - LEGACY_TARGETS,
            index - LEGACY_TARGETS,
            value,
        )
    } else {
        Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "invalid target",
        ))
    }
}

#[derive(Debug, PartialEq)]
pub(crate) struct Entry {
    pub target: String,
    pub result: Probe,
}

/// Validate every input before starting any network activity. The runner takes
/// an argument vector rather than shell text, and each command has a deadline.
pub(crate) fn probe_batch(targets: &[String]) -> Result<Vec<Entry>, &'static str> {
    probe_batch_with(targets, probe)
}

fn probe_batch_with<F>(targets: &[String], run: F) -> Result<Vec<Entry>, &'static str>
where
    F: Fn(&str) -> Probe + Sync,
{
    if targets.is_empty() || targets.len() > ALL_TARGETS {
        return Err("Choose one to 24 targets");
    }
    if !targets.iter().all(|s| valid_target(s)) {
        return Err("Invalid target");
    }
    let mut entries = Vec::with_capacity(targets.len());
    // Limit concurrent processes on this small router while testing the whole list.
    for group in targets.chunks(MAX_TARGETS) {
      entries.extend(std::thread::scope(|scope| {
        let workers: Vec<_> = group
            .iter()
            .map(|target| {
                let run = &run;
                scope.spawn(move || Entry {
                    target: target.clone(),
                    result: run(target),
                })
            })
            .collect();
        workers
            .into_iter()
            .map(|worker| worker.join().expect("latency worker panicked"))
            .collect::<Vec<_>>()
      }));
    }
    Ok(entries)
}

#[cfg(test)]
mod tests {
    use super::valid_target;

    #[test]
    fn order_defaults_to_identity_and_rejects_corrupt_data() {
        let dir = std::env::temp_dir().join(format!("openui-order-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("order.json");
        assert_eq!(super::read_order(&path).unwrap(), (0..super::ALL_TARGETS).collect::<Vec<_>>());
        let mut order: Vec<usize> = (0..super::ALL_TARGETS).collect();
        let moved = order.remove(6);
        order.insert(0, moved);
        std::fs::write(&path, serde_json::to_vec(&order).unwrap()).unwrap();
        assert_eq!(super::read_order(&path).unwrap(), order);
        order[1] = order[0];
        std::fs::write(&path, serde_json::to_vec(&order).unwrap()).unwrap();
        assert!(super::read_order(&path).is_err());
        std::fs::remove_dir_all(dir).unwrap();
    }

    #[test]
    fn validates_addresses_and_rejects_shell_and_option_syntax() {
        for good in [
            "google.com",
            "https://google.com",
            "https://[2001:db8::1]",
            "host.tailnet.ts.net",
            "100.100.100.101",
            "192.0.2.122",
            "2001:db8::1",
        ] {
            assert!(valid_target(good), "{good}");
        }
        for bad in [
            "",
            "-c",
            "http://google.com",
            "https://google.com/path",
            "https://google.com:443",
            "https://user@google.com",
            "x;reboot",
            "x y",
            "$(id)",
            "a..b",
            "-a.test",
            "test-.com",
        ] {
            assert!(!valid_target(bad), "{bad}");
        }
    }

    #[test]
    fn parses_only_valid_measured_round_trip_times() {
        assert_eq!(
            super::parse_rtt("64 bytes from 1.1.1.1: icmp_seq=1 ttl=55 time=12.3 ms\n"),
            Some(12.3)
        );
        assert_eq!(super::parse_rtt("request timed out"), None);
        assert_eq!(super::parse_rtt("time=NaN ms"), None);
        assert_eq!(super::parse_rtt("time=-3 ms"), None);
        assert_eq!(super::parse_https_time("0.123\n"), Some(123.0));
        assert_eq!(super::parse_https_time("0.000000"), None);
        assert_eq!(super::parse_https_time("nan"), None);
        use super::TailPath;
        assert_eq!(
            super::parse_tailnet_ping(
                "pong from lab (100.82.1.2) via 192.0.2.5:41641 in 82ms\n",
                "100.82.1.2"
            ),
            Some((82.0, TailPath::Direct))
        );
        assert_eq!(
            super::parse_tailnet_ping(
                "pong from lab (100.82.1.2) via DERP(hkg) in 0.12s\n",
                "100.82.1.2"
            ),
            Some((120.0, TailPath::Relay))
        );
        assert_eq!(
            super::parse_tailnet_ping(
                "pong from router (100.82.1.2) via 192.0.2.5:41641 in 82ms\n",
                "100.82.1.3"
            ),
            None
        );
        assert!(!super::tailnet_candidate("192.0.2.122"));
        assert!(super::tailnet_candidate("100.82.1.2"));
    }

    #[test]
    fn rejects_invalid_batches_before_network_activity() {
        assert!(super::probe_batch(&[]).is_err());
        assert!(super::probe_batch(&["-c".into()]).is_err());
        assert!(super::probe_batch(&vec!["localhost".into(); 25]).is_err());
        let input: Vec<String> = (0..24).map(|i| format!("host{i}.test")).collect();
        let active=std::sync::atomic::AtomicUsize::new(0);
        let max=std::sync::atomic::AtomicUsize::new(0);
        let rows = super::probe_batch_with(&input, |_| {
            use std::sync::atomic::Ordering;
            let n=active.fetch_add(1,Ordering::SeqCst)+1;max.fetch_max(n,Ordering::SeqCst);
            std::thread::sleep(std::time::Duration::from_millis(2));
            active.fetch_sub(1,Ordering::SeqCst);super::Probe::Reply(12.0)
        }).unwrap();
        assert!(max.load(std::sync::atomic::Ordering::SeqCst)<=6);
        assert_eq!(
            rows.iter().map(|r| &r.target).collect::<Vec<_>>(),
            input.iter().collect::<Vec<_>>()
        );
    }

    #[test]
    fn names_are_bounded_and_corruption_is_not_silently_overwritten() {
        let dir = std::env::temp_dir().join(format!("u60-names-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let file = dir.join("names.json");
        let mut names = super::read_names(&file).unwrap();
        assert_eq!(names.len(), super::ALL_TARGETS);
        names[2] = super::TargetName { target: "example.com".into(), name: "家庭 Router".into() };
        std::fs::write(&file, serde_json::to_vec(&names).unwrap()).unwrap();
        assert_eq!(super::read_names(&file).unwrap()[2].name, "家庭 Router");
        let mut targets = vec![String::new(); super::ALL_TARGETS];
        targets[2] = "example.com".into();
        let mut order: Vec<usize> = (0..super::ALL_TARGETS).collect();
        let moved = order.remove(2);order.insert(0, moved);
        assert_eq!(super::names_in_order(&targets, &names, &order)[0], "家庭 Router");
        targets[2] = "other.example".into();
        assert_eq!(super::names_in_order(&targets, &names, &order)[0], "");
        std::fs::write(&file, b"not json").unwrap();
        assert!(super::read_names(&file).is_err());
        assert!(super::valid_name("中文 English"));
        assert!(!super::valid_name("bad\nname"));
        assert!(!super::valid_name(&"中".repeat(25)));
        std::fs::remove_dir_all(dir).unwrap();
    }

    #[test]
    fn validates_screen_requests() {
        use super::super::ipc::{parse, Job, Request};
        assert_eq!(parse("latency names\n"), Ok(Request::LatencyNames));
        assert_eq!(parse("latency name 0 example.com e4bda0\n"), Ok(Request::LatencyName { index: 0, target: "example.com".into(), name: "你".into() }));
        assert!(parse("latency name 0 example.com 00\n").is_err());
        assert!(parse("latency name 0 example.com ff\n").is_err());
        assert!(parse("latency name 24 example.com 41\n").is_err());
        assert!(parse(&format!("latency name 0 example.com {}", "41".repeat(73))).is_err());
        assert!(matches!(
            parse("latency run google.com 100.100.100.101\n"),
            Ok(Request::Job(Job::Latency(_)))
        ));
        assert!(matches!(parse(&format!("latency run {}\n", vec!["host.test"; 24].join(" "))),Ok(Request::Job(Job::Latency(v))) if v.len()==24));
        assert!(parse(&format!("latency run {}\n", vec!["host.test"; 25].join(" "))).is_err());
        assert!(matches!(
            parse("latency results\n"),
            Ok(Request::LatencyResults)
        ));
        assert!(parse("latency run google.com;reboot\n").is_err());
        assert!(parse("latency run -c\n").is_err());
        assert!(matches!(
            parse("latency set 2 https://google.com\n"),
            Ok(Request::LatencySet { index: 2, .. })
        ));
        assert!(matches!(
            parse("latency set 1 -\n"),
            Ok(Request::LatencySet { index: 1, .. })
        ));
        assert!(matches!(
            parse("latency targets2\n"),
            Ok(Request::LatencyTargets { extended: true })
        ));
        assert!(matches!(
            parse("latency targets\n"),
            Ok(Request::LatencyTargets { extended: false })
        ));
        assert!(matches!(
            parse("latency set 5 google.com\n"),
            Ok(Request::LatencySet { index: 5, .. })
        ));
        assert!(parse("latency set 6 google.com\n").is_err());
        assert!(matches!(parse("latency targets3\n"), Ok(Request::LatencyTargetsV3)));
        assert!(matches!(parse("latency set3 23 example.com\n"), Ok(Request::LatencySetV3 { index: 23, .. })));
        assert!(matches!(parse("latency move 23 0\n"), Ok(Request::LatencyMove { from: 23, to: 0 })));
        assert!(parse("latency set3 24 example.com\n").is_err());
        assert!(parse("latency set3 0 bad;id\n").is_err());
        assert!(parse("latency move 24 0\n").is_err());
        assert!(parse("latency move 0 24\n").is_err());
        assert!(parse("latency move 1 2\nlatency move 3 4\n").is_err());
        assert!(parse("latency set 0 google.com;id\n").is_err());
        assert!(parse("latency set 0 google.com\nlatency run 1\n").is_err());
    }

    #[test]
    fn save_load_clear_and_reject_corrupt_data() {
        let dir = std::env::temp_dir().join(format!("u60-latency-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let file = dir.join("targets.json");
        let _ = std::fs::remove_file(&file);
        assert_eq!(super::read_targets(&file, 4).unwrap(), vec![""; 4]);
        super::update_targets(&file, 4, 2, "google.com").unwrap();
        assert_eq!(super::read_targets(&file, 4).unwrap()[2], "google.com");
        assert!(super::update_targets(&file, 4, 0, "x;id").is_err());
        assert_eq!(super::read_targets(&file, 4).unwrap()[2], "google.com");
        let extra = dir.join("extra.json");
        super::update_targets(&extra, 2, 1, "new.example").unwrap();
        assert_eq!(super::read_targets(&extra, 2).unwrap()[1], "new.example");
        assert_eq!(super::read_targets(&file, 4).unwrap()[2], "google.com");
        super::update_targets(&file, 4, 2, "").unwrap();
        assert_eq!(super::read_targets(&file, 4).unwrap(), vec![""; 4]);
        std::fs::write(&file, b"[\"bad;value\"]").unwrap();
        assert!(super::read_targets(&file, 4).is_err());
        assert!(super::update_targets(&file, 4, 0, "localhost").is_err());
        std::fs::remove_dir_all(dir).unwrap();
    }
}
