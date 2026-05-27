#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <folly/Range.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <folly/portability/GTest.h>

#include <thrift/lib/cpp/protocol/TType.h>
#include <thrift/lib/cpp2/op/Encode.h>
#include <thrift/lib/cpp2/protocol/BinaryProtocol.h>
#include <thrift/lib/cpp2/protocol/CompactProtocol.h>
#include <thrift/lib/cpp2/protocol/JSONProtocol.h>
#include <thrift/lib/cpp2/protocol/Cpp2Ops.h>
#include <thrift/lib/cpp2/protocol/detail/protocol_methods.h>

namespace apache {
namespace thrift {
namespace test {
namespace {

template <class T>
protocol::TType thriftType();

template <>
protocol::TType thriftType<std::int16_t>() {
  return protocol::T_I16;
}

template <>
protocol::TType thriftType<std::int32_t>() {
  return protocol::T_I32;
}

template <>
protocol::TType thriftType<std::int64_t>() {
  return protocol::T_I64;
}

template <class T>
const std::vector<T>& values();

template <>
const std::vector<std::int16_t>& values<std::int16_t>() {
  static const std::vector<std::int16_t> kValues = {
      0,
      1,
      -1,
      127,
      -128,
      128,
      -129,
      std::numeric_limits<std::int16_t>::min(),
      std::numeric_limits<std::int16_t>::max()};
  return kValues;
}

template <>
const std::vector<std::int32_t>& values<std::int32_t>() {
  static const std::vector<std::int32_t> kValues = {
      0,
      1,
      -1,
      63,
      -64,
      64,
      -65,
      8191,
      -8192,
      8192,
      -8193,
      std::numeric_limits<std::int32_t>::min(),
      std::numeric_limits<std::int32_t>::max()};
  return kValues;
}

template <>
const std::vector<std::int64_t>& values<std::int64_t>() {
  static const std::vector<std::int64_t> kValues = {
      0,
      1,
      -1,
      63,
      -64,
      64,
      -65,
      8191,
      -8192,
      8192,
      -8193,
      (std::int64_t{1} << 32) - 1,
      -(std::int64_t{1} << 32),
      std::numeric_limits<std::int64_t>::min(),
      std::numeric_limits<std::int64_t>::max()};
  return kValues;
}

std::string drain(folly::IOBufQueue& queue) {
  auto buf = queue.move();
  return folly::StringPiece(buf->coalesce()).str();
}

template <class Writer, class F>
std::string serializeWith(F&& f) {
  folly::IOBufQueue queue;
  Writer writer;
  writer.setOutput(&queue);
  f(writer);
  return drain(queue);
}

template <class Writer, class T>
std::string writeListManually(const std::vector<T>& in) {
  return serializeWith<Writer>([&](Writer& writer) {
    writer.writeListBegin(thriftType<T>(), static_cast<std::uint32_t>(in.size()));
    for (const auto value : in) {
      if constexpr (std::is_same_v<T, std::int16_t>) {
        writer.writeI16(value);
      } else if constexpr (std::is_same_v<T, std::int32_t>) {
        writer.writeI32(value);
      } else {
        writer.writeI64(value);
      }
    }
    writer.writeListEnd();
  });
}

template <class Writer, class T>
std::string writeListBatched(const std::vector<T>& in) {
  return serializeWith<Writer>([&](Writer& writer) {
    const auto size = static_cast<std::uint32_t>(in.size());
    if constexpr (std::is_same_v<T, std::int16_t>) {
      writer.writeI16List(in.data(), size);
    } else if constexpr (std::is_same_v<T, std::int32_t>) {
      writer.writeI32List(in.data(), size);
    } else {
      writer.writeI64List(in.data(), size);
    }
  });
}

template <class Writer, class T>
std::string writeListWithProtocolMethods(const std::vector<T>& in) {
  return serializeWith<Writer>([&](Writer& writer) {
    using Methods = detail::pm::
        protocol_methods<type_class::list<type_class::integral>, std::vector<T>>;
    Methods::write(writer, in);
  });
}

template <class Writer, class T>
std::string writeListWithCpp2OpsHelper(const std::vector<T>& in) {
  return serializeWith<Writer>(
      [&](Writer& writer) { detail::writeVarintList(writer, in); });
}

template <class Writer, class Tag, class T>
std::string writeListWithOpEncode(const std::vector<T>& in) {
  return serializeWith<Writer>([&](Writer& writer) { op::encode<Tag>(writer, in); });
}

template <class Reader, class T>
std::vector<T> readListBatched(const std::string& serialized) {
  auto buf = folly::IOBuf::copyBuffer(serialized);
  Reader reader;
  reader.setInput(buf.get());

  protocol::TType elemType;
  std::uint32_t size;
  reader.readListBegin(elemType, size);
  EXPECT_EQ(elemType, thriftType<T>());

  std::vector<T> out(size);
  if constexpr (std::is_same_v<T, std::int16_t>) {
    reader.readI16List(out.data(), size);
  } else if constexpr (std::is_same_v<T, std::int32_t>) {
    reader.readI32List(out.data(), size);
  } else {
    reader.readI64List(out.data(), size);
  }
  reader.readListEnd();
  return out;
}

template <class Reader, class T>
std::vector<T> readListWithProtocolMethods(const std::string& serialized) {
  auto buf = folly::IOBuf::copyBuffer(serialized);
  Reader reader;
  reader.setInput(buf.get());

  std::vector<T> out{T{7}, T{8}};
  using Methods = detail::pm::
      protocol_methods<type_class::list<type_class::integral>, std::vector<T>>;
  Methods::read(reader, out);
  return out;
}

template <class Reader, class Tag, class T>
std::vector<T> readListWithOpDecode(const std::string& serialized) {
  auto buf = folly::IOBuf::copyBuffer(serialized);
  Reader reader;
  reader.setInput(buf.get());

  std::vector<T> out;
  op::decode<Tag>(reader, out);
  return out;
}

template <class Writer>
void expectBatchedWritersMatchElementLoop() {
  EXPECT_EQ(
      (writeListManually<Writer>(values<std::int16_t>())),
      (writeListBatched<Writer>(values<std::int16_t>())));
  EXPECT_EQ(
      (writeListManually<Writer>(values<std::int32_t>())),
      (writeListBatched<Writer>(values<std::int32_t>())));
  EXPECT_EQ(
      (writeListManually<Writer>(values<std::int64_t>())),
      (writeListBatched<Writer>(values<std::int64_t>())));
}

template <class Writer>
void expectProtocolEntryPointsMatchElementLoop() {
  EXPECT_EQ(
      (writeListManually<Writer>(values<std::int16_t>())),
      (writeListWithProtocolMethods<Writer>(values<std::int16_t>())));
  EXPECT_EQ(
      (writeListManually<Writer>(values<std::int32_t>())),
      (writeListWithProtocolMethods<Writer>(values<std::int32_t>())));
  EXPECT_EQ(
      (writeListManually<Writer>(values<std::int64_t>())),
      (writeListWithProtocolMethods<Writer>(values<std::int64_t>())));

  EXPECT_EQ(
      (writeListManually<Writer>(values<std::int16_t>())),
      (writeListWithCpp2OpsHelper<Writer>(values<std::int16_t>())));
  EXPECT_EQ(
      (writeListManually<Writer>(values<std::int32_t>())),
      (writeListWithCpp2OpsHelper<Writer>(values<std::int32_t>())));
  EXPECT_EQ(
      (writeListManually<Writer>(values<std::int64_t>())),
      (writeListWithCpp2OpsHelper<Writer>(values<std::int64_t>())));
}

TEST(ProtocolListFastPathTest, BinaryBatchedWritersMatchElementLoop) {
  expectBatchedWritersMatchElementLoop<BinaryProtocolWriter>();
}

TEST(ProtocolListFastPathTest, CompactBatchedWritersMatchElementLoop) {
  expectBatchedWritersMatchElementLoop<CompactProtocolWriter>();
}

TEST(ProtocolListFastPathTest, BinaryBatchedReadersRoundTrip) {
  EXPECT_EQ(
      values<std::int16_t>(),
      (readListBatched<BinaryProtocolReader, std::int16_t>(
          writeListBatched<BinaryProtocolWriter>(values<std::int16_t>()))));
  EXPECT_EQ(
      values<std::int32_t>(),
      (readListBatched<BinaryProtocolReader, std::int32_t>(
          writeListBatched<BinaryProtocolWriter>(values<std::int32_t>()))));
  EXPECT_EQ(
      values<std::int64_t>(),
      (readListBatched<BinaryProtocolReader, std::int64_t>(
          writeListBatched<BinaryProtocolWriter>(values<std::int64_t>()))));
}

TEST(ProtocolListFastPathTest, ProtocolMethodsMatchElementLoop) {
  expectProtocolEntryPointsMatchElementLoop<BinaryProtocolWriter>();
  expectProtocolEntryPointsMatchElementLoop<CompactProtocolWriter>();
  expectProtocolEntryPointsMatchElementLoop<JSONProtocolWriter>();
}

TEST(ProtocolListFastPathTest, BinaryProtocolMethodsReadAppendValues) {
  {
    auto out = readListWithProtocolMethods<BinaryProtocolReader, std::int16_t>(
        writeListBatched<BinaryProtocolWriter>(values<std::int16_t>()));
    std::vector<std::int16_t> expected{7, 8};
    expected.insert(
        expected.end(), values<std::int16_t>().begin(), values<std::int16_t>().end());
    EXPECT_EQ(expected, out);
  }
  {
    auto out = readListWithProtocolMethods<BinaryProtocolReader, std::int32_t>(
        writeListBatched<BinaryProtocolWriter>(values<std::int32_t>()));
    std::vector<std::int32_t> expected{7, 8};
    expected.insert(
        expected.end(), values<std::int32_t>().begin(), values<std::int32_t>().end());
    EXPECT_EQ(expected, out);
  }
  {
    auto out = readListWithProtocolMethods<BinaryProtocolReader, std::int64_t>(
        writeListBatched<BinaryProtocolWriter>(values<std::int64_t>()));
    std::vector<std::int64_t> expected{7, 8};
    expected.insert(
        expected.end(), values<std::int64_t>().begin(), values<std::int64_t>().end());
    EXPECT_EQ(expected, out);
  }
}

TEST(ProtocolListFastPathTest, JSONProtocolMethodsReadAppendValues) {
  {
    auto out = readListWithProtocolMethods<JSONProtocolReader, std::int16_t>(
        writeListManually<JSONProtocolWriter>(values<std::int16_t>()));
    std::vector<std::int16_t> expected{7, 8};
    expected.insert(
        expected.end(), values<std::int16_t>().begin(), values<std::int16_t>().end());
    EXPECT_EQ(expected, out);
  }
  {
    auto out = readListWithProtocolMethods<JSONProtocolReader, std::int32_t>(
        writeListManually<JSONProtocolWriter>(values<std::int32_t>()));
    std::vector<std::int32_t> expected{7, 8};
    expected.insert(
        expected.end(), values<std::int32_t>().begin(), values<std::int32_t>().end());
    EXPECT_EQ(expected, out);
  }
  {
    auto out = readListWithProtocolMethods<JSONProtocolReader, std::int64_t>(
        writeListManually<JSONProtocolWriter>(values<std::int64_t>()));
    std::vector<std::int64_t> expected{7, 8};
    expected.insert(
        expected.end(), values<std::int64_t>().begin(), values<std::int64_t>().end());
    EXPECT_EQ(expected, out);
  }
}

TEST(ProtocolListFastPathTest, OpEncodeMatchesElementLoopAndDecodes) {
  EXPECT_EQ(
      (writeListManually<BinaryProtocolWriter>(values<std::int32_t>())),
      (writeListWithOpEncode<
          BinaryProtocolWriter,
          type::list<type::i32_t>>(values<std::int32_t>())));
  EXPECT_EQ(
      (writeListManually<CompactProtocolWriter>(values<std::int64_t>())),
      (writeListWithOpEncode<
          CompactProtocolWriter,
          type::list<type::i64_t>>(values<std::int64_t>())));
  EXPECT_EQ(
      (writeListManually<JSONProtocolWriter>(values<std::int16_t>())),
      (writeListWithOpEncode<
          JSONProtocolWriter,
          type::list<type::i16_t>>(values<std::int16_t>())));

  EXPECT_EQ(
      values<std::int32_t>(),
      (readListWithOpDecode<
          BinaryProtocolReader,
          type::list<type::i32_t>,
          std::int32_t>(writeListBatched<BinaryProtocolWriter>(
          values<std::int32_t>()))));

  EXPECT_EQ(
      values<std::int64_t>(),
      (readListWithOpDecode<
          JSONProtocolReader,
          type::list<type::i64_t>,
          std::int64_t>(writeListManually<JSONProtocolWriter>(
          values<std::int64_t>()))));
}

} // namespace
} // namespace test
} // namespace thrift
} // namespace apache