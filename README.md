# Multithreaded HTTP Proxy Server

This is a multithreaded HTTP proxy server implemented in C. It supports caching of HTTP requests and responses using an LRU (Least Recently Used) eviction policy.

## ⚙ Features

- Multithreading with POSIX threads
- Thread synchronization using semaphores
- LRU caching mechanism
- Support for HTTP/1.0 and HTTP/1.1 GET requests
- Support for CONNECT method (allows HTTPS tunneling)
- Proper error handling and status codes
- Configurable port number

## 📦 Building

To get started, first clone the repository:

```bash
$ git clone https://github.com/ArshhAnsari/MultiThreadedWebProxyServer.git
$ cd MultiThreadedWebProxyServer

```

Then, to build the proxy server, simply run:

```bash
$ make
```

This will compile the proxy server executable.

## 🚀 Usage

Start the proxy server with an optional port number:

```bash
$ ./proxy_server [port]
```

If no port is specified, the default port `8080` is used.

## Testing

You can test the proxy server using curl:

```bash
$ curl -v --proxy http://localhost:8080 http://example.com
```

Or configure your browser to use the proxy server:
- Address: localhost
- Port: 8080 (or your custom port)

## 📘 Documentation

- 🔍 [Explanation.md](./Explanation.md): Covers the architecture, design decisions, and OS-level concepts used in building this project.
- 🎥 [ProxyServerDemo.md](./ProxyServerDemo.md): Walkthrough of test scenarios with commands and visual outputs.

## ⚠️ Limitations

* Only GET and CONNECT supported
* No SSL termination
* Limited to 400 concurrent connections

## Credits and Acknowledgments 🙌

This project builds upon the HTTP parsing functionality from the reference [Lovepreet Singh](https://github.com/Lovepreet-Singh-LPSK/MultiThreadedProxyServerClient) implementation. While the original code provided a basic structure, this version significantly enhances it with advanced features, including:

- **Comprehensive Multithreading**: Thread pool management with POSIX threads
- **Enhanced Synchronization**: Semaphores and mutexes for thread safety
- **Full Protocol Support**: HTTP + HTTPS tunneling via CONNECT
- **LRU Caching System**: Efficient cache with Least Recently Used eviction
- **Browser Compatibility**: Supports direct browser request formatting
- **Infinite Loop Prevention**: Avoids recursive proxy forwarding
- **Improved Error Handling**: Better HTTP status code reporting

These changes result in a robust, concurrent proxy server with efficient caching and reliable HTTPS support.


## 🪪 License
This project is freely available for educational purposes.

