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