#ifndef ASYNCPING_H
#define ASYNCPING_H

#include <stdint.h>

enum asyncping_result_t {
    aprPending = 0,
    aprSuccess = 1,
    aprTimeout = -1,
    aprInvalidResponse = -2,
    aprSocketCreationFailed = -3,
    aprSocketSendFailed = -4,
    aprNotStarted = -5
};

class AsyncPing {
    private:
        int _timeout;
        uint64_t _startetAt;
        uint64_t _finishedAt;
        asyncping_result_t _lastResult = aprNotStarted;

        int _socket = 0;
        uint16_t _seqNo = 0;
        uint8_t _pingLen;
        uint32_t _cursor;
        uint8_t _buf[64];

        void close();
    public:
        void begin(uint32_t dstIp, int timeoutMs = 1000, uint8_t pingLen = 32);
        asyncping_result_t result();

        int lastPingTimeMicros() { return _finishedAt - _startetAt; }

};

#endif /* ASYNCPING_H */
