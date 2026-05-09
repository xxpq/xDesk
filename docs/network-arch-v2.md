# DeskX 智能多协议网络架构设计

> 版本: 2.0 (多协议版)
> 更新: 2026-05-09
> 状态: 设计中

---

## 1. 架构概览

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              DeskX Client                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────┐      ┌──────────────────────────────────────────────┐     │
│  │   业务层    │ ←──→ │            网络抽象层 (NetLayer)             │     │
│  │  (codec/   │      │  ┌─────────────────────────────────────────┐  │     │
│  │  display)  │      │  │         协议管理器 (ProtocolMgr)        │  │     │
│  │            │      │  │  ┌─────────┐  ┌─────────┐  ┌─────────┐ │  │     │
│  │            │      │  │  │  TCP    │  │  KCP    │  │  QUIC   │ │  │     │
│  │            │      │  │  │ Stack   │  │ Stack   │  │ Stack   │ │  │     │
│  │            │      │  │  │ (备选)  │  │ (首选)  │  │ (备选)  │ │  │     │
│  │            │      │  │  └────┬────┘  └────┬────┘  └────┬────┘ │  │     │
│  │            │      │  │       │              │             │     │  │     │
│  │            │      │  │       └──────────────┼─────────────┘     │  │     │
│  │            │      │  │                      │                   │  │     │
│  │            │      │  │              ┌────────┴────────┐         │  │     │
│  │            │      │  │              │  流量分配器      │         │  │     │
│  │            │      │  │              │  (Dispatcher)    │         │  │     │
│  │            │      │  │              └────────┬────────┘         │  │     │
│  │            │      │  └───────────────────────┼──────────────────┘  │     │
│  │            │      └──────────────────────────┼────────────────────┘     │
│  └─────────────┘                                 │                            │
│                                                  ▼                            │
│                                          ┌──────────────┐                     │
│                                          │   物理传输层  │                     │
│                                          │ TCP + UDP    │                     │
│                                          └──────────────┘                     │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 传输策略矩阵

| 模式 | Primary | Secondary | Tertiary | 适用场景 |
|------|---------|-----------|----------|-----------|
| LAN_ONLY | TCP | - | - | 纯局域网 |
| WAN_ONLY | KCP | - | - | 纯公网 |
| HYBRID_AUTO | TCP | KCP | - | 自动检测(默认) |
| DUAL_STACK | TCP + KCP | QUIC | - | 高可靠性需求 |
| TRIPLE_RED | KCP | TCP | QUIC | 极端可靠性 |

---

## 3. 核心数据结构

```cpp
// src/net/v2/frame.hpp

#pragma pack(push, 1)

// 统一帧头 (24 字节)
struct FrameHeader {
    uint32_t    magic;          // 魔数: 0x44455850 ('DESK')
    uint16_t    version;        // 协议版本: 2.0
    uint8_t     channel;        // 传输通道: 0=TCP, 1=KCP, 2=QUIC
    uint8_t     frame_type;     // 帧类型
    uint16_t    flags;          // 控制标志
    uint32_t    sequence;       // 序列号
    uint32_t    timestamp;      // 时间戳 (ms)
    uint32_t    payload_size;   // 数据长度
    uint32_t    checksum;       // CRC32校验
};

// 握手包 (40 字节)
struct HandshakeV2 {
    FrameHeader header;
    uint8_t     protocols;      // 支持的协议掩码
    uint8_t     preferred;      // 首选协议
    uint8_t     compression;    // 压缩级别
    uint8_t     rgb_bits;       // RGB 位深
    uint16_t    viewport_w;     // 客户端视口宽
    uint16_t    viewport_h;     // 客户端视口高
    uint8_t     reserved[8];    // 保留
};

// 心跳包 (32 字节)
struct Heartbeat {
    FrameHeader header;
    uint32_t    latency_tcp;    // TCP 链路延迟
    uint32_t    latency_kcp;    // KCP 链路延迟
    uint8_t     link_status;    // 链路状态掩码
    uint8_t     reserved[3];
};

#pragma pack(pop)

// 帧类型枚举
enum class FrameType : uint8_t {
    HANDSHAKE      = 0x01,   // 握手
    HANDSHAKE_ACK  = 0x02,   // 握手确认
    HB_REQUEST     = 0x03,   // 心跳请求
    HB_RESPONSE    = 0x04,   // 心跳响应
    CTRL_HELLO     = 0x10,   // 控制-初始化
    CTRL_SCREEN    = 0x11,   // 控制-屏幕信息
    INPUT_KEY      = 0x20,   // 输入-键盘
    INPUT_MOUSE    = 0x21,   // 输入-鼠标
    FRAME_VIDEO    = 0x30,   // 视频帧
    FRAME_RAW      = 0x31,   // 原始帧
    DISCONNECT     = 0xFE,   // 断开
    KEEPALIVE      = 0xFF,   // 保活
};

// 链路状态
enum class LinkStatus : uint8_t {
    DOWN          = 0x00,
    CONNECTING    = 0x01,
    UP            = 0x02,
    DEGRADED      = 0x03,
    RECONNECTING  = 0x04,
};

// 传输协议
enum class TransportProtocol : uint8_t {
    TCP   = 0x01,
    KCP   = 0x02,
    QUIC  = 0x04,
    ALL   = 0x07,
};
```

---

## 4. 协议栈接口

```cpp
// src/net/v2/stack.hpp

class IProtocolStack {
public:
    virtual ~IProtocolStack() = default;
    
    virtual bool connect(const std::string& ip, uint16_t port) = 0;
    virtual bool listen(uint16_t port) = 0;
    virtual void close() = 0;
    
    virtual int send(const uint8_t* data, size_t len) = 0;
    virtual int recv(uint8_t* buff, size_t max_len) = 0;
    
    virtual uint32_t latency() const = 0;
    virtual LinkStatus status() const = 0;
    virtual TransportProtocol type() const = 0;
    virtual bool isHealthy() const = 0;
};

// TCP 栈
class TCPStack : public IProtocolStack { /* ... */ };

// KCP 栈  
class KCPStack : public IProtocolStack { /* ... */ };
```

---

## 5. 故障转移状态机

```
                    ┌──────────────┐
                    │   INITIAL   │
                    └──────┬───────┘
                           │ 所有链路建立完成
                           ▼
                    ┌──────────────┐
              ┌────→│   HEALTHY    │←────┐
              │     └──────┬───────┘     │
              │            │              │ 主链路恢复
     主链路断开            │    主链路恢复
              │            │              │
              │            ▼              │
              │     ┌──────────────┐      │
              │     │  DEGRADED    │──────┘
              │     │  降级运行     │
              │     └──────────────┘
              │            │
              │    所有链路断开
              │            │
              │            ▼
              │     ┌──────────────┐
              └─────│   FAILOVER   │ (仅备用可用)
                    └──────────────┘
```

---

## 6. 帧优先级策略

| 帧类型 | 优先级 | TCP通道 | KCP通道 | 说明 |
|--------|--------|---------|---------|------|
| CTRL_HELLO | CRITICAL | ✓ | ✓ | 连接建立 |
| CTRL_SCREEN | CRITICAL | ✓ | ✓ | 分辨率信息 |
| INPUT_KEY | HIGH | ✓ | ✓ | 键盘(可靠) |
| INPUT_MOUSE | HIGH | - | ✓ | 鼠标(低延迟) |
| FRAME_VIDEO | MEDIUM | ✓ | ✓ | 压缩画面 |
| FRAME_RAW | LOW | - | ✓ | 缩放画面 |

---

## 7. 文件结构

```
src/
├── net/
│   ├── v2/
│   │   ├── frame.hpp          # 帧结构定义
│   │   ├── stack.hpp          # 协议栈接口
│   │   ├── tcp_stack.cpp/hpp  # TCP 实现
│   │   ├── kcp_stack.cpp/hpp  # KCP 实现
│   │   ├── dispatcher.cpp/hpp # 流量分发器
│   │   ├── aggregator.cpp/hpp # 数据聚合器
│   │   ├── heartbeat.cpp/hpp  # 心跳检测
│   │   └── manager.cpp/hpp    # 协议管理器
│   └── legacy/
│       └── net.cpp/hpp        # 兼容旧版
├── Makefile                   # 更新构建
└── ...
```

---

## 9. 实现状态

| 组件 | 状态 | 文件 |
|------|------|------|
| 帧结构定义 | ✅ 已完成 | `src/net/v2/frame.hpp` |
| 协议栈接口 | ✅ 已完成 | `src/net/v2/stack.hpp` |
| TCP 协议栈 | ✅ 已完成 | `src/net/v2/tcp_stack.{hpp,cpp}` |
| KCP 协议栈 | ✅ 已完成 | `src/net/v2/kcp_stack.{hpp,cpp}` |
| KCP 核心库 | ✅ 已下载 | `src/net/v2/ikcp.{h,c}` |
| 流量分配器 | ✅ 已完成 | `src/net/v2/dispatcher.{hpp,cpp}` |
| 数据聚合器 | ✅ 已完成 | `src/net/v2/dispatcher.{hpp,cpp}` |
| 协议管理器 | ✅ 已完成 | `src/net/v2/manager.{hpp,cpp}` |
| 网络层入口 | ✅ 已完成 | `src/net/v2.hpp` |
| 集成测试 | ⏳ 待开始 | - |

---

## 10. 编译配置

```makefile
# Makefile 添加
NET_V2_SRC = src/net/v2/ikcp.c \
             src/net/v2/tcp_stack.cpp \
             src/net/v2/kcp_stack.cpp \
             src/net/v2/dispatcher.cpp \
             src/net/v2/manager.cpp

NET_V2_HDR = src/net/v2.hpp \
             src/net/v2/frame.hpp \
             src/net/v2/stack.hpp \
             src/net/v2/tcp_stack.hpp \
             src/net/v2/kcp_stack.hpp \
             src/net/v2/dispatcher.hpp \
             src/net/v2/manager.hpp \
             src/net/v2/ikcp.h

CXXFLAGS += -I$(SRC_DIR)/net/v2 -DWIN32_LEAN_AND_MEAN
LDFLAGS += -lws2_32  # Windows only
```

---

## 11. 使用示例

```cpp
// 客户端初始化
ProtocolManager manager;
manager.initClient("192.168.1.100", 1742, 1743);
manager.setMode(TransportMode::DUAL_STACK);
manager.startHeartbeat(500);

// 发送数据
manager.send(frameData, size, FrameType::FRAME_VIDEO);

// 设置回调
manager.setDataCallback([](auto d, auto l, auto t) { ... });
manager.setStatusCallback([](auto old, auto now) { ... });
```

---

## 12. 依赖

- KCP: https://github.com/skywind3000/kcp (单文件，无依赖)
- CRC32: header-only 实现 (集成在 frame.hpp)
- 现有 SDL2, LZ4 依赖不变

---

*文档最后更新: 2026-05-09*
