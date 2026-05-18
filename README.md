# BCR Demo

这是一个最小可闭环的 BCR 演示工程，链路如下：

`Chrome/Edge 扩展 -> 手动启动的 bcr-agent (本地 HTTP) -> TCP -> Qt WebEngine 客户端`

它不是生产级 BCR 实现，而是一个便于联调和演示的 PoC，重点覆盖：

- 浏览器扩展读取当前标签页 URL
- 手动运行的 `bcr-agent` 在本机监听 `http://127.0.0.1:45455`
- `bcr-agent` 把动作转发给远端 `bcr-client`
- `bcr-client` 用 `QWebEngineView` 加载页面，并执行整屏/退出整屏

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
        ├── manifest.json
        ├── popup.css
        ├── popup.html
        └── popup.js
```

## 构建

环境目标是 Qt `6.8.3`。

```bash
cmake -S . -B build
cmake --build build -j
```

会生成两个二进制：

- `build/bcr-client`：Qt WebEngine 客户端
- `build/bcr-agent`：手动运行的本地 HTTP 转发器

## 启动顺序

### 1. 启动 `bcr-client`

在展示机或被控机上运行：

```bash
./build/bcr-client --address 0.0.0.0 --port 45454
```

说明：

- `--address` 是 `bcr-client` 监听地址，远程浏览器机器需要能访问到它
- `--port` 是 `bcr-client` 监听端口，默认 `45454`

### 2. 在浏览器机器上手动启动 `bcr-agent`

`bcr-agent` 默认监听本机：

- `http://127.0.0.1:45455/command`

你需要把它指向 `bcr-client` 的 IP 和端口。

Linux/macOS 示例：

```bash
./build/bcr-agent --client-host 192.168.1.10 --client-port 45454
```

Windows PowerShell 示例：

```powershell
.\build\bcr-agent.exe --client-host 192.168.1.10 --client-port 45454
```

这里的 `192.168.1.10` 要换成运行 `bcr-client` 那台机器的 IP。

可选参数：

- `--listen-address 127.0.0.1`：本机 HTTP 监听地址，默认 `127.0.0.1`
- `--listen-port 45455`：本机 HTTP 监听端口，默认 `45455`
- `--timeout-ms 3000`：转发到 `bcr-client` 的超时

注意：

- 扩展当前写死访问 `http://127.0.0.1:45455`
- 如果你改了 `--listen-port` 或 `--listen-address`，就必须同步修改 [remote/chrome-extension/popup.js](/home/zooeywm/repos/qt/bcr-demo/remote/chrome-extension/popup.js:1) 里的 `agentEndpoint`

### 3. 加载浏览器扩展

Chrome 和 Edge 都按已解压扩展加载即可，不再需要 Native Messaging 注册表或安装脚本。

1. 打开浏览器扩展页
2. 开启 `开发者模式`
3. 选择 `加载已解压的扩展程序`
4. 选中目录：

```text
remote/chrome-extension
```

如果你改过 `manifest.json` 或 `popup.js`，要在扩展页点一次“重新加载”。

## 如何使用

启动好 `bcr-client` 和 `bcr-agent` 后，点击扩展弹窗里的按钮：

- `在 Qt 中打开当前页`：把当前标签页 URL 发给 `bcr-client`
- `打开当前页并整屏`：打开 URL 后立即整屏
- `仅切到整屏`：不改 URL，只切整屏
- `退出整屏`：让 `bcr-client` 退出整屏
- `测试连接`：发送 `ping`

本地在 `bcr-client` 窗口里按 `Esc` 也可以退出整屏。

## HTTP 协议

扩展向 `bcr-agent` 发送：

```http
POST /command
```

请求体 JSON：

```json
{"action":"openUrl","url":"https://www.qt.io"}
{"action":"openUrlAndFullscreen","url":"https://www.qt.io"}
{"action":"enterFullscreen"}
{"action":"exitFullscreen"}
{"action":"ping"}
```

`bcr-agent` 再把它转成一行一个 JSON 发给 `bcr-client`：

```json
{"type":"openUrl","url":"https://www.qt.io"}
{"type":"openUrlAndFullscreen","url":"https://www.qt.io"}
{"type":"enterFullscreen"}
{"type":"exitFullscreen"}
{"type":"ping"}
```

`bcr-agent` 还提供一个简单健康检查：

```http
GET /health
```

## 常见问题

### 1. 扩展里提示“无法连接本地 bcr-agent”

说明扩展没连上 `http://127.0.0.1:45455`。检查：

- `bcr-agent` 是否已经手动启动
- `bcr-agent` 是否监听在 `127.0.0.1:45455`
- 扩展是否已经重新加载到最新版本

### 2. 扩展里提示连接 `bcr-client` 失败

说明 `bcr-agent` 已启动，但它连不到远端 `bcr-client`。检查：

- `--client-host` 是否写成了 `bcr-client` 机器 IP
- `--client-port` 是否和 `bcr-client` 的监听端口一致
- `bcr-client` 是否已经启动
- 防火墙是否允许访问该 TCP 端口

### 3. 当前标签页无法发送

扩展当前只允许 `http://` 和 `https://` 页面；`chrome://`、`edge://`、扩展页、空白页不会发送。
