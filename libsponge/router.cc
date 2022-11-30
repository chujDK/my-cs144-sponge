#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    _route_table.push_back({{route_prefix, prefix_length}, {next_hop, interface_num}});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    cerr << "DEBUG: recv: " << dgram.header().summary() + " payload=\"" + dgram.payload().concatenate() + "\"\n";
    if (dgram.header().ttl < 2) {
        return;
    }

    const auto dst_ip = dgram.header().dst;
    std::optional<uint8_t> max_match{};
    auto chosen_route = _route_table.front();
    for (auto route : _route_table) {
        const auto &[sub_net, next_hop] = route;
        const auto &[sub_net_ip, sub_net_len] = sub_net;
        const auto sub_net_shift = 32 - sub_net_len;

        if ((static_cast<uint64_t>((dst_ip ^ sub_net_ip)) >> sub_net_shift) == 0) {
            if (max_match.value_or(33) > sub_net_shift) {
                max_match = sub_net_shift;
                chosen_route = route;
            }
        }
    }

    if (max_match.has_value()) {
        const auto &[sub_net, next_hop] = chosen_route;
        const auto &[next_hop_ip_opt, next_hop_if] = next_hop;
        auto &next_hop_interface = interface(next_hop_if);

        dgram.header().ttl--;
        next_hop_interface.send_datagram(dgram, next_hop_ip_opt.value_or(Address::from_ipv4_numeric(dst_ip)));
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
