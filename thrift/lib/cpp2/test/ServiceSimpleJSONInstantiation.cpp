// 手动补充 SimpleJSONProtocol 的模板实例化
// 因为 thrift1 v2022.11.14 默认只生成 Binary/Compact 的实例化

#include <thrift/lib/cpp2/test/gen-cpp2/Service_types.tcc>
#include <thrift/lib/cpp2/protocol/SimpleJSONProtocol.h>

namespace apache { namespace thrift { namespace test {

template void TestStruct::readNoXfer<>(apache::thrift::SimpleJSONProtocolReader*);
template uint32_t TestStruct::write<>(apache::thrift::SimpleJSONProtocolWriter*) const;
template uint32_t TestStruct::serializedSize<>(apache::thrift::SimpleJSONProtocolWriter const*) const;
template uint32_t TestStruct::serializedSizeZC<>(apache::thrift::SimpleJSONProtocolWriter const*) const;

template void TestStructIOBuf::readNoXfer<>(apache::thrift::SimpleJSONProtocolReader*);
template uint32_t TestStructIOBuf::write<>(apache::thrift::SimpleJSONProtocolWriter*) const;
template uint32_t TestStructIOBuf::serializedSize<>(apache::thrift::SimpleJSONProtocolWriter const*) const;
template uint32_t TestStructIOBuf::serializedSizeZC<>(apache::thrift::SimpleJSONProtocolWriter const*) const;

template void TestStructRecursive::readNoXfer<>(apache::thrift::SimpleJSONProtocolReader*);
template uint32_t TestStructRecursive::write<>(apache::thrift::SimpleJSONProtocolWriter*) const;
template uint32_t TestStructRecursive::serializedSize<>(apache::thrift::SimpleJSONProtocolWriter const*) const;
template uint32_t TestStructRecursive::serializedSizeZC<>(apache::thrift::SimpleJSONProtocolWriter const*) const;

template void TestUnsignedIntStruct::readNoXfer<>(apache::thrift::SimpleJSONProtocolReader*);
template uint32_t TestUnsignedIntStruct::write<>(apache::thrift::SimpleJSONProtocolWriter*) const;
template uint32_t TestUnsignedIntStruct::serializedSize<>(apache::thrift::SimpleJSONProtocolWriter const*) const;
template uint32_t TestUnsignedIntStruct::serializedSizeZC<>(apache::thrift::SimpleJSONProtocolWriter const*) const;

}}} // namespace apache::thrift::test
