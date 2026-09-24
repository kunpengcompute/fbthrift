# FbThrift Request Path Optimization Introduction

English|[简体中文](./README.md)

## Latest News

- [2026.09.30]: Released the FbThrift v1.1.0 optimization patch. Added dynamic receive buffer, Folly IOBuf TLS memory pool, ThreadManager direct-func, and lock removal on the request hot path, along with a complete benchmark and automated build pipeline.
- [2026.06.30]: Released the FbThrift v1.0.0 patch based on Meta's open-source FbThrift serialization framework. This patch optimizes batch encoding of integer arrays in the FbThrift serialization framework, significantly improving serialization performance through the Arm SVE2 instruction set and compiler auto-vectorization.

## Project Introduction

FbThrift is a high-performance RPC framework and serialization library open sourced by Meta. It is widely used in distributed systems and microservice architectures, and supports transport methods such as Compact Protocol, Binary Protocol, Header, and Rocket.

This project performs performance optimization on the end-to-end request path of FbThrift. FbThrift v1.0.0 provides batch serialization optimization for Compact Protocol and Binary Protocol.
FbThrift v1.1.0 further covers network packet reception, buffer allocation, CPU task scheduling, and the common request path, reducing system calls, memory allocation, and synchronization overhead in high-QPS scenarios.

## Directory Structure

```text
fbthrift/
├── docs/                           # Documentation directory
│   ├── en/                         # English documents
│   │   ├── api_reference.md        # api_reference
│   │   ├── quick_start.md          # quick_start
│   │   └── release_notes.md        # release_notes
│   └── LICENSE
│   ├── zh/                         # Chinese documents
│   │   ├── api_reference.md        # API Reference
│   │   ├── quick_start.md          # Quick Start
│   │   └── release_notes.md        # Release Notes
│   └── LICENSE
├── LICENSE
├── fbthrift_opt_simd.patch         # FbThrift v1.1.0 optimization patch file
└── README.md                       # Project introduction
```

## Feature Description

### v1.1.0 Optimization Features

| Optimization | Location | Core Approach |
| -- | -- | -- |
| Dynamic receive buffer | network packet reception | Dynamically adjusts the read buffer based on recent complete frame lengths, reducing the number of `recv()` calls for large messages. |
| Folly IOBuf TLS memory pool | Buffer allocation | Reuses data blocks per thread and splits them into slices, reducing frequent `malloc/free` calls. |
| ThreadManager direct-func | CPU task scheduling | The `Task` directly stores and executes `folly::Func`, avoiding the creation of a `FunctionRunner` for regular RPCs. |
| Lock removal on the request hot path | Common request path | Removes redundant locks that no longer provide effective concurrent write protection, reducing multi-core contention. |

Each optimization retains the necessary compatibility or fallback path. For specific interfaces, build methods, and risk boundaries, see the documentation.

## Release Notes

For details about the version release of the FbThrift performance optimization patch repository, see [Release Notes](docs/en/release_notes.md).

## Quick Start

For complete instructions on compiling the optimized FbThrift from scratch and running the benchmark, see [Quick Start](docs/en/quick_start.md).

## Documentation

| Document | Description |
| --------- | --------- |
| [Quick Start](docs/en/quick_start.md) | Provides guidance on manual compilation, script-based compilation, and benchmark execution. |
| [Release Notes](docs/en/release_notes.md) | Provides v1.1.0 version information, performance verification, and compatibility notes. |
| [API Reference](docs/en/api_reference.md) | Provides serialization, ThreadManager, and Header interface descriptions by version. |

## Disclaimer

This repository participates in the open-source FbThrift project and provides performance optimizations for the FbThrift request path and serialization path. The code follows the design and coding style of the open-source software and retains the necessary compatibility and fallback mechanisms. Any vulnerabilities and security issues in the software are addressed by the corresponding upstream community in accordance with its vulnerability and security response mechanisms. Please pay close attention to notifications and version updates released by the upstream community.

## License

FbThrift is licensed under Apache-2.0. For details, see [LICENSE](LICENSE).

The documentation of this project is licensed under CC-BY 4.0. For details, see [LICENSE](docs/LICENSE).

## Contribution Statement

We welcome your contributions to the community. If you have any questions/suggestions or want to provide feedback on feature requirements and bug reports, you can submit [issues](https://gitcode.com/boostkit/community/blob/master/docs/contributor/issue-submit.md). For details, see the [contribution guideline](https://gitcode.com/boostkit/community/blob/master/docs/contributor/contributing.md). You are also welcome to share insights in [Discussions](https://gitcode.com/boostkit/community/discussions). Thank you for your support.

## Acknowledgments

Thank you for every PR from the community. Contributions to FbThrift are welcome!
