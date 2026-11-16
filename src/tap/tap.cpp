#include "packetforge/tap.hpp"
#include <stdexcept>
#if PF_TAP
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
///A TAP interface is a virtual Ethernet interface created and managed by the Linux kernel.It lets a userspace application exchange Ethernet frames with the Linux networking stack.In PacketForge, TAP provides a way to receive live Ethernet traffic without implementing a real NIC driver.An important distinction:- TAP handles Layer 2 Ethernet frames.
namespace pf {
Tap::Tap(const std::string& name) {
#if PF_TAP
    if (name.empty() || name.size() >= IFNAMSIZ)
        throw std::invalid_argument("invalid TAP name");
    fd_ = ::open("/dev/net/tun", O_RDWR | O_NONBLOCK | O_CLOEXEC);//Open the Linux virtual network device
    //O_CLOEXEC    Close the descriptor if the process executes another program
    if (fd_ < 0)
        throw std::runtime_error(std::string("open TAP: ") + std::strerror(errno));
    
    struct ifreq request{};
    request.ifr_flags = IFF_TAP | IFF_NO_PI;
    //ifreq is a Linux structure used to configure network interfaces.- IFF_TAP specifies an Ethernet-level TAP device.- IFF_NO_PI disables an additional kernel packet-information header, so reads return Ethernet frames directly.
    std::memcpy(request.ifr_name, name.c_str(), name.size() + 1);//This copies the requested interface name, including its terminating null byte, into the structure.
    if (ioctl(fd_, TUNSETIFF, &request) < 0) {
        auto error = std::string(std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        //ioctl() is one of the most important Linux system calls to recognize for an interview.It sends a device-specific control request to the kernel.Here, TUNSETIFF asks Linux to create or attach the requested virtual interface with the specified flags.The operation may require administrator privileges or CAP_NET_ADMIN.Notice that this is not a physical NIC and not hardware DMA. We are using a kernel-provided virtual networking interface.
        throw std::runtime_error("TUNSETIFF (root/CAP_NET_ADMIN may be required): " + error);
    }
#else
    (void)name;
    throw std::runtime_error("TAP unavailable: Linux with PACKETFORGE_TAP=ON is required");
#endif
}
Tap::~Tap() {
#if PF_TAP
    if (fd_ >= 0)
        ::close(fd_);
#endif
}
bool Tap::read(std::vector<uint8_t>& packet, int timeout_ms) {
#if PF_TAP
    struct pollfd p{fd_, POLLIN, 0};// check if there is new bytes periodacly
    int ready = ::poll(&p, 1, timeout_ms);
    if (ready < 0) {
        if (errno == EINTR)
            return false;
        throw std::runtime_error("TAP poll failed");
    }
    if (!ready)
        return false;
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
        throw std::runtime_error("TAP disconnected");
    packet.resize(65550);
    auto n = ::read(fd_, packet.data(), packet.size());
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR)
            return false;
        throw std::runtime_error("TAP read failed");
    }
    packet.resize(static_cast<size_t>(n));
    return n > 0;
#else
    (void)packet;
    (void)timeout_ms;
    return false;
#endif
}
} // namespace pf
