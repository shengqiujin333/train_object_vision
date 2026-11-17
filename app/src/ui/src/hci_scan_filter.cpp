#include "hci_scan_filter.h"

#ifdef __linux__

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>

// 板端未安装 libbluetooth 开发头（只有运行库），这里只用 Linux 内核公开的
// AF_BLUETOOTH / BTPROTO_HCI / HCI_CHANNEL_RAW 常量，不新增链接依赖。
#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_HCI
#define BTPROTO_HCI 1
#endif
#ifndef HCI_CHANNEL_RAW
#define HCI_CHANNEL_RAW 0
#endif
#ifndef SOL_HCI
#define SOL_HCI 0
#endif
#ifndef HCI_FILTER
#define HCI_FILTER 2
#endif
#ifndef HCI_EVENT_PKT
#define HCI_EVENT_PKT 0x04
#endif
#ifndef EVT_LE_META_EVENT
#define EVT_LE_META_EVENT 0x3e
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

namespace {

struct SockAddrHci {
    unsigned short family;
    unsigned short device;
    unsigned short channel;
};

// struct hci_filter 的内核布局：uint32 type_mask; uint32 event_mask[2];
// uint16 opcode; 尾部按 4 字节对齐补 2 字节，共 16 字节。
struct HciSockFilter {
    unsigned int type_mask;
    unsigned int event_mask[2];
    unsigned short opcode;
    unsigned short pad;
};

int errnoToResult(int e) {
    return (e == EPERM || e == EACCES) ? -1 : -2;
}

int openRawSocket(unsigned short deviceId, bool nonBlocking) {
    const int flags = SOCK_RAW | (nonBlocking ? (SOCK_NONBLOCK | SOCK_CLOEXEC) : 0);
    const int fd = static_cast<int>(::socket(AF_BLUETOOTH, flags, BTPROTO_HCI));
    if (fd < 0) return -1000 + errnoToResult(errno);

    SockAddrHci addr{};
    addr.family = static_cast<unsigned short>(AF_BLUETOOTH);
    addr.device = deviceId;
    addr.channel = static_cast<unsigned short>(HCI_CHANNEL_RAW);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int rc = errnoToResult(errno);
        ::close(fd);
        return rc;
    }
    return fd;
}

int writeHciCommand(int fd, const unsigned char* cmd, int len) {
    const ssize_t written = ::write(fd, cmd, static_cast<std::size_t>(len));
    if (written != static_cast<ssize_t>(len)) return errnoToResult(errno);
    return 0;
}

}  // namespace

int railhmi::openBleAdvSocket(unsigned short deviceId) {
    const int fd = openRawSocket(deviceId, true);
    if (fd < 0) return fd;

    HciSockFilter f{};
    f.type_mask = 1u << HCI_EVENT_PKT;
    f.event_mask[0] = 0u;
    f.event_mask[1] = 1u << (EVT_LE_META_EVENT - 32);  // 0x3e -> bit 30 of the second word
    f.opcode = 0;
    f.pad = 0;
    if (::setsockopt(fd, SOL_HCI, HCI_FILTER, &f, sizeof(f)) < 0) {
        const int rc = errnoToResult(errno);
        ::close(fd);
        return rc;
    }
    return fd;
}

int railhmi::configureBleScan(int fd) {
    // HCI_COMMAND_PKT | LE Set Scan Parameters (OGF 0x08 / OCF 0x000b -> 0x200b)
    // plen=7 | passive | interval=0x0060(60ms) | window=0x0060(60ms)
    // own_address_type=public | filter_policy=accept all
    const unsigned char cmd[] = {0x01, 0x0b, 0x20, 0x07,
                                 0x00, 0x60, 0x00, 0x60, 0x00, 0x00, 0x00};
    return writeHciCommand(fd, cmd, static_cast<int>(sizeof(cmd)));
}

int railhmi::enableBleScanNoDup(int fd) {
    // HCI_COMMAND_PKT | LE Set Scan Enable (OGF 0x08 / OCF 0x000c -> 0x200c)
    // plen=2 | LE_Scan_Enable=1 | Filter_Duplicates=0
    const unsigned char cmd[6] = {0x01, 0x0c, 0x20, 0x02, 0x01, 0x00};
    return writeHciCommand(fd, cmd, static_cast<int>(sizeof(cmd)));
}

int railhmi::disableBleScan(int fd) {
    const unsigned char cmd[6] = {0x01, 0x0c, 0x20, 0x02, 0x00, 0x00};
    return writeHciCommand(fd, cmd, static_cast<int>(sizeof(cmd)));
}

int railhmi::readBleAdvPacket(int fd, unsigned char* buffer, int capacity) {
    if (fd < 0 || capacity <= 0) return -2;
    const ssize_t n = ::read(fd, buffer, static_cast<std::size_t>(capacity));
    if (n > 0) return static_cast<int>(n);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    return -2;
}

void railhmi::closeBleAdvSocket(int fd) {
    if (fd >= 0) ::close(fd);
}

int railhmi::disableBleDuplicateFilter(unsigned short deviceId) {
    const int fd = openRawSocket(deviceId, false);
    if (fd < 0) return fd;
    const int rc = enableBleScanNoDup(fd);
    ::close(fd);
    return rc;
}

#else

int railhmi::disableBleDuplicateFilter(unsigned short) { return -2; }
int railhmi::openBleAdvSocket(unsigned short) { return -2; }
int railhmi::configureBleScan(int) { return -2; }
int railhmi::enableBleScanNoDup(int) { return -2; }
int railhmi::disableBleScan(int) { return -2; }
int railhmi::readBleAdvPacket(int, unsigned char*, int) { return -2; }
void railhmi::closeBleAdvSocket(int) {}

#endif
