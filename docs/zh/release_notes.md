# 版本说明书

独立发布的 URMA 传输补丁见[URMA补丁说明](#urma补丁说明)。

## 版本配套说明

### 产品版本信息

<a name="table62675726"></a>
<table><tbody><tr id="row41561572"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.1.1"><p id="p11044137"><a name="p11044137"></a><a name="p11044137"></a>产品名称</p>
</th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.1.1 "><p id="p1597721693713"><a name="p1597721693713"></a><a name="p1597721693713"></a>Kunpeng BoostKit</p>
</td>
</tr>
<tr id="row24726251"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.2.1"><p id="p56669300"><a name="p56669300"></a><a name="p56669300"></a>产品版本</p>
</th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.2.1 "><p id="p11923034"><a name="p11923034"></a><a name="p11923034"></a><span id="text189831542174711"><a name="text189831542174711"></a><a name="text189831542174711"></a>26.2.RC1</span></p>
</td>
</tr>
<tr id="row1930811171892"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.3.1"><p id="p2030912172097"><a name="p2030912172097"></a><a name="p2030912172097"></a>软件名称</p>
</th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.3.1 "><p id="p1730912179911"><a name="p1730912179911"></a><a name="p1730912179911"></a>FbThrift性能优化补丁</p>
</td>
</tr>
<tr id="row34726251"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.2.1"><p id="p56669300"><a name="p56669300"></a><a name="p56669300"></a>软件版本</p>
</th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.2.1 "><p id="p11923034"><a name="p11923034"></a><a name="p11923034"></a><span id="text189831542174711"><a name="text189831542174711"></a><a name="text189831542174711"></a>V1.1.0</span></p>
</td>
</tr>
</tbody>
</table>

### 与操作系统、编译器和CPU配套说明

|操作系统|CPU类型|编译器|
|--|--|--|
|Debian 12|鲲鹏920处理器、鲲鹏950处理器|Clang 16|

>![表示说明的图片](public_sys-resources/icon-note.gif) **说明：**
>Binary Protocol优化不依赖SVE2指令集，在所有aarch64和x86-64平台上均可通过编译器自动向量化获得性能提升。Compact Protocol的SVE2优化仅在支持SVE2指令集的CPU上生效，不支持时自动回退到scalar路径。

### 病毒扫描结果

不涉及软件包发布，不涉及病毒扫描。

## 版本兼容性说明

### FbThrift V1.1.0兼容性与风险说明

- ThreadManager保留原有`FunctionRunner`和`std::shared_ptr<Runnable>`接口；如出现兼容性问题，可通过`--thrift_thread_manager_direct_func_enabled=false`回退。
- `THeader::removeHeader()`新增`frameLength`引用参数，`FramingHandler::removeFrame()`返回值由三元组扩展为四元组。自定义调用方、派生类及Python/Cython绑定必须同步修改后重新编译。
- Header自适应逻辑会刷新Pipeline读缓冲设置，依赖`setReadBufferSize()`固定值的业务需要执行专项回归。
- 2KB至512KB范围、16倍倍率及10样本权重为当前经验参数，建议结合实际请求大小和延迟目标继续压测调优。

## 版本使用注意事项

版本使用注意事项具体请参见《[快速入门](./quick_start.md)》。

## 版本更新说明

### FbThrift V1.1.0

#### FbThrift V1.1.0新增特性

|特性描述|更新说明|
|--|--|
|ThreadManager直接调度folly::Func|ThreadManager::Task使用`std::variant<std::shared_ptr<Runnable>`, folly::Func>保存任务。普通RPC通过addFunc()直接入队和执行，消除常规热路径上的FunctionRunner分配、shared_ptr引用计数及虚函数转发。|
|旧任务接口惰性兼容|getRunnable()仅在removeNextPending()、任务过期回调等旧接口明确需要Runnable时，才延迟创建FunctionRunner。原有显式Runnable提交路径保持不变。|
|ThreadManager运行时回退|新增thrift_thread_manager_direct_func_enabled开关，默认启用直接调度；设置为false可立即恢复原有FunctionRunner::create()路径。|
|Header帧长度透传|THeader::removeHeader()新增frameLength引用参数，HeaderClientChannel、HeaderServerChannel及DuplexChannel将完整帧长度透传至FramingHandler。|
|Header自适应读缓冲|FramingHandler::read()根据帧长度维护10样本权重的滑动平均值，以平均帧长16倍动态设置读缓冲，并将范围限制在2KB至512KB。|
|专项单元测试|ThreadManager新增5个直接调度专项UT；Header帧长和FramingHandler补充14个测试源码用例，覆盖正常帧、空队列、空指针、分帧及自适应计算路径。|

### FbThrift V1.0.0

#### FbThrift V1.0.0新增特性

|特性描述|更新说明|
|--|--|
|Compact Protocol批量Varint编码优化|针对连续整型数组（int32/int64），引入基于ARM SVE2指令集的批量Varint编码内核。通过dispatchVarintEncode32/dispatchVarintEncode64进行跨TU运行时分发，在支持SVE2的CPU上自动启用向量化内核，不支持时回退到scalar路径。|
|Binary Protocol批量写入优化|针对连续整型数组（int16/int32/int64），引入基于memcpy`和bswap的紧凑循环，利用编译器自动向量化提升性能。在aarch64上自动展开为NEON指令，在x86-64上自动展开为SSE2指令。|
|编译期SFINAE自动分发|通过if constexpr和has_batched_int_list_writer探针，在编译期自动识别协议和容器类型，对符合条件的连续整型数组自动选择批量处理路径，对不满足条件的类型透明回退到逐元素处理，对下游业务代码零侵入。|

#### FbThrift V1.1.0优化介绍

|协议|优化类型|支持元素类型|触发条件|
|--|--|--|--|
|Compact Protocol|ARM SVE2批量Varint编码|int32_t, int64_t|编译开启THRIFT_ENABLE_ARM_SVE2且运行时CPU支持SVE2。|
|Compact Protocol|Scalar批量Varint编码（回退）|int32_t, int64_t|编译未开启SVE2或运行时CPU不支持SVE2。|
|Binary Protocol|编译器自动向量化批量写入|int16_t, int32_t, int64_t|容器为连续内存布局。|

## URMA补丁说明

### 产品版本信息

FbThrift URMA 补丁的发布日期为 2026-12-30。将 [fbthrift_urma.patch](../../fbthrift_urma.patch) 直接应用于干净的 Meta FbThrift `v2022.11.14.00` 源码，完整性校验使用随包的 [SHA-256 文件](../../fbthrift_urma.patch.sha256)。

|软件|配套要求|
|--|--|
|Folly|使用 [BoostKit Folly URMA 分支](https://gitcode.com/boostkit/folly/tree/dev_urma_20221104)，编译时启用 `FOLLY_USE_URMA`。FbThrift、Fizz、Wangle 和应用使用同一份 Folly。|
|Fizz、Wangle|与 FbThrift 配套的 `v2022.11.14.00`。|
|URMA 运行环境|编译需要 UMDK 的 `urma_api.h` 和 `liburma.so`。运行 URMA 的客户端和服务端各自需要可用的本地设备。|

### URMA新增特性

|特性描述|更新说明|
|--|--|
|同端口接入|普通 TCP 和 URMA 客户端可连接同一明文服务端口。识别到 URMA HELLO 前缀后开始握手计时，普通 TCP 连接沿用原有空闲超时。|
|Header 和 Rocket 收发|批量写入保留每条序列化消息的边界。小消息通过 SEND/RECV 传输，超过 Folly 的 READ 阈值且双方支持时，由接收端单边读取正文。|
|接收缓冲生命周期|Header 和 Rocket 的解析器可持有接收正文的 IOBuf。业务反序列化到 `std::string` 时仍会复制数据。|
|Rocket 发送控制|URMA 上报待发送字节和仍持有的写缓冲容量，Rocket 使用这些信息执行发送背压与内存限制。|

### URMA兼容性与使用注意事项

- 服务端默认使用 TCP。启用 URMA 时须显式指定本地设备。设备初始化或 URMA 握手失败时向调用方报错。
- URMA 使用明文传输。显式要求 TLS、配置 TLS 凭据、启用 io_uring 套接字、双工服务或使用自定义 acceptor 时，服务端启动会拒绝该配置。
- 同一进程内的 URMA 服务端须使用相同配置。启用 Folly 注册内存池后，该进程内的普通 TCP 连接也可能从中分配 IOBuf。

编译与同端口接入验证见《[URMA补丁快速入门](quick_start.md#urma补丁快速入门)》。服务端配置和客户端通道示例见《[URMA补丁接口说明](api_reference.md#urma补丁接口说明)》。

## 版本配套文档

### FbThrift V1.1.0版本配套文档

<a name="table1191773710200"></a>
<table><thead align="left"><tr id="row1291816372202"><th class="cellrowborder" valign="top" width="45.019999999999996%" id="mcps1.1.4.1.1"><p id="p291823714205"><a name="p291823714205"></a><a name="p291823714205"></a>文档名称</p>
</th>
<th class="cellrowborder" valign="top" width="38.019999999999996%" id="mcps1.1.4.1.2"><p id="p13918183762016"><a name="p13918183762016"></a><a name="p13918183762016"></a>内容简介</p>
</th>
<th class="cellrowborder" valign="top" width="16.96%" id="mcps1.1.4.1.3"><p id="p89181437152019"><a name="p89181437152019"></a><a name="p89181437152019"></a>交付形式</p>
</th>
</tr>
</thead>
<tbody><tr id="row179181137112015"><td class="cellrowborder" valign="top" width="45.019999999999996%" headers="mcps1.1.4.1.1 "><p id="p1918123710208"><a name="p1918123710208"></a><a name="p1918123710208"></a>《版本说明书》</p>
</td>
<td class="cellrowborder" valign="top" width="38.019999999999996%" headers="mcps1.1.4.1.2 "><p id="p491893752010"><a name="p491893752010"></a><a name="p491893752010"></a>本文档提供FbThrift性能优化补丁仓的版本发布、性能验证和兼容性信息。</p>
</td>
<td class="cellrowborder" valign="top" width="16.96%" headers="mcps1.1.4.1.3 "><p id="p491893752011"><a name="p491893752011"></a><a name="p491893752011"></a>开源仓</p>
</td>
</tr>
<tr id="row939116371143"><td class="cellrowborder" valign="top" width="45.019999999999996%" headers="mcps1.1.4.1.1 "><p id="p1039163711413"><a name="p1039163711413"></a><a name="p1039163711413"></a>《快速入门》</p>
</td>
<td class="cellrowborder" valign="top" width="38.019999999999996%" headers="mcps1.1.4.1.2 "><p id="p1139217371746"><a name="p1139217371746"></a><a name="p1139217371746"></a>本文档提供序列化、ThreadManager和Header读缓冲优化的构建与使用指导。</p>
</td>
<td class="cellrowborder" valign="top" width="16.96%" headers="mcps1.1.4.1.3 "><p id="p1139217371747"><a name="p1139217371747"></a><a name="p1139217371747"></a>开源仓</p>
</td>
</tr>
<tr id="row2918153732018"><td class="cellrowborder" valign="top" width="45.019999999999996%" headers="mcps1.1.4.1.1 "><p id="p598512211215"><a name="p598512211215"></a><a name="p598512211215"></a>《API参考》</p>
</td>
<td class="cellrowborder" valign="top" width="38.019999999999996%" headers="mcps1.1.4.1.2 "><p id="p15918183742019"><a name="p15918183742019"></a><a name="p15918183742019"></a>本文档提供序列化、ThreadManager和Header帧处理相关接口定义与说明。</p>
</td>
<td class="cellrowborder" valign="top" width="16.96%" headers="mcps1.1.4.1.3 "><p id="p15918183742029"><a name="p15918183742029"></a><a name="p15918183742029"></a>开源仓</p>
</td>
</tr>
</tbody>
</table>

### 获取文档的方法

您可以通过访问[开源仓](https://gitcode.com/boostkit/fbthrift)浏览和获取相关文档。
