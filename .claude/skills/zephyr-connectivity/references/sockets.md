# Sockets, TLS, and DNS

## Overview

### Quick Start

1. **Enable Sockets**: `CONFIG_NET_SOCKETS=y` in `prj.conf`
2. **Choose Protocol**: TCP (`CONFIG_NET_TCP=y`) or UDP (`CONFIG_NET_UDP=y`)
3. **Optional TLS**: `CONFIG_NET_SOCKETS_SOCKOPT_TLS=y` for secure sockets
4. **Optional DNS**: `CONFIG_DNS_RESOLVER=y` for hostname resolution
5. **Create Socket**: `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)`

### Core TCP Client Pattern

```c
#include <zephyr/net/socket.h>

int tcp_client_example(void)
{
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
    };
    inet_pton(AF_INET, "192.168.1.100", &addr.sin_addr);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        return -errno;
    }

    int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        close(sock);
        return -errno;
    }

    /* Send/receive data */
    send(sock, "Hello", 5, 0);

    char buf[128];
    int len = recv(sock, buf, sizeof(buf), 0);

    close(sock);
    return 0;
}
```

### Core TCP Server Pattern

```c
int tcp_server_example(void)
{
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
        .sin_addr.s_addr = INADDR_ANY,
    };

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        return -errno;
    }

    int ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        close(sock);
        return -errno;
    }

    ret = listen(sock, 5);  /* Backlog of 5 */
    if (ret < 0) {
        close(sock);
        return -errno;
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client = accept(sock, (struct sockaddr *)&client_addr, &client_len);
        if (client < 0) {
            continue;
        }

        /* Handle client connection */
        char buf[128];
        int len = recv(client, buf, sizeof(buf), 0);
        if (len > 0) {
            send(client, buf, len, 0);  /* Echo back */
        }

        close(client);
    }
}
```

### Socket Types

| Family | Type | Protocol | Description | Kconfig |
|--------|------|----------|-------------|---------|
| AF_INET/AF_INET6 | SOCK_STREAM | IPPROTO_TCP | TCP stream | `CONFIG_NET_TCP` |
| AF_INET/AF_INET6 | SOCK_STREAM | IPPROTO_TLS_1_2 | TLS over TCP | `CONFIG_NET_SOCKETS_SOCKOPT_TLS` |
| AF_INET/AF_INET6 | SOCK_DGRAM | IPPROTO_UDP | UDP datagram | `CONFIG_NET_UDP` |
| AF_INET/AF_INET6 | SOCK_DGRAM | IPPROTO_DTLS_1_2 | DTLS over UDP | `CONFIG_NET_SOCKETS_ENABLE_DTLS` |
| AF_INET/AF_INET6 | SOCK_RAW | IPPROTO_IP | Raw IP packets | `CONFIG_NET_SOCKETS_INET_RAW` |
| AF_PACKET | SOCK_RAW | ETH_P_ALL | Raw L2 packets | `CONFIG_NET_SOCKETS_PACKET` |
| AF_CAN | SOCK_RAW | CAN_RAW | CAN bus | `CONFIG_NET_SOCKETS_CAN` |

### Detailed References

- **BSD Sockets API**: [#sockets](#sockets)
- **TLS/DTLS Secure Sockets**: [#tls](#tls)
- **DNS Resolution**: [#dns](#dns)
- **Kconfig Options**: [#kconfig](#kconfig)
- **File Locations**: [#locations](#locations)

### TLS Quick Start

```c
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#define CA_CERT_TAG 1

static const unsigned char ca_cert[] = { /* DER-encoded CA cert */ };

int tls_client_example(void)
{
    /* 1. Register credentials */
    tls_credential_add(CA_CERT_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
                       ca_cert, sizeof(ca_cert));

    /* 2. Create TLS socket */
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);

    /* 3. Configure TLS options */
    sec_tag_t tags[] = { CA_CERT_TAG };
    setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, tags, sizeof(tags));

    char hostname[] = "example.com";
    setsockopt(sock, SOL_TLS, TLS_HOSTNAME, hostname, sizeof(hostname));

    /* 4. Connect and use like regular socket */
    struct sockaddr_in addr = { /* ... */ };
    connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    send(sock, "GET / HTTP/1.1\r\n\r\n", 18, 0);

    close(sock);
    return 0;
}
```

### DNS Quick Start (getaddrinfo)

```c
#include <zephyr/net/socket.h>

int dns_lookup_example(void)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,      /* or AF_UNSPEC for IPv4/IPv6 */
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;

    int ret = getaddrinfo("example.com", "443", &hints, &res);
    if (ret != 0) {
        printk("DNS lookup failed: %d\n", ret);
        return -1;
    }

    /* Use resolved address */
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    connect(sock, res->ai_addr, res->ai_addrlen);

    freeaddrinfo(res);  /* Free when done */
    return 0;
}
```

### Minimum Kconfig

#### Basic Sockets
```
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_IPV4=y
CONFIG_NET_IPV6=y
```

#### With TLS
```
CONFIG_NET_SOCKETS_SOCKOPT_TLS=y
CONFIG_MBEDTLS=y
CONFIG_MBEDTLS_BUILTIN=y
CONFIG_MBEDTLS_ENABLE_HEAP=y
CONFIG_MBEDTLS_HEAP_SIZE=60000
```

#### With DNS
```
CONFIG_DNS_RESOLVER=y
CONFIG_DNS_SERVER_IP_ADDRESSES=y
CONFIG_DNS_SERVER1="8.8.8.8"
```

### Common Socket Options

| Level | Option | Description |
|-------|--------|-------------|
| SOL_SOCKET | SO_REUSEADDR | Reuse local address |
| SOL_SOCKET | SO_RCVTIMEO | Receive timeout |
| SOL_SOCKET | SO_SNDTIMEO | Send timeout |
| SOL_SOCKET | SO_BINDTODEVICE | Bind to specific interface |
| SOL_TLS | TLS_SEC_TAG_LIST | TLS credential tags |
| SOL_TLS | TLS_HOSTNAME | Server hostname for SNI |
| SOL_TLS | TLS_PEER_VERIFY | Peer verification level |
| SOL_TLS | TLS_CIPHERSUITE_LIST | Allowed ciphersuites |

### Non-Blocking Sockets

```c
/* Set non-blocking mode */
int flags = fcntl(sock, F_GETFL, 0);
fcntl(sock, F_SETFL, flags | O_NONBLOCK);

/* Or use MSG_DONTWAIT per-call */
recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
```

### poll() for Multiple Sockets

```c
#include <zephyr/net/socket.h>

struct zsock_pollfd fds[2];
fds[0].fd = sock1;
fds[0].events = ZSOCK_POLLIN;
fds[1].fd = sock2;
fds[1].events = ZSOCK_POLLIN;

int ret = zsock_poll(fds, 2, 5000);  /* 5 second timeout */
if (ret > 0) {
    if (fds[0].revents & ZSOCK_POLLIN) {
        /* sock1 has data */
    }
    if (fds[1].revents & ZSOCK_POLLIN) {
        /* sock2 has data */
    }
}
```

### POSIX API Mode

Enable `CONFIG_POSIX_API=y` to use standard function names without `zsock_` prefix:

```c
/* With CONFIG_POSIX_API=y */
socket();   /* Instead of zsock_socket() */
connect();  /* Instead of zsock_connect() */
send();     /* Instead of zsock_send() */
recv();     /* Instead of zsock_recv() */
close();    /* Instead of zsock_close() */
```

### Related Skills

- **zephyr-wifi**: WiFi connectivity before socket operations
- **zephyr-kconfig**: Configure `CONFIG_NET_*` options
- **zephyr-shell-commands**: Network shell for debugging (`CONFIG_NET_SHELL=y`)

## Dns

### Table of Contents
- [Overview](#overview)
- [getaddrinfo (BSD Sockets API)](#getaddrinfo-bsd-sockets-api)
- [dns_resolve API (Async)](#dns_resolve-api-async)
- [mDNS Support](#mdns-support)
- [LLMNR Support](#llmnr-support)
- [Service Discovery (DNS-SD)](#service-discovery-dns-sd)
- [DNS Server Configuration](#dns-server-configuration)

### Overview

Zephyr provides two DNS resolution approaches:

| API | Style | Use Case |
|-----|-------|----------|
| `getaddrinfo()` | Synchronous (blocking) | Simple client apps, POSIX compatibility |
| `dns_resolve` | Asynchronous (callback) | Event-driven apps, multiple queries |

Both support:
- IPv4 (A records) and IPv6 (AAAA records)
- CNAME resolution (automatic chaining)
- mDNS (`.local` domains)
- LLMNR (link-local name resolution)

### getaddrinfo (BSD Sockets API)

Standard POSIX-compatible synchronous DNS lookup.

#### Basic Usage

```c
#include <zephyr/net/socket.h>

int resolve_and_connect(const char *hostname, const char *port)
{
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,     /* IPv4 or IPv6 */
        .ai_socktype = SOCK_STREAM, /* TCP */
    };
    struct addrinfo *res;

    int ret = getaddrinfo(hostname, port, &hints, &res);
    if (ret != 0) {
        printk("DNS lookup failed: %d\n", ret);
        return -1;
    }

    /* Try each result until one works */
    struct addrinfo *rp;
    int sock = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) {
            continue;
        }

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  /* Success */
        }

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);  /* Always free when done */

    if (sock < 0) {
        printk("Could not connect\n");
        return -1;
    }

    return sock;
}
```

#### Hints Structure

```c
struct addrinfo {
    int ai_flags;           /* AI_* flags */
    int ai_family;          /* AF_INET, AF_INET6, AF_UNSPEC */
    int ai_socktype;        /* SOCK_STREAM, SOCK_DGRAM */
    int ai_protocol;        /* IPPROTO_TCP, IPPROTO_UDP */
    socklen_t ai_addrlen;   /* Length of ai_addr */
    struct sockaddr *ai_addr;  /* Socket address */
    char *ai_canonname;     /* Canonical name */
    struct addrinfo *ai_next;  /* Next result */
};
```

#### Hint Flags

| Flag | Description |
|------|-------------|
| `AI_PASSIVE` | For server binding (returns `INADDR_ANY`) |
| `AI_CANONNAME` | Request canonical name in result |
| `AI_NUMERICHOST` | Hostname is numeric (skip DNS) |
| `AI_NUMERICSERV` | Service is numeric port |
| `AI_V4MAPPED` | Return IPv4-mapped IPv6 if no IPv6 |
| `AI_ADDRCONFIG` | Only return if host has that address family |

#### Return Values

| Value | Meaning |
|-------|---------|
| 0 | Success |
| EAI_AGAIN | Temporary failure |
| EAI_FAIL | Non-recoverable failure |
| EAI_MEMORY | Memory allocation failure |
| EAI_NONAME | Name not found |
| EAI_SERVICE | Service not found for socket type |

#### IPv4-Only Lookup

```c
struct addrinfo hints = {
    .ai_family = AF_INET,       /* Only IPv4 */
    .ai_socktype = SOCK_STREAM,
};
getaddrinfo("example.com", "80", &hints, &res);
```

#### IPv6-Only Lookup

```c
struct addrinfo hints = {
    .ai_family = AF_INET6,      /* Only IPv6 */
    .ai_socktype = SOCK_STREAM,
};
getaddrinfo("example.com", "80", &hints, &res);
```

### dns_resolve API (Async)

Asynchronous DNS resolution with callbacks. Better for event-driven applications.

#### Basic Query

```c
#include <zephyr/net/dns_resolve.h>

#define DNS_TIMEOUT (5 * MSEC_PER_SEC)

static void dns_callback(enum dns_resolve_status status,
                         struct dns_addrinfo *info,
                         void *user_data)
{
    char addr_str[NET_IPV6_ADDR_LEN];

    switch (status) {
    case DNS_EAI_INPROGRESS:
        if (info->ai_family == AF_INET) {
            net_addr_ntop(AF_INET,
                         &net_sin(&info->ai_addr)->sin_addr,
                         addr_str, sizeof(addr_str));
        } else if (info->ai_family == AF_INET6) {
            net_addr_ntop(AF_INET6,
                         &net_sin6(&info->ai_addr)->sin6_addr,
                         addr_str, sizeof(addr_str));
        }
        printk("Resolved: %s\n", addr_str);
        break;

    case DNS_EAI_ALLDONE:
        printk("DNS resolution complete\n");
        break;

    case DNS_EAI_CANCELED:
        printk("DNS query canceled\n");
        break;

    case DNS_EAI_FAIL:
        printk("DNS resolution failed\n");
        break;

    case DNS_EAI_NODATA:
        printk("No data found\n");
        break;
    }
}

int start_dns_lookup(const char *hostname)
{
    uint16_t dns_id;

    int ret = dns_get_addr_info(hostname,
                                DNS_QUERY_TYPE_A,  /* IPv4 */
                                &dns_id,
                                dns_callback,
                                NULL,              /* user_data */
                                DNS_TIMEOUT);
    if (ret < 0) {
        printk("Failed to start DNS query: %d\n", ret);
        return ret;
    }

    printk("DNS query started, ID: %u\n", dns_id);
    return 0;
}
```

#### Query Types

| Type | Record | Description |
|------|--------|-------------|
| `DNS_QUERY_TYPE_A` | A | IPv4 address |
| `DNS_QUERY_TYPE_AAAA` | AAAA | IPv6 address |

#### Canceling Queries

```c
uint16_t dns_id;
dns_get_addr_info("example.com", DNS_QUERY_TYPE_A, &dns_id, callback, NULL, 5000);

/* Cancel before timeout */
dns_cancel_addr_info(dns_id);
```

#### Using Custom DNS Context

```c
/* Get default context */
struct dns_resolve_context *ctx = dns_resolve_get_default();

/* Or create custom context with specific servers */
static struct dns_resolve_context my_ctx;

static const char *dns_servers[] = { "8.8.8.8", NULL };
dns_resolve_init(&my_ctx, dns_servers, NULL);

dns_resolve_name(&my_ctx, "example.com", DNS_QUERY_TYPE_A,
                 &dns_id, callback, NULL, DNS_TIMEOUT);
```

### mDNS Support

Multicast DNS for `.local` domains (RFC 6762).

#### Enable mDNS

```
CONFIG_MDNS_RESOLVER=y
CONFIG_MDNS_RESPONDER=y  /* To advertise services */
```

#### mDNS Queries

mDNS is automatic for `.local` hostnames:

```c
/* Uses mDNS automatically */
getaddrinfo("mydevice.local", "80", &hints, &res);

/* Or with async API */
dns_get_addr_info("mydevice.local", DNS_QUERY_TYPE_A,
                  &dns_id, callback, NULL, DNS_TIMEOUT);
```

#### mDNS Responder

Register a hostname for your device:

```c
#include <zephyr/net/mdns_responder.h>

/* Device will respond to "zephyr.local" */
/* Configured via Kconfig */
```

Kconfig:
```
CONFIG_MDNS_RESPONDER=y
CONFIG_MDNS_RESPONDER_DNS_SD=y  /* For service discovery */
CONFIG_NET_HOSTNAME="zephyr"    /* Responds to zephyr.local */
```

### LLMNR Support

Link-Local Multicast Name Resolution (RFC 4795) for local network resolution without DNS server.

#### Enable LLMNR

```
CONFIG_LLMNR_RESOLVER=y
CONFIG_LLMNR_RESPONDER=y
```

LLMNR is used automatically for unqualified hostnames that don't match mDNS patterns.

### Service Discovery (DNS-SD)

Query for services on the network (RFC 6763).

```c
#include <zephyr/net/dns_resolve.h>

static void service_callback(enum dns_resolve_status status,
                             struct dns_addrinfo *info,
                             void *user_data)
{
    if (status == DNS_EAI_INPROGRESS && info) {
        if (info->ai_family == AF_LOCAL) {
            /* Service discovery result */
            char service_name[128];
            memcpy(service_name, info->ai_canonname,
                   MIN(info->ai_addrlen, sizeof(service_name) - 1));
            printk("Found service: %s\n", service_name);
        }
    }
}

int discover_services(void)
{
    int ret = dns_resolve_service(dns_resolve_get_default(),
                                  "_http._tcp.local",
                                  NULL,
                                  service_callback,
                                  NULL,
                                  5000);
    return ret;
}
```

Kconfig for large service discovery responses:
```
CONFIG_DNS_RESOLVER_MAX_ANSWER_SIZE=1024
CONFIG_DNS_RESOLVER_MAX_NAME_LEN=128
```

### DNS Server Configuration

#### Static Configuration (Kconfig)

```
CONFIG_DNS_RESOLVER=y
CONFIG_DNS_SERVER_IP_ADDRESSES=y
CONFIG_DNS_SERVER1="8.8.8.8"
CONFIG_DNS_SERVER2="8.8.4.4"
CONFIG_DNS_SERVER3="2001:4860:4860::8888"  /* IPv6 */
```

#### Dynamic Configuration (DHCP)

DNS servers are automatically configured when using DHCP:

```
CONFIG_NET_DHCPV4=y
# DNS servers from DHCP are used automatically
```

#### Runtime Configuration

```c
#include <zephyr/net/dns_resolve.h>

static struct dns_resolve_context dns_ctx;

int configure_dns_servers(void)
{
    static const char *servers[] = {
        "192.168.1.1",   /* Primary */
        "8.8.8.8",       /* Fallback */
        NULL
    };

    return dns_resolve_init(&dns_ctx, servers, NULL);
}
```

#### DNS Resolver Kconfig Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_DNS_RESOLVER` | n | Enable DNS resolver |
| `CONFIG_DNS_RESOLVER_MAX_QUERY_LEN` | 64 | Max query name length |
| `CONFIG_DNS_RESOLVER_MAX_ANSWER_SIZE` | 512 | Max DNS response size |
| `CONFIG_DNS_RESOLVER_MAX_NAME_LEN` | 64 | Max resolved name length |
| `CONFIG_DNS_RESOLVER_ADDITIONAL_QUERIES` | 1 | Max CNAME chain depth |
| `CONFIG_DNS_NUM_CONCUR_QUERIES` | 1 | Concurrent query limit |

## Kconfig

### Core Networking

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_NETWORKING` | n | Enable networking subsystem |
| `CONFIG_NET_IPV4` | n | Enable IPv4 support |
| `CONFIG_NET_IPV6` | n | Enable IPv6 support |
| `CONFIG_NET_TCP` | n | Enable TCP protocol |
| `CONFIG_NET_UDP` | n | Enable UDP protocol |

### BSD Sockets

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_NET_SOCKETS` | n | Enable BSD sockets API |
| `CONFIG_POSIX_API` | n | Use POSIX function names (no `zsock_` prefix) |
| `CONFIG_NET_SOCKETS_POSIX_NAMES` | n | Alternative to POSIX_API for socket names only |
| `CONFIG_NET_SOCKETS_POLL_MAX` | 3 | Max sockets in poll() |
| `CONFIG_NET_SOCKETS_PRIORITY_DEFAULT` | 50 | Socket implementation priority |

### TLS/DTLS

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_NET_SOCKETS_SOCKOPT_TLS` | n | Enable TLS socket options |
| `CONFIG_NET_SOCKETS_ENABLE_DTLS` | n | Enable DTLS support |
| `CONFIG_NET_SOCKETS_TLS_MAX_CONTEXTS` | 1 | Max concurrent TLS connections |
| `CONFIG_NET_SOCKETS_TLS_PRIORITY` | 45 | TLS socket implementation priority |
| `CONFIG_NET_SOCKETS_DTLS_TIMEOUT` | 30000 | DTLS handshake timeout (ms) |

### mbedTLS

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_MBEDTLS` | n | Enable mbedTLS library |
| `CONFIG_MBEDTLS_BUILTIN` | y | Use builtin mbedTLS (vs external) |
| `CONFIG_MBEDTLS_ENABLE_HEAP` | n | Enable mbedTLS heap (required for TLS) |
| `CONFIG_MBEDTLS_HEAP_SIZE` | 0 | mbedTLS heap size (40000-60000 typical) |
| `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN` | 16384 | Max TLS record size |
| `CONFIG_MBEDTLS_PEM_CERTIFICATE_FORMAT` | n | Enable PEM cert parsing |
| `CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED` | n | Enable PSK ciphersuites |
| `CONFIG_MBEDTLS_DEBUG` | n | Enable mbedTLS debug output |
| `CONFIG_MBEDTLS_DEBUG_LEVEL` | 0 | Debug verbosity (0-4) |

### TLS Credential Storage

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_TLS_CREDENTIALS` | y | Enable TLS credentials subsystem |
| `CONFIG_TLS_MAX_CREDENTIALS_NUMBER` | 4 | Max stored credentials |
| `CONFIG_TLS_CREDENTIAL_FILENAMES` | n | Reference creds by filename |

### DNS Resolver

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_DNS_RESOLVER` | n | Enable DNS resolver |
| `CONFIG_DNS_SERVER_IP_ADDRESSES` | n | Enable static DNS server config |
| `CONFIG_DNS_SERVER1` | "" | Primary DNS server IP |
| `CONFIG_DNS_SERVER2` | "" | Secondary DNS server IP |
| `CONFIG_DNS_RESOLVER_MAX_QUERY_LEN` | 64 | Max query name length |
| `CONFIG_DNS_RESOLVER_MAX_ANSWER_SIZE` | 512 | Max DNS response size |
| `CONFIG_DNS_RESOLVER_ADDITIONAL_QUERIES` | 1 | Max CNAME chain depth |
| `CONFIG_DNS_NUM_CONCUR_QUERIES` | 1 | Concurrent queries |

### mDNS/LLMNR

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_MDNS_RESOLVER` | n | Enable mDNS queries |
| `CONFIG_MDNS_RESPONDER` | n | Enable mDNS responder |
| `CONFIG_MDNS_RESPONDER_DNS_SD` | n | Enable DNS-SD in responder |
| `CONFIG_LLMNR_RESOLVER` | n | Enable LLMNR queries |
| `CONFIG_LLMNR_RESPONDER` | n | Enable LLMNR responder |
| `CONFIG_NET_HOSTNAME` | "zephyr" | Device hostname |

### Socket Offloading

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_NET_SOCKETS_OFFLOAD` | n | Enable socket offloading |
| `CONFIG_NET_SOCKETS_OFFLOAD_DISPATCHER` | n | Socket dispatcher for multi-interface |

### Raw Sockets

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_NET_SOCKETS_PACKET` | n | Raw L2 packets (AF_PACKET SOCK_RAW) |
| `CONFIG_NET_SOCKETS_PACKET_DGRAM` | n | L2 without header (AF_PACKET SOCK_DGRAM) |
| `CONFIG_NET_SOCKETS_INET_RAW` | n | Raw IP packets (AF_INET SOCK_RAW) |
| `CONFIG_NET_SOCKETS_CAN` | n | CAN bus sockets (AF_CAN) |

### Network Buffers

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_NET_PKT_RX_COUNT` | 4 | Receive packet count |
| `CONFIG_NET_PKT_TX_COUNT` | 4 | Transmit packet count |
| `CONFIG_NET_BUF_RX_COUNT` | 16 | Receive buffer count |
| `CONFIG_NET_BUF_TX_COUNT` | 16 | Transmit buffer count |
| `CONFIG_NET_BUF_DATA_SIZE` | 128 | Buffer fragment size |

### File Descriptors

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_ZVFS_OPEN_ADD_SIZE_NET_SAMPLE` | 0 | Additional FDs for networking |
| `CONFIG_NET_MAX_CONTEXTS` | 6 | Max network contexts (connections) |

### Example Configurations

#### Minimal TCP Client
```
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_TCP=y
CONFIG_NET_IPV4=y
CONFIG_NET_PKT_RX_COUNT=8
CONFIG_NET_PKT_TX_COUNT=8
CONFIG_NET_BUF_RX_COUNT=16
CONFIG_NET_BUF_TX_COUNT=16
```

#### TLS Client
```
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_TCP=y
CONFIG_NET_IPV4=y

CONFIG_NET_SOCKETS_SOCKOPT_TLS=y
CONFIG_MBEDTLS=y
CONFIG_MBEDTLS_BUILTIN=y
CONFIG_MBEDTLS_ENABLE_HEAP=y
CONFIG_MBEDTLS_HEAP_SIZE=60000
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=4096

CONFIG_MAIN_STACK_SIZE=4096
CONFIG_NET_PKT_RX_COUNT=16
CONFIG_NET_PKT_TX_COUNT=16
CONFIG_NET_BUF_RX_COUNT=32
CONFIG_NET_BUF_TX_COUNT=32
```

#### TLS Server
```
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_TCP=y
CONFIG_NET_IPV4=y
CONFIG_NET_IPV6=y

CONFIG_NET_SOCKETS_SOCKOPT_TLS=y
CONFIG_NET_SOCKETS_TLS_MAX_CONTEXTS=4
CONFIG_MBEDTLS=y
CONFIG_MBEDTLS_BUILTIN=y
CONFIG_MBEDTLS_ENABLE_HEAP=y
CONFIG_MBEDTLS_HEAP_SIZE=80000

CONFIG_MAIN_STACK_SIZE=4096
CONFIG_NET_MAX_CONTEXTS=10
```

#### DTLS Client
```
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_UDP=y
CONFIG_NET_IPV4=y

CONFIG_NET_SOCKETS_SOCKOPT_TLS=y
CONFIG_NET_SOCKETS_ENABLE_DTLS=y
CONFIG_NET_SOCKETS_DTLS_TIMEOUT=30000
CONFIG_MBEDTLS=y
CONFIG_MBEDTLS_BUILTIN=y
CONFIG_MBEDTLS_ENABLE_HEAP=y
CONFIG_MBEDTLS_HEAP_SIZE=60000
```

#### DNS with mDNS
```
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_IPV4=y

CONFIG_DNS_RESOLVER=y
CONFIG_DNS_SERVER_IP_ADDRESSES=y
CONFIG_DNS_SERVER1="8.8.8.8"

CONFIG_MDNS_RESOLVER=y
CONFIG_MDNS_RESPONDER=y
CONFIG_NET_HOSTNAME="mydevice"
```

#### Full Network Stack
```
CONFIG_NETWORKING=y
CONFIG_NET_SOCKETS=y
CONFIG_POSIX_API=y
CONFIG_NET_TCP=y
CONFIG_NET_UDP=y
CONFIG_NET_IPV4=y
CONFIG_NET_IPV6=y

CONFIG_NET_SOCKETS_SOCKOPT_TLS=y
CONFIG_NET_SOCKETS_ENABLE_DTLS=y
CONFIG_MBEDTLS=y
CONFIG_MBEDTLS_BUILTIN=y
CONFIG_MBEDTLS_ENABLE_HEAP=y
CONFIG_MBEDTLS_HEAP_SIZE=80000

CONFIG_DNS_RESOLVER=y
CONFIG_MDNS_RESOLVER=y
CONFIG_LLMNR_RESOLVER=y

CONFIG_NET_PKT_RX_COUNT=32
CONFIG_NET_PKT_TX_COUNT=32
CONFIG_NET_BUF_RX_COUNT=64
CONFIG_NET_BUF_TX_COUNT=64
CONFIG_NET_MAX_CONTEXTS=16
CONFIG_MAIN_STACK_SIZE=4096
```

## Locations

Quick reference for finding socket, TLS, and DNS related files in the Zephyr tree.

### Headers (include/zephyr/net/)

| File | Purpose |
|------|---------|
| `socket.h` | Main BSD sockets API |
| `socket_types.h` | Socket type definitions |
| `socket_poll.h` | poll() implementation |
| `socket_select.h` | select() implementation |
| `socket_offload.h` | Offloaded socket API |
| `socket_service.h` | Socket service API |
| `socket_net_mgmt.h` | Network management sockets |
| `socketutils.h` | Socket utility functions |
| `socketcan.h` | CAN bus sockets |
| `socketcan_utils.h` | CAN socket utilities |
| `tls_credentials.h` | TLS credential management |
| `dns_resolve.h` | DNS resolver API |
| `dns_sd.h` | DNS Service Discovery |

### Implementation (subsys/net/lib/)

#### Sockets (subsys/net/lib/sockets/)

| File | Purpose |
|------|---------|
| `sockets.c` | Core socket implementation |
| `sockets_inet.c` | IPv4/IPv6 socket support |
| `sockets_tls.c` | TLS/DTLS socket support |
| `sockets_packet.c` | Raw packet sockets (AF_PACKET) |
| `sockets_can.c` | CAN bus sockets |
| `sockets_misc.c` | Miscellaneous socket functions |
| `sockets_net_mgmt.c` | Network management sockets |
| `sockets_service.c` | Socket service implementation |
| `socket_dispatcher.c` | Socket dispatcher |
| `socket_offload.c` | Socket offloading support |
| `socket_obj_core.c` | Socket object core |
| `socketpair.c` | socketpair() implementation |
| `getaddrinfo.c` | getaddrinfo() implementation |
| `getnameinfo.c` | getnameinfo() implementation |

#### DNS (subsys/net/lib/dns/)

| File | Purpose |
|------|---------|
| `resolve.c` | DNS resolver core |
| `dns_pack.c` | DNS packet packing/unpacking |
| `dns_cache.c` | DNS cache implementation |
| `dns_sd.c` | DNS Service Discovery |
| `mdns_responder.c` | mDNS responder |
| `llmnr_responder.c` | LLMNR responder |
| `dispatcher.c` | DNS dispatcher |

### Kconfig Files

| Path | Purpose |
|------|---------|
| `subsys/net/lib/sockets/Kconfig` | Socket configuration options |
| `subsys/net/lib/dns/Kconfig` | DNS configuration options |
| `modules/mbedtls/Kconfig` | mbedTLS configuration |
| `modules/mbedtls/Kconfig.tls-generic` | Generic TLS options |

### Samples (samples/net/)

#### Socket Samples (samples/net/sockets/)

| Sample | Description |
|--------|-------------|
| `echo_server/` | TCP/UDP echo server |
| `echo_client/` | TCP/UDP echo client |
| `echo/` | Simple echo example |
| `echo_async/` | Async echo with poll() |
| `echo_async_select/` | Async echo with select() |
| `echo_service/` | Socket service example |
| `tcp/` | Basic TCP example |
| `http_get/` | HTTPS GET with TLS |
| `http_client/` | HTTP client |
| `http_server/` | HTTP server |
| `dumb_http_server/` | Simple HTTP server |
| `dumb_http_server_mt/` | Multi-threaded HTTP server |
| `big_http_download/` | Large file download |
| `websocket_client/` | WebSocket client |
| `coap_client/` | CoAP client |
| `coap_server/` | CoAP server |
| `coap_download/` | CoAP download |
| `coap_upload/` | CoAP upload |
| `sntp_client/` | SNTP client |
| `can/` | CAN socket example |
| `packet/` | Raw packet sockets |
| `net_mgmt/` | Network management sockets |
| `socketpair/` | socketpair() example |
| `txtime/` | TX time scheduling |

#### DNS Sample

| Sample | Description |
|--------|-------------|
| `dns_resolve/` | DNS resolution example |

### Documentation (doc/connectivity/networking/)

| Path | Purpose |
|------|---------|
| `api/sockets.rst` | BSD sockets documentation |
| `api/dns_resolve.rst` | DNS resolver documentation |

### Tests (tests/net/)

| Path | Purpose |
|------|---------|
| `socket/` | Socket unit tests |
| `socket/tls/` | TLS socket tests |
| `socket/udp/` | UDP socket tests |
| `socket/tcp/` | TCP socket tests |
| `dns/` | DNS tests |

### Key Paths Summary

```
# Headers
include/zephyr/net/socket.h           # Main sockets API
include/zephyr/net/tls_credentials.h  # TLS credentials
include/zephyr/net/dns_resolve.h      # DNS resolver

# Implementation
subsys/net/lib/sockets/               # Socket implementation
subsys/net/lib/dns/                   # DNS implementation

# Samples
samples/net/sockets/echo_server/      # TCP/UDP server example
samples/net/sockets/http_get/         # TLS client example
samples/net/dns_resolve/              # DNS example

# Configuration
subsys/net/lib/sockets/Kconfig        # Socket Kconfig
subsys/net/lib/dns/Kconfig            # DNS Kconfig
```

## Sockets

### Table of Contents
- [Socket Functions](#socket-functions)
- [UDP Datagram Sockets](#udp-datagram-sockets)
- [Receiving Data](#receiving-data)
- [Socket Options](#socket-options)
- [Non-Blocking I/O](#non-blocking-io)
- [poll() and select()](#poll-and-select)
- [Short Read/Write Behavior](#short-readwrite-behavior)
- [IPv6 Considerations](#ipv6-considerations)

### Socket Functions

#### socket()

```c
int socket(int family, int type, int protocol);
```

| Parameter | Values |
|-----------|--------|
| family | `AF_INET` (IPv4), `AF_INET6` (IPv6), `AF_PACKET` (raw L2), `AF_CAN` |
| type | `SOCK_STREAM` (TCP), `SOCK_DGRAM` (UDP), `SOCK_RAW` |
| protocol | `IPPROTO_TCP`, `IPPROTO_UDP`, `IPPROTO_TLS_1_2`, `IPPROTO_DTLS_1_2` |

Returns: socket file descriptor on success, -1 on error (check `errno`)

#### connect()

```c
int connect(int sock, const struct sockaddr *addr, socklen_t addrlen);
```

For TCP: Initiates 3-way handshake (blocking by default)
For UDP: Sets default peer address for `send()`

#### bind()

```c
int bind(int sock, const struct sockaddr *addr, socklen_t addrlen);
```

Associates socket with local address. For servers, use `INADDR_ANY` (0.0.0.0) or `in6addr_any`.

#### listen()

```c
int listen(int sock, int backlog);
```

Marks socket as passive (server). `backlog` specifies max pending connections.

#### accept()

```c
int accept(int sock, struct sockaddr *addr, socklen_t *addrlen);
```

Blocks until a client connects. Returns new socket for the connection.

#### send() / sendto()

```c
ssize_t send(int sock, const void *buf, size_t len, int flags);
ssize_t sendto(int sock, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
```

| Flag | Description |
|------|-------------|
| MSG_DONTWAIT | Non-blocking for this call only |
| MSG_MORE | More data coming (cork) |

#### recv() / recvfrom()

```c
ssize_t recv(int sock, void *buf, size_t len, int flags);
ssize_t recvfrom(int sock, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);
```

| Flag | Description |
|------|-------------|
| MSG_PEEK | Read without removing from queue |
| MSG_DONTWAIT | Non-blocking for this call only |
| MSG_WAITALL | Block until full amount received |
| MSG_TRUNC | Return real datagram length even if truncated |

#### close()

```c
int close(int sock);
```

Terminates connection and releases socket resources.

#### shutdown()

```c
int shutdown(int sock, int how);
```

| how | Effect |
|-----|--------|
| SHUT_RD (0) | No more receives |
| SHUT_WR (1) | No more sends (sends FIN) |
| SHUT_RDWR (2) | Both |

### UDP Datagram Sockets

```c
#include <zephyr/net/socket.h>

int udp_client(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(5000),
    };
    inet_pton(AF_INET, "192.168.1.100", &dest.sin_addr);

    /* Option 1: sendto() each time */
    sendto(sock, "Hello", 5, 0, (struct sockaddr *)&dest, sizeof(dest));

    /* Option 2: connect() then send() */
    connect(sock, (struct sockaddr *)&dest, sizeof(dest));
    send(sock, "Hello", 5, 0);

    close(sock);
    return 0;
}

int udp_server(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(5000),
        .sin_addr.s_addr = INADDR_ANY,
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    char buf[256];
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);

    int len = recvfrom(sock, buf, sizeof(buf), 0,
                       (struct sockaddr *)&client, &client_len);

    /* Echo back */
    sendto(sock, buf, len, 0, (struct sockaddr *)&client, client_len);

    close(sock);
    return 0;
}
```

### Receiving Data

#### Complete receive helper (TCP)

TCP is a stream protocol; data may arrive in fragments:

```c
int recv_all(int sock, void *buf, size_t len)
{
    size_t received = 0;
    char *ptr = buf;

    while (received < len) {
        int ret = recv(sock, ptr + received, len - received, 0);
        if (ret < 0) {
            return -errno;
        }
        if (ret == 0) {
            return received;  /* Connection closed */
        }
        received += ret;
    }
    return received;
}
```

#### Complete send helper (TCP)

```c
int send_all(int sock, const void *buf, size_t len)
{
    size_t sent = 0;
    const char *ptr = buf;

    while (sent < len) {
        int ret = send(sock, ptr + sent, len - sent, 0);
        if (ret < 0) {
            return -errno;
        }
        sent += ret;
    }
    return sent;
}
```

### Socket Options

#### setsockopt() / getsockopt()

```c
int setsockopt(int sock, int level, int optname, const void *optval, socklen_t optlen);
int getsockopt(int sock, int level, int optname, void *optval, socklen_t *optlen);
```

#### Common SOL_SOCKET Options

```c
/* Reuse address (for server restart) */
int optval = 1;
setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

/* Receive timeout (5 seconds) */
struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

/* Send timeout */
setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

/* Bind to specific network interface */
struct ifreq ifr = { .ifr_name = "eth0" };
setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
```

#### TCP Options (IPPROTO_TCP)

```c
/* Disable Nagle algorithm (send immediately) */
int optval = 1;
setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
```

#### IPv6 Options (IPPROTO_IPV6)

```c
/* IPv6 only (don't accept IPv4-mapped addresses) */
int optval = 1;
setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &optval, sizeof(optval));
```

### Non-Blocking I/O

#### fcntl method (persistent)

```c
#include <fcntl.h>

int flags = fcntl(sock, F_GETFL, 0);
fcntl(sock, F_SETFL, flags | O_NONBLOCK);

/* Now recv/send return -1 with errno=EAGAIN if would block */
int ret = recv(sock, buf, len, 0);
if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    /* No data available, try again later */
}
```

#### MSG_DONTWAIT (per-call)

```c
int ret = recv(sock, buf, len, MSG_DONTWAIT);
```

### poll() and select()

#### poll() - Preferred

```c
#include <zephyr/net/socket.h>

struct zsock_pollfd fds[3];

/* TCP server socket */
fds[0].fd = server_sock;
fds[0].events = ZSOCK_POLLIN;

/* Connected client 1 */
fds[1].fd = client1_sock;
fds[1].events = ZSOCK_POLLIN;

/* Connected client 2 */
fds[2].fd = client2_sock;
fds[2].events = ZSOCK_POLLIN | ZSOCK_POLLOUT;

int ret = zsock_poll(fds, 3, 5000);  /* 5 sec timeout, -1 = infinite */
if (ret < 0) {
    /* Error */
} else if (ret == 0) {
    /* Timeout */
} else {
    if (fds[0].revents & ZSOCK_POLLIN) {
        /* New connection waiting */
        int new_client = accept(server_sock, ...);
    }
    if (fds[1].revents & ZSOCK_POLLIN) {
        /* Client 1 has data */
        recv(client1_sock, ...);
    }
    if (fds[1].revents & ZSOCK_POLLHUP) {
        /* Client 1 disconnected */
        close(client1_sock);
    }
}
```

#### Poll Events

| Event | Direction | Description |
|-------|-----------|-------------|
| ZSOCK_POLLIN | Input | Data available to read |
| ZSOCK_POLLOUT | Input | Socket ready to write |
| ZSOCK_POLLERR | Output | Error occurred |
| ZSOCK_POLLHUP | Output | Peer closed connection |
| ZSOCK_POLLNVAL | Output | Invalid file descriptor |

#### select() - POSIX compatible

```c
#include <sys/select.h>

fd_set readfds;
FD_ZERO(&readfds);
FD_SET(sock1, &readfds);
FD_SET(sock2, &readfds);

struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
int max_fd = (sock1 > sock2) ? sock1 : sock2;

int ret = select(max_fd + 1, &readfds, NULL, NULL, &tv);
if (ret > 0) {
    if (FD_ISSET(sock1, &readfds)) {
        /* sock1 has data */
    }
}
```

### Short Read/Write Behavior

Zephyr's socket implementation aggressively uses POSIX short-read/short-write semantics:

- `recv()` may return fewer bytes than requested
- `send()` may send fewer bytes than requested
- Always check return values and loop if needed

```c
/* WRONG: Assumes all data received at once */
recv(sock, buf, 1024, 0);

/* CORRECT: Handle partial receives */
int total = 0;
while (total < expected_len) {
    int ret = recv(sock, buf + total, expected_len - total, 0);
    if (ret <= 0) break;
    total += ret;
}
```

### IPv6 Considerations

#### Dual-Stack Socket (IPv4 + IPv6)

```c
/* Create IPv6 socket that also accepts IPv4 */
int sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

/* Disable IPv6-only mode (allow IPv4-mapped addresses) */
int optval = 0;
setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &optval, sizeof(optval));

struct sockaddr_in6 addr = {
    .sin6_family = AF_INET6,
    .sin6_port = htons(8080),
    .sin6_addr = in6addr_any,  /* Accepts both IPv4 and IPv6 */
};
bind(sock, (struct sockaddr *)&addr, sizeof(addr));
```

#### IPv6-Only Socket

```c
int optval = 1;
setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &optval, sizeof(optval));
```

#### Address Structures

```c
/* IPv4 */
struct sockaddr_in addr4 = {
    .sin_family = AF_INET,
    .sin_port = htons(8080),
};
inet_pton(AF_INET, "192.168.1.1", &addr4.sin_addr);

/* IPv6 */
struct sockaddr_in6 addr6 = {
    .sin6_family = AF_INET6,
    .sin6_port = htons(8080),
};
inet_pton(AF_INET6, "2001:db8::1", &addr6.sin6_addr);
```

## Tls

### Table of Contents
- [Overview](#overview)
- [TLS Credentials](#tls-credentials)
- [TLS Client](#tls-client)
- [TLS Server](#tls-server)
- [DTLS (UDP)](#dtls-udp)
- [TLS Socket Options](#tls-socket-options)
- [Certificate Formats](#certificate-formats)
- [PSK Authentication](#psk-authentication)
- [Session Caching](#session-caching)
- [Troubleshooting](#troubleshooting)

### Overview

Zephyr secure sockets use mbedTLS to provide TLS 1.2 (TCP) and DTLS 1.2 (UDP) support. The API extends standard BSD sockets with:

1. Secure protocol types (`IPPROTO_TLS_1_2`, `IPPROTO_DTLS_1_2`)
2. TLS-specific socket options (`SOL_TLS`)
3. Credential management via `sec_tag_t` tags

### TLS Credentials

#### Credential Types

| Type | Description | Paired With |
|------|-------------|-------------|
| `TLS_CREDENTIAL_CA_CERTIFICATE` | Trusted CA cert (verify server) | - |
| `TLS_CREDENTIAL_PUBLIC_CERTIFICATE` | Client/server cert | `TLS_CREDENTIAL_PRIVATE_KEY` |
| `TLS_CREDENTIAL_PRIVATE_KEY` | Private key | `TLS_CREDENTIAL_PUBLIC_CERTIFICATE` |
| `TLS_CREDENTIAL_PSK` | Pre-shared key | `TLS_CREDENTIAL_PSK_ID` |
| `TLS_CREDENTIAL_PSK_ID` | PSK identity | `TLS_CREDENTIAL_PSK` |

#### Registering Credentials

```c
#include <zephyr/net/tls_credentials.h>

#define CA_TAG 1
#define CLIENT_TAG 2
#define PSK_TAG 3

/* CA certificate for server verification */
static const unsigned char ca_cert[] = { /* DER data */ };

/* Client certificate and key for mutual TLS */
static const unsigned char client_cert[] = { /* DER data */ };
static const unsigned char client_key[] = { /* DER data */ };

int setup_credentials(void)
{
    int ret;

    /* Register CA certificate */
    ret = tls_credential_add(CA_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
                             ca_cert, sizeof(ca_cert));
    if (ret < 0) {
        return ret;
    }

    /* Register client cert and key (same tag for paired credentials) */
    ret = tls_credential_add(CLIENT_TAG, TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
                             client_cert, sizeof(client_cert));
    if (ret < 0) {
        return ret;
    }

    ret = tls_credential_add(CLIENT_TAG, TLS_CREDENTIAL_PRIVATE_KEY,
                             client_key, sizeof(client_key));
    if (ret < 0) {
        return ret;
    }

    return 0;
}
```

#### Credential Management API

```c
/* Add credential */
int tls_credential_add(sec_tag_t tag, enum tls_credential_type type,
                       const void *cred, size_t credlen);

/* Get credential (for inspection) */
int tls_credential_get(sec_tag_t tag, enum tls_credential_type type,
                       void *cred, size_t *credlen);

/* Delete credential */
int tls_credential_delete(sec_tag_t tag, enum tls_credential_type type);
```

### TLS Client

#### Basic TLS Client

```c
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#define CA_TAG 1

int tls_client(const char *host, uint16_t port)
{
    int sock, ret;

    /* 1. Create TLS socket */
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
    if (sock < 0) {
        return -errno;
    }

    /* 2. Set credentials */
    sec_tag_t sec_tags[] = { CA_TAG };
    ret = setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
                     sec_tags, sizeof(sec_tags));
    if (ret < 0) {
        close(sock);
        return -errno;
    }

    /* 3. Set hostname for SNI and certificate verification */
    ret = setsockopt(sock, SOL_TLS, TLS_HOSTNAME, host, strlen(host) + 1);
    if (ret < 0) {
        close(sock);
        return -errno;
    }

    /* 4. Connect (TLS handshake happens automatically) */
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    inet_pton(AF_INET, "1.2.3.4", &addr.sin_addr);  /* Use resolved IP */

    ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        close(sock);
        return -errno;
    }

    /* 5. Use socket normally */
    send(sock, "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", 38, 0);

    char buf[512];
    int len = recv(sock, buf, sizeof(buf) - 1, 0);
    if (len > 0) {
        buf[len] = '\0';
        printk("%s\n", buf);
    }

    close(sock);
    return 0;
}
```

#### Mutual TLS (Client Certificate)

```c
#define CA_TAG 1
#define CLIENT_TAG 2  /* Has both cert and key */

sec_tag_t sec_tags[] = { CA_TAG, CLIENT_TAG };
setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tags, sizeof(sec_tags));
```

### TLS Server

```c
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#define SERVER_TAG 1  /* Has server cert + private key */

int tls_server(uint16_t port)
{
    int sock, client, ret;

    /* Create TLS server socket */
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
    if (sock < 0) {
        return -errno;
    }

    /* Set server credentials */
    sec_tag_t sec_tags[] = { SERVER_TAG };
    setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tags, sizeof(sec_tags));

    /* Optional: Require client certificate */
    int verify = TLS_PEER_VERIFY_REQUIRED;
    setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));

    /* Bind and listen */
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    listen(sock, 5);

    /* Accept connections (TLS handshake on accept) */
    while (1) {
        client = accept(sock, NULL, NULL);
        if (client < 0) {
            continue;
        }

        char buf[256];
        int len = recv(client, buf, sizeof(buf), 0);
        if (len > 0) {
            send(client, buf, len, 0);
        }

        close(client);
    }

    close(sock);
    return 0;
}
```

### DTLS (UDP)

DTLS provides security for UDP datagrams.

#### DTLS Client

```c
int dtls_client(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_DTLS_1_2);

    sec_tag_t tags[] = { CA_TAG };
    setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, tags, sizeof(tags));

    /* For DTLS client role is implicit from connect() */
    struct sockaddr_in addr = { /* ... */ };
    connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    /* Handshake happens on first send or explicitly */
    send(sock, "Hello", 5, 0);

    char buf[256];
    recv(sock, buf, sizeof(buf), 0);

    close(sock);
    return 0;
}
```

#### DTLS Server

```c
int dtls_server(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_DTLS_1_2);

    sec_tag_t tags[] = { SERVER_TAG };
    setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, tags, sizeof(tags));

    /* Set DTLS role to server (required for DTLS) */
    int role = TLS_DTLS_ROLE_SERVER;
    setsockopt(sock, SOL_TLS, TLS_DTLS_ROLE, &role, sizeof(role));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(5684),
        .sin_addr.s_addr = INADDR_ANY,
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    char buf[256];
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);

    /* recvfrom triggers handshake */
    int len = recvfrom(sock, buf, sizeof(buf), 0,
                       (struct sockaddr *)&client, &client_len);

    sendto(sock, buf, len, 0, (struct sockaddr *)&client, client_len);

    close(sock);
    return 0;
}
```

### TLS Socket Options

All options use level `SOL_TLS` (282).

#### TLS_SEC_TAG_LIST

Array of `sec_tag_t` referencing credentials:

```c
sec_tag_t tags[] = { CA_TAG, CLIENT_TAG };
setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, tags, sizeof(tags));
```

#### TLS_HOSTNAME

Server hostname for SNI and certificate CN/SAN verification:

```c
char hostname[] = "api.example.com";
setsockopt(sock, SOL_TLS, TLS_HOSTNAME, hostname, sizeof(hostname));

/* Disable hostname verification */
setsockopt(sock, SOL_TLS, TLS_HOSTNAME, NULL, 0);
```

#### TLS_PEER_VERIFY

Peer certificate verification level:

```c
int verify = TLS_PEER_VERIFY_REQUIRED;  /* 2 = required (default for clients) */
/* TLS_PEER_VERIFY_NONE = 0, TLS_PEER_VERIFY_OPTIONAL = 1 */
setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
```

#### TLS_CIPHERSUITE_LIST

Restrict allowed ciphersuites (IANA IDs):

```c
int suites[] = {
    0x009F,  /* TLS_DHE_RSA_WITH_AES_256_GCM_SHA384 */
    0x00A3,  /* TLS_DHE_DSS_WITH_AES_256_GCM_SHA384 */
};
setsockopt(sock, SOL_TLS, TLS_CIPHERSUITE_LIST, suites, sizeof(suites));
```

#### TLS_CIPHERSUITE_USED (read-only)

Get negotiated ciphersuite after handshake:

```c
int suite;
socklen_t len = sizeof(suite);
getsockopt(sock, SOL_TLS, TLS_CIPHERSUITE_USED, &suite, &len);
```

#### TLS_ALPN_LIST

Application Layer Protocol Negotiation:

```c
const char *alpn[] = { "h2", "http/1.1", NULL };
setsockopt(sock, SOL_TLS, TLS_ALPN_LIST, alpn, sizeof(alpn));
```

#### TLS_DTLS_ROLE

For DTLS, explicitly set client/server role:

```c
int role = TLS_DTLS_ROLE_SERVER;  /* 1 = server, 0 = client (default) */
setsockopt(sock, SOL_TLS, TLS_DTLS_ROLE, &role, sizeof(role));
```

#### TLS_DTLS_HANDSHAKE_TIMEOUT_MIN/MAX

DTLS handshake retransmission timeouts (milliseconds):

```c
int min_timeout = 1000;  /* 1 second */
int max_timeout = 60000; /* 60 seconds */
setsockopt(sock, SOL_TLS, TLS_DTLS_HANDSHAKE_TIMEOUT_MIN, &min_timeout, sizeof(min_timeout));
setsockopt(sock, SOL_TLS, TLS_DTLS_HANDSHAKE_TIMEOUT_MAX, &max_timeout, sizeof(max_timeout));
```

#### TLS_NATIVE

Force native TLS implementation when offloading is available:

```c
int native = 1;
setsockopt(sock, SOL_TLS, TLS_NATIVE, &native, sizeof(native));
```

### Certificate Formats

#### DER (Binary) - Default

Certificates must be DER-encoded by default:

```c
/* In ca_certificate.h */
static const unsigned char ca_cert[] = {
    0x30, 0x82, 0x03, 0x77, /* ... DER bytes ... */
};
```

#### PEM Support

Enable PEM with `CONFIG_MBEDTLS_PEM_CERTIFICATE_FORMAT=y`:

```c
static const char ca_cert_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDxTCCAq2gAwIBAgIQAqxcJmoLQJuPC3nyrkYldzANBgkqhkiG9w0BAQsFADBs\n"
    /* ... */
    "-----END CERTIFICATE-----\n";
```

#### Converting Certificates

```bash
# PEM to DER
openssl x509 -in cert.pem -outform DER -out cert.der

# DER to C array
xxd -i cert.der > cert.h
```

### PSK Authentication

Pre-shared key authentication (no certificates):

```c
#define PSK_TAG 1

static const unsigned char psk[] = { 0x01, 0x02, 0x03, 0x04 };
static const char psk_id[] = "Client_identity";

int setup_psk(void)
{
    tls_credential_add(PSK_TAG, TLS_CREDENTIAL_PSK, psk, sizeof(psk));
    tls_credential_add(PSK_TAG, TLS_CREDENTIAL_PSK_ID, psk_id, strlen(psk_id));

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);

    sec_tag_t tags[] = { PSK_TAG };
    setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, tags, sizeof(tags));

    /* Connect as usual */
    return 0;
}
```

Enable PSK in Kconfig:
```
CONFIG_MBEDTLS_KEY_EXCHANGE_PSK_ENABLED=y
```

### Session Caching

TLS session caching reduces handshake overhead for reconnections:

```c
/* Enable session caching */
int cache = TLS_SESSION_CACHE_ENABLED;
setsockopt(sock, SOL_TLS, TLS_SESSION_CACHE, &cache, sizeof(cache));

/* Purge session cache */
setsockopt(sock, SOL_TLS, TLS_SESSION_CACHE_PURGE, NULL, 0);
```

Kconfig:
```
CONFIG_NET_SOCKETS_TLS_MAX_CONTEXTS=4
CONFIG_MBEDTLS_SSL_SESSION_TICKETS=y
```

### Troubleshooting

#### Handshake Failures

1. **Certificate errors**: Verify CA certificate matches server's issuer
2. **Hostname mismatch**: Check `TLS_HOSTNAME` matches cert CN/SAN
3. **Time sync**: mbedTLS validates cert dates; ensure RTC is set
4. **Memory**: Increase `CONFIG_MBEDTLS_HEAP_SIZE` (typically 60000+)

#### Debug Logging

```
CONFIG_MBEDTLS_DEBUG=y
CONFIG_MBEDTLS_DEBUG_LEVEL=4
CONFIG_NET_SOCKETS_LOG_LEVEL_DBG=y
```

#### Common Errors

| Error | Cause | Fix |
|-------|-------|-----|
| ENOENT | Credential not found | Verify `tls_credential_add()` called before socket |
| ENOMEM | mbedTLS heap exhausted | Increase `CONFIG_MBEDTLS_HEAP_SIZE` |
| ECONNRESET | Handshake failed | Check certs, hostname, server compatibility |
| EAGAIN (non-blocking TLS) | Same data must be resent | Retry `send()` with identical buffer |

#### mbedTLS Heap Sizing

Minimum heap sizes:
- Basic TLS client: ~40KB
- TLS client with session caching: ~50KB
- TLS server: ~60KB
- Multiple concurrent connections: Add ~20KB per connection
