# 版本说明书

## 版本配套说明

### 产品版本信息

|产品名称|Kunpeng BoostKit|
|--|--|
|产品版本|26.1.0|
|软件名称|fbthrift序列化优化补丁仓|
|软件包版本|1.0.0|

### 与操作系统、编译器和CPU配套说明

|操作系统|CPU类型|编译器|
|--|--|--|
|Debian 12 等支持ARM SVE2指令集的Linux系统|鲲鹏920处理器 |clang-16|

> **说明：** Binary Protocol优化不依赖SVE2指令集，在所有aarch64和x86-64平台上均可通过编译器自动向量化获得性能提升。Compact Protocol的SVE2优化仅在支持SVE2指令集的CPU上生效，不支持时自动回退到scalar路径。

## 版本更新说明

### V1.0.0

**新增特性**

|特性描述|更新说明|
|--|--|
|Compact Protocol批量Varint编码优化|针对连续整型数组（int32/int64），引入基于ARM SVE2指令集的批量Varint编码内核。通过`dispatchVarintEncode32`/`dispatchVarintEncode64`进行跨TU运行时分发，在支持SVE2的CPU上自动启用向量化内核，不支持时回退到scalar路径。|
|Binary Protocol批量写入优化|针对连续整型数组（int16/int32/int64），引入基于`memcpy`和`bswap`的紧凑循环，利用编译器自动向量化提升性能。在aarch64上自动展开为NEON指令，在x86-64上自动展开为SSE2指令。|
|编译期SFINAE自动分发|通过`if constexpr`和`has_batched_int_list_writer`探针，在编译期自动识别协议和容器类型，对符合条件的连续整型数组自动选择批量处理路径，对不满足条件的类型透明回退到逐元素处理，对下游业务代码零侵入。|

**优化范围**

|协议|优化类型|支持元素类型|触发条件|
|--|--|--|--|
|Compact Protocol|ARM SVE2批量Varint编码|int32_t, int64_t|编译开启`THRIFT_ENABLE_ARM_SVE2`且运行时CPU支持SVE2|
|Compact Protocol|Scalar批量Varint编码（回退）|int32_t, int64_t|编译未开启SVE2或运行时CPU不支持SVE2|
|Binary Protocol|编译器自动向量化批量写入|int16_t, int32_t, int64_t|容器为连续内存布局|
