//! Runtime policy for an explicitly selected Tailscale site subnet.
//!
//! The unreachable rule follows Tailscale's table-52 lookup but precedes the
//! proxy tables. The agent owns only its recorded rule and named chain entries;
//! no stock routes or boot hooks are changed.
use crate::{clash, egress::run, storage, tailscale, wireguard};
use std::{fs, path::Path};

const OWNER: &str = "/data/local/tmp/openui-tailscale/site-route-owner";
const PRIORITY: &str = "10418";
const COMMENT: &str = "openui-ts-site-route";

fn valid(cidr: &str) -> bool {
    tailscale::private_lan_cidr(cidr).as_deref() == Some(cidr)
}

fn current_rules(text: &str) -> Vec<&str> {
    text.lines()
        .filter(|l| l.trim_start().starts_with("10418:"))
        .collect()
}

fn expected_rule(line: &str, cidr: &str) -> bool {
    let words: Vec<_> = line.split_ascii_whitespace().collect();
    words == ["10418:", "from", "all", "to", cidr, "unreachable"]
}

fn owner(path: &Path) -> Result<Option<String>, String> {
    match fs::read_to_string(path) {
        Ok(s) if valid(s.trim_end()) && s.trim_end().len() + 1 == s.len() && s.ends_with('\n') => {
            Ok(Some(s.trim_end().to_owned()))
        }
        Ok(_) => Err("Invalid saved site route ownership".into()),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(_) => Err("Cannot read site route ownership".into()),
    }
}

fn rules() -> Result<String, String> {
    run("ip", &["-4", "rule", "show"])
}

fn bypass(cidr: &str, add: bool, backends: &[(&str, bool)]) -> Result<(), String> {
    for (prefix, owned) in backends {
        if !owned {
            continue;
        }
        let chain = format!("{prefix}_MARK");
        if run("iptables", &["-w", "2", "-t", "mangle", "-S", &chain]).is_err() {
            continue;
        }
        let check = [
            "-w",
            "2",
            "-t",
            "mangle",
            "-C",
            &chain,
            "-d",
            cidr,
            "-m",
            "comment",
            "--comment",
            COMMENT,
            "-j",
            "RETURN",
        ];
        let present = run("iptables", &check).is_ok();
        if add && !present {
            run(
                "iptables",
                &[
                    "-w",
                    "2",
                    "-t",
                    "mangle",
                    "-I",
                    &chain,
                    "2",
                    "-d",
                    cidr,
                    "-m",
                    "comment",
                    "--comment",
                    COMMENT,
                    "-j",
                    "RETURN",
                ],
            )?;
        } else if !add && present {
            run(
                "iptables",
                &[
                    "-w",
                    "2",
                    "-t",
                    "mangle",
                    "-D",
                    &chain,
                    "-d",
                    cidr,
                    "-m",
                    "comment",
                    "--comment",
                    COMMENT,
                    "-j",
                    "RETURN",
                ],
            )?;
        }
    }
    Ok(())
}

fn mss(cidr: &str, add: bool) -> Result<(), String> {
    for (direction, from, to) in [
        ("-d", "br-lan", "tailscale0"),
        ("-s", "tailscale0", "br-lan"),
    ] {
        let suffix = [
            "FORWARD",
            direction,
            cidr,
            "-i",
            from,
            "-o",
            to,
            "-p",
            "tcp",
            "-m",
            "tcp",
            "--tcp-flags",
            "SYN,RST",
            "SYN",
            "-m",
            "comment",
            "--comment",
            COMMENT,
            "-j",
            "TCPMSS",
            "--clamp-mss-to-pmtu",
        ];
        let mut args = vec!["-w", "2", "-t", "mangle", "-C"];
        args.extend(suffix);
        let present = run("iptables", &args).is_ok();
        if add && !present {
            args[4] = "-A";
            run("iptables", &args)?;
        } else if !add && present {
            args[4] = "-D";
            run("iptables", &args)?;
        }
    }
    Ok(())
}

/// Must be called under the global transition lock. No shell interpolation.
pub(crate) fn reconcile(wanted: Option<&str>) -> Result<(), String> {
    let backends = [
        (clash::EGRESS.prefix, clash::EGRESS.owned()),
        (wireguard::EGRESS.prefix, wireguard::EGRESS.owned()),
    ];
    reconcile_inner(wanted, Path::new(OWNER), &backends)
}

fn reconcile_inner(
    wanted: Option<&str>,
    owner_path: &Path,
    backends: &[(&str, bool)],
) -> Result<(), String> {
    if wanted.is_some_and(|s| !valid(s)) {
        return Err("Invalid site subnet".into());
    }
    let mut owned = owner(owner_path)?;
    if owned.as_deref() != wanted {
        if let Some(old) = owned.as_deref() {
            bypass(old, false, backends)?;
            mss(old, false)?;
            let text = rules()?;
            let matching = current_rules(&text);
            if matching.len() > 1 || matching.iter().any(|line| !expected_rule(line, old)) {
                return Err("Site route priority is in use by another rule".into());
            }
            if !matching.is_empty() {
                run(
                    "ip",
                    &[
                        "-4",
                        "rule",
                        "del",
                        "priority",
                        PRIORITY,
                        "to",
                        old,
                        "unreachable",
                    ],
                )?;
            }
            storage::durable_remove(owner_path)
                .map_err(|_| "Cannot remove site route ownership")?;
            owned = None;
        }
    }
    if let Some(cidr) = wanted {
        let text = rules()?;
        let existing = current_rules(&text);
        if owned.is_none() {
            if !existing.is_empty() {
                return Err("Site route priority is already in use".into());
            }
            fs::create_dir_all(owner_path.parent().unwrap())
                .map_err(|_| "Cannot create site route directory")?;
            storage::atomic_write(owner_path, format!("{cidr}\n").as_bytes())
                .map_err(|_| "Cannot record site route ownership")?;
        } else if existing.len() > 1 || existing.iter().any(|line| !expected_rule(line, cidr)) {
            return Err("Site route priority is in use by another rule".into());
        }
        if existing.is_empty() {
            run(
                "ip",
                &[
                    "-4",
                    "rule",
                    "add",
                    "priority",
                    PRIORITY,
                    "to",
                    cidr,
                    "unreachable",
                ],
            )?;
        }
        mss(cidr, true)?;
        bypass(cidr, true, backends)?;
    }
    Ok(())
}

/// Rule fragment for a freshly built egress chain. Input is canonicalized earlier.
pub(crate) fn egress_bypass(prefix: &str, cidr: &str) -> String {
    assert!(valid(cidr));
    format!("iptables -w 2 -t mangle -A {prefix}_MARK -d {cidr} -m comment --comment {COMMENT} -j RETURN\n")
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn recognizes_only_its_precise_guard() {
        let r = "10418:\tfrom all to 10.203.44.0/24 unreachable";
        assert!(expected_rule(r, "10.203.44.0/24"));
        assert!(!expected_rule(r, "10.203.45.0/24"));
        assert!(!expected_rule(
            "10418: from all to 10.203.44.0/24 lookup main",
            "10.203.44.0/24"
        ));
        assert_eq!(
            current_rules(
                "0: from all lookup local\n10418: from all to 10.203.44.0/24 unreachable\n"
            )
            .len(),
            1
        );
    }
    #[test]
    fn bypass_applies_to_dns_as_well_as_other_protocols() {
        let rule = egress_bypass("OUCL", "10.203.44.0/24");
        assert!(rule.contains("-d 10.203.44.0/24"));
        assert!(rule.ends_with("-j RETURN\n"));
        assert!(!rule.contains("--dport"));
    }

    /// Explicit opt-in only: the namespace check happens before any network writes.
    #[test]
    #[cfg(target_os = "linux")]
    fn isolated_kernel_policy_lifecycle() {
        if std::env::var_os("OPENUI_SITE_NETNS_TEST").as_deref() != Some(std::ffi::OsStr::new("1"))
        {
            return;
        }
        let before = fs::read_link("/proc/thread-self/ns/net").unwrap();
        // SAFETY: unshare changes only this test thread's network namespace.
        assert_eq!(
            unsafe { libc::unshare(libc::CLONE_NEWNET) },
            0,
            "cannot isolate network namespace: {}",
            std::io::Error::last_os_error()
        );
        let after = fs::read_link("/proc/thread-self/ns/net").unwrap();
        assert_ne!(
            before, after,
            "refusing to modify the original network namespace"
        );

        let temp = std::env::temp_dir().join(format!("openui-site-netns-{}", std::process::id()));
        fs::create_dir(&temp).unwrap();
        struct Cleanup(std::path::PathBuf);
        impl Drop for Cleanup {
            fn drop(&mut self) {
                let _ = fs::remove_dir_all(&self.0);
            }
        }
        let _cleanup = Cleanup(temp.clone());
        let marker = temp.join("owner");
        let backends = [("OUCL", true), ("OUWG", false)];
        let net = "10.203.44.0/24";
        run("ip", &["link", "set", "lo", "up"]).unwrap();
        run(
            "ip",
            &["-4", "rule", "add", "priority", "5270", "lookup", "52"],
        )
        .unwrap();
        run("iptables", &["-t", "mangle", "-N", "OUCL_MARK"]).unwrap();
        run(
            "iptables",
            &[
                "-t",
                "mangle",
                "-A",
                "OUCL_MARK",
                "!",
                "-i",
                "br-lan",
                "-j",
                "RETURN",
            ],
        )
        .unwrap();
        run(
            "iptables",
            &[
                "-t",
                "mangle",
                "-A",
                "OUCL_MARK",
                "-j",
                "MARK",
                "--set-xmark",
                "0x20000000/0x20000000",
            ],
        )
        .unwrap();

        reconcile_inner(Some(net), &marker, &backends).unwrap();
        reconcile_inner(Some(net), &marker, &backends).unwrap();
        let fw = run("iptables", &["-t", "mangle", "-S", "OUCL_MARK"]).unwrap();
        assert_eq!(fw.matches(COMMENT).count(), 1);
        assert!(fw.find(COMMENT).unwrap() < fw.find("--set-xmark").unwrap());
        let mss_rules = run("iptables", &["-t", "mangle", "-S", "FORWARD"]).unwrap();
        assert_eq!(mss_rules.matches(COMMENT).count(), 2);
        assert!(run("ip", &["-4", "route", "get", "10.203.44.122"]).is_err());
        run(
            "ip",
            &["-4", "route", "add", net, "dev", "lo", "table", "52"],
        )
        .unwrap();
        assert!(run("ip", &["-4", "route", "get", "10.203.44.122"])
            .unwrap()
            .contains("dev lo"));
        run(
            "ip",
            &["-4", "route", "del", net, "dev", "lo", "table", "52"],
        )
        .unwrap();
        assert!(run("ip", &["-4", "route", "get", "10.203.44.122"]).is_err());
        reconcile_inner(None, &marker, &backends).unwrap();
        assert!(!marker.exists());
        assert!(!run("iptables", &["-t", "mangle", "-S", "FORWARD"])
            .unwrap()
            .contains(COMMENT));
        assert!(!run("iptables", &["-t", "mangle", "-S", "OUCL_MARK"])
            .unwrap()
            .contains(COMMENT));
        assert!(current_rules(&rules().unwrap()).is_empty());

        run(
            "ip",
            &[
                "-4",
                "rule",
                "add",
                "priority",
                PRIORITY,
                "to",
                "10.0.0.0/8",
                "unreachable",
            ],
        )
        .unwrap();
        assert!(reconcile_inner(Some(net), &marker, &backends).is_err());
        assert!(!marker.exists());
        assert!(rules().unwrap().contains("10.0.0.0/8"));
    }
}
