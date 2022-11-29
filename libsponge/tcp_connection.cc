#include "tcp_connection.hh"

#include <algorithm>
#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const {
    return _current_time_tick - _time_tick_of_last_segment_received;
}

void TCPConnection::send(TCPSegment &seg) {
    auto ackno = _receiver.ackno();
    auto window_size = _receiver.window_size();
    auto out_seg = seg;
    if (ackno.has_value()) {
        // we need send a packet with ack
        out_seg.header().ackno = ackno.value();
        out_seg.header().win = std::min(window_size, static_cast<size_t>(std::numeric_limits<uint16_t>::max()));
        out_seg.header().ack = true;
    }

    if (_rst) {
        out_seg.header().rst = true;
    }

    _segments_out.push(out_seg);
}

size_t TCPConnection::try_send() {
    _sender.fill_window();
    return send_all();
}

size_t TCPConnection::send_all() {
    size_t n_sent = 0;
    while (!_sender.segments_out().empty()) {
        auto out_seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        n_sent++;
        send(out_seg);
    }
    return n_sent;
}

void TCPConnection::handle_rst() {
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _linger_after_streams_finish = false;
    _rst = true;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_tick_of_last_segment_received = _current_time_tick;

    if (seg.header().rst) {
        handle_rst();
        return;
    }

    if (seg.header().fin) {
        if (!_sender.stream_in().eof()) {
            // peer send fin first, we can do a passive close
            _linger_after_streams_finish = false;
        }
    }

    _receiver.segment_received(seg);

    if (_receiver.ackno().has_value() && seg.header().ack) {
        // ACK
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _sender.fill_window();
    }
    auto n_sent = send_all();

    if (n_sent == 0 && seg.length_in_sequence_space() > 0) {
        // if the incoming segment occupied any sequence numbers, the TCPConnection makes sure that at least one segment
        // is sent in reply, to reflect an update in the ackno and window size
        n_sent = try_send();
        if (n_sent == 0) {
            _sender.send_empty_segment();
            send_all();
        }
    }

    if (_receiver.ackno().has_value() && (seg.length_in_sequence_space() == 0) &&
        seg.header().seqno == _receiver.ackno().value() - 1) {
        _sender.send_empty_segment();
        send_all();
    }

    set_linger_start_time();
}

void TCPConnection::set_linger_start_time() {
    if (check_prereq()) {
        _linger_start_time = {_current_time_tick};
    } else {
        _linger_start_time.reset();
    }
}

bool TCPConnection::check_prereq() const {
    if (_receiver.stream_out().input_ended() && _receiver.unassembled_bytes() == 0) {
        // Prereq #1: The inbound stream has been fully assembled and has ended.
        if (_sender.stream_in().eof() && _sender.next_seqno_absolute() == _sender.stream_in().bytes_read() + 2) {
            // Prereq #2: The out bound stream has been ended by the local application and fully sent (including the
            // fact that it ended, i.e. a segment with fin) to the remote peer.
            if (_sender.bytes_in_flight() == 0) {
                // Prereq #3: The out bound stream has been fully acknowledged by the remote peer.
                return true;
            }
        }
    }
    return false;
}

bool TCPConnection::active() const {
    if (_rst) {
        return false;
    }

    if (check_prereq()) {
        if (!_linger_after_streams_finish) {
            return false;
        } else {
            if (_linger_start_time.has_value() &&
                _current_time_tick - _linger_start_time.value() >= 10 * _cfg.rt_timeout) {
                return false;
            }
        }
    }
    return true;
}

size_t TCPConnection::write(const string &data) {
    auto nbytes = _sender.stream_in().write(data);
    try_send();
    return nbytes;
}

void TCPConnection::goto_rst() {
    handle_rst();

    if (!send_all()) {
        _sender.send_empty_segment();
        send_all();
    }
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _current_time_tick += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        goto_rst();
    }
    send_all();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    try_send();

    set_linger_start_time();
}

void TCPConnection::connect() {
    if (_sender.next_seqno_absolute() == 0) {
        try_send();
    } else {
        cerr << "Warning: sender already running!\n";
        return;
    }
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            goto_rst();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
