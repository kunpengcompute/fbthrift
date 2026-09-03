/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <thrift/lib/cpp/transport/THeader.h>

#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <folly/portability/GTest.h>

#include <algorithm>
#include <cstring>
#include <initializer_list>

using namespace apache::thrift::transport;
using namespace folly;

static std::unique_ptr<IOBuf> makeHeaderMsg(
    size_t sz, THeader::StringToStringMap& h) {
  THeader hdr;
  auto buf = IOBuf::create(sz);
  if (sz > 0)
    buf->writableData()[0] = 0x80;
  std::memset(buf->writableData() + 1, 'X', sz > 1 ? sz - 1 : 0);
  buf->append(sz);
  return hdr.addHeader(std::move(buf), h);
}

static std::unique_ptr<IOBuf> makeBytes(
    std::initializer_list<uint8_t> bytes, size_t capacity = 0) {
  auto buf = IOBuf::create(std::max(capacity, bytes.size()));
  std::copy(bytes.begin(), bytes.end(), buf->writableData());
  buf->append(bytes.size());
  return buf;
}

static std::unique_ptr<IOBuf> makeLengthPrefixed(
    uint32_t declaredSize, std::initializer_list<uint8_t> payload) {
  auto buf = IOBuf::create(4 + payload.size());
  auto* out = buf->writableData();
  out[0] = (declaredSize >> 24) & 0xff;
  out[1] = (declaredSize >> 16) & 0xff;
  out[2] = (declaredSize >> 8) & 0xff;
  out[3] = declaredSize & 0xff;
  std::copy(payload.begin(), payload.end(), out + 4);
  buf->append(4 + payload.size());
  return buf;
}

TEST(THeaderFrameLengthTest, NullQueue) {
  THeader h;
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_EQ(nullptr, h.removeHeader(nullptr, n, ph, fl));
  EXPECT_EQ(0u, fl);
}

TEST(THeaderFrameLengthTest, EmptyQueue) {
  THeader h;
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  IOBufQueue q;
  EXPECT_EQ(nullptr, h.removeHeader(&q, n, ph, fl));
  EXPECT_EQ(0u, fl);
}

TEST(THeaderFrameLengthTest, ShortQueueReportsExactNeededBytes) {
  IOBufQueue q;
  q.append(makeBytes({0x00, 0x00}));
  THeader h;
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_EQ(nullptr, h.removeHeader(&q, n, ph, fl));
  EXPECT_EQ(2u, n);
  EXPECT_EQ(0u, fl);
}

TEST(THeaderFrameLengthTest, NormalHeader) {
  THeader::StringToStringMap ph;
  IOBufQueue q;
  auto frame = makeHeaderMsg(100, ph);
  const auto expectedFrameLength = frame->computeChainDataLength();
  q.append(std::move(frame));
  q.append(makeBytes({0xaa, 0xbb, 0xcc}));
  THeader h;
  size_t n = 0, fl = 0;
  auto buf = h.removeHeader(&q, n, ph, fl);
  ASSERT_NE(nullptr, buf);
  EXPECT_EQ(expectedFrameLength, fl);
  EXPECT_EQ(3u, q.front()->computeChainDataLength());
}

TEST(THeaderFrameLengthTest, CachedQueueFrameLength) {
  THeader::StringToStringMap ph;
  IOBufQueue q(IOBufQueue::cacheChainLength());
  auto frame = makeHeaderMsg(32, ph);
  const auto expectedFrameLength = frame->computeChainDataLength();
  q.append(std::move(frame));
  q.append(makeBytes({0xaa, 0xbb}));
  THeader h;
  size_t n = 0, fl = 0;
  ASSERT_NE(nullptr, h.removeHeader(&q, n, ph, fl));
  EXPECT_EQ(expectedFrameLength, fl);
  EXPECT_EQ(2u, q.chainLength());
}

TEST(THeaderFrameLengthTest, IncompleteFramedMessage) {
  IOBufQueue q;
  q.append(makeLengthPrefixed(12, {0x80, 0x01, 0x00, 0x01}));
  THeader h;
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_EQ(nullptr, h.removeHeader(&q, n, ph, fl));
  EXPECT_EQ(8u, n);
  EXPECT_EQ(0u, fl);
  EXPECT_EQ(8u, q.front()->computeChainDataLength());
}

TEST(THeaderFrameLengthTest, ForcedClientTypeIncompleteFrame) {
  IOBufQueue q;
  q.append(makeLengthPrefixed(12, {0x80, 0x01, 0x00, 0x01}));
  THeader h;
  h.setClientType(THRIFT_FRAMED_DEPRECATED);
  h.forceClientType(true);
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_EQ(nullptr, h.removeHeader(&q, n, ph, fl));
  EXPECT_EQ(8u, n);
  EXPECT_EQ(0u, fl);
}

TEST(THeaderFrameLengthTest, ForceClientType) {
  size_t ps = 12;
  uint32_t fs = ps;
  size_t extra = 10;
  auto f = IOBuf::create(4 + ps + extra);
  f->writableData()[0] = (fs >> 24) & 0xFF;
  f->writableData()[1] = (fs >> 16) & 0xFF;
  f->writableData()[2] = (fs >> 8) & 0xFF;
  f->writableData()[3] = fs & 0xFF;
  f->writableData()[4] = 0x80;
  f->writableData()[5] = 0x01;
  f->writableData()[6] = 0;
  f->writableData()[7] = 0x01;
  std::memset(f->writableData() + 8, 0, ps - 4);
  std::memset(f->writableData() + 4 + ps, 'E', extra);
  f->append(4 + ps + extra);
  IOBufQueue q;
  q.append(std::move(f));
  THeader h;
  h.setClientType(THRIFT_FRAMED_DEPRECATED);
  h.forceClientType(true);
  THeader::StringToStringMap ph;
  size_t n = 0, fl = 0;
  auto buf_fc = h.removeHeader(&q, n, ph, fl);
  ASSERT_NE(nullptr, buf_fc);
  EXPECT_EQ(4 + ps, fl); // frame header(4) + payload
}

TEST(THeaderFrameLengthTest, UnframedBinary) {
  uint8_t data[] = {0x80, 0x01, 0, 0x01, 0, 0, 0, 0, 0x01, 0, 0, 0, 0};
  size_t sz = sizeof(data);
  auto full = IOBuf::create(sz + 10);
  std::memcpy(full->writableData(), data, sz);
  std::memset(full->writableData() + sz, 'E', 10);
  full->append(sz + 10);
  IOBufQueue q;
  q.append(std::move(full));
  THeader h;
  THeader::StringToStringMap ph;
  size_t n = 0, fl = 0;
  auto buf_ub = h.removeHeader(&q, n, ph, fl);
  ASSERT_NE(nullptr, buf_ub);
  EXPECT_EQ(sz, fl); // sz is the TBinaryProtocol frame size
}

TEST(THeaderFrameLengthTest, FramedBinary) {
  uint8_t payload[] = {0x80, 0x01, 0, 0x01, 0, 0, 0, 0, 0x01, 0, 0, 0, 0, 0x00};
  uint32_t ps = sizeof(payload);
  auto full = IOBuf::create(4 + ps + 10);
  full->writableData()[0] = (ps >> 24) & 0xFF;
  full->writableData()[1] = (ps >> 16) & 0xFF;
  full->writableData()[2] = (ps >> 8) & 0xFF;
  full->writableData()[3] = ps & 0xFF;
  std::memcpy(full->writableData() + 4, payload, ps);
  std::memset(full->writableData() + 4 + ps, 'E', 10);
  full->append(4 + ps + 10);
  IOBufQueue q;
  q.append(std::move(full));
  THeader h;
  THeader::StringToStringMap ph;
  size_t n = 0, fl = 0;
  auto buf_fb = h.removeHeader(&q, n, ph, fl);
  ASSERT_NE(nullptr, buf_fb);
  EXPECT_EQ(4 + ps, fl); // frame header(4) + payload
}

TEST(THeaderFrameLengthTest, BigFrameRejectedByDefault) {
  IOBufQueue q;
  q.append(makeBytes({0x42, 0x49, 0x47, 0x46}));
  THeader h;
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_THROW(h.removeHeader(&q, n, ph, fl), TTransportException);
  EXPECT_EQ(0u, fl);
}

TEST(THeaderFrameLengthTest, IncompleteBigFrameSize) {
  IOBufQueue q;
  q.append(makeBytes({0x42, 0x49, 0x47, 0x46, 0x00, 0x00, 0x00, 0x00}));
  THeader h(THeader::ALLOW_BIG_FRAMES);
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_EQ(nullptr, h.removeHeader(&q, n, ph, fl));
  EXPECT_EQ(4u, n);
  EXPECT_EQ(0u, fl);
}

TEST(THeaderFrameLengthTest, IncompleteBigFramePayload) {
  IOBufQueue q;
  q.append(makeBytes(
      {0x42,
       0x49,
       0x47,
       0x46,
       0x00,
       0x00,
       0x00,
       0x00,
       0x00,
       0x00,
       0x00,
       0x10,
       0x0f,
       0xff,
       0x00,
       0x00}));
  THeader h(THeader::ALLOW_BIG_FRAMES);
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_EQ(nullptr, h.removeHeader(&q, n, ph, fl));
  EXPECT_EQ(12u, n);
  EXPECT_EQ(0u, fl);
}

TEST(THeaderFrameLengthTest, AsciiFrameSizeThrowsAndResetsLength) {
  IOBufQueue q;
  q.append(makeBytes({'P', 'I', 'N', 'G'}));
  THeader h;
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_THROW(h.removeHeader(&q, n, ph, fl), TTransportException);
  EXPECT_EQ(0u, fl);
}

TEST(THeaderFrameLengthTest, UnknownTransportMagicThrowsAndResetsLength) {
  IOBufQueue q;
  q.append(makeLengthPrefixed(4, {0x12, 0x34, 0x56, 0x78}));
  THeader h;
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_THROW(h.removeHeader(&q, n, ph, fl), TTransportException);
  EXPECT_EQ(0u, fl);
}

TEST(THeaderFrameLengthTest, HeaderFrameTooSmallThrowsAndResetsLength) {
  IOBufQueue q;
  q.append(
      makeLengthPrefixed(8, {0x0f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));
  THeader h;
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_THROW(h.removeHeader(&q, n, ph, fl), TTransportException);
  EXPECT_EQ(0u, fl);
}

TEST(THeaderFrameLengthTest, InvalidHeaderSizeThrowsAndResetsLength) {
  IOBufQueue q;
  q.append(makeLengthPrefixed(
      10, {0x0f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}));
  THeader h;
  size_t n = 0, fl = 999;
  THeader::StringToStringMap ph;
  EXPECT_THROW(h.removeHeader(&q, n, ph, fl), TTransportException);
  EXPECT_EQ(0u, fl);
}
