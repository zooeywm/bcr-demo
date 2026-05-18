# BCR Demo

这是一个最小可闭环的 BCR 演示工程，当前链路如下：

`服务端浏览器插件 -> bcr-agent -> bcr-client -> bcr-agent -> 服务端浏览器当前活动标签页`

这版的目标不是“把浏览器完整搬到客户端”，而是：

- 服务端浏览器负责保留真实登录态
- 插件自动把当前活动标签页的 `url/tabs/auth snapshot` 同步给 `bcr-agent`
- `bcr-agent` 自动把这份状态推给 `bcr-client`
- `bcr-client` 负责主导后续 URL 和跳转
- `bcr-client` 的 URL 变化会回推给 `bcr-agent`
- 服务端浏览器当前活动标签页自动跟随客户端 URL
- 服务端网页内容被插件直接白屏覆盖，只保留浏览器壳和登录上下文

## 目录结构

```text
.
├── CMakeLists.txt
├── src
│   ├── bcragentservice.*
│   ├── bcrserver.*
│   ├── agentmain.cpp
│   ├── clientmain.cpp
│   └── mainwindow.*
└── remote
    └── chrome-extension
        ├── background.js
        ├── content.js
        ├── manifest.json
        ├── popup.css
        ├── popup.html
        └── popup.js
```

## 构建

目标环境是 Qt `6.8.3`。

```bash
cmake -S . -B build
cmake --build build -j
```

会生成两个二进制：

- `build/bcr-client`：Qt WebEngine 客户端
- `build/bcr-agent`：运行在服务端浏览器机器上的状态中枢

## 启动顺序

### 1. 在服务端浏览器机器启动 `bcr-agent`

因为浏览器插件要访问本机 `127.0.0.1`，而远端 `bcr-client` 也要访问这台机器的 agent，所以最稳的启动方式是让 agent 监听所有网卡：

Linux/macOS：

```bash
./build/bcr-agent \
  --listen-address 0.0.0.0 \
  --listen-port 45455 \
  --client-host 192.168.1.10 \
  --client-port 45454
```

Windows PowerShell：

```powershell
.\build\bcr-agent.exe `
  --listen-address 0.0.0.0 `
  --listen-port 45455 `
  --client-host 192.168.1.10 `
  --client-port 45454
```

说明：

- `--listen-address 0.0.0.0`：这样浏览器本机能通过 `127.0.0.1:45455` 访问 agent，远端 `bcr-client` 也能通过这台机器的实际 IP 访问 agent
- `--listen-port 45455`：插件默认写死访问这个端口
- `--client-host` / `--client-port`：指向 `bcr-client` 的监听地址

### 2. 在客户端机器启动 `bcr-client`

```bash
./build/bcr-client \
  --address 0.0.0.0 \
  --port 45454 \
  --agent-base-url http://192.168.1.20:45455
```

说明：

- `--address` / `--port`：`bcr-client` 自己的 TCP 监听地址
- `--agent-base-url`：指向服务端浏览器机器上的 `bcr-agent`
- 这里的 `192.168.1.20` 要换成运行 `bcr-agent` 那台机器的局域网 IP

### 3. 在服务端浏览器加载扩展

Chrome 和 Edge 都按已解压扩展加载：

1. 打开扩展页
2. 开启 `开发者模式`
3. 选择 `加载已解压的扩展程序`
4. 选中目录：

```text
remote/chrome-extension
```

这次扩展是后台自动工作的，不需要手动点 popup。

如果你改过扩展代码，要在扩展页点一次“重新加载”。

### 4. 打开服务端浏览器页面

只要服务端浏览器打开普通 `http/https` 页面，插件后台就会自动：

- 采集当前活动标签页的 URL
- 采集当前活动标签页的 Cookie / `localStorage` / `sessionStorage` / `userAgent`
- 把 `tabs` 元数据和鉴权快照发给 `bcr-agent`
- `bcr-agent` 自动把这份状态推给 `bcr-client`

所以这一版不再依赖手动点击扩展弹窗。

## 当前行为

### 浏览器端

- 当前活动标签页网页内容会被白色覆盖层遮住
- 浏览器地址栏、标签栏、菜单栏不会被清空
- 插件会持续把当前活动标签页状态同步给 `bcr-agent`
- 当前活动标签页会轮询 `desired-state`，并自动跟随客户端 URL

### 客户端

- 收到服务端浏览器状态后，会自动打开对应 URL
- 会尽量注入服务端浏览器当前标签页的 Cookie / `localStorage` / `sessionStorage` / `userAgent`
- 用户在 `bcr-client` 内继续点击、跳转后，会把新 URL 回推给 `bcr-agent`
- 服务端浏览器当前活动标签页随后会跟随这个 URL

## 会同步什么

当前自动同步的内容：

- 当前活动标签页 URL
- 当前窗口 tabs 元数据
- 当前活动标签页可见 Cookie
- 当前活动标签页 `localStorage`
- 当前活动标签页 `sessionStorage`
- 当前活动标签页 `userAgent`

## 不会同步什么

这版不会完整复制下面这些东西：

- 浏览器壳本身
- 浏览器插件自己的内部状态
- 系统级单点登录
- 客户端证书
- 密码管理器自动填充
- 完整多标签页拓扑控制

注意最后一点：

- 当前已经会同步 `tabs` 元数据
- 但真正自动控制的只有“当前活动标签页 URL”
- 不会自动在服务端创建/关闭/重排整套 tab 结构

## HTTP 接口

### 插件 -> agent

```http
POST /extension/state
```

发送当前活动标签页 URL、tabs 元数据和 auth snapshot。

### client -> agent

```http
POST /client/state
```

发送客户端当前 URL 和 tabs 元数据，作为服务端浏览器活动标签页的期望状态。

### 浏览器活动标签页 -> agent

```http
GET /desired-state
```

当前活动标签页轮询这个接口，拿到客户端最近一次同步过来的目标 URL。

### 诊断

```http
GET /health
```

会返回：

- agent 监听地址
- client 地址
- 最近一次 `latestDesiredState`
- 最近一次 `latestExtensionState`

## 常见问题

### 1. 服务端浏览器打开了，但客户端没自动跳

优先检查：

- `bcr-agent` 是否已经启动
- 服务端浏览器扩展是否已经重新加载到最新版本
- 当前页面是否是 `http/https`
- `bcr-agent` 的 `--client-host` / `--client-port` 是否确实指向 `bcr-client`

### 2. 客户端能打开，但服务端浏览器不跟随客户端跳转

优先检查：

- `bcr-client` 启动时是否带了 `--agent-base-url`
- `--agent-base-url` 是否写成了服务端 `bcr-agent` 的真实局域网地址
- 服务端浏览器当前活动标签页是否还是普通 `http/https` 页面

### 3. 页面打开了，但仍然掉到登录页

这说明站点登录态不只依赖 Cookie / Storage。优先检查：

- 是否还依赖额外 SSO 跳转
- 登录态是否绑定了别的域名
- 是否依赖浏览器插件、客户端证书或系统身份

### 4. 为什么服务端浏览器没有彻底“全空白”

因为这版只会把网页内容白屏覆盖，不会修改浏览器地址栏、标签栏和菜单栏。这是当前方案的明确边界。
