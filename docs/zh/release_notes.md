# 版本说明书

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
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.2.1 "><p id="p11923034"><a name="p11923034"></a><a name="p11923034"></a><span id="text189831542174711"><a name="text189831542174711"></a><a name="text189831542174711"></a>26.1.RC1</span></p>
</td>
</tr>
<tr id="row1930811171892"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.3.1"><p id="p2030912172097"><a name="p2030912172097"></a><a name="p2030912172097"></a>软件名称</p>
</th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.3.1 "><p id="p1730912179911"><a name="p1730912179911"></a><a name="p1730912179911"></a>FbThrift性能优化补丁仓</p>
</td>
</tr>
<tr id="row34726251"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.2.1"><p id="p56669300"><a name="p56669300"></a><a name="p56669300"></a>软件版本</p>
</th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.2.1 "><p id="p11923034"><a name="p11923034"></a><a name="p11923034"></a><span id="text189831542174711"><a name="text189831542174711"></a><a name="text189831542174711"></a>v1.2.0</span></p>
</td>
</tr>
</tbody>
</table>

### 与操作系统、编译器和CPU配套说明

|操作系统|CPU类型|编译器|
|--|--|--|
|Debian 12等Linux系统|鲲鹏920、鲲鹏950等AArch64处理器|Clang 16|

> **说明：** Binary Protocol优化不依赖SVE2指令集，在所有aarch64和x86-64平台上均可通过编译器自动向量化获得性能提升。Compact Protocol的SVE2优化仅在支持SVE2指令集的CPU上生效，不支持时自动回退到scalar路径。

## 版本更新说明

### v1.2.0

**新增特性**

|特性描述|更新说明|
|--|--|
|ThreadManager直接调度`folly::Func`|`ThreadManager::Task`使用`std::variant<std::shared_ptr<Runnable>, folly::Func>`保存任务。普通RPC通过`addFunc()`直接入队和执行，消除常规热路径上的`FunctionRunner`分配、`shared_ptr`引用计数及虚函数转发。|
|旧任务接口惰性兼容|`getRunnable()`仅在`removeNextPending()`、任务过期回调等旧接口明确需要`Runnable`时，才延迟创建`FunctionRunner`。原有显式`Runnable`提交路径保持不变。|
|ThreadManager运行时回退|新增`thrift_thread_manager_direct_func_enabled`开关，默认启用直接调度；设置为`false`可立即恢复原有`FunctionRunner::create()`路径。|
|Header帧长度透传|`THeader::removeHeader()`新增`frameLength`输出参数，HeaderClientChannel、HeaderServerChannel及DuplexChannel将完整帧长度透传至FramingHandler。|
|Header自适应读缓冲|`FramingHandler::read()`根据帧长度维护10样本权重的滑动平均值，以平均帧长16倍动态设置读缓冲，并将范围限制在2KB至512KB。|
|专项单元测试|ThreadManager新增5个直接调度专项UT；Header帧长和FramingHandler补充14个测试源码用例，覆盖正常帧、空队列、空指针、分帧及自适应计算路径。|

**性能验证参考**

ThreadManager优化在鲲鹏950测试环境完成Header与Rocket交替A/B验证。测试结果如下表所示，数据用于说明当前基准场景的收益，不作为所有业务负载的固定性能承诺。

|测试协议|性能指标|平均变化|
|--|--|--|
|Header|QPS|提升12.21%|
|Header|吞吐量|提升12.21%|
|Header|P99延迟|降低10.25%|
|Rocket|QPS|提升2.67%|
|Rocket|服务端CPU效率|提升10.07%|

**兼容性与风险说明**

- ThreadManager保留原有`FunctionRunner`和`std::shared_ptr<Runnable>`接口；如出现兼容性问题，可通过`--thrift_thread_manager_direct_func_enabled=false`回退。
- `THeader::removeHeader()`新增`frameLength`引用参数，`FramingHandler::removeFrame()`返回值由三元组扩展为四元组。自定义调用方、派生类及Python/Cython绑定必须同步修改后重新编译。
- Header自适应逻辑会刷新Pipeline读缓冲设置，依赖`setReadBufferSize()`固定值的业务需要执行专项回归。
- 2KB至512KB范围、16倍倍率及10样本权重为当前经验参数，建议结合实际请求大小和延迟目标继续压测调优。
- PR 11合入前需完成空队列长度获取、Python/Cython与ChannelTest签名同步、CTest注册等审查项，确保异常路径和新增测试能够进入持续集成。

**关联变更**

- [PR 10：优化ThreadManager folly::Func任务调度](https://gitcode.com/boostkit/fbthrift/pull/10)
- [PR 11：增加帧长透传与自适应读缓冲](https://gitcode.com/boostkit/fbthrift/pull/11)

### v1.0.0

**新增特性**

|特性描述|更新说明|
|--|--|
|Compact Protocol批量Varint编码优化|针对连续整型数组（int32/int64），引入基于ARM SVE2指令集的批量Varint编码内核。通过`dispatchVarintEncode32`/`dispatchVarintEncode64`进行跨TU运行时分发，在支持SVE2的CPU上自动启用向量化内核，不支持时回退到scalar路径。|
|Binary Protocol批量写入优化|针对连续整型数组（int16/int32/int64），引入基于`memcpy`和`bswap`的紧凑循环，利用编译器自动向量化提升性能。在aarch64上自动展开为NEON指令，在x86-64上自动展开为SSE2指令。|
|编译期SFINAE自动分发|通过`if constexpr`和`has_batched_int_list_writer`探针，在编译期自动识别协议和容器类型，对符合条件的连续整型数组自动选择批量处理路径，对不满足条件的类型透明回退到逐元素处理，对下游业务代码零侵入。|

**优化范围**

|协议|优化类型|支持元素类型|触发条件|
|--|--|--|--|
|Compact Protocol|ARM SVE2批量Varint编码|int32_t, int64_t|编译开启`THRIFT_ENABLE_ARM_SVE2`且运行时CPU支持SVE2。|
|Compact Protocol|Scalar批量Varint编码（回退）|int32_t, int64_t|编译未开启SVE2或运行时CPU不支持SVE2。|
|Binary Protocol|编译器自动向量化批量写入|int16_t, int32_t, int64_t|容器为连续内存布局。|

## 版本配套文档

### v1.2.0版本配套文档

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
