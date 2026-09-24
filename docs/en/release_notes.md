# Release Notes

## Version Mapping

### Product Version

<a name="table62675726"></a>
<table><tbody><tr id="row41561572"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.1.1"><p id="p11044137"><a name="p11044137"></a><a name="p11044137"></a>Product Name</p></th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.1.1 "><p id="p1597721693713"><a name="p1597721693713"></a><a name="p1597721693713"></a>Kunpeng BoostKit</p></td>
</tr>
<tr id="row24726251"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.2.1"><p id="p56669300"><a name="p56669300"></a><a name="p56669300"></a>Product Version</p></th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.2.1 "><p id="p11923034"><a name="p11923034"></a><a name="p11923034"></a><span id="text189831542174711"><a name="text189831542174711"></a><a name="text189831542174711"></a>26.2.RC1</span></p></td>
</tr>
<tr id="row1930811171892"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.3.1"><p id="p2030912172097"><a name="p2030912172097"></a><a name="p2030912172097"></a>Software Name</p></th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.3.1 "><p id="p1730912179911"><a name="p1730912179911"></a><a name="p1730912179911"></a>FbThrift performance optimization patch</p></td>
</tr>
<tr id="row34726251"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.2.1"><p id="p56669300"><a name="p56669300"></a><a name="p56669300"></a>Software Version</p></th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.2.1 "><p id="p11923034"><a name="p11923034"></a><a name="p11923034"></a><span id="text189831542174711"><a name="text189831542174711"></a><a name="text189831542174711"></a>v1.1.0</span></p></td>
</tr>
</tbody>
</table>

### OS, Compiler, and CPU

|OS|CPU|Compiler|
|--|--|--|
|Linux systems such as Debian 12|AArch64 processors such as Kunpeng 920 and Kunpeng 950|Clang 16|

> ![Note icon](public_sys-resources/icon-note.gif)**NOTE**
>
> The Binary Protocol optimization does not depend on the SVE2 instruction set and can achieve performance improvements via automatic compiler vectorization on AArch64 and x86-64 platforms. The SVE2 optimization for the Compact Protocol takes effect only on CPUs that support the SVE2 instruction set, automatically falling back to the scalar path when it is not supported.

### Virus Scan Results

No software package release or virus scanning is involved.

## Version Compatibility Notes

### FbThrift v1.1.0 Compatibility and Risk Description

- ThreadManager retains the original `FunctionRunner` and `std::shared_ptr<Runnable>` interfaces. If compatibility issues occur, you can roll back by using `--thrift_thread_manager_direct_func_enabled=false`.
- `THeader::removeHeader()` adds a `frameLength` reference parameter, and the return value of `FramingHandler::removeFrame()` is expanded from a triple to a quadruple. Custom callers, derived classes, and Python/Cython bindings must be modified synchronously and then recompiled.
- The Header adaptive logic refreshes the Pipeline read buffer settings. Services that depend on a fixed value of `setReadBufferSize()` need to perform dedicated regression testing.
- The range of 2 KB to 512 KB, the 16x multiplier, and the 10-sample weight are current empirical parameters. It is recommended to continue stress testing and tuning based on the actual request size and latency targets.

## Important Notes

For version usage instructions, see [Quick Start](./quick_start.md).

## Change Description

### FbThrift v1.1.0

#### New Features in FbThrift v1.1.0

| Feature | Description |
|--|--|
| Direct scheduling of `folly::Func` by ThreadManager | `ThreadManager::Task` stores tasks using `std::variant<std::shared_ptr<Runnable>, folly::Func>`. Regular RPCs are enqueued and executed directly via `addFunc()`, eliminating `FunctionRunner` allocation, `shared_ptr` reference counting, and virtual function forwarding on the regular hot path. |
| Lazy compatibility for legacy task interfaces | `getRunnable()` defers the creation of a `FunctionRunner` until a legacy interface such as `removeNextPending()` or a task expiration callback explicitly requires a `Runnable`. The existing explicit `Runnable` submission path remains unchanged. |
| ThreadManager runtime fallback | A new `thrift_thread_manager_direct_func_enabled` switch is added, with direct scheduling enabled by default. Setting it to `false` immediately restores the original `FunctionRunner::create()` path. |
| Header frame length propagation | `THeader::removeHeader()` adds a new `frameLength` output parameter, and HeaderClientChannel, HeaderServerChannel, and DuplexChannel propagate the complete frame length to FramingHandler. |
| Header adaptive read buffer | `FramingHandler::read()` maintains a moving average weighted by 10 samples based on frame length, dynamically sets the read buffer to 16 times the average frame length, and limits the range to 2 KB to 512 KB. |
| Dedicated unit tests | ThreadManager adds 5 dedicated unit tests for direct scheduling; Header frame length and FramingHandler add 14 test source cases, covering normal frames, empty queues, null pointers, framing, and adaptive calculation paths. |

### FbThrift v1.0.0

#### New Features in FbThrift v1.0.0

|Feature|Description|
|--|--|
|Compact Protocol batch Varint encoding optimization|Introduces a batch Varint encoding kernel based on the Arm SVE2 instruction set for contiguous integer arrays (int32/int64). It utilizes `dispatchVarintEncode32`/`dispatchVarintEncode64` for cross-translation-unit runtime dispatch, automatically enabling the vectorized kernel on CPUs that support SVE2 and falling back to the scalar path when unsupported.|
|Binary Protocol batch write optimization|Introduces a tight loop based on `memcpy` and `bswap` for contiguous integer arrays (int16/int32/int64), leveraging automatic compiler vectorization to boost performance. The loop automatically unrolls into NEON instructions on AArch64 and into SSE2 instructions on x86-64.|
|Compile-time SFINAE automatic dispatch|Utilizes `if constexpr` and `has_batched_int_list_writer` traits to automatically identify protocol and container types at compile time. It automatically selects the batch processing path for qualified contiguous integer arrays, while transparently falling back to per-element processing for unqualified types, ensuring zero intrusion into downstream service code.|

#### FbThrift v1.0.0 Optimization Introduction

|Protocol|Optimization Type|Supported Element Types|Trigger Condition|
|--|--|--|--|
|Compact Protocol|Arm SVE2 batch Varint encoding|int32_t, int64_t|`THRIFT_ENABLE_ARM_SVE2` is enabled during compilation, and the CPU supports SVE2 at runtime.|
|Compact Protocol|Scalar batch Varint encoding (fallback)|int32_t, int64_t|SVE2 is not enabled during compilation, or the CPU does not support SVE2 at runtime.|
|Binary Protocol|Compiler auto-vectorization batch write|int16_t, int32_t, int64_t|The container uses a contiguous memory layout.|

## Documentation

### FbThrift v1.1.0 Documentation

<a name="table1191773710200"></a>
<table><thead align="left"><tr id="row1291816372202"><th class="cellrowborder" valign="top" width="45.019999999999996%" id="mcps1.1.4.1.1"><p id="p291823714205"><a name="p291823714205"></a><a name="p291823714205"></a>Document Name</p></th>
<th class="cellrowborder" valign="top" width="38.019999999999996%" id="mcps1.1.4.1.2"><p id="p13918183762016"><a name="p13918183762016"></a><a name="p13918183762016"></a>Description</p></th>
<th class="cellrowborder" valign="top" width="16.96%" id="mcps1.1.4.1.3"><p id="p89181437152019"><a name="p89181437152019"></a><a name="p89181437152019"></a>Delivery Method</p></th>
</tr>
</thead>
<tbody><tr id="row179181137112015"><td class="cellrowborder" valign="top" width="45.019999999999996%" headers="mcps1.1.4.1.1 "><p id="p1918123710208"><a name="p1918123710208"></a><a name="p1918123710208"></a>Release Notes</p></td>
<td class="cellrowborder" valign="top" width="38.019999999999996%" headers="mcps1.1.4.1.2 "><p id="p491893752010"><a name="p491893752010"></a><a name="p491893752010"></a>Provides version release, performance verification, and compatibility information for the FbThrift performance optimization patch repository.</p></td>
<td class="cellrowborder" valign="top" width="16.96%" headers="mcps1.1.4.1.3 "><p id="p491893752011"><a name="p491893752011"></a><a name="p491893752011"></a>Open-source repository</p></td>
</tr>
<tr id="row939116371143"><td class="cellrowborder" valign="top" width="45.019999999999996%" headers="mcps1.1.4.1.1 "><p id="p1039163711413"><a name="p1039163711413"></a><a name="p1039163711413"></a>Quick Start</p></td>
<td class="cellrowborder" valign="top" width="38.019999999999996%" headers="mcps1.1.4.1.2 "><p id="p1139217371746"><a name="p1139217371746"></a><a name="p1139217371746"></a>Provides build and usage guidance for serialization, ThreadManager, and Header read buffer optimization.</p></td>
<td class="cellrowborder" valign="top" width="16.96%" headers="mcps1.1.4.1.3 "><p id="p1139217371747"><a name="p1139217371747"></a><a name="p1139217371747"></a>Open-source repository</p></td>
</tr>
<tr id="row2918153732018"><td class="cellrowborder" valign="top" width="45.019999999999996%" headers="mcps1.1.4.1.1 "><p id="p598512211215"><a name="p598512211215"></a><a name="p598512211215"></a>API Reference</p></td>
<td class="cellrowborder" valign="top" width="38.019999999999996%" headers="mcps1.1.4.1.2 "><p id="p15918183742019"><a name="p15918183742019"></a><a name="p15918183742019"></a>Provides the definitions and descriptions of interfaces related to serialization, ThreadManager, and Header frame processing.</p></td>
<td class="cellrowborder" valign="top" width="16.96%" headers="mcps1.1.4.1.3 "><p id="p15918183742029"><a name="p15918183742029"></a><a name="p15918183742029"></a>Open-source repository</p></td>
</tr>
</tbody>
</table>

### FbThrift v1.0.0 Documentation

This document already includes the previous documentation.

### Obtaining Documentation

Visit the [open-source repository](https://gitcode.com/boostkit/fbthrift) to view or download related documents.
