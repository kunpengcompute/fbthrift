# fbthrift 序列化优化补丁仓介绍

## 最新消息

- 2026-03-30：发布补丁仓v1.0.0版本，针对fbthrift序列化框架进行整型数组批量编码优化，通过ARM SVE2指令集和编译器自动向量化技术显著提升序列化性能。

## 项目介绍

fbthrift是Meta开源的高性能RPC框架和序列化库，广泛应用于分布式系统和微服务架构中。它支持多种语言（如C++、Java、Python等），序列化部分提供了丰富的序列化协议（如Compact Protocol、Binary Protocol等）。

本项目是针对fbthrift序列化框架的优化仓库，聚焦于连续整型数组（如`list<int32_t>`、`list<int64_t>`）的序列化性能优化。fbthrift默认采用逐元素（per-element）的编码与解码，这种方式存在较强的数据依赖，难以被编译器自动向量化（SIMD），导致序列化性能在处理大规模数组时成为瓶颈。

本优化方案的核心思路：

- **Compact Protocol**：引入基于ARM SVE2的批量Varint编码逻辑，通过`dispatchVarintEncode32`进行跨TU的运行时分发，在支持SVE2的CPU上自动启用向量化内核。
- **Binary Protocol**：引入基于`memcpy`和`bswap`的紧凑循环，利用编译器自动向量化提升性能，无需特定CPU指令集支持。
- **零侵入**：对下游业务代码保持透明，无需更改编译选项或业务逻辑，SFINAE机制自动选择最优路径。

## 目录结构

```text
fbthrift/
├── docs/                           # 文档目录
│   ├── zh/                         # 中文文档
│   │   ├── api.md                  # API参考文档
│   │   ├── quick_start.md          # 快速入门文档
│   │   └── release_notes.md        # 版本说明书
│   └── LICENSE
├── LICENSE
└── README.md
```

## 版本说明

详见[版本说明书](docs/zh/release_notes.md)

## 快速上手

详见[快速入门](docs/zh/quick_start.md)

## 文档

| 资源名称 | 资源简介 |
|---------|---------|
| [快速入门](docs/zh/quick_start.md) | 提供fbthrift序列化优化的编译安装和测试指导。 |
| [版本说明书](docs/zh/release_notes.md) | 提供fbthrift序列化优化版本的基础信息和特性更新信息。 |
| [API参考](docs/zh/api.md) | 提供优化后的接口说明及相关改动。 |

## 免责声明

此代码仓计划参与fbthrift软件开源，仅对fbthrift序列化部分函数进行性能优化，编码风格遵照原生开源软件，继承原生开源软件安全设计，不破坏原生开源软件设计及编码风格和方式，软件的任何漏洞与安全问题，均由相应的上游社区根据其漏洞和安全响应机制解决。请密切关注上游社区发布的通知和版本更新。对软件的漏洞及安全问题不承担任何责任。

## License

fbthrift遵循 Apache-2.0许可证，具体请参见[LICENSE文件](LICENSE)。

本项目的文档适用CC-BY 4.0许可证，具体请参见[LICENSE文件](docs/LICENSE)。

## 贡献指南

如果使用过程中有任何问题，或者需要反馈特性需求和bug报告，可以提交issues联系我们。

## 建议与交流

欢迎大家为社区做贡献。如果有任何疑问或建议，请提交Issues，我们会尽快回复。感谢您的支持。

## 致谢

fbthrift补丁仓由华为公司的下列部门联合贡献：

鲲鹏计算Boostkit开发部

感谢来自社区的每一个PR，欢迎贡献fbthrift补丁仓！
