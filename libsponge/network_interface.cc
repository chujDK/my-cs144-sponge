#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

void NetworkInterface::send_arp(const uint16_t type,
                                const EthernetAddress &target_ethernet_address,
                                const uint32_t target_ip_address) {
    EthernetFrame arp_frame;
    if (type == ARPMessage::OPCODE_REQUEST) {
        arp_frame.header().dst = ETHERNET_BROADCAST;
    } else {
        arp_frame.header().dst = target_ethernet_address;
    }
    arp_frame.header().src = _ethernet_address;
    arp_frame.header().type = EthernetHeader::TYPE_ARP;

    ARPMessage arp_msg;
    arp_msg.opcode = type;
    arp_msg.target_ethernet_address = target_ethernet_address;
    arp_msg.target_ip_address = target_ip_address;

    arp_msg.sender_ethernet_address = _ethernet_address;
    arp_msg.sender_ip_address = _ip_address.ipv4_numeric();

    arp_frame.payload().append(arp_msg.serialize());
    _frames_out.push(arp_frame);
}

void NetworkInterface::do_send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    EthernetAddress dst_ethernet_address{_arp_table[next_hop_ip].first};
    EthernetFrame frame;
    frame.header().dst = dst_ethernet_address;
    frame.header().src = _ethernet_address;
    frame.header().type = EthernetHeader::TYPE_IPv4;
    frame.payload().append(dgram.serialize());
    _frames_out.push(frame);
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    if (_arp_table.find(next_hop_ip) != _arp_table.end()) {
        do_send_datagram(dgram, next_hop);
    } else {
        _ip_datagram.emplace_back(dgram, next_hop);
        // send a ARP datagram
        send_arp(ARPMessage::OPCODE_REQUEST, {}, next_hop_ip);
    }
}

void NetworkInterface::try_send_all() {
    for (auto iter = _ip_datagram.cbegin(); iter != _ip_datagram.cend();) {
        if (_arp_table.find(iter->second.ipv4_numeric()) != _arp_table.end()) {
            do_send_datagram(iter->first, iter->second);
            _ip_datagram.erase(iter++);
        } else {
            ++iter;
        }
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    InternetDatagram ipv4_dgram;
    ARPMessage arp_msg_received;
    EthernetFrame arp_reply_frame;
    if (frame.header().dst != ETHERNET_BROADCAST && frame.header().dst != _ethernet_address) {
        // drop
        return {};
    }

    switch (frame.header().type) {
        case EthernetHeader::TYPE_IPv4:
            if (ipv4_dgram.parse(frame.payload()) == ParseResult::NoError) {
                return ipv4_dgram;
            }
            return {};
        case EthernetHeader::TYPE_ARP:
            if (arp_msg_received.parse(frame.payload()) == ParseResult::NoError) {
                if (arp_msg_received.opcode == ARPMessage::OPCODE_REPLY) {
                    _arp_table[arp_msg_received.sender_ip_address] = {arp_msg_received.sender_ethernet_address,
                                                                      _current_time};
                    try_send_all();
                } else if (arp_msg_received.opcode == ARPMessage::OPCODE_REQUEST) {
                    if (arp_msg_received.target_ip_address == _ip_address.ipv4_numeric()) {
                        // send back a ARP reply
                        send_arp(ARPMessage::OPCODE_REPLY,
                                 arp_msg_received.sender_ethernet_address,
                                 arp_msg_received.sender_ip_address);
                    }
                    // also keep track of the sender's MAC
                    _arp_table[arp_msg_received.sender_ip_address] = {arp_msg_received.sender_ethernet_address,
                                                                      _current_time};
                }
            }
            return {};
        default:
            return {};
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { _current_time += ms_since_last_tick; }
