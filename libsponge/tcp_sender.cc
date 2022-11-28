#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

void TCPSender::TCPFlightTracker::ackno_received(const WrappingInt32 &ackno,
                                                 const uint16_t window_size,
                                                 const WrappingInt32 &last_ackno,
                                                 const uint64_t checkpoint) {
    // When the receiver gives the sender an ackno that acknowledges the successful receipt of new data  (the ackno
    // reflects an absolute sequence number bigger than any previous ackno)
    if (unwrap(ackno, _isn, checkpoint) > unwrap(last_ackno, _isn, checkpoint)) {
        // a. Set the RTO back to its “initial value.”
        if (window_size != 0) {
            // When filling window, treat a '0' window size as equal to '1' but don't back off RTO
            _rto = _init_rto;
        }
        // b. If the sender has any outstanding data,
        // restart the retransmission timer so that it will expire after RTO milliseconds (for the current value of
        // RTO).
        if (!_segments.empty()) {
            _time = {0};
        }
    }

    _checkpoint = checkpoint;
    for (auto iter = _segments.begin(); iter != _segments.end();) {
        auto absolute_seqno = unwrap(iter->header().seqno, _isn, checkpoint);
        auto absolute_end = absolute_seqno + iter->length_in_sequence_space();
        auto absolute_ackno = unwrap(ackno, _isn, checkpoint);
        if (iter->header().syn) {
            // this is an outgoing SYN package, we need to check it carefully: Impossible ackno (beyond next seqno) is
            // ignored
            if (absolute_ackno == absolute_seqno + 1) {
                _segments.erase(iter++);
            } else {
                ++iter;
            }
        } else if (absolute_ackno >= absolute_end) {
            _segments.erase(iter++);
        } else {
            ++iter;
        }
    }

    if (_segments.empty()) {
        // all outstanding data has been acknowledged
        _time.reset();
    }
}

std::optional<TCPSegment> TCPSender::TCPFlightTracker::tick(const size_t ms_since_last_tick) {
    if (!_time.has_value()) {
        return std::nullopt;
    }

    _time = _time.value() + ms_since_last_tick;
    if (_time.value() >= _rto) {
        // if _segments is empty, then the _time must stopped, so here it must have some segments
        auto earliest = _segments.begin();
        auto earliest_seqno = unwrap(earliest->header().seqno, _isn, _checkpoint);
        for (auto segment = _segments.begin(); segment != _segments.end(); ++segment) {
            auto absolute_seqno = unwrap(segment->header().seqno, _isn, _checkpoint);
            if (absolute_seqno < earliest_seqno) {
                earliest = segment;
                earliest_seqno = absolute_seqno;
            }
        }
        _time = {0};
        auto segment = *earliest;
        _segments.erase(earliest);
        return {segment};
    }
    return std::nullopt;
}

uint64_t TCPSender::TCPFlightTracker::max_seqno() const {
    if (_segments.empty()) {
        return _isn.raw_value();
    }
    auto newest = _segments.front();
    auto newest_seqno = unwrap(newest.header().seqno, _isn, _checkpoint);
    for (auto segment : _segments) {
        auto absolute_seqno = unwrap(segment.header().seqno, _isn, _checkpoint);
        if (absolute_seqno > newest_seqno) {
            newest = segment;
            newest_seqno = absolute_seqno;
        }
    }
    return newest_seqno;
}

void TCPSender::TCPFlightTracker::track(const TCPSegment &segment) {
    if (!_time.has_value()) {
        _time = 0;
    }
    _segments.push_back(segment);
}

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::TCPFlightTracker::bytes_in_flight() const {
    uint64_t nbytes = 0;
    for (const auto &segment : _segments) {
        nbytes += segment.length_in_sequence_space();
    }
    return nbytes;
}

uint64_t TCPSender::bytes_in_flight() const { return _tracker.bytes_in_flight(); }

void TCPSender::fill_window() {
    if (_next_seqno == 0) {
        // should send a SYN
        TCPSegment segment;
        segment.header().seqno = _isn;
        segment.header().syn = true;
        segment.payload() = Buffer{""};
        send(segment);
        return;
    }

    auto window_right = unwrap(_ackno, _isn, _checkpoint) + _window_size;
    if (_window_size == 0) {
        // If the receiver has announced a window size of zero, the fill_window method should act like the window size
        // is one.
        window_right++;
    }

    if (_stream.eof()) {
        // should send a FIN
        if (window_right <= _next_seqno) {
            // no more space
            return;
        }
        if (_next_seqno == _stream.bytes_read() + 2) {
            // FIN already sent
            return;
        }
        TCPSegment segment;
        segment.header().seqno = wrap(_stream.bytes_read() + 1, _isn);
        segment.header().fin = true;
        segment.payload() = Buffer{""};
        send(segment);
        return;
    }

    if (window_right > _next_seqno) {
        auto real_window_size = window_right - _next_seqno;
        for (uint64_t payload_start = 0; payload_start < real_window_size;
             payload_start += TCPConfig::MAX_PAYLOAD_SIZE) {
            auto payload_length = min(TCPConfig::MAX_PAYLOAD_SIZE, real_window_size - payload_start);

            auto data = _stream.read(payload_length);
            if (_stream.eof() && payload_start + data.length() + 1 <= real_window_size) {
                send(std::move(data), wrap(_next_seqno, _isn), true);
                break;
            }
            if (data.length() < payload_length) {
                auto fin = _stream.input_ended();
                send(std::move(data), wrap(_next_seqno, _isn), fin);
                break;
            } else {
                send(std::move(data), wrap(_next_seqno, _isn));
            }
        }
    }
}

void TCPSender::send(std::string &&data, const WrappingInt32 &seqno, bool fin) {
    Buffer segment_payload{std::move(data)};
    TCPSegment segment;
    segment.header().seqno = seqno;
    segment.header().fin = fin;
    segment.payload() = segment_payload;
    if (segment.length_in_sequence_space() == 0) {
        // this send method doesn't send empty payload
        return;
    }
    send(segment);
}

void TCPSender::send(const TCPSegment &segment, bool resend) {
    _checkpoint = _next_seqno;
    if (!resend) {
        _next_seqno += segment.length_in_sequence_space();
    }
    _segments_out.push(segment);
    if (segment.length_in_sequence_space()) {
        // only tracking segments convey some data
        _tracker.track(segment);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // delegate to the tracker
    _tracker.ackno_received(ackno, window_size, _ackno, _checkpoint);

    // c. Reset the count of “consecutive retransmissions” back to zero.
    _consecutive_retransmissions = 0;

    _ackno = ackno;
    _window_size = window_size;

    // check the max seqno in the outstanding segments, determine if there has space to send
    if (_tracker.max_seqno() < unwrap(_ackno, _isn, _checkpoint) + _window_size) {
        // FIXME: should we call fill_window here??
        // fill_window();
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // delegate to the tracker
    auto segment = _tracker.tick(ms_since_last_tick);
    if (segment.has_value()) {
        // a. retransmit
        send(segment.value(), true);
        // b. if the window size is not zero
        if (_window_size != 0) {
            // i. Keep track of the number of consecutive retransmissions, and increment it
            _consecutive_retransmissions++;
            // ii. Double the value of RTO.
            _tracker.double_rto();
        }
    }
}

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = wrap(_next_seqno, _isn);
    segment.payload() = {""};
    send(segment);
}
