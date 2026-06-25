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
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.3.1 "><p id="p1730912179911"><a name="p1730912179911"></a><a name="p1730912179911"></a>FbThrift序列化优化补丁仓</p>
</td>
</tr>
<tr id="row24726251"><th class="firstcol" valign="top" width="42.17%" id="mcps1.1.3.2.1"><p id="p56669300"><a name="p56669300"></a><a name="p56669300"></a>软件版本</p>
</th>
<td class="cellrowborder" valign="top" width="57.830000000000005%" headers="mcps1.1.3.2.1 "><p id="p11923034"><a name="p11923034"></a><a name="p11923034"></a><span id="text189831542174711"><a name="text189831542174711"></a><a name="text189831542174711"></a>v1.0.0</span></p>
</td>
</tr>
</tbody>
</table>

### 与操作系统、编译器和CPU配套说明

|操作系统|CPU类型|编译器|
|--|--|--|
|Debian 12等支持ARM SVE2指令集的Linux系统|鲲鹏920处理器 |Clang 16|

> **说明：** Binary Protocol优化不依赖SVE2指令集，在所有aarch64和x86-64平台上均可通过编译器自动向量化获得性能提升。Compact Protocol的SVE2优化仅在支持SVE2指令集的CPU上生效，不支持时自动回退到scalar路径。

## 版本更新说明

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

### v1.0.0版本配套文档

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
<td class="cellrowborder" valign="top" width="38.019999999999996%" headers="mcps1.1.4.1.2 "><p id="p491893752010"><a name="p491893752010"></a><a name="p491893752010"></a>本文档提供FbThrift序列化优化补丁仓的版本发布信息。</p>
</td>
<td class="cellrowborder" valign="top" width="16.96%" headers="mcps1.1.4.1.3 "><p id="p491893752011"><a name="p491893752011"></a><a name="p491893752011"></a>开源仓</p>
</td>
</tr>
<tr id="row939116371143"><td class="cellrowborder" valign="top" width="45.019999999999996%" headers="mcps1.1.4.1.1 "><p id="p1039163711413"><a name="p1039163711413"></a><a name="p1039163711413"></a>《快速入门》</p>
</td>
<td class="cellrowborder" valign="top" width="38.019999999999996%" headers="mcps1.1.4.1.2 "><p id="p1139217371746"><a name="p1139217371746"></a><a name="p1139217371746"></a>本文档提供FbThrift序列化优化补丁仓的快速上手指导。</p>
</td>
<td class="cellrowborder" valign="top" width="16.96%" headers="mcps1.1.4.1.3 "><p id="p1139217371747"><a name="p1139217371747"></a><a name="p1139217371747"></a>开源仓</p>
</td>
</tr>
<tr id="row2918153732018"><td class="cellrowborder" valign="top" width="45.019999999999996%" headers="mcps1.1.4.1.1 "><p id="p598512211215"><a name="p598512211215"></a><a name="p598512211215"></a>《API参考》</p>
</td>
<td class="cellrowborder" valign="top" width="38.019999999999996%" headers="mcps1.1.4.1.2 "><p id="p15918183742019"><a name="p15918183742019"></a><a name="p15918183742019"></a>本文档提供FbThrift序列化优化相关接口定义和说明。</p>
</td>
<td class="cellrowborder" valign="top" width="16.96%" headers="mcps1.1.4.1.3 "><p id="p15918183742029"><a name="p15918183742029"></a><a name="p15918183742029"></a>开源仓</p>
</td>
</tr>
</tbody>
</table>

### 获取文档的方法

您可以通过访问[开源仓](https://gitcode.com/boostkit/fbthrift)浏览和获取相关文档。
