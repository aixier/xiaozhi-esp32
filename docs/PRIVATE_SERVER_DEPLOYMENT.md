# 小智 AI 私有服务器部署指南

本文档详细说明如何将小智 ESP32 设备接入私有服务器，包括凭证获取、回调设置、配置下发等完整流程。

---

## 目录

1. [部署概述](#1-部署概述)
2. [OTA 配置接口实现](#2-ota-配置接口实现)
3. [凭证下发机制](#3-凭证下发机制)
4. [激活流程控制](#4-激活流程控制)
5. [WebSocket 服务实现](#5-websocket-服务实现)
6. [MQTT + UDP 服务实现](#6-mqtt--udp-服务实现)
7. [设备固件配置](#7-设备固件配置)
8. [完整示例代码](#8-完整示例代码)

---

## 1. 部署概述

### 1.1 架构图

```
┌─────────────────┐         ┌─────────────────────────────────────┐
│   ESP32 设备     │         │           私有服务器                 │
│                 │         │                                     │
│  ┌───────────┐  │  HTTP   │  ┌─────────────┐                   │
│  │ OTA 模块  │──┼────────►│  │  OTA API    │ 返回配置凭证       │
│  └───────────┘  │         │  └─────────────┘                   │
│                 │         │         │                           │
│  ┌───────────┐  │  WSS    │  ┌──────▼──────┐    ┌───────────┐  │
│  │ Protocol  │◄─┼────────►│  │ WebSocket   │◄──►│  ASR/TTS  │  │
│  │  模块     │  │         │  │   Server    │    │   服务    │  │
│  └───────────┘  │         │  └─────────────┘    └───────────┘  │
│                 │         │         │                           │
│  ┌───────────┐  │         │  ┌──────▼──────┐    ┌───────────┐  │
│  │ MCP Server│◄─┼────────►│  │ LLM 路由    │◄──►│  大模型   │  │
│  └───────────┘  │         │  └─────────────┘    │ Qwen/GPT  │  │
│                 │         │                     └───────────┘  │
└─────────────────┘         └─────────────────────────────────────┘
```

### 1.2 通信流程

```
设备启动
    │
    ▼
┌─────────────────────────────────────┐
│ 1. 请求 OTA 接口获取配置            │
│    GET/POST https://your-server/ota │
└─────────────────┬───────────────────┘
                  │
                  ▼
┌─────────────────────────────────────┐
│ 2. 服务端返回凭证和连接配置          │
│    - WebSocket URL + Token          │
│    - 或 MQTT 配置 + UDP 密钥        │
└─────────────────┬───────────────────┘
                  │
                  ▼
┌─────────────────────────────────────┐
│ 3. 设备使用凭证连接实时通信服务      │
│    WebSocket 或 MQTT+UDP            │
└─────────────────┬───────────────────┘
                  │
                  ▼
┌─────────────────────────────────────┐
│ 4. 握手成功，开始语音交互            │
└─────────────────────────────────────┘
```

---

## 2. OTA 配置接口实现

### 2.1 接口规范

**请求**

```
POST /ota
Content-Type: application/json
```

**请求头 (设备发送)**

| Header | 说明 | 示例 |
|--------|------|------|
| `Device-Id` | 设备 MAC 地址 | `AA:BB:CC:DD:EE:FF` |
| `Client-Id` | 设备 UUID | `550e8400-e29b-41d4-a716-446655440000` |
| `User-Agent` | 固件版本 | `zhengchen-eye/1.8.5` |
| `Accept-Language` | 语言 | `zh-CN` |
| `Activation-Version` | 激活版本 | `1` (无序列号) 或 `2` (有序列号) |
| `Serial-Number` | 序列号 (可选) | `SN123456` |

**请求体 (设备发送)**

```json
{
    "board": "zhengchen-eye",
    "chip": "esp32s3",
    "application": {
        "name": "xiaozhi",
        "version": "1.8.5"
    },
    "flash_size": 16777216,
    "minimum_free_heap_size": 1234567,
    "mac_address": "AA:BB:CC:DD:EE:FF"
}
```

### 2.2 响应格式

**最小响应 (无激活，WebSocket 模式)**

```json
{
    "websocket": {
        "url": "wss://your-server.com/xiaozhi/ws",
        "token": "Bearer eyJhbGciOiJIUzI1NiIs...",
        "version": 3
    },
    "server_time": {
        "timestamp": 1702900000000,
        "timezone_offset": 480
    }
}
```

**完整响应 (包含所有可选字段)**

```json
{
    "firmware": {
        "version": "1.8.6",
        "url": "https://your-server.com/firmware/v1.8.6.bin"
    },
    "websocket": {
        "url": "wss://your-server.com/xiaozhi/ws",
        "token": "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
        "version": 3
    },
    "mqtt": {
        "endpoint": "mqtt.your-server.com:8883",
        "client_id": "device_AABBCCDDEEFF",
        "username": "xiaozhi_device",
        "password": "mqtt_password_here",
        "publish_topic": "xiaozhi/AABBCCDDEEFF/up",
        "subscribe_topic": "xiaozhi/AABBCCDDEEFF/down",
        "keepalive": 240
    },
    "server_time": {
        "timestamp": 1702900000000,
        "timezone_offset": 480
    }
}
```

### 2.3 字段说明

#### firmware (可选)

| 字段 | 类型 | 说明 |
|------|------|------|
| `version` | string | 新固件版本号 |
| `url` | string | 固件下载地址 |

设备会比较版本号，如果新版本更高则自动升级。

#### websocket (WebSocket 模式必需)

| 字段 | 类型 | 说明 |
|------|------|------|
| `url` | string | WebSocket 连接地址 |
| `token` | string | 认证 Token (Bearer 或自定义格式) |
| `version` | int | 协议版本 (推荐 3) |

#### mqtt (MQTT 模式必需)

| 字段 | 类型 | 说明 |
|------|------|------|
| `endpoint` | string | MQTT 服务器地址:端口 |
| `client_id` | string | MQTT 客户端 ID |
| `username` | string | MQTT 用户名 |
| `password` | string | MQTT 密码 |
| `publish_topic` | string | 设备发布主题 |
| `subscribe_topic` | string | 设备订阅主题 (在服务端处理) |
| `keepalive` | int | 心跳间隔 (秒) |

#### server_time (推荐)

| 字段 | 类型 | 说明 |
|------|------|------|
| `timestamp` | number | 服务器时间戳 (毫秒) |
| `timezone_offset` | int | 时区偏移 (分钟，如 +8 区 = 480) |

---

## 3. 凭证下发机制

### 3.1 Token 生成策略

**方式1: JWT Token (推荐)**

```python
import jwt
import time

def generate_device_token(device_id: str, secret: str) -> str:
    payload = {
        "device_id": device_id,
        "iat": int(time.time()),
        "exp": int(time.time()) + 86400 * 30,  # 30天有效期
        "scope": ["audio", "mcp"]
    }
    return jwt.encode(payload, secret, algorithm="HS256")
```

**方式2: 简单 Token**

```python
import hashlib
import time

def generate_simple_token(device_id: str, secret: str) -> str:
    timestamp = str(int(time.time()))
    data = f"{device_id}:{timestamp}:{secret}"
    return hashlib.sha256(data.encode()).hexdigest()
```

### 3.2 设备识别

设备通过以下信息标识自己：

```python
# 从请求头获取设备信息
device_id = request.headers.get("Device-Id")      # MAC 地址
client_id = request.headers.get("Client-Id")      # UUID
user_agent = request.headers.get("User-Agent")    # 固件版本
serial_number = request.headers.get("Serial-Number")  # 序列号 (可选)
```

### 3.3 设备注册与绑定

**首次连接处理流程:**

```python
def handle_ota_request(request):
    device_id = request.headers.get("Device-Id")

    # 查询设备是否已注册
    device = db.get_device(device_id)

    if device is None:
        # 新设备，自动注册
        device = db.create_device(
            device_id=device_id,
            client_id=request.headers.get("Client-Id"),
            firmware_version=parse_version(request.headers.get("User-Agent")),
            registered_at=datetime.now()
        )

    # 生成凭证
    token = generate_device_token(device_id, SECRET_KEY)

    # 返回配置
    return {
        "websocket": {
            "url": f"wss://your-server.com/ws",
            "token": f"Bearer {token}",
            "version": 3
        },
        "server_time": {
            "timestamp": int(time.time() * 1000),
            "timezone_offset": 480
        }
    }
```

---

## 4. 激活流程控制

### 4.1 跳过激活 (推荐)

**不返回 `activation` 字段即可跳过激活:**

```json
{
    "websocket": {
        "url": "wss://your-server.com/ws",
        "token": "Bearer xxx"
    }
}
```

设备收到此响应后直接进入正常工作模式。

### 4.2 启用激活码验证

如需用户输入激活码绑定设备：

**OTA 响应:**

```json
{
    "activation": {
        "message": "请在控制台输入激活码",
        "code": "ABC123",
        "timeout_ms": 30000
    }
}
```

**设备行为:**
1. 显示激活码 `ABC123` 在屏幕上
2. 语音播报激活码
3. 循环调用 `/ota/activate` 接口等待确认

**激活确认接口:**

```
POST /ota/activate

请求体: {}  (无序列号设备)

响应:
- 200: 激活成功，设备继续请求 OTA 获取配置
- 202: 等待中，用户尚未确认
- 4xx: 激活失败
```

### 4.3 HMAC 硬件验证 (仅工厂设备)

仅适用于 eFuse 中烧录了序列号和 HMAC 密钥的设备：

**OTA 响应:**

```json
{
    "activation": {
        "challenge": "random-challenge-string-32bytes"
    }
}
```

**设备计算 HMAC 并提交:**

```
POST /ota/activate

请求体:
{
    "algorithm": "hmac-sha256",
    "serial_number": "SN123456",
    "challenge": "random-challenge-string-32bytes",
    "hmac": "64位十六进制HMAC值"
}
```

**服务端验证:**

```python
import hmac
import hashlib

def verify_device_hmac(serial_number: str, challenge: str,
                       device_hmac: str, device_secret: str) -> bool:
    expected = hmac.new(
        device_secret.encode(),
        challenge.encode(),
        hashlib.sha256
    ).hexdigest()
    return hmac.compare_digest(expected, device_hmac)
```

---

## 5. WebSocket 服务实现

### 5.1 连接验证

**设备连接时的请求头:**

```
GET /ws HTTP/1.1
Host: your-server.com
Upgrade: websocket
Authorization: Bearer eyJhbGciOiJIUzI1NiIs...
Protocol-Version: 3
Device-Id: AA:BB:CC:DD:EE:FF
Client-Id: 550e8400-e29b-41d4-a716-446655440000
```

**服务端验证:**

```python
async def websocket_handler(websocket, path):
    # 验证 Token
    token = websocket.request_headers.get("Authorization", "").replace("Bearer ", "")
    try:
        payload = jwt.decode(token, SECRET_KEY, algorithms=["HS256"])
        device_id = payload["device_id"]
    except jwt.InvalidTokenError:
        await websocket.close(1008, "Invalid token")
        return

    # 验证 Device-Id 匹配
    header_device_id = websocket.request_headers.get("Device-Id")
    if header_device_id != device_id:
        await websocket.close(1008, "Device ID mismatch")
        return

    # 连接成功，等待客户端 hello
    await handle_session(websocket, device_id)
```

### 5.2 Hello 握手处理

**接收客户端 hello:**

```json
{
    "type": "hello",
    "version": 3,
    "transport": "websocket",
    "features": {
        "aec": false,
        "mcp": true
    },
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,
        "channels": 1,
        "frame_duration": 60
    }
}
```

**发送服务端 hello:**

```json
{
    "type": "hello",
    "transport": "websocket",
    "session_id": "session-uuid-string",
    "audio_params": {
        "sample_rate": 24000,
        "frame_duration": 60
    }
}
```

**实现代码:**

```python
import uuid

async def handle_hello(websocket, message: dict) -> str:
    session_id = str(uuid.uuid4())

    # 记录客户端音频参数
    client_audio = message.get("audio_params", {})
    client_sample_rate = client_audio.get("sample_rate", 16000)
    client_frame_duration = client_audio.get("frame_duration", 60)

    # 记录客户端特性
    features = message.get("features", {})
    supports_aec = features.get("aec", False)
    supports_mcp = features.get("mcp", False)

    # 发送服务端 hello
    response = {
        "type": "hello",
        "transport": "websocket",
        "session_id": session_id,
        "audio_params": {
            "sample_rate": 24000,  # TTS 输出采样率
            "frame_duration": 60
        }
    }
    await websocket.send(json.dumps(response))

    return session_id
```

### 5.3 消息路由回调

**完整消息处理框架:**

```python
class WebSocketSession:
    def __init__(self, websocket, device_id: str):
        self.websocket = websocket
        self.device_id = device_id
        self.session_id = None
        self.is_listening = False

        # 回调函数
        self.on_audio_received = None      # 音频数据回调
        self.on_listen_start = None        # 开始监听回调
        self.on_listen_stop = None         # 停止监听回调
        self.on_abort = None               # 中止播放回调
        self.on_mcp_message = None         # MCP 消息回调

    async def handle_message(self, data):
        if isinstance(data, bytes):
            # 二进制音频数据
            await self._handle_audio(data)
        else:
            # JSON 消息
            message = json.loads(data)
            msg_type = message.get("type")

            if msg_type == "hello":
                self.session_id = await self._handle_hello(message)
            elif msg_type == "listen":
                await self._handle_listen(message)
            elif msg_type == "abort":
                await self._handle_abort(message)
            elif msg_type == "mcp":
                await self._handle_mcp(message)

    async def _handle_audio(self, data: bytes):
        """处理音频数据"""
        if self.on_audio_received:
            # Protocol v3 格式解析
            # |type 1u|reserved 1u|payload_size 2u|payload...|
            if len(data) > 4:
                payload_size = int.from_bytes(data[2:4], 'big')
                opus_data = data[4:4+payload_size]
                await self.on_audio_received(opus_data)

    async def _handle_listen(self, message: dict):
        """处理监听状态变化"""
        state = message.get("state")

        if state == "detect":
            # 唤醒词检测
            wake_word = message.get("text", "")
            print(f"Wake word detected: {wake_word}")

        elif state == "start":
            # 开始监听
            mode = message.get("mode", "auto")
            self.is_listening = True
            if self.on_listen_start:
                await self.on_listen_start(mode)

        elif state == "stop":
            # 停止监听
            self.is_listening = False
            if self.on_listen_stop:
                await self.on_listen_stop()

    async def _handle_abort(self, message: dict):
        """处理中止请求"""
        reason = message.get("reason", "")
        if self.on_abort:
            await self.on_abort(reason)

    async def _handle_mcp(self, message: dict):
        """处理 MCP 消息"""
        payload = message.get("payload")
        if payload and self.on_mcp_message:
            response = await self.on_mcp_message(payload)
            # 发送 MCP 响应
            await self.send_mcp(response)
```

### 5.4 发送消息到设备

```python
class WebSocketSession:
    # ... 继续上面的类

    async def send_tts_start(self):
        """通知设备 TTS 开始"""
        await self.websocket.send(json.dumps({
            "type": "tts",
            "state": "start"
        }))

    async def send_tts_sentence(self, text: str):
        """发送 TTS 句子文本"""
        await self.websocket.send(json.dumps({
            "type": "tts",
            "state": "sentence_start",
            "text": text
        }))

    async def send_tts_stop(self):
        """通知设备 TTS 结束"""
        await self.websocket.send(json.dumps({
            "type": "tts",
            "state": "stop"
        }))

    async def send_stt_result(self, text: str):
        """发送语音识别结果"""
        await self.websocket.send(json.dumps({
            "type": "stt",
            "text": text
        }))

    async def send_emotion(self, emotion: str):
        """发送情绪变化"""
        await self.websocket.send(json.dumps({
            "type": "llm",
            "emotion": emotion  # neutral, happy, sad, angry, etc.
        }))

    async def send_audio(self, opus_data: bytes):
        """发送 TTS 音频数据 (Protocol v3)"""
        # 构建 Protocol v3 包头
        header = bytes([
            0x00,                              # type: 0 = audio
            0x00,                              # reserved
            (len(opus_data) >> 8) & 0xFF,      # payload_size high byte
            len(opus_data) & 0xFF              # payload_size low byte
        ])
        await self.websocket.send(header + opus_data)

    async def send_mcp(self, payload: dict):
        """发送 MCP 消息"""
        await self.websocket.send(json.dumps({
            "type": "mcp",
            "payload": payload
        }))

    async def send_system_command(self, command: str):
        """发送系统命令"""
        await self.websocket.send(json.dumps({
            "type": "system",
            "command": command  # 如 "reboot"
        }))

    async def send_alert(self, status: str, message: str, emotion: str):
        """发送警告消息"""
        await self.websocket.send(json.dumps({
            "type": "alert",
            "status": status,
            "message": message,
            "emotion": emotion
        }))
```

---

## 6. MQTT + UDP 服务实现

### 6.1 MQTT 配置下发

**OTA 响应:**

```json
{
    "mqtt": {
        "endpoint": "mqtt.your-server.com:8883",
        "client_id": "xiaozhi_AABBCCDDEEFF",
        "username": "device_user",
        "password": "device_password",
        "publish_topic": "xiaozhi/AABBCCDDEEFF/up",
        "subscribe_topic": "xiaozhi/AABBCCDDEEFF/down",
        "keepalive": 240
    }
}
```

### 6.2 UDP 通道建立

当设备通过 MQTT 发送 hello 时，服务端返回 UDP 配置：

**客户端 hello (MQTT):**

```json
{
    "type": "hello",
    "version": 3,
    "transport": "udp",
    "features": {"mcp": true},
    "audio_params": {
        "format": "opus",
        "sample_rate": 16000,
        "channels": 1,
        "frame_duration": 60
    }
}
```

**服务端 hello (MQTT):**

```json
{
    "type": "hello",
    "transport": "udp",
    "session_id": "session-uuid",
    "audio_params": {
        "sample_rate": 24000,
        "frame_duration": 60
    },
    "udp": {
        "server": "udp.your-server.com",
        "port": 12345,
        "key": "0123456789ABCDEF0123456789ABCDEF",
        "nonce": "FEDCBA9876543210FEDCBA9876543210"
    }
}
```

### 6.3 UDP 加密密钥生成

```python
import os

def generate_udp_credentials():
    """生成 UDP 加密凭证"""
    key = os.urandom(16).hex().upper()    # AES-128 密钥
    nonce = os.urandom(16).hex().upper()  # CTR 模式初始向量
    return key, nonce
```

### 6.4 UDP 音频解密

```python
from Crypto.Cipher import AES
from Crypto.Util import Counter
import struct

class UDPAudioHandler:
    def __init__(self, key_hex: str, nonce_hex: str):
        self.key = bytes.fromhex(key_hex)
        self.nonce = bytes.fromhex(nonce_hex)
        self.remote_sequence = 0

    def decrypt_packet(self, data: bytes) -> tuple:
        """
        解密 UDP 音频包

        包格式:
        |type 1u|flags 1u|payload_len 2u|ssrc 4u|timestamp 4u|sequence 4u|encrypted_payload...|
        """
        if len(data) < 16:
            return None, None

        # 解析包头
        pkt_type = data[0]
        if pkt_type != 0x01:
            return None, None

        payload_len = struct.unpack('>H', data[2:4])[0]
        timestamp = struct.unpack('>I', data[8:12])[0]
        sequence = struct.unpack('>I', data[12:16])[0]

        # 检查序列号
        if sequence <= self.remote_sequence:
            print(f"Old packet: {sequence} <= {self.remote_sequence}")
            return None, None
        self.remote_sequence = sequence

        # 解密
        nonce = bytearray(self.nonce)
        nonce[2:4] = struct.pack('>H', payload_len)
        nonce[8:12] = struct.pack('>I', timestamp)
        nonce[12:16] = struct.pack('>I', sequence)

        cipher = AES.new(self.key, AES.MODE_CTR, nonce=bytes(nonce[:8]),
                        initial_value=int.from_bytes(nonce[8:], 'big'))

        encrypted = data[16:16+payload_len]
        decrypted = cipher.decrypt(encrypted)

        return decrypted, timestamp
```

---

## 7. 设备固件配置

### 7.1 修改默认 OTA URL

**方法1: 修改 Kconfig**

编辑 `main/Kconfig.projbuild`:

```diff
config OTA_URL
    string "Default OTA URL"
-   default "https://api.tenclass.net/xiaozhi/ota/"
+   default "https://your-server.com/api/ota/"
```

**方法2: menuconfig 配置**

```bash
idf.py menuconfig
# 导航到: Xiaozhi Assistant -> Default OTA URL
# 修改为你的服务器地址
```

### 7.2 运行时修改 (NVS)

通过串口或代码设置:

```cpp
#include "settings.h"

void set_custom_ota_url() {
    Settings settings("wifi", true);
    settings.SetString("ota_url", "https://your-server.com/api/ota/");
}
```

### 7.3 编译自定义固件

```bash
# 克隆仓库
git clone https://github.com/78/xiaozhi-esp32.git
cd xiaozhi-esp32

# 配置目标芯片
idf.py set-target esp32s3

# 配置板型和 OTA URL
idf.py menuconfig

# 编译
idf.py build

# 烧录
idf.py flash
```

---

## 8. 完整示例代码

### 8.1 Python FastAPI 服务端

```python
# server.py
from fastapi import FastAPI, WebSocket, Request, Header
from fastapi.responses import JSONResponse
import jwt
import time
import uuid
import json

app = FastAPI()
SECRET_KEY = "your-secret-key-here"

# 设备数据库 (示例用内存存储)
devices = {}
sessions = {}

@app.post("/api/ota/")
async def ota_handler(
    request: Request,
    device_id: str = Header(None, alias="Device-Id"),
    client_id: str = Header(None, alias="Client-Id"),
    user_agent: str = Header(None, alias="User-Agent")
):
    """OTA 配置接口"""

    # 解析请求体
    try:
        body = await request.json()
    except:
        body = {}

    # 注册/更新设备
    if device_id not in devices:
        devices[device_id] = {
            "device_id": device_id,
            "client_id": client_id,
            "firmware": user_agent,
            "registered_at": time.time()
        }

    # 生成 Token
    token = jwt.encode({
        "device_id": device_id,
        "exp": int(time.time()) + 86400 * 30
    }, SECRET_KEY, algorithm="HS256")

    # 返回配置 (无激活)
    return JSONResponse({
        "websocket": {
            "url": "wss://your-server.com/api/ws",
            "token": f"Bearer {token}",
            "version": 3
        },
        "server_time": {
            "timestamp": int(time.time() * 1000),
            "timezone_offset": 480
        }
    })

@app.websocket("/api/ws")
async def websocket_endpoint(websocket: WebSocket):
    """WebSocket 实时通信"""

    # 验证 Token
    auth = websocket.headers.get("authorization", "").replace("Bearer ", "")
    try:
        payload = jwt.decode(auth, SECRET_KEY, algorithms=["HS256"])
        device_id = payload["device_id"]
    except:
        await websocket.close(1008, "Invalid token")
        return

    await websocket.accept()

    session_id = None

    try:
        while True:
            data = await websocket.receive()

            if "text" in data:
                message = json.loads(data["text"])
                msg_type = message.get("type")

                if msg_type == "hello":
                    # 处理握手
                    session_id = str(uuid.uuid4())
                    sessions[session_id] = {
                        "device_id": device_id,
                        "websocket": websocket,
                        "audio_params": message.get("audio_params", {})
                    }

                    await websocket.send_json({
                        "type": "hello",
                        "transport": "websocket",
                        "session_id": session_id,
                        "audio_params": {
                            "sample_rate": 24000,
                            "frame_duration": 60
                        }
                    })

                elif msg_type == "listen":
                    state = message.get("state")
                    if state == "detect":
                        print(f"Wake word: {message.get('text')}")
                    elif state == "start":
                        print(f"Start listening, mode: {message.get('mode')}")
                    elif state == "stop":
                        print("Stop listening")
                        # 这里调用 ASR 处理收集的音频

                elif msg_type == "abort":
                    print(f"Abort speaking: {message.get('reason')}")

                elif msg_type == "mcp":
                    # 处理 MCP 请求并返回响应
                    mcp_payload = message.get("payload", {})
                    response = handle_mcp_request(mcp_payload)
                    await websocket.send_json({
                        "type": "mcp",
                        "payload": response
                    })

            elif "bytes" in data:
                # 处理音频数据
                audio_data = data["bytes"]
                # Protocol v3: 前4字节是包头
                if len(audio_data) > 4:
                    opus_payload = audio_data[4:]
                    # 发送到 ASR 服务处理
                    # await asr_service.process(opus_payload)

    except Exception as e:
        print(f"WebSocket error: {e}")
    finally:
        if session_id and session_id in sessions:
            del sessions[session_id]

def handle_mcp_request(payload: dict) -> dict:
    """处理 MCP 请求"""
    method = payload.get("method")
    params = payload.get("params", {})
    req_id = payload.get("id")

    if method == "initialize":
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "result": {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "private-server", "version": "1.0.0"}
            }
        }

    elif method == "tools/list":
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "result": {"tools": []}
        }

    elif method == "tools/call":
        tool_name = params.get("name")
        # 转发到设备执行
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "result": {
                "content": [{"type": "text", "text": "true"}]
            }
        }

    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "error": {"message": "Method not found"}
    }

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
```

### 8.2 运行服务

```bash
# 安装依赖
pip install fastapi uvicorn pyjwt

# 运行服务
python server.py

# 使用 nginx 反向代理添加 HTTPS/WSS
```

### 8.3 Nginx 配置示例

```nginx
server {
    listen 443 ssl;
    server_name your-server.com;

    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;

    location /api/ {
        proxy_pass http://127.0.0.1:8000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

---

## 附录: 回调事件汇总

| 事件 | 触发条件 | 服务端处理 |
|------|----------|------------|
| `hello` | 设备连接握手 | 返回 session_id 和音频参数 |
| `listen.detect` | 唤醒词检测 | 记录日志，准备接收音频 |
| `listen.start` | 开始监听 | 初始化 ASR 流 |
| `listen.stop` | 停止监听 | 结束 ASR，获取识别结果 |
| `abort` | 用户中止 | 停止 TTS 播放 |
| `audio (binary)` | 音频数据 | 发送到 ASR 处理 |
| `mcp.initialize` | MCP 初始化 | 返回服务器信息 |
| `mcp.tools/list` | 获取工具列表 | 转发到设备 |
| `mcp.tools/call` | 调用工具 | 转发到设备执行 |
| `goodbye` | 会话结束 | 清理资源 |

---

**文档版本**: 1.0
**最后更新**: 2025-12-18
**维护者**: Claude Code
