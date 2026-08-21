# API参考

## FbThrift v1.2.0

> 本章介绍FbThrift v1.2.0新增的ThreadManager任务调度与Header帧处理接口。

### FbThrift v1.2.0接口介绍

|接口名称|所属模块|接口说明|
|--|--|--|
|ThreadManager::Impl::addFunc|ThreadManager|直接接收并入队`folly::Func`，避免普通RPC任务预先构造`FunctionRunner`。|
|ThreadManager::Task::getRunnable|ThreadManager|旧接口需要`Runnable`时，按需将`folly::Func`延迟包装为`FunctionRunner`。|
|THeader::removeHeader|Header Transport|解析消息头并通过输出参数返回完整帧长度。|
|FramingHandler::removeFrame|Header Transport|返回解帧结果、剩余长度、Header对象及帧长度。|
|FramingHandler::read|Header Transport|根据帧长度滑动平均值动态调整读缓冲区。|

### ThreadManager任务调度接口

#### ThreadManager::Impl::addFunc

##### ThreadManager::Impl::addFunc函数功能

直接将一次性`folly::Func`构造成`ThreadManager::Task`并进入优先级队列。该接口复用原有的优先级计算、请求上下文保存、QueueObserver通知、任务计数和空闲线程唤醒逻辑，但不在普通RPC热路径上创建`FunctionRunner`及其`shared_ptr`控制块。

##### ThreadManager::Impl::addFunc函数定义

```cpp
void ThreadManager::Impl::addFunc(
    size_t priority,
    folly::Func func,
    int64_t expiration,
    ThreadManager::Source source) noexcept;
```

##### ThreadManager::Impl::addFunc参数说明

|参数名|参数说明|输入/输出|
|--|--|--|
|priority|任务优先级，入队前会与任务来源共同映射为队列优先级|输入|
|func|待执行的一次性、可移动函数对象|输入|
|expiration|排队过期时间，单位为毫秒；0表示不启用过期控制|输入|
|source|任务来源，例如`INTERNAL`或`UPSTREAM`|输入|

##### 执行与回退

`Task`使用`std::variant<std::shared_ptr<Runnable>, folly::Func>`保存两类任务。工作线程在`Task::run()`中直接调用`folly::Func`；`Task::skip()`负责丢弃并释放未执行任务。两条路径都使用`std::exchange`先转移任务所有权，保证任务最多执行一次。

运行时开关`--thrift_thread_manager_direct_func_enabled`默认启用直接调度。将其设置为`false`后，所有相关入口恢复通过`FunctionRunner::create()`包装并调用原有`add()`接口。

#### ThreadManager::Task::getRunnable

##### ThreadManager::Task::getRunnable函数功能

为`removeNextPending()`、任务过期回调等仍要求`std::shared_ptr<Runnable>`的旧接口提供兼容转换。若任务已经保存为`Runnable`则直接返回；若任务保存为`folly::Func`，仅在首次调用时创建`FunctionRunner`并将`variant`切换为`shared_ptr<Runnable>`。

##### ThreadManager::Task::getRunnable函数定义

```cpp
const std::shared_ptr<Runnable>& ThreadManager::Task::getRunnable() const;
```

>**说明**：该接口是惰性兼容路径，不属于普通RPC任务的常规执行路径。

### Header帧解析与自适应读缓冲接口

#### THeader::removeHeader

##### THeader::removeHeader函数功能

从`IOBufQueue`中解析Header帧。除返回消息数据和待补充字节数外，FbThrift v1.2.0新增`frameLength`输出参数，用于向上层传递本次完整帧的实际长度。空帧或数据不完整时返回0。

##### THeader::removeHeader函数定义

```cpp
std::unique_ptr<folly::IOBuf> THeader::removeHeader(
    folly::IOBufQueue* queue,
    size_t& needed,
    StringToStringMap& persistentReadHeaders,
    size_t& frameLength);
```

##### THeader::removeHeader参数说明

|参数名|参数说明|输入/输出|
|--|--|--|
|queue|待解析的输入缓冲队列|输入/输出|
|needed|消息不完整时仍需读取的字节数|输出|
|persistentReadHeaders|连接级持久Header集合|输入/输出|
|frameLength|本次完整帧长度；未形成完整帧时为0|输出|

#### FramingHandler::removeFrame

##### FramingHandler::removeFrame函数功能

Header客户端、服务端及DuplexChannel统一通过四元组返回解帧结果，使`frameLength`能够从`THeader`解析层透传到`FramingHandler::read()`。

##### FramingHandler::removeFrame函数定义

```cpp
virtual std::tuple<
    std::unique_ptr<folly::IOBuf>,
    size_t,
    std::unique_ptr<apache::thrift::transport::THeader>,
    size_t>
removeFrame(folly::IOBufQueue* queue) = 0;
```

四个返回项依次为消息数据、仍需读取的字节数、Header对象和完整帧长度。该接口由三元组扩展为四元组，所有自定义派生类及调用方必须同步更新签名和结构化绑定。

#### FramingHandler::read自适应策略

收到完整帧后，`read()`使用10个样本权重更新请求大小滑动平均值。

```cpp
avgRequestSize_ = avgRequestSize_ == 0
    ? frameLength
    : (avgRequestSize_ * 9 + frameLength) / 10;

readSize = std::clamp(avgRequestSize_ * 16, size_t(2048), size_t(524288));
```

计算结果用于刷新Pipeline读缓冲设置，缓冲区范围限制为2KB至512KB。不完整帧场景还会综合`remaining`与队列尾部可用空间，减少大帧的重复读取，同时避免小请求长期占用过大的固定缓冲区。

> **兼容性说明：** 自适应逻辑会在读路径中刷新缓冲设置。依赖`setReadBufferSize()`固定缓冲区语义的调用方，应在升级到FbThrift v1.2.0时执行回归验证。

## FbThrift v1.0.0

> 本章介绍FbThrift v1.0.0提供的Compact Protocol与Binary Protocol批量序列化接口。

### FbThrift v1.0.0接口介绍

|接口名称|所属模块|接口说明|
|--|--|--|
|writeI16List|Binary Protocol|批量写入int16连续数组。|
|writeI32List|Compact Protocol / Binary Protocol|批量写入int32连续数组，Compact Protocol可使用SVE2内核加速Varint编码。|
|writeI64List|Compact Protocol / Binary Protocol|批量写入int64连续数组，Compact Protocol可使用SVE2内核加速Varint编码。|
|dispatchVarintEncode32|Compact Protocol|根据编译选项和运行时CPU能力选择scalar或SVE2内核。|
|dispatchVarintEncode64|Compact Protocol|根据编译选项和运行时CPU能力选择scalar或SVE2内核。|
|hasRuntimeSve2|Compact Protocol|运行时检测CPU是否支持SVE2指令集。|
|writeBeContiguous|Binary Protocol|通过bswap和memcpy执行批量大端写入。|

### Compact Protocol接口

#### writeI32List（Compact Protocol）

##### writeI32List（Compact Protocol）函数功能

在CompactProtocolWriter中批量写入int32连续数组。该函数负责写入List的Header，然后调用跨编译单元的分发函数`dispatchVarintEncode32`进行批量Varint编码，最后写入Trailer。当运行时CPU支持SVE2指令集时，自动启用向量化内核加速编码。

##### writeI32List（Compact Protocol）函数定义

```cpp
uint32_t CompactProtocolWriter::writeI32List(const int32_t* data, uint32_t size);
```

##### writeI32List（Compact Protocol）参数说明

|参数名|参数说明|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int32数组的指针|有效的int32_t指针|输入|
|size|数组元素个数|非负整数|输入|

##### writeI32List（Compact Protocol）返回值

返回写入的总字节数。

#### writeI64List（Compact Protocol）

##### writeI64List（Compact Protocol）函数功能

在CompactProtocolWriter中批量写入int64连续数组。逻辑与`writeI32List`一致，调用`dispatchVarintEncode64`进行批量Varint编码。

##### writeI64List（Compact Protocol）函数定义

```cpp
uint32_t CompactProtocolWriter::writeI64List(const int64_t* data, uint32_t size);
```

##### writeI64List（Compact Protocol）参数说明

|参数名|参数说明|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int64数组的指针|有效的int64_t指针|输入|
|size|数组元素个数|非负整数|输入|

##### writeI64List（Compact Protocol）返回值

返回写入的总字节数。

#### dispatchVarintEncode32

##### dispatchVarintEncode32函数功能

跨编译单元的运行时分发函数。根据编译时是否定义了`THRIFT_HAS_ARM_SVE2`宏以及运行时`hasRuntimeSve2()`的检测结果，选择执行SVE2内核路径或scalar回退路径。这是唯一一个跨编译单元的调用，定义在`CompactProtocolSve.cpp`中，隔离编译以防止全局编译选项污染。

##### dispatchVarintEncode32函数定义

```cpp
uint32_t dispatchVarintEncode32(const int32_t* data, uint32_t size, folly::io::QueueAppender& out);
```

##### dispatchVarintEncode32参数说明

|参数名|参数说明|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int32数组的指针|有效的int32_t指针|输入|
|size|数组元素个数|非负整数|输入|
|out|QueueAppender输出对象|有效的QueueAppender引用|输入/输出|

##### dispatchVarintEncode32返回值

返回编码写入的总字节数。

##### dispatchVarintEncode32分发逻辑

|构建方式|THRIFT_HAS_ARM_SVE2宏|hasRuntimeSve2()|命中分支|
|--|--|--|--|
|CMake未开启THRIFT_ENABLE_ARM_SVE2|未定义|编译期剔除|永远走scalar路径|
|开启SVE2，但运行在老CPU|定义|false|scalar路径|
|开启SVE2，且运行在支持的CPU（如Graviton3）|定义|true|SVE2 kernel路径|

#### dispatchVarintEncode64

##### dispatchVarintEncode64函数功能

与`dispatchVarintEncode32`逻辑一致，针对int64类型进行批量Varint编码分发。

##### dispatchVarintEncode64函数定义

```cpp
uint32_t dispatchVarintEncode64(const int64_t* data, uint32_t size, folly::io::QueueAppender& out);
```

##### dispatchVarintEncode64参数说明

|参数名|参数说明|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int64数组的指针|有效的int64_t指针|输入|
|size|数组元素个数|非负整数|输入|
|out|QueueAppender输出对象|有效的QueueAppender引用|输入/输出|

##### dispatchVarintEncode64返回值

返回编码写入的总字节数。

#### hasRuntimeSve2

##### hasRuntimeSve2函数功能

运行时检测当前CPU是否支持SVE2指令集。该函数通过读取CPU特性寄存器（如`getauxval(AT_HWCAP)`）进行判断，结果被缓存以避免重复检测的开销。

##### hasRuntimeSve2函数定义

```cpp
bool hasRuntimeSve2();
```

##### hasRuntimeSve2返回值

|返回值|说明|
|--|--|
|true|当前CPU支持SVE2指令集|
|false|当前CPU不支持SVE2指令集|

### Binary Protocol接口

#### writeI16List（Binary Protocol）

##### writeI16List（Binary Protocol）函数功能

在BinaryProtocolWriter中批量写入int16连续数组。通过一次性`ensure`扩容，在紧凑循环中执行`bswap`和`memcpy`，最后通过`out_.append(total)`一次性提交所有写入。编译器会自动将内层循环向量化。

##### writeI16List（Binary Protocol）函数定义

```cpp
uint32_t BinaryProtocolWriter::writeI16List(const int16_t* data, uint32_t size);
```

##### writeI16List（Binary Protocol）参数说明

|参数名|参数说明|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int16数组的指针|有效的int16_t指针|输入|
|size|数组元素个数|非负整数|输入|

##### writeI16List（Binary Protocol）返回值

返回写入的总字节数。

#### writeI32List（Binary Protocol）

##### writeI32List（Binary Protocol）函数功能

在BinaryProtocolWriter中批量写入int32连续数组。逻辑与`writeI16List`一致。

##### writeI32List（Binary Protocol）函数定义

```cpp
uint32_t BinaryProtocolWriter::writeI32List(const int32_t* data, uint32_t size);
```

##### writeI32List（Binary Protocol）参数说明

|参数名|参数说明|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int32数组的指针|有效的int32_t指针|输入|
|size|数组元素个数|非负整数|输入|

##### writeI32List（Binary Protocol）返回值

返回写入的总字节数。

#### writeI64List（Binary Protocol）

##### writeI64List（Binary Protocol）函数功能

在BinaryProtocolWriter中批量写入int64连续数组。逻辑与`writeI16List`一致。

##### writeI64List（Binary Protocol）函数定义

```cpp
uint32_t BinaryProtocolWriter::writeI64List(const int64_t* data, uint32_t size);
```

##### writeI64List（Binary Protocol）参数说明

|参数名|参数说明|取值范围|输入/输出|
|--|--|--|--|
|data|指向连续int64数组的指针|有效的int64_t指针|输入|
|size|数组元素个数|非负整数|输入|

##### writeI64List（Binary Protocol）返回值

返回写入的总字节数。

#### writeBeContiguous

##### writeBeContiguous函数功能

Binary Protocol的紧凑循环辅助函数，执行大端字节序的批量写入。在一个紧凑的循环中依次对每个元素执行`bswap`转换为网络字节序，然后`memcpy`写入输出缓冲区并前移指针。该循环天然契合编译器自动向量化优化。

##### writeBeContiguous函数定义

```cpp
namespace detail {
template <class T>
void writeBeContiguous(folly::io::QueueAppender& out, const T* data, uint32_t size);
}
```

##### writeBeContiguous参数说明

|参数名|参数说明|取值范围|输入/输出|
|--|--|--|--|
|out|QueueAppender输出对象|有效的QueueAppender引用|输入/输出|
|data|指向连续整型数组的指针|有效的T类型指针（T为int16_t/int32_t/int64_t）|输入|
|size|数组元素个数|非负整数|输入|

##### writeBeContiguous返回值

无返回值。

### 编译期分发机制

优化方案通过SFINAE（Substitution Failure Is Not An Error）技术在编译期自动选择最优序列化路径。当以下所有条件同时满足时，才会批量处理API。

- **元素类型**：必须是`int16_t`、`int32_t`或`int64_t`。
- **协议支持**：`has_batched_int_list_writer<Protocol, Elem>`为true，即Protocol必须提供`writeI{N}List`成员（目前仅`CompactProtocolWriter`和`BinaryProtocolWriter`满足，`JSONProtocolWriter`在编译期被剔除）。
- **容器连续性**：`is_contiguous_elem_container<Container, Elem>`为true，即容器必须提供连续的内存布局（如`std::vector`满足，而`std::list`或`std::deque`不满足）。

对于不满足上述条件的类型或容器，SFINAE机制自动回退到原有的逐元素序列化循环，确保兼容性。

## 修订记录
