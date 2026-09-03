thrift_generate(
  "Service"
  "TestService"
  "cpp2"
  ""
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}"
  "thrift/lib/cpp2/test"
  THRIFT_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}
)

bypass_source_check("${Service-cpp2-SOURCES}")

add_executable(SerializationTest
  SerializationTest.cpp
  ServiceSimpleJSONInstantiation.cpp
  ProtocolListFastPathTest.cpp
  ${Service-cpp2-SOURCES}
)
add_dependencies(SerializationTest Service-cpp2-target)
target_include_directories(SerializationTest PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}
)
target_link_libraries(SerializationTest
  thriftcpp2
  thriftprotocol
  Folly::folly
  ${GTEST_BOTH_LIBRARIES}
  ${GMOCK_BOTH_LIBRARIES}
  pthread
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(SerializationTest PRIVATE
    -fprofile-instr-generate
    -fcoverage-mapping
  )
  target_link_options(SerializationTest PRIVATE
    -fprofile-instr-generate
  )
endif()

#gtest_add_tests(SerializationTest "" SerializationTest.cpp)
add_test(NAME SerializationTest COMMAND SerializationTest)
set_tests_properties(SerializationTest PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[  PASSED  \\] [0-9]+ tests"
)

# THeader::removeHeader frameLength tests
add_executable(THeaderFrameLength-t
  ../../cpp/transport/test/THeaderFrameLengthTest.cpp
)
target_link_libraries(THeaderFrameLength-t
  transport thriftcpp2 thrift-core rpcmetadata thriftprotocol thrifttype concurrency async
  Folly::folly
  GTest::gtest GTest::gtest_main
  pthread
)

# FramingHandler / ServerFramingHandler / ClientFramingHandler tests
add_executable(FramingHandlerTest-t
  ../async/tests/FramingHandlerTest.cpp
)
target_link_libraries(FramingHandlerTest-t
  thriftcpp2 thrift-core transport async concurrency rpcmetadata thriftprotocol thrifttype thriftannotation thriftanyrep thriftfrozen2 thriftmetadata
  Folly::folly fizz::fizz wangle::wangle
  GTest::gtest GTest::gmock
  event double-conversion
  boost_context boost_filesystem boost_program_options boost_regex boost_system boost_thread
  fmt::fmt
  ssl crypto sodium z zstd lzma
  dl pthread atomic
)

add_test(NAME THeaderFrameLength COMMAND THeaderFrameLength-t)

add_test(NAME FramingHandlerTest COMMAND FramingHandlerTest-t)
