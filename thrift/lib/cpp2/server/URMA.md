# Basic URMA transport

`tcp_base` supports opting a normal `ThriftServer` into Folly's basic URMA
RM SEND/RECV transport. The default remains TCP. The listening TCP connection
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
[`8b88dff270fd9da54bfd42a6caa04a9dca411b15`](https://gitcode.com/donghuanan/folly/commit/8b88dff270fd9da54bfd42a6caa04a9dca411b15).
Use this revision or a compatible descendant that provides both
`UrmaAsyncSocket::newSocket(EventBase*, const SocketAddress&)` and the owning
`UrmaAsyncSocket(AsyncSocket::UniquePtr)` constructor. Build Folly with
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

## Tests

With `enable_tests=ON`, build `UrmaServerTest-t` and run
`ctest -R '^UrmaServerTest$' --output-on-failure` from the build directory.
Hardware lifecycle cases require the opt-in environment variable
`FBTHRIFT_URMA_TEST_DEVICE=udmac0d1e2`. These cases check shared context lifetime,
borrowing an application context, conflicting configurations and bind failure.
