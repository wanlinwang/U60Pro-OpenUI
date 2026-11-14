# Tailscale 站点子网互联

站点互联默认关闭，需要用户自行配置。此功能不刷写原厂固件、不修改分区；组件事务和快照不能等同于完整的原厂固件备份。

在「代理 → Tailscale → 站点互联」中填写已由对端公布并批准的私有 IPv4 子网，例如 `10.203.44.0/24`。需要已经开启「远程管理（子网路由）」，且不能选择 Tailscale 出口节点。Tailscale 实际会接收本 Tailnet 上的所有已批准子网路由，并非只接收输入的网段；开启前应检查全部已批准网段及本机直连网络是否有重叠。当前只在启用时检查重叠，网络环境发生变化后需要重新检查。

指定的站点子网流量在代理标记前绕过 Clash / WireGuard 的上网代理规则，并由 Tailscale 的路由表转发。IPv4 站点路由缺失时，运行中的代理策略还会配置一条不可达后备规则，以减少退回普通网络的机会。这不是开机早期、防火墙重载、第三方策略路由或守护进程故障期间的防泄漏保证。站点路由范围之外的流量继续由原有的设备选择和代理配置决定；DNS、IPv6 和代理订阅的实际行为需要分别验证。Tailscale 优先尝试直连，也可能使用中继。

运行时资源：IPv4 规则优先级 `10418`，`OUCL_MARK` / `OUWG_MARK` 中注释为 `openui-ts-site-route` 的精确绕过规则，以及 mangle `FORWARD` 中同一注释标识的 TCP MSS 规则。所有权记录位于 `/data/local/tmp/openui-tailscale/site-route-owner`；不清空主路由表或原厂防火墙链。配置持久保存在现有 Tailscale `config.json`，字段为 `site_cidr`；禁用后该字段省略。

**旧版 Agent 不认识启用时的 `site_cidr` 字段。** 回退前先在当前版本中关闭站点互联并确认规则已清理，再使用安装器打印的同一设备事务恢复命令。如果当前版本无法操作，应先恢复安装前的 Tailscale 配置备份并核对、清理属于本功能的运行时规则，然后恢复 Agent；不能仅覆盖旧二进制。不要删除 Tailscale 身份状态或原厂恢复备份。

已做的开发验证：前端测试与构建；在 U60 的独立网络命名空间中测试策略路由前后顺序、目标路由缺失、站点绕过、MSS、重复调用、资源冲突和清理；Tailscale 相关纯逻辑测试。这些测试不能代替整机重启、断电恢复、实际代理配置、IPv6 或实际出口的验收。部署后应使用实际客户端分别测试两个方向、观察 `tailscale status` 的直连/中继状态，并在合适时间单独测试重启。

参考：[Tailscale 站点互联](https://tailscale.com/docs/features/site-to-site)、[路由注入](https://tailscale.com/docs/reference/route-injection)、[连接类型](https://tailscale.com/docs/reference/connection-types)。
