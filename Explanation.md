# 🧠 Understanding a Multithreaded HTTP Proxy Server

### 📚 A Practical Application of Operating Systems Concepts

## 🎯 Motivation

While studying Operating Systems, I wanted to go beyond theory and apply key concepts in a real-world project. I built this multithreaded HTTP/HTTPS proxy server in C to reinforce my understanding of OS fundamentals like **process/thread management**, **synchronization**, **memory handling**, and **network programming**.

This project became a hands-on extension of what I learned in class—connecting theoretical knowledge with a practical, working system that showcases real-world concurrency, resource management, and low-level socket operations.

---

## 🌐 What Is a Proxy Server and Why Build One?

A **proxy server** acts as a middleman between a client (like your web browser) and the internet. When you use a proxy, your requests go to the proxy first, which then contacts the destination server. This is useful for:

* **Caching**: Reduce load times and save bandwidth
* **Privacy**: Hide the client’s IP address
* **Access Control**: Filter or block unwanted content
* **Load Balancing**: Distribute traffic across servers

This project implements a **multithreaded HTTP/HTTPS proxy server** in C, with features like:

* Multithreading using POSIX threads
* Thread-safe **LRU (Least Recently Used)** caching
* **Semaphores and mutexes** for synchronization
* Support for **HTTP/1.0**, **HTTP/1.1**, and **CONNECT (HTTPS tunneling)**
* Configurable port and proper error/status handling

---

## 🧱 Project Structure

| File              | Description                                                   |
| ----------------- | ------------------------------------------------------------- |
| `Makefile`        | Defines how the project is built, specifying compilation flags and dependencies                                          |
| `proxy_server.c`  | Core logic: The main implementation file containing server logic, threading, caching, and request handling |
| `proxy_parse.c/h` | HTTP request parsing logic and Header file that declares structures and functions for parsing HTTP requests                     |

---

## 🔩 How It Works – Component Breakdown

### 1. **Socket Setup and Server Initialization**

At the core of any network application is socket programming. The proxy server begins by setting up a socket to listen for incoming connections:

```C
// Create proxy socket
proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    
// Set socket options to reuse address
int reuse = 1;
setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    
// Bind the socket to an address and port
bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
// Listen for connections
listen(proxy_socketId, MAX_CLIENTS);
```
This sequence creates a TCP socket, configures it to reuse the address (helpful during development), binds it to the specified port (defaulting to 5000), and starts listening for client connections.

---

### 2. **Multithreading**

A key operating system concept demonstrated here is concurrency through multithreading. Rather than processing requests sequentially (which would block the server while handling each request), the proxy creates a new thread for each incoming connection:

```c
// Accept client connection
client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);

// Create a thread to handle the client
pthread_t thread;
struct thread_args *args = malloc(sizeof(struct thread_args));
args->socket = client_socketId;

pthread_create(&thread, NULL, handle_client, (void*)args);
pthread_detach(thread);
```

Here, the main thread accepts a connection, then immediately creates a new worker thread to handle that specific client. The pthread_detach call ensures the thread resources are automatically reclaimed when the thread finishes its work.

---

### 3. **Synchronization**
With multiple threads running concurrently, the server needs mechanisms to safely coordinate access to shared resources. The code uses two primary synchronization tools:
#### Semaphores

Used to **limit max concurrent clients connections**:

```c
// Initialize semaphore with max clients
sem_init(&seamaphore, 0, MAX_CLIENTS);

// Wait on semaphore before handling a client
sem_wait(&seamaphore);

// Release semaphore after handling client
sem_post(&seamaphore);
```

#### Mutexes

Used to **safely access the shared cache**:

```c
// Initialize mutex
pthread_mutex_init(&lock, NULL);

// Lock mutex before accessing cache
pthread_mutex_lock(&lock);

// Critical section: operations on the cache

// Unlock mutex after cache operations
pthread_mutex_unlock(&lock);
```
This prevents race conditions where two threads might try to modify the cache simultaneously, which could corrupt the data structure.

---

### 4. **Caching with LRU Strategy**
The proxy implements a Least Recently Used (LRU) caching strategy to store and quickly retrieve previously accessed web content:

Cache is implemented as a **doubly-linked list** for efficient insertions/removals

```c
struct cache_element {
    char *request;
    char *data;
    int len;
    struct cache_element *next;
    struct cache_element *prev;
};
```
**The LRU algorithm works by:**

1. Moving a cache element to the front of the list when it's accessed (marking it as "recently used")
2. Adding new elements to the front of the list
3. Removing elements from the back of the list when the cache reaches its capacity limit

The cache operations are protected by a mutex to ensure thread safety:

```c
// Add to cache with thread safety
void add_to_cache(char *request, char *data, int len) {
    pthread_mutex_lock(&lock);
    
    // Cache implementation logic
    
    pthread_mutex_unlock(&lock);
}
```
---

### 5. **Request Handling**

The proxy handles two types of HTTP methods differently:

* **GET**: Normal HTTP requests that can be cached
```c
if (!strcmp(request->method, "GET")) {
    // Check if in cache
    // If not, fetch from remote server
    // Store in cache
    // Return to client
}
```
* **CONNECT**: Used for HTTPS connections that require **tunneling**
```c
else if (!strcmp(request->method, "CONNECT")) {
    // Establish connection to remote server
    // Set up tunneling between client and server
    // No caching for encrypted data
}
```
For HTTPS **(CONNECT method)**, the proxy acts as a simple tunnel, passing encrypted data between client and server without interpreting it, since the data is encrypted and cannot be cached.

---

### 6. **Memory Management**

Careful manual allocation/deallocation using `malloc`/`free`, with error checking:

```c
// Allocate memory
buffer = (char*)malloc(MAX_BYTES);

// Check allocation success
if (buffer == NULL) {
    perror("Memory allocation failed");
    return NULL;
}

// Use memory

// Free memory when done
free(buffer);
```

Memory leaks are particularly dangerous in long-running server applications, as they can eventually consume all available system memory.

---

## 🔁 End-to-End Flow

1. **Client Connection**:
* Client configures browser/tool to use the proxy
* The main thread accepts the connection
* A new worker thread is spawned to handle this specific client

2. **Request Parsing**:
* Worker thread reads the HTTP request from the client
* Request is parsed to extract method, URL, and headers
* For direct browser requests, format conversion is applied

3. **Cache Lookup**:
* For GET requests, the proxy checks if this request exists in the cache
* If found (cache hit), it returns the cached response directly to the client
* Cache access is protected by a mutex for thread safety

4. **Remote Server Communication**:
* If not in cache (cache miss), the proxy connects to the remote server
* For GET: It forwards the request and receives the response
* For CONNECT: It establishes a tunnel for encrypted communication

5. **Response Handling**:

* For GET: The response is stored in the cache for future use (if cacheable)
* The response is sent back to the client
* Cache updates are protected by mutex locks

6. **Connection Cleanup**:
* The connections are closed
* Memory is freed
* The semaphore is released, allowing another client to be served

---

## 💡 Key OS Concepts Demonstrated

| Concept               | Application                                |
| --------------------- | ------------------------------------------ |
| **Thread Management** | POSIX threads handle client connections    |
| **Concurrency**       | Multiple clients served simultaneously     |
| **Synchronization**   | Semaphores + mutexes for thread safety     |
| **Memory Handling**   | Manual memory management for buffers/cache |
| **Networking**        | Low-level socket programming               |
| **Protocol Handling** | HTTP parsing, CONNECT tunneling            |

---

## 🧠 Educational Takeaways

This project isn't just about writing a server — it's a **practical demonstration of OS fundamentals**:

1. **Concurrency vs. Parallelism**: The proxy demonstrates concurrency by handling multiple client requests simultaneously, even if they execute on a single CPU core.

2. **Thread Safety**: The careful use of synchronization primitives shows how to avoid race conditions when multiple threads access shared data.

3. **Resource Management**: The semaphore implementation demonstrates how to limit resource consumption (in this case, the number of client connections).

4. **Performance Optimization**: The caching system shows how to balance memory usage against performance gains.

5. **Protocol Handling**: The different approaches for HTTP and HTTPS traffic demonstrate protocol-aware processing.

---

## 📎 Final Notes

> **Want to take it further?**

> Feel free to fork, extend, and enhance it. Add logging, metrics, SSL termination, support for more HTTP methods, or even a user interface. The codebase is modular by design — built to be **educational, modifiable, and extensible**.

> *"The best way to learn is to build and improve."*

---

