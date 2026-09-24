# API Reference

## FbThrift v1.1.0

This chapter describes the ThreadManager task scheduling and Header frame processing interfaces newly added in FbThrift v1.1.0.

### FbThrift v1.1.0 Interface Introduction

| Interface Name | Module | Description |
|---|---|---|
| ThreadManager::Impl::addFunc | ThreadManager | Directly receives and enqueues `folly::Func`, avoiding the need for regular RPC tasks to pre-construct a `FunctionRunner`. |
| ThreadManager::Task::getRunnable | ThreadManager | When the legacy interface requires a `Runnable`, lazily wraps `folly::Func` into a `FunctionRunner` on demand. |
| THeader::removeHeader | Header Transport | Parses the message header and returns the complete frame length via an output parameter. |
| FramingHandler::removeFrame | Header Transport | Returns the deframing result, remaining length, Header object, and frame length. |
| FramingHandler::read | Header Transport | Dynamically adjusts the read buffer based on the moving average of frame lengths. |

### ThreadManager Task Scheduling Interfaces

#### ThreadManager::Impl::addFunc

##### ThreadManager::Impl::addFunc Function Description

Directly constructs a one-off `folly::Func` into a `ThreadManager::Task` and enqueues it into the priority queue. This interface reuses the existing priority calculation, request context saving, `QueueObserver` notification, task counting, and idle thread wake-up logic, but does not create a `FunctionRunner` or its `shared_ptr` control block on the regular RPC hot path.

##### ThreadManager::Impl::addFunc Function Definition

```cpp
void ThreadManager::Impl::addFunc(
    size_t priority,
    folly::Func func,
    int64_t expiration,
    ThreadManager::Source source) noexcept;
```

##### ThreadManager::Impl::addFunc Parameters

|Parameter|Description|Input/Output|
|--|--|--|
|priority|Task priority. Before enqueueing, it is mapped together with the task source to the queue priority.|Input|
|func|One-off, movable function object to be executed.|Input|
|expiration|Queue expiration time in milliseconds; `0` means expiration control is disabled. | Input |
|source|Task source, for example, `INTERNAL` or `UPSTREAM`.|Input|

##### Execution and Rollback

`Task` uses `std::variant<std::shared_ptr<Runnable>, folly::Func>` to store two types of tasks. The worker thread directly invokes `folly::Func` in `Task::run()`; `Task::skip()` is responsible for discarding and releasing unexecuted tasks. Both paths use `std::exchange` to first transfer task ownership, ensuring that a task is executed at most once.

The runtime switch `--thrift_thread_manager_direct_func_enabled` enables direct scheduling by default. After it is set to `false`, all related entry points revert to wrapping through `FunctionRunner::create()` and calling the original `add()` interface.

#### ThreadManager::Task::getRunnable

##### ThreadManager::Task::getRunnable Function Description

Provides compatible conversion for legacy interfaces such as `removeNextPending()` and task expiration callbacks that still require `std::shared_ptr<Runnable>`. If the task is already stored as a `Runnable`, it is returned directly; if the task is stored as a `folly::Func`, a `FunctionRunner` is created only on the first call, and the `variant` is switched to `shared_ptr<Runnable>`.

##### ThreadManager::Task::getRunnable Function Definition

```cpp
const std::shared_ptr<Runnable>& ThreadManager::Task::getRunnable() const;
```

>![Note icon](public_sys-resources/icon-note.gif)**NOTE**
>
>This interface is a lazy compatibility path and is not part of the regular execution path for regular RPC tasks.

### Header Frame Parsing and Adaptive Read Buffer Interfaces

#### THeader::removeHeader

##### THeader::removeHeader Function Description

Parses the Header frame from `IOBufQueue`. In addition to returning the message data and the number of bytes to be supplemented, FbThrift v1.1.0 adds the `frameLength` output parameter to pass the actual length of the complete frame to the upper layer. Returns 0 when the frame is empty or the data is incomplete.

##### THeader::removeHeader Function Definition

```cpp
std::unique_ptr<folly::IOBuf> THeader::removeHeader(
    folly::IOBufQueue* queue,
    size_t& needed,
    StringToStringMap& persistentReadHeaders,
    size_t& frameLength);
```

##### THeader::removeHeader Parameters

|Parameter|Description|Input/Output|
|--|--|--|
|queue|Input buffer queue to be parsed.|Input/Output|
|needed|Number of bytes still to be read when the message is incomplete.|Output|
|persistentReadHeaders|Connection-level persistent Header set.|Input/Output|
|frameLength|Length of the current complete frame; `0` when no complete frame is formed.|Output|

#### FramingHandler::removeFrame

##### FramingHandler::removeFrame Function Description

The Header client, server, and DuplexChannel uniformly return the deframing result as a four-tuple, allowing `frameLength` to be propagated from the `THeader` parsing layer to `FramingHandler::read()`.

##### FramingHandler::removeFrame Function Definition

```cpp
virtual std::tuple<
    std::unique_ptr<folly::IOBuf>,
    size_t,
    std::unique_ptr<apache::thrift::transport::THeader>,
    size_t>
removeFrame(folly::IOBufQueue* queue) = 0;
```

This interface is extended from a three-tuple to a four-tuple. The four return values are, in order, the message data, the number of bytes still to be read, the Header object, and the complete frame length. This change is synchronized with the `THeader::removeHeader` change described above, and `size_t& frameLength` is the only newly added variable.

#### FramingHandler::read Adaptive Strategy

After a complete frame is received, `read()` updates the moving average of the request size based on 10 samples.

```cpp
avgRequestSize_ = avgRequestSize_ == 0
    ? frameLength
    : (avgRequestSize_ * 9 + frameLength) / 10;

readSize = std::clamp(avgRequestSize_ * 16, size_t(2048), size_t(524288));
```

The calculated result is used to refresh the Pipeline read buffer setting, with the buffer size limited to the range of 2 KB to 512 KB. In the incomplete-frame scenario, the remaining bytes and the available space at the tail of the queue are also taken into account, reducing repeated reads for large frames while preventing small requests from occupying an excessively large fixed buffer for a long time.

> ![Image indicating a note](public_sys-resources/icon-note.gif) **NOTE**
>
>The adaptive logic refreshes the buffer settings in the read path. Callers that rely on the fixed buffer semantics of `setReadBufferSize()` should perform regression verification when upgrading to FbThrift v1.1.0.

## FbThrift v1.0.0

This chapter introduces the batch serialization interfaces for Compact Protocol and Binary Protocol provided by FbThrift v1.0.0.

### FbThrift v1.0.0 Interface Introduction

|Interface Name|Module|Description|
|--|--|--|
|writeI16List|Binary Protocol|Batch writes a contiguous int16 array.|
|writeI32List|Compact Protocol / Binary Protocol|Batch writes a contiguous int32 array. Compact Protocol can use the SVE2 kernel to accelerate Varint encoding.|
|writeI64List|Compact Protocol / Binary Protocol|Batch writes a contiguous int64 array. Compact Protocol can use the SVE2 kernel to accelerate Varint encoding.|
|dispatchVarintEncode32|Compact Protocol|Selects the scalar or SVE2 kernel based on compilation options and runtime CPU capabilities.|
|dispatchVarintEncode64|Compact Protocol|Selects the scalar or SVE2 kernel based on compilation options and runtime CPU capabilities.|
|hasRuntimeSve2|Compact Protocol|Detects at runtime whether the CPU supports the SVE2 instruction set.|
|writeBeContiguous|Binary Protocol|Performs batch big-endian writes via `bswap` and `memcpy`.|

### Compact Protocol Interfaces

#### writeI32List (Compact Protocol)

##### writeI32List (Compact Protocol) Function Description

Writes contiguous int32 arrays in batches within `CompactProtocolWriter`. This function is responsible for writing the list header, invoking the cross-translation-unit dispatch function `dispatchVarintEncode32` to perform batch Varint encoding, and finally writing the trailer. If the runtime CPU supports the SVE2 instruction set, a vectorized kernel is automatically enabled to accelerate the encoding.

##### writeI32List (Compact Protocol) Function Definition

```cpp
uint32_t CompactProtocolWriter::writeI32List(const int32_t* data, uint32_t size);
```

##### writeI32List (Compact Protocol) Parameters

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int32 array|Valid int32_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|

##### writeI32List (Compact Protocol) Return Value

Returns the total number of bytes written.

#### writeI64List (Compact Protocol)

##### writeI64List (Compact Protocol) Function Description

Writes contiguous int64 arrays in batches within `CompactProtocolWriter`. The logic is the same as that of `writeI32List`. The `dispatchVarintEncode64` function is called to perform batch Varint encoding.

##### writeI64List (Compact Protocol) Function Definition

```cpp
uint32_t CompactProtocolWriter::writeI64List(const int64_t* data, uint32_t size);
```

##### writeI64List (Compact Protocol) Parameters

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int64 array|Valid int64_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|

##### writeI64List (Compact Protocol) Return Value

Returns the total number of bytes written.

#### dispatchVarintEncode32

##### dispatchVarintEncode32 Function Description

A cross-translation-unit runtime dispatch function. It selects either the SVE2 kernel path or the scalar fallback path based on whether the `THRIFT_HAS_ARM_SVE2` macro is defined at compile time and the detection result of `hasRuntimeSve2()` at runtime. This is the only cross-translation-unit call, and it is defined in `CompactProtocolSve.cpp` to isolate compilation and prevent global compilation option pollution.

##### dispatchVarintEncode32 Function Definition

```cpp
uint32_t dispatchVarintEncode32(const int32_t* data, uint32_t size, folly::io::QueueAppender& out);
```

##### dispatchVarintEncode32 Parameters

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int32 array|Valid int32_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|
|out|`QueueAppender` output object|Valid `QueueAppender` reference|Input/Output|

##### dispatchVarintEncode32 Return Value

Returns the total number of bytes written after encoding.

##### dispatchVarintEncode32 Dispatch Logic

|Build Mode|THRIFT_HAS_ARM_SVE2 Macro|hasRuntimeSve2()|Target Branch|
|--|--|--|--|
|`THRIFT_ENABLE_ARM_SVE2` disabled in CMake|Undefined|Eliminated at compile time|Scalar path always|
|SVE2 enabled, but running on legacy CPUs|Defined|false|Scalar path|
|SVE2 enabled, and running on supported CPUs (e.g., Graviton3)|Defined|true|SVE2 kernel path|

#### dispatchVarintEncode64

##### dispatchVarintEncode64 Function Description

Shares the same logic as `dispatchVarintEncode32`, performing batch Varint encoding dispatch specifically for the int64 type.

##### dispatchVarintEncode64 Function Definition

```cpp
uint32_t dispatchVarintEncode64(const int64_t* data, uint32_t size, folly::io::QueueAppender& out);
```

##### dispatchVarintEncode64 Parameters

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int64 array|Valid int64_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|
|out|`QueueAppender` output object|Valid `QueueAppender` reference|Input/Output|

##### dispatchVarintEncode64 Return Value

Returns the total number of bytes written after encoding.

#### hasRuntimeSve2

##### hasRuntimeSve2 Function Description

Detects whether the current CPU supports the SVE2 instruction set at runtime. This function makes the determination by reading CPU feature registers (such as `getauxval(AT_HWCAP)`), and the result is cached to avoid the overhead of repeated detection.

##### hasRuntimeSve2 Function Definition

```cpp
bool hasRuntimeSve2();
```

##### hasRuntimeSve2 Return Value

|Return Value|Description|
|--|--|
|true|The current CPU supports the SVE2 instruction set.|
|false|The current CPU does not support the SVE2 instruction set.|

### Binary Protocol Interfaces

#### writeI16List (Binary Protocol)

##### writeI16List (Binary Protocol) Function Description

Writes contiguous int16 arrays in batches within `BinaryProtocolWriter`. It expands the buffer using a single `ensure` call, performs `bswap` and `memcpy` operations inside a tight loop, and finally commits all writes at once via `out_.append(total)`. The compiler automatically vectorizes the inner loop.

##### writeI16List (Binary Protocol) Function Definition

```cpp
uint32_t BinaryProtocolWriter::writeI16List(const int16_t* data, uint32_t size);
```

##### writeI16List (Binary Protocol) Parameters

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int16 array|Valid int16_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|

##### writeI16List (Binary Protocol) Return Value

Returns the total number of bytes written.

#### writeI32List (Binary Protocol)

##### writeI32List (Binary Protocol) Function Description

Writes contiguous int32 arrays in batches within `BinaryProtocolWriter`. The logic is the same as that of `writeI16List`.

##### writeI32List (Binary Protocol) Function Definition

```cpp
uint32_t BinaryProtocolWriter::writeI32List(const int32_t* data, uint32_t size);
```

##### writeI32List (Binary Protocol) Parameters

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int32 array|Valid int32_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|

##### writeI32List (Binary Protocol) Return Value

Returns the total number of bytes written.

#### writeI64List (Binary Protocol)

##### writeI64List (Binary Protocol) Function Description

Writes contiguous int64 arrays in batches within `BinaryProtocolWriter`. The logic is the same as that of `writeI16List`.

##### writeI64List (Binary Protocol) Function Definition

```cpp
uint32_t BinaryProtocolWriter::writeI64List(const int64_t* data, uint32_t size);
```

##### writeI64List (Binary Protocol) Parameters

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int64 array|Valid int64_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|

##### writeI64List (Binary Protocol) Return Value

Returns the total number of bytes written.

#### writeBeContiguous

##### writeBeContiguous Function Description

A tight-loop helper function for the Binary Protocol. It performs batch writing in big-endian byte order. Within a tight loop, it sequentially executes `bswap` on each element to convert it to network byte order, then executes `memcpy` to write the data into the output buffer and advance the pointer. This loop is naturally well-suited for automatic compiler vectorization.

##### writeBeContiguous Function Definition

```cpp
namespace detail {
template <class T>
void writeBeContiguous(folly::io::QueueAppender& out, const T* data, uint32_t size);
}
```

##### writeBeContiguous Parameters

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|out|`QueueAppender` output object|Valid `QueueAppender` reference|Input/Output|
|data|Pointer to a contiguous integer array|Valid T-type pointer (T can be int16_t, int32_t, or int64_t.)|Input|
|size|Number of elements in the array|Non-negative integer|Input|

##### writeBeContiguous Return Value

None

### Compile-time Dispatch Mechanism

The optimization uses SFINAE (Substitution Failure Is Not An Error) to automatically select the optimal serialization path at compile time. The batch processing API is used only when all of the following conditions are met:

- **Element type**: Must be `int16_t`, `int32_t`, or `int64_t`.
- **Protocol support**: `has_batched_int_list_writer<Protocol, Elem>` is true. That is, the protocol must provide the `writeI{N}List` member (currently only `CompactProtocolWriter` and `BinaryProtocolWriter` satisfy this requirement, while `JSONProtocolWriter` is eliminated at compile time).
- **Container contiguity**: `is_contiguous_elem_container<Container, Elem>` is true. That is, the container must provide a contiguous memory layout (e.g., `std::vector` satisfies this, whereas `std::list` and `std::deque` do not).

For types or containers that do not meet the above conditions, the SFINAE mechanism automatically falls back to the original per-element serialization loop, ensuring compatibility.

## Change History

|Release|Date|Description|
|:---|:---|:---|
|01|2026-9-30|This is the first official release.|
