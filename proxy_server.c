#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#define MAX_BYTES 4096      // Max allowed size of request/response
#define MAX_CLIENTS 400     // Max number of client requests served at a time
#define MAX_SIZE 200*(1<<20)     // Size of the cache (200MB)
#define MAX_ELEMENT_SIZE 10*(1<<20)     // Max size of an element in cache (10MB)

// Cache element structure to store response data
typedef struct cache_element cache_element;

struct cache_element {
    char* data;               // Stores HTTP response
    int len;                  // Length of data
    char* url;                // URL of the request
    time_t lru_time_track;    // Timestamp for LRU tracking
    cache_element* next;      // Pointer to next element
};

// Function declarations
cache_element* find(char* url);
int add_cache_element(char* data, int size, char* url);
void remove_cache_element();
void cleanup_cache();
void* thread_fn(void* socketNew);
int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq);
int connectRemoteServer(char* host_addr, int port_num);
int sendErrorMessage(int socket, int status_code);
int checkHTTPversion(char *msg);
void signal_handler(int sig);

// Global variables
int port_number = 8080;               // Default Port
int proxy_socketId;                   // Socket descriptor of proxy server
pthread_t tid[MAX_CLIENTS];           // Array to store the thread ids of clients
sem_t seamaphore;                     // Semaphore for limiting concurrent clients
pthread_mutex_t lock;                 // Mutex for cache access

cache_element* head = NULL;           // Pointer to the cache
int cache_size = 0;                   // Current size of the cache

/**
 * Send an HTTP error message to the client.
 * 
 * @param socket Client socket
 * @param status_code HTTP status code
 * @return 1 on success, -1 on failure
 */
int sendErrorMessage(int socket, int status_code) {
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S GMT", &data);

    switch(status_code) {
        case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: close\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer/1.0\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Request</H1>\n</BODY></HTML>", currentTime);
                  printf("400 Bad Request\n");
                  send(socket, str, strlen(str), 0);
                  break;

        case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: close\r\nDate: %s\r\nServer: ProxyServer/1.0\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
                  printf("403 Forbidden\n");
                  send(socket, str, strlen(str), 0);
                  break;

        case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: close\r\nDate: %s\r\nServer: ProxyServer/1.0\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
                  printf("404 Not Found\n");
                  send(socket, str, strlen(str), 0);
                  break;

        case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: close\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer/1.0\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
                  printf("500 Internal Server Error\n");
                  send(socket, str, strlen(str), 0);
                  break;

        case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: close\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer/1.0\r\n\r\n<HTML><HEAD><TITLE>501 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
                  printf("501 Not Implemented\n");
                  send(socket, str, strlen(str), 0);
                  break;

        case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: close\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer/1.0\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
                  printf("505 HTTP Version Not Supported\n");
                  send(socket, str, strlen(str), 0);
                  break;

        default:  return -1;
    }
    return 1;
}

/**
 * Connect to a remote server.
 * 
 * @param host_addr Host address or domain name
 * @param port_num Port number
 * @return Socket descriptor on success, -1 on failure
 */
int connectRemoteServer(char* host_addr, int port_num) {
    // Creating Socket for remote server
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (remoteSocket < 0) {
        printf("Error in Creating Socket.\n");
        return -1;
    }
    
    // Get host by the name or ip address provided
    struct hostent *host = gethostbyname(host_addr);    
    if (host == NULL) {
        fprintf(stderr, "No such host exists: %s\n", host_addr);    
        close(remoteSocket);
        return -1;
    }

    // Insert ip address and port number of host in struct `server_addr`
    struct sockaddr_in server_addr;

    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);

    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

    // Connect to Remote server
    if (connect(remoteSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0) {
        fprintf(stderr, "Error in connecting to %s:%d\n", host_addr, port_num); 
        close(remoteSocket);
        return -1;
    }
    
    return remoteSocket;
}

/**
 * Handle an HTTP request.
 * 
 * @param clientSocket Client socket
 * @param request Parsed HTTP request
 * @param tempReq Original HTTP request string
 * @return 0 on success, -1 on failure
 */
int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq) {
    char *buf = (char*)malloc(sizeof(char) * MAX_BYTES);
    if (buf == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return -1;
    }
    
    // Build the HTTP request to forward to the server
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");

    size_t len = strlen(buf);

    // Set headers
    if (ParsedHeader_set(request, "Connection", "close") < 0) {
        printf("Failed to set Connection header\n");
    }

    if (ParsedHeader_get(request, "Host") == NULL) {
        if (ParsedHeader_set(request, "Host", request->host) < 0) {
            printf("Failed to set Host header\n");
        }
    }

    // Add headers to the request
    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
        printf("Header unparsing failed\n");
    }

    // Determine server port
    int server_port = 80;    // Default Remote Server Port
    if (request->port != NULL) {
        server_port = atoi(request->port);
    }

    // Connect to the remote server
    int remoteSocketID = connectRemoteServer(request->host, server_port);

    if (remoteSocketID < 0) {
        free(buf);
        return -1;
    }

    // Send request to remote server
    int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);
    if (bytes_send < 0) {
        printf("Failed to send request to remote server\n");
        close(remoteSocketID);
        free(buf);
        return -1;
    }

    bzero(buf, MAX_BYTES);
    
    // Receive response from remote server and forward to client
    bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
    
    // Allocate temp buffer to store the entire response
    char *temp_buffer = (char*)malloc(sizeof(char) * MAX_BYTES);
    if (temp_buffer == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        close(remoteSocketID);
        free(buf);
        return -1;
    }
    
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    // Continue receiving data and forwarding to client
    while (bytes_send > 0) {
        // Forward data to client
        if (send(clientSocket, buf, bytes_send, 0) < 0) {
            fprintf(stderr, "Error in sending data to client\n");
            break;
        }
        
        // Store data in temp buffer for caching
        if (temp_buffer_index + bytes_send >= temp_buffer_size) {
            temp_buffer_size += MAX_BYTES;
            char *new_buffer = (char*)realloc(temp_buffer, temp_buffer_size);
            if (new_buffer == NULL) {
                fprintf(stderr, "Memory reallocation failed\n");
                break;
            }
            temp_buffer = new_buffer;
        }
        
        memcpy(temp_buffer + temp_buffer_index, buf, bytes_send);
        temp_buffer_index += bytes_send;
        
        bzero(buf, MAX_BYTES);
        bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
    }

    // Null terminate the response
    if (temp_buffer_index < temp_buffer_size) {
        temp_buffer[temp_buffer_index] = '\0';
    } else {
        temp_buffer = (char*)realloc(temp_buffer, temp_buffer_size + 1);
        temp_buffer[temp_buffer_index] = '\0';
    }

    // Add the response to the cache
    add_cache_element(temp_buffer, temp_buffer_index, tempReq);
    
    printf("Request handled successfully\n");
    
    free(buf);
    free(temp_buffer);
    close(remoteSocketID);
    return 0;
}

/**
 * Check HTTP version.
 * 
 * @param msg HTTP version string
 * @return 1 if valid version, -1 otherwise
 */
int checkHTTPversion(char *msg) {
    if (strncmp(msg, "HTTP/1.1", 8) == 0) {
        return 1;
    } else if (strncmp(msg, "HTTP/1.0", 8) == 0) {            
        return 1;    // Handling this similar to version 1.1
    } else {
        return -1;
    }
}

/**
 * Find a cache element by URL.
 * 
 * @param url URL to find
 * @return Cache element if found, NULL otherwise
 */
cache_element* find(char* url) {
    pthread_mutex_lock(&lock);
    
    cache_element* temp = head;
    while (temp != NULL) {
        if (strcmp(temp->url, url) == 0) {
            // Update LRU timestamp
            temp->lru_time_track = time(NULL);
            pthread_mutex_unlock(&lock);
            return temp;
        }
        temp = temp->next;
    }
    
    pthread_mutex_unlock(&lock);
    return NULL;
}

/**
 * Add an element to the cache with LRU eviction policy.
 * 
 * @param data Response data
 * @param size Size of the data
 * @param url Request URL
 * @return 0 on success, -1 on failure
 */
int add_cache_element(char* data, int size, char* url) {
    if (size > MAX_ELEMENT_SIZE) {
        printf("Response too large to cache\n");
        return -1;
    }
    
    pthread_mutex_lock(&lock);
    
    // Check if the URL already exists in cache
    cache_element* existing = head;
    while (existing != NULL) {
        if (strcmp(existing->url, url) == 0) {
            // URL exists, update the data
            cache_size -= existing->len;
            
            // Allocate new memory for data
            char* new_data = (char*)malloc(size + 1);
            if (new_data == NULL) {
                pthread_mutex_unlock(&lock);
                return -1;
            }
            
            // Copy new data
            memcpy(new_data, data, size);
            new_data[size] = '\0';
            
            // Free old data and update
            free(existing->data);
            existing->data = new_data;
            existing->len = size;
            existing->lru_time_track = time(NULL);
            
            cache_size += size;
            pthread_mutex_unlock(&lock);
            return 0;
        }
        existing = existing->next;
    }
    
    // Free up space if needed
    while (cache_size + size > MAX_SIZE && head != NULL) {
        remove_cache_element();
    }
    
    // Create new cache element
    cache_element* new_element = (cache_element*)malloc(sizeof(cache_element));
    if (new_element == NULL) {
        pthread_mutex_unlock(&lock);
        return -1;
    }
    
    // Allocate and copy data
    new_element->data = (char*)malloc(size + 1);
    if (new_element->data == NULL) {
        free(new_element);
        pthread_mutex_unlock(&lock);
        return -1;
    }
    memcpy(new_element->data, data, size);
    new_element->data[size] = '\0';
    
    // Allocate and copy URL
    new_element->url = (char*)malloc(strlen(url) + 1);
    if (new_element->url == NULL) {
        free(new_element->data);
        free(new_element);
        pthread_mutex_unlock(&lock);
        return -1;
    }
    strcpy(new_element->url, url);
    
    // Set other fields
    new_element->len = size;
    new_element->lru_time_track = time(NULL);
    
    // Add to front of the list
    new_element->next = head;
    head = new_element;
    cache_size += size;
    
    printf("Added to cache: %d bytes, total cache size: %d\n", size, cache_size);
    
    pthread_mutex_unlock(&lock);
    return 0;
}

/**
 * Remove the least recently used element from the cache.
 */
void remove_cache_element() {
    if (head == NULL) {
        return;
    }
    
    // Find the LRU element
    cache_element* current = head;
    cache_element* prev = NULL;
    cache_element* lru = head;
    cache_element* lru_prev = NULL;
    
    while (current != NULL) {
        if (current->lru_time_track < lru->lru_time_track) {
            lru = current;
            lru_prev = prev;
        }
        prev = current;
        current = current->next;
    }
    
    // Remove the LRU element
    if (lru_prev == NULL) {
        // LRU is the head
        head = lru->next;
    } else {
        lru_prev->next = lru->next;
    }
    
    // Update cache size
    cache_size -= lru->len;
    
    printf("Removed from cache: %d bytes, total cache size: %d\n", lru->len, cache_size);
    
    // Free memory
    free(lru->data);
    free(lru->url);
    free(lru);
}

/**
 * Clean up the entire cache.
 */
void cleanup_cache() {
    pthread_mutex_lock(&lock);
    
    cache_element* current = head;
    cache_element* next;
    
    while (current != NULL) {
        next = current->next;
        free(current->data);
        free(current->url);
        free(current);
        current = next;
    }
    
    head = NULL;
    cache_size = 0;
    
    pthread_mutex_unlock(&lock);
}

/**
 * Thread function to handle client connections.
 * 
 * @param socketNew Client socket pointer
 * @return NULL
 */
void* thread_fn(void* socketNew) {
    // Wait for semaphore to control concurrent clients
    sem_wait(&seamaphore);
    
    int p;
    sem_getvalue(&seamaphore, &p);
    printf("Semaphore value: %d\n", p);
    
    int* t = (int*)(socketNew);
    int socket = *t;           // Socket descriptor of the connected Client
    free(t);                   // Free the memory allocated for socket
    
    int bytes_send_client, len;    // Bytes Transferred

    // Create buffer for client request
    char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));
    if (buffer == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        close(socket);
        sem_post(&seamaphore);
        return NULL;
    }
    
    // Receive request from client
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);
    
    // Print the first few bytes for debugging
    if (bytes_send_client > 0) {
        char debug_buffer[128] = {0};
        int debug_len = bytes_send_client > 100 ? 100 : bytes_send_client;
        memcpy(debug_buffer, buffer, debug_len);
        debug_buffer[debug_len] = '\0';
        printf("Request start: %s\n", debug_buffer);
        
        // Special handling for the CONNECT method
        if (strncmp(buffer, "CONNECT ", 8) == 0) {
            printf("Detected CONNECT method, processing directly\n");
            
            // Find host and port
            char *host_port = buffer + 8;
            char *space = strchr(host_port, ' ');
            if (space) {
                *space = '\0'; // Temporarily terminate string for easier extraction
                
                char host[256];
                int port = 443; // Default HTTPS port
                
                char *colon = strchr(host_port, ':');
                if (colon) {
                    // We have a port specification
                    int host_len = colon - host_port;
                    strncpy(host, host_port, host_len);
                    host[host_len] = '\0';
                    port = atoi(colon + 1);
                } else {
                    // No port, use the whole host_port string
                    strcpy(host, host_port);
                }
                
                *space = ' '; // Restore the original string
                
                printf("CONNECT: Connecting to %s:%d\n", host, port);
                
                // Connect to remote server
                int remote_socket = connectRemoteServer(host, port);
                if (remote_socket < 0) {
                    sendErrorMessage(socket, 502); // Bad Gateway
                    printf("Failed to connect to remote server\n");
                    free(buffer);
                    close(socket);
                    sem_post(&seamaphore);
                    return NULL;
                }
                
                // Send 200 Connection established
                char response[] = "HTTP/1.1 200 Connection Established\r\nProxy-agent: ProxyServer/1.0\r\n\r\n";
                send(socket, response, strlen(response), 0);
                
                // Set up for tunneling data between client and server
                fd_set read_fds;
                int max_fd = (socket > remote_socket) ? socket : remote_socket;
                char tunnel_buffer[MAX_BYTES];
                
                // Continue tunneling until one side closes the connection
                struct timeval timeout;
                int activity;
                
                while (1) {
                    // Clear the socket set
                    FD_ZERO(&read_fds);
                    FD_SET(socket, &read_fds);
                    FD_SET(remote_socket, &read_fds);
                    
                    // Set timeout for select
                    timeout.tv_sec = 30;
                    timeout.tv_usec = 0;
                    
                    // Wait for activity on either socket
                    activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
                    
                    if (activity < 0) {
                        printf("Select error\n");
                        break;
                    }
                    
                    if (activity == 0) {
                        // Timeout occurred
                        printf("Timeout in CONNECT tunnel\n");
                        break;
                    }
                    
                    // Check client socket activity
                    if (FD_ISSET(socket, &read_fds)) {
                        int bytes_read = recv(socket, tunnel_buffer, sizeof(tunnel_buffer), 0);
                        if (bytes_read <= 0) {
                            printf("Client closed connection\n");
                            break;
                        }
                        
                        // Forward to remote server
                        send(remote_socket, tunnel_buffer, bytes_read, 0);
                    }
                    
                    // Check remote socket activity
                    if (FD_ISSET(remote_socket, &read_fds)) {
                        int bytes_read = recv(remote_socket, tunnel_buffer, sizeof(tunnel_buffer), 0);
                        if (bytes_read <= 0) {
                            printf("Server closed connection\n");
                            break;
                        }
                        
                        // Forward to client
                        send(socket, tunnel_buffer, bytes_read, 0);
                    }
                }
                
                // Close sockets and cleanup
                close(remote_socket);
                free(buffer);
                close(socket);
                sem_post(&seamaphore);
                return NULL;
            }
        }
    }
    
    // Continue receiving until we get the full request (ending with "\r\n\r\n")
    while (bytes_send_client > 0) {
        len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL) {    
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        } else {
            break;
        }
    }
    
    // Handle the case of direct browser requests that don't have an absolute URL
    if (bytes_send_client > 0 && strncmp(buffer, "GET /", 5) == 0) {
        printf("Direct browser request detected, converting to proxy format\n");
        
        // Extract the Host header
        char *host_header = strstr(buffer, "Host: ");
        if (host_header) {
            char host[256] = {0};
            char *host_end = strstr(host_header, "\r\n");
            if (host_end) {
                int host_len = host_end - (host_header + 6);
                strncpy(host, host_header + 6, host_len);
                host[host_len] = '\0';
                
                // Extract the path
                char *path_start = buffer + 4;  // Skip "GET "
                char *path_end = strstr(path_start, " HTTP");
                if (path_end) {
                    char path[1024] = {0};
                    int path_len = path_end - path_start;
                    strncpy(path, path_start, path_len);
                    path[path_len] = '\0';
                    
                    // Extract HTTP version
                    char version[16] = {0};
                    if (strncmp(path_end + 1, "HTTP/1.1", 8) == 0) {
                        strcpy(version, "HTTP/1.1");
                    } else {
                        strcpy(version, "HTTP/1.0");
                    }
                    
                    // Check if this is a direct request to the proxy itself 
                    if (strcmp(host, "localhost:5000") == 0 || 
                        strcmp(host, "127.0.0.1:5000") == 0 ||
                        strcmp(host, "localhost:8080") == 0 ||
                        strcmp(host, "127.0.0.1:8080") == 0) {
                        // Send a simple response for direct requests to the proxy
                        char *proxy_response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
                                           "<html><body><h1>Proxy Server</h1>"
                                           "<p>This is a HTTP/HTTPS proxy server. Configure your browser to use this as a proxy.</p>"
                                           "<p>Do not access this URL directly.</p></body></html>";
                        
                        send(socket, proxy_response, strlen(proxy_response), 0);
                        
                        printf("Sent direct proxy access response\n");
                        free(buffer);
                        close(socket);
                        sem_post(&seamaphore);
                        
                        int p;
                        sem_getvalue(&seamaphore, &p);
                        printf("Semaphore post value: %d\n", p);
                        
                        return NULL;
                    }
                    
                    // Create a new buffer with absolute URL for other hosts
                    char *new_buffer = (char *)malloc(MAX_BYTES);
                    if (new_buffer) {
                        snprintf(new_buffer, MAX_BYTES, 
                                "GET http://%s%s %s\r\n%s", 
                                host, path, version, host_header);
                        
                        // Copy the rest of the headers
                        char *headers_start = strstr(buffer, "\r\n") + 2;
                        int headers_len = buffer + strlen(buffer) - headers_start;
                        strncat(new_buffer, headers_start, headers_len);
                        
                        // Replace the original buffer with the new one
                        strcpy(buffer, new_buffer);
                        free(new_buffer);
                        
                        printf("Converted request: %s\n", buffer);
                    }
                }
            }
        }
    }
    
    // Create a copy of the request for cache lookup
    char *tempReq = NULL;
    if (bytes_send_client > 0) {
        tempReq = (char*)malloc(strlen(buffer) * sizeof(char) + 1);
        if (tempReq == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            free(buffer);
            close(socket);
            sem_post(&seamaphore);
            return NULL;
        }
        strcpy(tempReq, buffer);
    }
    
    // Check if request is in cache
    struct cache_element* temp = NULL;
    if (tempReq != NULL) {
        temp = find(tempReq);
    }

    if (temp != NULL) {
        // Request found in cache, send response to client
        printf("Cache hit! Sending cached response\n");
        
        // Send the cached response in chunks
        int total_sent = 0;
        int remaining = temp->len;
        int chunk_size = MAX_BYTES;
        
        while (remaining > 0) {
            int to_send = (remaining < chunk_size) ? remaining : chunk_size;
            int sent = send(socket, temp->data + total_sent, to_send, 0);
            
            if (sent <= 0) {
                fprintf(stderr, "Error sending cached response\n");
                break;
            }
            
            total_sent += sent;
            remaining -= sent;
        }
        
        printf("Sent %d bytes from cache\n", total_sent);
    }
    else if (bytes_send_client > 0) {
        // Request not in cache, parse the request and handle it
        len = strlen(buffer);
        struct ParsedRequest* request = ParsedRequest_create();
        
        if (ParsedRequest_parse(request, buffer, len) < 0) {
            printf("Parsing failed\n");
            sendErrorMessage(socket, 400);  // Bad Request
        }
        else {    
            bzero(buffer, MAX_BYTES);
            
            // Support for GET and CONNECT methods
            if (!strcmp(request->method, "GET")) {
                if (request->host && request->path && (checkHTTPversion(request->version) == 1)) {
                    // Handle GET request
                    if (handle_request(socket, request, tempReq) == -1) {    
                        sendErrorMessage(socket, 500);  // Internal Server Error
                    }
                }
                else {
                    sendErrorMessage(socket, 400);  // Bad Request
                }
            }
            else if (!strcmp(request->method, "CONNECT")) {
                // Handle CONNECT method (used by browsers for HTTPS)
                printf("CONNECT method detected for: %s\n", request->host);
                
                // Extract host and port from the request
                char host[256];
                int port = 443; // Default HTTPS port
                
                if (request->port != NULL) {
                    port = atoi(request->port);
                }
                
                strcpy(host, request->host);
                printf("Connecting to %s:%d\n", host, port);
                
                // Connect to remote server
                int remote_socket = connectRemoteServer(host, port);
                if (remote_socket < 0) {
                    sendErrorMessage(socket, 502);  // Bad Gateway
                    printf("Failed to connect to remote server\n");
                } else {
                    // Send 200 Connection established
                    char response[] = "HTTP/1.1 200 Connection Established\r\nProxy-agent: ProxyServer/1.0\r\n\r\n";
                    send(socket, response, strlen(response), 0);
                    
                    // Set up for tunneling data between client and server
                    fd_set read_fds;
                    int max_fd = (socket > remote_socket) ? socket : remote_socket;
                    char buffer[MAX_BYTES];
                    
                    // Tunnel data for a limited time or until connection closes
                    struct timeval timeout;
                    int activity;
                    
                    // Continue tunneling until one side closes the connection
                    while (1) {
                        // Clear the socket set
                        FD_ZERO(&read_fds);
                        FD_SET(socket, &read_fds);
                        FD_SET(remote_socket, &read_fds);
                        
                        // Set timeout for select
                        timeout.tv_sec = 30;
                        timeout.tv_usec = 0;
                        
                        // Wait for activity on either socket
                        activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
                        
                        if (activity < 0) {
                            printf("Select error\n");
                            break;
                        }
                        
                        if (activity == 0) {
                            // Timeout occurred
                            printf("Timeout in CONNECT tunnel\n");
                            break;
                        }
                        
                        // Check client socket activity
                        if (FD_ISSET(socket, &read_fds)) {
                            int bytes_read = recv(socket, buffer, sizeof(buffer), 0);
                            if (bytes_read <= 0) {
                                printf("Client closed connection\n");
                                break;
                            }
                            
                            // Forward to remote server
                            send(remote_socket, buffer, bytes_read, 0);
                        }
                        
                        // Check remote socket activity
                        if (FD_ISSET(remote_socket, &read_fds)) {
                            int bytes_read = recv(remote_socket, buffer, sizeof(buffer), 0);
                            if (bytes_read <= 0) {
                                printf("Server closed connection\n");
                                break;
                            }
                            
                            // Forward to client
                            send(socket, buffer, bytes_read, 0);
                        }
                    }
                    
                    // Close the remote socket
                    close(remote_socket);
                }
            }
            else {
                printf("Method not supported: %s\n", request->method);
                sendErrorMessage(socket, 501);  // Not Implemented
            }
        }
        
        // Free parsed request
        ParsedRequest_destroy(request);
    }
    else if (bytes_send_client < 0) {
        perror("Error in receiving from client");
    }
    else {
        printf("Client disconnected\n");
    }

    // Clean up
    free(buffer);
    if (tempReq != NULL) {
        free(tempReq);
    }
    
    // Close socket and release semaphore
    shutdown(socket, SHUT_RDWR);
    close(socket);
    sem_post(&seamaphore);    
    
    sem_getvalue(&seamaphore, &p);
    printf("Semaphore post value: %d\n", p);
    
    return NULL;
}

/**
 * Signal handler for clean shutdown.
 * 
 * @param sig Signal number
 */
void signal_handler(int sig) {
    printf("\nCleaning up and shutting down proxy server...\n");
    
    // Close server socket
    if (proxy_socketId > 0) {
        close(proxy_socketId);
    }
    
    // Clean up the cache
    cleanup_cache();
    
    // Destroy mutex and semaphore
    pthread_mutex_destroy(&lock);
    sem_destroy(&seamaphore);
    
    exit(0);
}

/**
 * Main function.
 */
int main(int argc, char * argv[]) {
    int client_socketId, client_len; 
    struct sockaddr_in server_addr, client_addr; 
    
    // Set up signal handler for clean shutdown
    signal(SIGINT, signal_handler);
    
    // Initialize semaphore and mutex
    sem_init(&seamaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);
    
    // Parse command line arguments
    if (argc == 2) {
        port_number = atoi(argv[1]);
    } else if (argc > 2) {
        printf("Usage: %s [port_number]\n", argv[0]);
        exit(1);
    }
    
    printf("Setting Proxy Server Port: %d\n", port_number);
    
    // Create proxy socket
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    
    if (proxy_socketId < 0) {
        perror("Failed to create socket");
        exit(1);
    }
    
    // Set socket options to reuse address
    int reuse = 1;
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
    }
    
    // Set up server address
    bzero((char*)&server_addr, sizeof(server_addr));  
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    // Bind the socket
    if (bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding failed");
        exit(1);
    }
    
    printf("Binding on port: %d\n", port_number);
    
    // Listen for connections
    if (listen(proxy_socketId, MAX_CLIENTS) < 0) {
        perror("Failed to listen");
        exit(1);
    }
    
    printf("Proxy server listening on port %d...\n", port_number);
    
    client_len = sizeof(client_addr);
    
    int thread_idx = 0;
    
    // Main server loop
    while (1) {
        // Accept client connection
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        
        if (client_socketId < 0) {
            perror("Accept failed");
            continue;
        }
        
        printf("Client connected: %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        // Allocate memory for socket descriptor to pass to thread
        int *client_sock = malloc(sizeof(int));
        if (client_sock == NULL) {
            perror("Memory allocation failed");
            close(client_socketId);
            continue;
        }
        
        *client_sock = client_socketId;
        
        // Create thread to handle client
        if (pthread_create(&tid[thread_idx], NULL, thread_fn, (void*)client_sock) != 0) {
            perror("Thread creation failed");
            free(client_sock);
            close(client_socketId);
            continue;
        }
        
        // Detach thread to automatically clean up when it exits
        pthread_detach(tid[thread_idx]);
        
        thread_idx = (thread_idx + 1) % MAX_CLIENTS;
    }
    
    // Clean up (this part will not be reached in normal operation)
    close(proxy_socketId);
    cleanup_cache();
    pthread_mutex_destroy(&lock);
    sem_destroy(&seamaphore);
    
    return 0;
}
