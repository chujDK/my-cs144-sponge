#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _memory(), _input_end(false), _n_written(0), _n_read(0), _capacity(capacity) {}

size_t ByteStream::write(const string &data) {
    size_t n_bytes = min((_capacity - _memory.length()), data.length());
    _n_written += n_bytes;

    _memory.append(data.substr(0, n_bytes));

    return n_bytes;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const { return _memory.substr(0, min(len, _memory.length())); }

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t nbytes = min(len, _memory.length());
    _n_read += nbytes;
    _memory.erase(0, nbytes);
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    size_t nbytes = min(len, _memory.length());
    _n_read += nbytes;
    auto ret = _memory.substr(0, nbytes);
    _memory.erase(0, nbytes);
    return ret;
}

void ByteStream::end_input() { _input_end = true; }

bool ByteStream::input_ended() const { return _input_end; }

size_t ByteStream::buffer_size() const { return _memory.length(); }

bool ByteStream::buffer_empty() const { return _memory.empty(); }

bool ByteStream::eof() const { return _input_end && _memory.empty(); }

size_t ByteStream::bytes_written() const { return _n_written; }

size_t ByteStream::bytes_read() const { return _n_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _memory.length(); }
