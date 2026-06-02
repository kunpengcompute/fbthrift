# API参考

## 函数说明

fbthrift序列化优化补丁仓已优化和新增的函数如[**表1** fbthrift序列化优化函数列表](#fbthrift序列化优化函数列表)所示。

**表1** fbthrift序列化优化函数列表<a id="fbthrift序列化优化函数列表"></a>

|名称|所属模块|说明|
|--|--|--|
|writeI16List|Binary Protocol|新增接口，批量写入int16连续数组。|
|writeI32List|Compact Protocol / Binary Protocol|新增接口，批量写入int32连续数组。Compact Protocol中使用SVE2内核加速Varint编码。|
|writeI64List|Compact Protocol / Binary Protocol|新增接口，批量写入int64连续数组。Compact Protocol中使用SVE2内核加速Varint编码。|
|dispatchVarintEncode32|Compact Protocol|新增接口，跨TU运行时分发函数，根据编译选项和运行时CPU能力选择scalar或SVE2内核。|
|dispatchVarintEncode64|Compact Protocol|新增接口，跨TU运行时分发函数，根据编译选项和运行时CPU能力选择scalar或SVE2内核。|
|hasRuntimeSve2|Compact Protocol|新增接口，运行时检测CPU是否支持SVE2指令集。|
|writeBeContiguous|Binary Protocol|新增接口，紧凑循环辅助函数，执行bswap和memcpy批量写入。|

## 函数定义

### Compact Protocol接口

#### writeI32List（Compact Protocol）

**函数功能**

在CompactProtocolWriter中批量写入int32连续数组。该函数负责写入List的Header，然后调用跨编译单元的分发函数`dispatchVarintEncode32`进行批量Varint编码，最后写入Trailer。当运行时CPU支持SVE2指令集时，自动启用向量化内核加速编码。

**函数定义**

```cpp
uint32_t CompactProtocolWriter::writeI32List(const int32_t* data, uint32_t size);
```

**参数说明**

|参数名|描述|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int32数组的指针|有效的int32_t指针|输入|
|size|数组元素个数|非负整数|输入|

**返回值**

返回写入的总字节数。

#### writeI64List（Compact Protocol）

**函数功能**

在CompactProtocolWriter中批量写入int64连续数组。逻辑与`writeI32List`一致，调用`dispatchVarintEncode64`进行批量Varint编码。

**函数定义**

```cpp
uint32_t CompactProtocolWriter::writeI64List(const int64_t* data, uint32_t size);
```

**参数说明**

|参数名|描述|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int64数组的指针|有效的int64_t指针|输入|
|size|数组元素个数|非负整数|输入|

**返回值**

返回写入的总字节数。

#### dispatchVarintEncode32

**函数功能**

跨编译单元的运行时分发函数。根据编译时是否定义了`THRIFT_HAS_ARM_SVE2`宏以及运行时`hasRuntimeSve2()`的检测结果，选择执行SVE2内核路径或scalar回退路径。这是唯一一个跨编译单元的调用，定义在`CompactProtocolSve.cpp`中，隔离编译以防止全局编译选项污染。

**函数定义**

```cpp
uint32_t dispatchVarintEncode32(const int32_t* data, uint32_t size, folly::io::QueueAppender& out);
```

**参数说明**

|参数名|描述|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int32数组的指针|有效的int32_t指针|输入|
|size|数组元素个数|非负整数|输入|
|out|QueueAppender输出对象|有效的QueueAppender引用|输入/输出|

**返回值**

返回编码写入的总字节数。

**分发逻辑**

|构建方式|THRIFT_HAS_ARM_SVE2宏|hasRuntimeSve2()|命中分支|
|--|--|--|--|
|CMake未开启THRIFT_ENABLE_ARM_SVE2|未定义|编译期剔除|永远走scalar路径|
|开启SVE2，但运行在老CPU|定义|false|scalar路径|
|开启SVE2，且运行在支持的CPU（如Graviton3）|定义|true|SVE2 kernel路径|

#### dispatchVarintEncode64

**函数功能**

与`dispatchVarintEncode32`逻辑一致，针对int64类型进行批量Varint编码分发。

**函数定义**

```cpp
uint32_t dispatchVarintEncode64(const int64_t* data, uint32_t size, folly::io::QueueAppender& out);
```

**参数说明**

|参数名|描述|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int64数组的指针|有效的int64_t指针|输入|
|size|数组元素个数|非负整数|输入|
|out|QueueAppender输出对象|有效的QueueAppender引用|输入/输出|

**返回值**

返回编码写入的总字节数。

#### hasRuntimeSve2

**函数功能**

运行时检测当前CPU是否支持SVE2指令集。该函数通过读取CPU特性寄存器（如`getauxval(AT_HWCAP)`）进行判断，结果被缓存以避免重复检测的开销。

**函数定义**

```cpp
bool hasRuntimeSve2();
```

**返回值**

|返回值|说明|
|--|--|
|true|当前CPU支持SVE2指令集|
|false|当前CPU不支持SVE2指令集|

### Binary Protocol接口

#### writeI16List（Binary Protocol）

**函数功能**

在BinaryProtocolWriter中批量写入int16连续数组。通过一次性`ensure`扩容，在紧凑循环中执行`bswap`和`memcpy`，最后通过`out_.append(total)`一次性提交所有写入。编译器会自动将内层循环向量化。

**函数定义**

```cpp
uint32_t BinaryProtocolWriter::writeI16List(const int16_t* data, uint32_t size);
```

**参数说明**

|参数名|描述|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int16数组的指针|有效的int16_t指针|输入|
|size|数组元素个数|非负整数|输入|

**返回值**

返回写入的总字节数。

#### writeI32List（Binary Protocol）

**函数功能**

在BinaryProtocolWriter中批量写入int32连续数组。逻辑与`writeI16List`一致。

**函数定义**

```cpp
uint32_t BinaryProtocolWriter::writeI32List(const int32_t* data, uint32_t size);
```

**参数说明**

|参数名|描述|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int32数组的指针|有效的int32_t指针|输入|
|size|数组元素个数|非负整数|输入|

**返回值**

返回写入的总字节数。

#### writeI64List（Binary Protocol）

**函数功能**

在BinaryProtocolWriter中批量写入int64连续数组。逻辑与`writeI16List`一致。

**函数定义**

```cpp
uint32_t BinaryProtocolWriter::writeI64List(const int64_t* data, uint32_t size);
```

**参数说明**

|参数名|描述|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int64数组的指针|有效的int64_t指针|输入|
|size|数组元素个数|非负整数|输入|

**返回值**

返回写入的总字节数。

#### writeBeContiguous

**函数功能**

Binary Protocol的紧凑循环辅助函数，执行大端字节序的批量写入。在一个紧凑的循环中依次对每个元素执行`bswap`转换为网络字节序，然后`memcpy`写入输出缓冲区并前移指针。该循环天然契合编译器自动向量化优化。

**函数定义**

```cpp
namespace detail {
template <class T>
void writeBeContiguous(folly::io::QueueAppender& out, const T* data, uint32_t size);
}
```

**参数说明**

|参数名|描述|取值范围|输入/输出|
|--|--|--|--|
|out|QueueAppender输出对象|有效的QueueAppender引用|输入/输出|
|data|指向连续整型数组的指针|有效的T类型指针（T为int16_t/int32_t/int64_t）|输入|
|size|数组元素个数|非负整数|输入|

**返回值**

无返回值。

## 编译期分发机制

优化方案通过SFINAE（Substitution Failure Is Not An Error）技术在编译期自动选择最优序列化路径。当以下所有条件同时满足时，才会走批量处理API.

- **元素类型**：必须是`int16_t`、`int32_t`或`int64_t`。
- **协议支持**：`has_batched_int_list_writer<Protocol, Elem>`为true，即Protocol必须提供`writeI{N}List`成员（目前仅`CompactProtocolWriter`和`BinaryProtocolWriter`满足，`JSONProtocolWriter`在编译期被剔除）。
- **容器连续性**：`is_contiguous_elem_container<Container, Elem>`为true，即容器必须提供连续的内存布局（如`std::vector`满足，而`std::list`或`std::deque`不满足）。

对于不满足上述条件的类型或容器，SFINAE机制自动回退到原有的逐元素序列化循环，确保兼容性。
