# FbThrift请求链路性能优化介绍

简体中文|[English](./README_EN.md)

## 最新消息

- [2026.09.30]：发布优化补丁FbThrift v1.1.0版本。新增动态收包缓冲区、Folly IOBuf TLS内存池、ThreadManager direct-func和请求热路径去锁优化，并提供完整Benchmark与自动构建流程。
- [2026.06.30]：基于Meta开源的FbThrift序列化发布补丁FbThrift v1.0.0版本。针对FbThrift序列化框架进行整型数组批量编码优化，通过ARM SVE2指令集和编译器自动向量化技术显著提升序列化性能。

## 项目介绍

FbThrift是Meta开源的高性能RPC框架和序列化库，广泛应用于分布式系统和微服务架构中，支持Compact Protocol、Binary Protocol以及Header、Rocket等传输方式。

本项目面向FbThrift端到端请求链路进行性能优化。FbThrift v1.0.0提供Compact Protocol与Binary Protocol批量序列化优化。
FbThrift v1.1.0进一步覆盖网络收包、缓冲区分配、CPU任务调度和请求公共路径，降低高QPS场景下的系统调用、内存分配和同步开销。

## 目录结构

```text
fbthrift/
├── docs/                           # 文档目录
│   ├── en/                         # 英文文档 English documents
│   │   ├── api_reference.md        # API reference sheet
│   │   ├── quick_start.md          # Menu for beginners
│   │   └── release_notes.md        # Release notes
│   └── LICENSE
│   ├── zh/                         # 中文文档
│   │   ├── api_reference.md        # API参考
│   │   ├── quick_start.md          # 快速入门
│   │   └── release_notes.md        # 版本说明书
│   └── LICENSE
├── LICENSE
├── fbthrift_opt_simd.patch         # FbThrift v1.1.0优化补丁文件
└── README.md                       # 项目介绍
```

## 特性介绍

### v1.1.0优化特性介绍

|优化项|作用位置|核心方案|
|--|--|--|
|动态收包缓冲区|网络收包|根据近期完整帧长度动态调整读缓冲区，减少大消息的`recv()`次数。|
|Folly IOBuf TLS内存池|缓冲区分配|每线程复用数据块并切分slice，减少高频`malloc/free`。|
|ThreadManager direct-func|CPU任务调度|`Task`直接保存和执行`folly::Func`，避免常规RPC创建`FunctionRunner`。|
|请求热路径去锁|请求公共路径|移除不再承担有效并发写保护的冗余锁，降低多核竞争。|

各优化均保留必要的兼容或回退路径，具体接口、构建方式和风险边界请参见中文文档。

## 版本说明

关于FbThrift性能优化补丁仓的版本发布情况请参见《[版本说明书](docs/zh/release_notes.md)》。

## 快速入门

从零编译优化版FbThrift及运行Benchmark的完整指导请参见《[快速入门](docs/zh/quick_start.md)》。

## 学习文档

| 学习资源名称 | 资源简介 |
| --------- | --------- |
| [快速入门](docs/zh/quick_start.md) | 提供手动编译、脚本编译和Benchmark运行指导。 |
| [版本说明书](docs/zh/release_notes.md) | 提供v1.1.0版本信息、性能验证及兼容性说明。 |
| [API参考](docs/zh/api_reference.md) | 按版本提供序列化、ThreadManager和Header接口说明。 |

## 免责声明

此代码仓计划参与FbThrift软件开源，对FbThrift请求链路和序列化路径进行性能优化。代码遵照开源软件的设计与编码风格，并保留必要的兼容和回退机制。软件的任何漏洞与安全问题由相应上游社区根据其漏洞和安全响应机制解决，请密切关注上游社区发布的通知和版本更新。

## License

FbThrift遵循Apache-2.0许可证，具体请参见[LICENSE文件](LICENSE)。

本项目的文档适用CC-BY 4.0许可证，具体请参见[LICENSE文件](docs/LICENSE)。

## 贡献声明

欢迎大家为社区做贡献，如果使用过程中有任何问题/建议，或者需要反馈特性需求和bug报告，可以提交[Issues](https://gitcode.com/boostkit/community/blob/master/docs/contributor/issue-submit.md)联系我们，具体贡献方法可参考[贡献指南](https://gitcode.com/boostkit/community/blob/master/docs/contributor/contributing.md)。同时也欢迎大家在[讨论专区](https://gitcode.com/boostkit/community/discussions)展开讨论交流。感谢您的支持。

## 致谢

感谢来自社区的每一个PR，欢迎贡献FbThrift！
