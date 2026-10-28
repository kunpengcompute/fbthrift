# Basic URMA transport

`tcp_base` supports opting a normal `ThriftServer` into Folly's URMA
RM transport, with small-record SEND/RECV and large-record one-sided READ.
The default remains TCP. The listening TCP connection
exchanges URMA endpoints; RPC payloads then pass through `UrmaAsyncSocket` and
the usual Header/Rocket request processing code.

Configure the server before `setup()` or `serve()`:

```cpp
folly::UrmaConfig config;
config.dev_name = "udmac0d1e2";
config.rx_depth = 1280;
server->setUseUrma(true);
server->setUrmaConfig(config);
```

The server initializes URMA before listening and keeps its context alive until
the last associated server transport is destroyed. Servers in the same process
must request the same configuration. If the application has already initialized
Folly URMA, pass its effective `UrmaInit::getConfig()`; that context is borrowed
and remains the application's responsibility to finalize.

The tested Folly `dev_urma` revision is
[`ac628255f236caa1540ed41c1509c8c2d7009f83`](https://gitcode.com/donghuanan/folly/commit/ac628255f236caa1540ed41c1509c8c2d7009f83).
Use the matching `dev_urma` implementation, including
`UrmaAsyncSocket::newSocket(EventBase*, const SocketAddress&)` and the owning
`UrmaAsyncSocket(AsyncSocket::UniquePtr)` constructor, `UrmaRecordWriter`,
`UrmaConfig::enable_read` / `read_threshold`, context acquire/release, and
the transport buffer callback and statistics. The earlier socket-compatibility
revision alone does not provide these APIs. Build Folly with
`FOLLY_USE_URMA=ON`, install fbthrift against that installation, and rebuild the
existing benchmark against the same headers and libraries. The original
benchmark service and client source files do not need modification; no
benchmark-side compatibility patch is needed.

For the existing benchmark, leave `dev_name` empty for TCP; pass
`--dev_name=udmac0d1e2 --use_io_uring=false` to both endpoints for URMA. The
benchmark's request scheduling, rate limiting and latency statistics are
unchanged. The compatible Folly default permits up to 128 MiB of pending data
per connection without preallocating that amount, covering ten outstanding
8 MiB requests plus framing.

This transport has no TLS, duplex-channel or custom-acceptor integration and
cannot be wrapped as an io_uring socket. Conflicting configurations fail at
startup. An invalid device or a failed URMA connection is reported as an error;
there is no automatic TCP fallback. Builds without URMA-enabled Folly continue
to support TCP; explicitly enabling URMA on such a build fails at startup.
`strictSSL` controls TLS configuration error handling; setting it alone does
not enable TLS. An explicit TLS requirement or supplied TLS credentials is
still rejected. Accepted negotiation sockets participate in the server's
`ShutdownSocketSet`, just like ordinary TCP connections.

Header and Rocket preserve record boundaries before batching, so combining
small RPCs cannot by itself make them eligible for READ. The Folly default uses
READ only above 4352 serialized bytes; the SEND block capacity of 3968 bytes
is a separate constraint. Application payload length excludes framing and is
therefore not the value compared with this threshold.

## Tests

With `enable_tests=ON`, build and run the focused tests:

```bash
cmake --build _build --target UrmaServerTest-t UrmaEgressTest-t \
  UrmaRequestRecordsTest-t UrmaRecordAllocationTest-t UrmaWriteBatchTest-t \
  ReplyQueueBackendYieldTest-t
FBTHRIFT_URMA_TEST_DEVICE=udmac0d1e2 \
  ctest --test-dir _build --output-on-failure \
  -R 'Urma(Egress|RequestRecords|RecordAllocation|WriteBatch|Server)Test|ReplyQueueBackendYieldTest'
```

Hardware lifecycle cases require the opt-in environment variable
`FBTHRIFT_URMA_TEST_DEVICE=udmac0d1e2`. These cases check shared context lifetime,
borrowing an application context, conflicting configurations, bind failure,
strictSSL without credentials, and shutdown of an accepted negotiation socket.
Without that variable, hardware cases are explicitly skipped.

Record tests compare exact serialized bytes and boundaries across different
IOBuf partitions, SETUP plus request, and small/large/small batches. A separate
allocation-fault executable checks queue retry, failure completion, and the
Rocket client's exactly-once error callbacks. Reply-queue tests check flush
ordering, independently rearmed notifications, bounded batches, lifecycle,
and the non-libevent backend gate. Folly's `UrmaInitTest` (enabled with
`FOLLY_BUILD_URMA_READ_TESTS=ON`) separately injects allocation and SDK failures
without using hardware.
`UrmaWriteBatchTest` exercises the Header batching handler and transport adapter
together: consecutive and immediate writes, ordinary transport fallback,
pending-write cancellation, cleanup, reentrant writes, and destruction from a
completion callback. It checks both byte content and the accompanying lengths.

## Rocket egress tracking

Rocket applies `socketWriteTimeout` to the business transport, including URMA.
The timeout measures time without write progress; changing it restarts the
interval for already pending writes. RNR and READ protocol deadlines remain
independent limits.

URMA reports pending application bytes and retained write-buffer capacity to
Rocket's existing egress tracker. The capacity includes queued IOBufs and an
active READ source copy, and decreases as writes complete or fail. Idle
registered caches and receive buffers are not pending egress. Existing stream
backpressure and egress memory limits consume these updates; a memory-limit
failure closes the business transport. TCP-specific socket options remain on
the TCP socket.

This requires a Folly URMA build exposing `setBufferCallback` and the buffered
byte statistics. Rebuild fbthrift and its applications against the same Folly
installation. With `enable_tests=ON`, build `UrmaEgressTest-t` and run:

```bash
FBTHRIFT_URMA_TEST_DEVICE=udmac0d1e2 \
  ctest --test-dir _build -R '^UrmaEgressTest$' --output-on-failure
```

The tests cover configuration forwarding, queued-memory accounting, cleanup,
memory-limit closure, TCP compatibility, and restarting a pending write's
timeout. Hardware cases use a local silent TCP peer to stall URMA negotiation;
no second server is needed for those cases.
