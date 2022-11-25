#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    return WrappingInt32{static_cast<uint32_t>(static_cast<uint64_t>(isn.raw_value()) + n)};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // change checkpoint to seqno
    auto checkpoint_seq = wrap(checkpoint, isn);
    // get offset
    auto offset = n - checkpoint_seq;
    if (offset > 0) {
        return checkpoint + offset;
    } else {
        uint64_t candidate1 = checkpoint + (1ul << 32) + offset;
        uint64_t candidate2 = checkpoint + offset;
        uint64_t off1 = candidate1 > checkpoint ? candidate1 - checkpoint : checkpoint - candidate1;
        uint64_t off2 = candidate2 > checkpoint ? candidate2 - checkpoint : checkpoint - candidate2;
        return off1 < off2 ? candidate1 : candidate2;
    }
}
