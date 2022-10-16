#include "AsyncPing.h"

#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/ip4.h"
#include "lwip/err.h"
#include "lwip/icmp.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#define PING_ID 0xAFAF

static void ping_prepare_echo(struct icmp_echo_hdr *iecho, uint16_t len, uint16_t seqNo) {
    size_t i;
    size_t data_len = len - sizeof(struct icmp_echo_hdr);

    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->chksum = 0;
    iecho->id = PING_ID;
    iecho->seqno = htons(seqNo);

    /* fill the additional data buffer with some data */
    for (i = 0; i < data_len; i++) {
        ((char*)iecho)[sizeof(struct icmp_echo_hdr) + i] = (char)i;
    }

    iecho->chksum = inet_chksum(iecho, len);
}

static err_t ping_send(int socket, ip4_addr_t *addr, uint16_t size, uint16_t seqNo) {
    struct sockaddr_in to;
    size_t ping_size = sizeof(struct icmp_echo_hdr) + size;
    
    // clamp size to 256b to reduce complexity (heap alloc)
    if (ping_size > 256)
        ping_size = 256;
    uint8_t buf[256];
    struct icmp_echo_hdr *iecho = (icmp_echo_hdr*)buf;

    ping_prepare_echo(iecho, (uint16_t)ping_size, seqNo);
    to.sin_len = sizeof(to);
    to.sin_family = AF_INET;
    inet_addr_from_ip4addr(&to.sin_addr, addr);
    if (sendto(socket, iecho, ping_size, 0, (struct sockaddr*)&to, sizeof(to)) == 0) {
        return ERR_VAL;
    }
    return ERR_OK;
}

static asyncping_result_t ping_recv(int socket, int16_t seqNo, uint8_t* buf, size_t bufLen, socklen_t& cursor) {
    struct sockaddr_in from;
    struct ip_hdr *iphdr;
    struct icmp_echo_hdr *iecho = NULL;

    // receive bytes
    int len = recvfrom(socket, buf, bufLen, 0, (struct sockaddr*)&from, &cursor);
    if (len >= (int)(sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr))) {
        /// Get from IP address
        ip4_addr_t fromaddr;
        inet_addr_to_ip4addr(&fromaddr, &from.sin_addr);

        // Get echo
        iphdr = (struct ip_hdr *)buf;
        iecho = (struct icmp_echo_hdr *)(buf + (IPH_HL(iphdr) * 4));
        return (iecho->id == PING_ID) && (iecho->seqno == htons(seqNo))
            ? aprSuccess
            : aprInvalidResponse;
    }
    return aprPending;
}

void AsyncPing::close() {
    closesocket(_socket);
    _socket = -1;
}

void AsyncPing::begin(uint32_t dstIp, int timeoutMs, uint8_t pingLen) {
    struct sockaddr_in address;
    ip4_addr_t ping_target;

    timeval time;
    gettimeofday(&time, NULL);
    _startetAt = time.tv_sec * 1000000 + time.tv_usec;
    _timeout = timeoutMs;
    _pingLen = pingLen;
    
    if ((_socket = socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP)) < 0) {
        _lastResult = aprSocketCreationFailed;
        return;
    }

    address.sin_addr.s_addr = dstIp;
    ping_target.addr = address.sin_addr.s_addr;

    // Setup socket
    struct timeval tout;

    // Timeout
    tout.tv_sec = 0;
    tout.tv_usec = 1000; // 1ms seems to be the min value for timeout to have best-possible non blocking behaviour

    if (setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout)) < 0) {
        close();
        _lastResult = aprSocketCreationFailed;
        return;
    }
    
    _cursor = 0;
    if (ping_send(_socket, &ping_target, pingLen, _seqNo) == ERR_OK) {
        _lastResult = aprPending;
    } else {
        _lastResult = aprSocketSendFailed;
    }
}

asyncping_result_t AsyncPing::result() {
    if (_lastResult != aprPending)
        return _lastResult;
    
    // check timeout
    timeval time;
    gettimeofday(&time, NULL);
    uint64_t currentTime = time.tv_sec * 1000000 + time.tv_usec;
    if (currentTime - _startetAt > _timeout * 1000)
    {
        // delete socket
        close();
        _finishedAt = _startetAt + _timeout * 1000;
        return (_lastResult = aprTimeout);
    }
    
    // check for data
    socklen_t cursor = _cursor; // avoid including sockets.h in header file
    
    _lastResult = ping_recv(_socket, _seqNo, _buf, sizeof(_buf), cursor);
    //_lastResult = ping_recv_orig(_socket);
    _cursor = cursor;
    if (_lastResult != aprPending) {
        close();
        _finishedAt = currentTime;
        _seqNo++;
    }
    return _lastResult;
}