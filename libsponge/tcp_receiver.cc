#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const auto &header = seg.header();
    if (!_isn.has_value()) {
        // want a syn
        if (header.syn) {
            _isn = {header.seqno};
            _checkpoint = 0;

            bool eof = header.fin;
            const auto &data = seg.payload();
            _reassembler.push_substring(data.copy(), 0, eof);
        }
    } else {
        bool eof = header.fin;
        const auto &data = seg.payload();
        auto seqno = header.seqno;
        auto absolute_no = unwrap(seqno, _isn.value(), _checkpoint);
        _checkpoint = absolute_no;
        auto stream_index = absolute_no - 1;
        _reassembler.push_substring(data.copy(), stream_index, eof);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (_isn.has_value()) {
        auto stream_index = _reassembler.stream_out().bytes_written();
        auto absolute = stream_index + 1;
        if (_reassembler.stream_out().input_ended()) {
            // we expect FIN now
            absolute++;
        }
        auto ackno = wrap(absolute, _isn.value());
        return ackno;
    } else {
        return {};
    }
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
