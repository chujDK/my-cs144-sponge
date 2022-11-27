#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <list>
#include <queue>

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    //! the last ack get from receiver
    WrappingInt32 _ackno{_isn};
    //! the last window size get from receiver
    uint16_t _window_size{0};

    //! the last sent byte's absolute seqno
    uint64_t _checkpoint{0};

    //! current consecutive retransmissions number
    unsigned int _consecutive_retransmissions{0};

    //! \brief Helper class tracking segments sent but not yet acknowledged (outstanding data)
    class TCPFlightTracker {
      private:
        //! all segments tracked, sorted by time tick, from old to new
        std::list<TCPSegment> _segments{};

        //! the initial sequence number
        WrappingInt32 _isn;

        //! init retransmission timeout
        unsigned int _init_rto;

        //! effective retransmission timeout
        unsigned int _rto;

        //! timer, None as not running
        std::optional<size_t> _time{std::nullopt};

        //! for the seqno unwrapping
        uint64_t _checkpoint{0};

      public:
        TCPFlightTracker(WrappingInt32 isn, size_t rto) : _isn(isn), _init_rto(rto), _rto(_init_rto) {}

        //! \brief given current ack number and checkpoint, untrack all fully acknowledged segment
        void ackno_received(const WrappingInt32 &ackno, uint64_t checkpoint);

        std::optional<TCPSegment> tick(const size_t ms_since_last_tick);

        void track(const TCPSegment &segment);

        //! \brief make an exponential "backoff".
        //! it slows down retransmissions on lousy networks to avoid further gumming up the works.
        void double_rto() { _rto *= 2; }

        uint64_t bytes_in_flight() const;
        uint64_t max_seqno() const;
    };

    //! the tracker
    TCPFlightTracker _tracker{_isn, _initial_retransmission_timeout};

    //! \name raw send helper
    //! \note they don't send any empty (zero seqno) payload
    //!@{

    //! \param[in] fin is supporting piggyback fin (fin contains data)
    void send(std::string &&data, const WrappingInt32 &seqno, bool fin = false);
    void send(const TCPSegment &segment);

    //!@}

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const { return _consecutive_retransmissions; }

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
