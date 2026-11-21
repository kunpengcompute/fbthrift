# FbThrift请求链路性能优化介绍

简体中文|[English](./README_EN.md)

本仓库提供两份独立的 FbThrift 补丁。

**URMA 传输** — [fbthrift_urma.patch](fbthrift_urma.patch)

在 Meta FbThrift `v2022.11.14.00` 上接入 Folly URMA。启用后，服务端在同一明文端口接收普通 TCP 客户端和 URMA 客户端，应用可继续使用 Header 或 Rocket。

**请求链路与序列化优化** — [fbthrift_folly.patch](fbthrift_folly.patch)

本文档介绍FbThrift请求链路性能优化补丁，涵盖V1.0.0与V1.1.0优化特性、目录结构、版本说明和学习文档，适用于使用FbThrift性能优化补丁的开发者。

## 最新消息

**URMA 补丁**

- [2026-12-30]：发布 FbThrift URMA 补丁，支持同端口接入 TCP 和 URMA，并按序列化后的消息边界传输 Header 和 Rocket 数据。

**请求链路与序列化优化补丁**

- [2026-09-30]：发布优化补丁FbThrift V1.1.0版本。新增动态收包缓冲区、Folly IOBuf TLS内存池、ThreadManager direct-func和请求热路径去锁优化，并提供完整Benchmark与自动构建流程。
- [2026-06-30]：基于Meta开源的FbThrift序列化发布补丁FbThrift V1.1.0版本。针对FbThrift序列化框架进行整型数组批量编码优化，通过ARM SVE2指令集和编译器自动向量化技术显著提升序列化性能。

## 项目介绍

FbThrift是Meta开源的高性能RPC框架和序列化库，广泛应用于分布式系统和微服务架构中，支持Compact Protocol、Binary Protocol以及Header、Rocket等传输方式。

本项目面向FbThrift端到端请求链路进行性能优化。FbThrift V1.1.0提供Compact Protocol与Binary Protocol批量序列化优化。
FbThrift V1.1.0进一步覆盖网络收包、缓冲区分配、CPU任务调度和请求公共路径，降低高QPS场景下的系统调用、内存分配和同步开销。

## 目录结构

```text
fbthrift/
├── docs/                           # 文档目录
│   ├── en/                         # 英文文档English documents
│   │   ├── api_reference.md        # api_reference
│   │   ├── quick_start.md          # quick_start
│   │   └── release_notes.md        # release_notes
│   └── LICENSE
│   ├── zh/                         # 中文文档
│   │   ├── api_reference.md        # API参考
│   │   ├── quick_start.md          # 快速入门
│   │   └── release_notes.md        # 版本说明书
│   └── LICENSE
├── LICENSE
├── fbthrift_opt_simd.patch         # FbThrift V1.1.0优化补丁文件
├── fbthrift_urma.patch             # FbThrift URMA独立补丁
├── fbthrift_urma.patch.sha256      # URMA补丁完整性校验文件
└── README.md                       # 项目介绍
```

## 特性介绍

### V1.1.0优化特性介绍

|优化项|作用位置|核心方案|
|--|--|--|
|动态收包缓冲区|网络收包|根据近期完整帧长度动态调整读缓冲区，减少大消息的recv()次数。|
|Folly IOBuf TLS内存池|缓冲区分配|每线程复用数据块并切分slice，减少高频malloc/free。|
|ThreadManager direct-func|CPU任务调度|Task直接保存和执行folly::Func，避免常规RPC创建FunctionRunner。|
|请求热路径去锁|请求公共路径|移除不再承担有效并发写保护的冗余锁，降低多核竞争。|

各优化均保留必要的兼容或回退路径，具体接口、构建方式和风险边界请参见中文文档。

### URMA补丁特性介绍

|优化项|作用位置|核心方案|
|--|--|--|
|同端口接入|ThriftServer|服务端默认使用 TCP。启用 URMA 后，按连接起始字节识别 URMA 握手，普通 TCP 连接继续原有处理流程。|
|按消息选择传输方式|Header、Rocket|批量发送时保留每条序列化消息的长度，Folly 按单条消息选择 SEND/RECV 或单边 READ。|
|发送背压|Rocket 服务端|按 URMA 实际保留的写缓冲容量跟踪发送内存，并对业务连接执行写超时。|

## 版本说明

关于FbThrift性能优化补丁仓的版本发布情况请参见《[版本说明书](docs/zh/release_notes.md)》。

## 快速入门

从零编译优化版FbThrift及运行Benchmark的完整指导请参见《[快速入门](docs/zh/quick_start.md)》。

## 学习文档

| 学习文档名称 | 内容简介 |
| --------- | --------- |
| 《[快速入门](docs/zh/quick_start.md)》 | 提供手动编译、脚本编译和Benchmark运行指导。 |
| 《[版本说明书](docs/zh/release_notes.md)》 | 提供V1.1.0版本信息、性能验证及兼容性说明。 |
| 《[API参考](docs/zh/api_reference.md)》 | 按版本提供序列化、ThreadManager和Header接口说明。 |

URMA 补丁的依赖、编译步骤和服务端接口分别见《[URMA补丁说明](docs/zh/release_notes.md#urma补丁说明)》《[URMA补丁快速入门](docs/zh/quick_start.md#urma补丁快速入门)》和《[URMA补丁接口说明](docs/zh/api_reference.md#urma补丁接口说明)》。

## 免责声明

此代码仓计划参与FbThrift软件开源，对FbThrift请求链路和序列化路径进行性能优化。代码遵照开源软件的设计与编码风格，并保留必要的兼容和回退机制。软件的任何漏洞与安全问题由相应上游社区根据其漏洞和安全响应机制解决，请密切关注上游社区发布的通知和版本更新。

## License

FbThrift遵循Apache-2.0许可证，具体请参见[LICENSE文件](LICENSE)。

本项目的文档适用CC-BY 4.0许可证，具体请参见[LICENSE文件](docs/LICENSE)。

## 贡献声明

欢迎大家为社区做贡献，如果使用过程中有任何问题/建议，或者需要反馈特性需求和bug报告，可以提交[Issues](https://gitcode.com/boostkit/community/blob/master/docs/contributor/issue-submit.md)联系我们，具体贡献方法可参考[贡献指南](https://gitcode.com/boostkit/community/blob/master/docs/contributor/contributing.md)。同时也欢迎大家在[讨论专区](https://gitcode.com/boostkit/community/discussions)展开讨论交流。感谢您的支持。

## 致谢

感谢来自社区的每一个PR，欢迎贡献FbThrift！
