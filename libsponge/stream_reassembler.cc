#include "stream_reassembler.hh"

#include <algorithm>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _unassembled(), _output(capacity), _capacity(capacity) {}

void StreamReassembler::insert_segment(const uint64_t index, const std::string &data) {
    uint64_t start = index;
    uint64_t end = index + data.length();

    // XXX  CCC  PPP  directly insert
    //
    // XXX    PPP     merge to the prev
    //   CCCC
    //
    // XXX   PPPP     merge to the next
    //      CC
    //
    // XXX  PPPP      merge them all to the prev
    //   CCCC

    auto iter = _unassembled.begin();
    if (iter == _unassembled.end()) {
        _unassembled[{start, end}] = data;
        return;
    }
    for (; iter != _unassembled.end(); iter++) {
        if (iter->first.second >= start && iter->first.first <= start) {
            break;
        }
        if (iter->first.second >= end && iter->first.first <= end) {
            break;
        }
    }

    auto capacity_left = _capacity - _output.buffer_size();
    auto max_append_length = capacity_left - (start - _output.bytes_written());
    end = min(start + max_append_length, end);

    std::pair<size_t, size_t> new_index;

    if (iter->first.second >= start && iter->first.first <= start) {
        // get the next end
        auto prev = iter;
        auto next = ++iter;
        auto prev_index = prev->first;
        auto prev_start = prev_index.first;
        auto prev_end = prev_index.second;
        auto prev_data = prev->second;

        auto append_start = prev_end - start;
        if (next != _unassembled.end() && next->first.first <= end && next->first.second >= end) {
            auto next_index = next->first;
            auto next_start = next_index.first;
            auto next_end = next_index.second;
            auto next_data = next->second;

            // merge the three
            auto n_append = next_start - prev_end;

            _unassembled.erase(prev->first);
            _unassembled.erase(next->first);

            new_index.first = prev_start;
            new_index.second = next_end;
            _unassembled[new_index] = prev_data.append(data.substr(append_start, n_append)).append(next_data);
        } else {
            // merge to the prev
            if (end <= prev_end) {
                // no need to append, just overwrite
                prev->second.replace(start - prev_start, data.length(), data);
                return;
            }

            _unassembled.erase(prev->first);

            auto n_append = end - prev_end;

            new_index.first = prev_start;
            new_index.second = prev_end + n_append;
            _unassembled[new_index] = prev_data.append(data.substr(append_start, n_append));
        }
    } else if (iter->first.second >= end && iter->first.first <= end) {
        // only need to merge to the next
        auto next_index = iter->first;
        auto next_start = next_index.first;
        auto next_end = next_index.second;
        auto next_data = iter->second;
        _unassembled.erase(iter->first);

        auto n_append = next_start - start;
        new_index.first = start;
        new_index.second = next_end;
        _unassembled[new_index] = data.substr(0, n_append).append(next_data);
    } else {
        new_index.first = start;
        new_index.second = end;
        _unassembled[new_index] = data.substr(0, end - start);
    }

    for (auto i = _unassembled.cbegin(); i != _unassembled.cend();) {
        if (i->first.first >= new_index.first && i->first.second <= new_index.second &&
            (!(i->first.first == new_index.first && i->first.second == new_index.second))) {
            _unassembled.erase(i++);
        } else {
            ++i;
        }
    }
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    size_t end_of_assembled = _output.bytes_written();

    if (eof) {
        // shouldn't we just update the _eof_index once?
        _eof_index = index + data.length();
        _eof_recv = true;
    }

    if (index + data.length() > end_of_assembled) {
        insert_segment(index, data);

        for (auto iter = _unassembled.cbegin(); iter != _unassembled.cend();) {
            if (iter->first.first <= end_of_assembled) {
                if (iter->first.second > end_of_assembled) {
                    _output.write(iter->second.substr(end_of_assembled - iter->first.first,
                                                      iter->first.second - end_of_assembled));
                }
                _unassembled.erase(iter++);
            } else {
                break;
            }
        }
    }

    if (_eof_recv && _output.bytes_written() == _eof_index) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t total = 0;
    for (const auto &iter : _unassembled) {
        total += iter.first.second - iter.first.first;
    }
    return total;
}

bool StreamReassembler::empty() const { return _unassembled.empty(); }
