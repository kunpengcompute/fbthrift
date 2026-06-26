# API Reference

## Functions

[**Table 1** Optimized functions for fbthrift serialization](#optimized-functions-for-fbthrift-serialization) lists the optimized and new functions in the fbthrift serialization optimization patch repository.

**Table 1** Optimized functions for fbthrift serialization<a id="optimized-functions-for-fbthrift-serialization"></a>

|Name|Module|Description|
|--|--|--|
|writeI16List|Binary Protocol|New interface for writing contiguous int16 arrays in batches.|
|writeI32List|Compact Protocol / Binary Protocol|New interface for writing contiguous int32 arrays in batches. The SVE2 kernel is used in the Compact Protocol to accelerate Varint encoding.|
|writeI64List|Compact Protocol / Binary Protocol|New interface for writing contiguous int64 arrays in batches. The SVE2 kernel is used in the Compact Protocol to accelerate Varint encoding.|
|dispatchVarintEncode32|Compact Protocol|New interface for cross-translation-unit runtime dispatching, selecting either the scalar or SVE2 kernel based on compilation options and runtime CPU capabilities.|
|dispatchVarintEncode64|Compact Protocol|New interface for cross-translation-unit runtime dispatching, selecting either the scalar or SVE2 kernel based on compilation options and runtime CPU capabilities.|
|hasRuntimeSve2|Compact Protocol|New interface for runtime detection of whether the CPU supports the SVE2 instruction set.|
|writeBeContiguous|Binary Protocol|New interface acting as a tight-loop helper function to perform `bswap` and `memcpy` for batch writing.|

## Function Definitions

### Compact Protocol Interfaces

#### writeI32List (Compact Protocol)

**Function Usage**

Writes contiguous int32 arrays in batches within `CompactProtocolWriter`. This function is responsible for writing the list header, invoking the cross-translation-unit dispatch function `dispatchVarintEncode32` to perform batch Varint encoding, and finally writing the trailer. If the runtime CPU supports the SVE2 instruction set, a vectorized kernel is automatically enabled to accelerate the encoding.

**Function Syntax**

```cpp
uint32_t CompactProtocolWriter::writeI32List(const int32_t* data, uint32_t size);
```

**Parameters**

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int32 array|Valid int32_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|

**Return Value**

Returns the total number of bytes written.

#### writeI64List (Compact Protocol)

**Function Usage**

Writes contiguous int64 arrays in batches within `CompactProtocolWriter`. The logic is the same as that of `writeI32List`. The `dispatchVarintEncode64` function is called to perform batch Varint encoding.

**Function Syntax**

```cpp
uint32_t CompactProtocolWriter::writeI64List(const int64_t* data, uint32_t size);
```

**Parameters**

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int64 array|Valid int64_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|

**Return Value**

Returns the total number of bytes written.

#### dispatchVarintEncode32

**Function Usage**

A cross-translation-unit runtime dispatch function. It selects either the SVE2 kernel path or the scalar fallback path based on whether the `THRIFT_HAS_ARM_SVE2` macro is defined at compile time and the detection result of `hasRuntimeSve2()` at runtime. This is the only cross-translation-unit call, and it is defined in `CompactProtocolSve.cpp` to isolate compilation and prevent global compilation option pollution.

**Function Syntax**

```cpp
uint32_t dispatchVarintEncode32(const int32_t* data, uint32_t size, folly::io::QueueAppender& out);
```

**Parameters**

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int32 array|Valid int32_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|
|out|`QueueAppender` output object|Valid `QueueAppender` reference|Input/Output|

**Return Value**

Returns the total number of bytes written after encoding.

**Dispatch Logic**

|Build Mode|`THRIFT_HAS_ARM_SVE2` Macro|hasRuntimeSve2()|Target Branch|
|--|--|--|--|
|`THRIFT_ENABLE_ARM_SVE2` disabled in CMake|Undefined|Eliminated at compile time|Scalar path always|
|SVE2 enabled, but running on legacy CPUs|Defined|false|Scalar path|
|SVE2 enabled, and running on supported CPUs (e.g., Graviton3)|Defined|true|SVE2 kernel path|

#### dispatchVarintEncode64

**Function Usage**

Shares the same logic as `dispatchVarintEncode32`, performing batch Varint encoding dispatch specifically for the int64 type.

**Function Syntax**

```cpp
uint32_t dispatchVarintEncode64(const int64_t* data, uint32_t size, folly::io::QueueAppender& out);
```

**Parameters**

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int64 array|Valid int64_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|
|out|`QueueAppender` output object|Valid `QueueAppender` reference|Input/Output|

**Return Value**

Returns the total number of bytes written after encoding.

#### hasRuntimeSve2

**Function Usage**

Detects whether the current CPU supports the SVE2 instruction set at runtime. This function makes the determination by reading CPU feature registers (such as `getauxval(AT_HWCAP)`), and the result is cached to avoid the overhead of repeated detection.

**Function Syntax**

```cpp
bool hasRuntimeSve2();
```

**Return Value**

|Return Value|Description|
|--|--|
|true|The current CPU supports the SVE2 instruction set.|
|false|The current CPU does not support the SVE2 instruction set.|

### Binary Protocol Interfaces

#### writeI16List (Binary Protocol)

**Function Usage**

Writes contiguous int16 arrays in batches within `BinaryProtocolWriter`. It expands the buffer using a single `ensure` call, performs `bswap` and `memcpy` operations inside a tight loop, and finally commits all writes at once via `out_.append(total)`. The compiler automatically vectorizes the inner loop.

**Function Syntax**

```cpp
uint32_t BinaryProtocolWriter::writeI16List(const int16_t* data, uint32_t size);
```

**Parameters**

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int16 array|Valid int16_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|

**Return Value**

Returns the total number of bytes written.

#### writeI32List (Binary Protocol)

**Function Usage**

Writes contiguous int32 arrays in batches within `BinaryProtocolWriter`. The logic is the same as that of `writeI16List`.

**Function Syntax**

```cpp
uint32_t BinaryProtocolWriter::writeI32List(const int32_t* data, uint32_t size);
```

**Parameters**

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int32 array|Valid int32_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|

**Return Value**

Returns the total number of bytes written.

#### writeI64List (Binary Protocol)

**Function Usage**

Writes contiguous int64 arrays in batches within `BinaryProtocolWriter`. The logic is the same as that of `writeI16List`.

**Function Syntax**

```cpp
uint32_t BinaryProtocolWriter::writeI64List(const int64_t* data, uint32_t size);
```

**Parameters**

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|data|Pointer to a contiguous int64 array|Valid int64_t pointer|Input|
|size|Number of elements in the array|Non-negative integer|Input|

**Return Value**

Returns the total number of bytes written.

#### writeBeContiguous

**Function Usage**

A tight-loop helper function for the Binary Protocol. It performs batch writing in big-endian byte order. Within a tight loop, it sequentially executes `bswap` on each element to convert it to network byte order, then executes `memcpy` to write the data into the output buffer and advance the pointer. This loop is naturally well-suited for automatic compiler vectorization.

**Function Syntax**

```cpp
namespace detail {
template <class T>
void writeBeContiguous(folly::io::QueueAppender& out, const T* data, uint32_t size);
}
```

**Parameters**

|Parameter|Description|Value Range|Input/Output|
|--|--|--|--|
|out|`QueueAppender` output object|Valid `QueueAppender` reference|Input/Output|
|data|Pointer to a contiguous integer array|Valid T-type pointer (T can be int16_t, int32_t, or int64_t.)|Input|
|size|Number of elements in the array|Non-negative integer|Input|

**Return Value**

None

## Compile-Time Dispatch Mechanism

The optimization scheme utilizes the Substitution Failure Is Not An Error (SFINAE) principle to automatically select the optimal serialization path at compile time. The batch processing API is used only when all of the following conditions are simultaneously met:

- **Element type**: Must be `int16_t`, `int32_t`, or `int64_t`.
- **Protocol support**: `has_batched_int_list_writer<Protocol, Elem>` is true. That is, the protocol must provide the `writeI{N}List` member (currently only `CompactProtocolWriter` and `BinaryProtocolWriter` satisfy this requirement, while `JSONProtocolWriter` is eliminated at compile time).
- **Container contiguity**: `is_contiguous_elem_container<Container, Elem>` is true. That is, the container must provide a contiguous memory layout (e.g., `std::vector` satisfies this, whereas `std::list` and `std::deque` do not).

For types or containers that do not meet the above conditions, the SFINAE mechanism automatically falls back to the original per-element serialization loop, ensuring compatibility.
