# Introduction to fbthrift Serialization Optimization

## Latest Updates

- [2026-06-30]: Released the patch repository v1.0.0 based on Meta's open-source fbthrift serialization. This optimization targets the fbthrift serialization framework to perform batch integer array encoding, significantly improving serialization performance through the Arm SVE2 instruction set and automatic compiler vectorization.

## Project Introduction

fbthrift is a high-performance RPC framework and serialization library open-sourced by Meta, widely used in distributed systems and microservice architectures. It supports multiple languages (such as C++, Java, and Python), and its serialization component provides a rich set of serialization protocols (such as Compact Protocol and Binary Protocol).

This project is an optimization repository targeting the fbthrift serialization framework, focusing on the serialization performance optimization of contiguous integer arrays (such as `list<int32_t>` and `list<int64_t>`). By default, fbthrift employs per-element encoding and decoding. This approach involves strong data dependencies, making it difficult for the compiler to perform auto-vectorization (SIMD), which causes serialization to become a bottleneck when processing large-scale arrays.

The core ideas of this optimization solution are as follows:

- **Compact Protocol**: Introduces batch Varint encoding logic based on Arm SVE2, utilizing `dispatchVarintEncode32` for cross-translation-unit runtime dispatch, which automatically enables the vectorized kernel on CPUs supporting SVE2.
- **Binary Protocol**: Introduces a tight loop based on `memcpy` and `bswap`, leveraging automatic compiler vectorization to boost performance without requiring specific CPU instruction set support.
- **Zero intrusion**: Remains transparent to downstream service code without requiring any changes to compilation options or service logic, as the SFINAE mechanism automatically selects the optimal path.

## Directory Structure

```text
fbthrift/
├── docs/                           # Documentation directory
│   ├── en/                         # English documentation
│   │   ├── api_reference.md        # API reference
│   │   ├── quick_start.md          # Quick Start
│   │   └── release_notes.md        # Release Notes
│   └── LICENSE
├── LICENSE
├── fbthrift_opt_simd.patch         # fbthrift serialization optimization patch
└── README_EN.md                    # Project introduction
```

## Release Notes

For details about the version release of the fbthrift serialization optimization patch repository, see [Release Notes](docs/en/release_notes.md).

## Quick Start

For details about how to compile, install, and test the fbthrift serialization optimization patch, see [Quick Start](docs/en/quick_start.md).

## Documentation

| Document Name| Description|
|---------|---------|
| [Quick Start](docs/en/quick_start.md)| Provides guidance on how to compile, install, and test the fbthrift serialization optimization patch repository.|
| [Release Notes](docs/en/release_notes.md)| Provides basic version information and updates of the fbthrift serialization optimization patch repository.|
| [API Reference](docs/en/api_reference.md)| Describes the optimized fbthrift serialization APIs and related modifications.|

## Disclaimer

This code repository contributes to the fbthrift open-source project solely for performance optimization of certain fbthrift serialization functions. It strictly adheres to the coding style and methods, as well as security design of the native open-source software. Any vulnerability and security issues of the software shall be resolved by the corresponding upstream communities according to their response mechanisms. Please pay attention to the notifications and version updates released by the upstream communities. This code repository does not assume any responsibility for software vulnerabilities and security issues.

## License

The fbthrift patch repository is licensed under Apache-2.0. For details, see [LICENSE](LICENSE).

The documents of this project are licensed under CC-BY 4.0. For details, see [LICENSE](docs/LICENSE).

## Contribution Guide

If you have any questions or want to provide feedback on feature requirements and bug reports, you can submit an issue.

## Suggestions and Feedback

You are welcome to contribute to the community. If you have any questions or suggestions, please submit an issue. We will respond as soon as possible. Thank you for your support.

## Acknowledgments

The fbthrift patch repository is jointly developed by the following Huawei department:

Kunpeng Computing BoostKit Development Dept

Thank you to everyone in the community for your PRs. We warmly welcome contributions to the fbthrift patch repository!
