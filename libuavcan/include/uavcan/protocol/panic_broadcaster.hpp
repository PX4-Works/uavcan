/*
 * Copyright (C) 2014 Pavel Kirienko <pavel.kirienko@gmail.com>
 */

#ifndef UAVCAN_PROTOCOL_PANIC_BROADCASTER_HPP_INCLUDED
#define UAVCAN_PROTOCOL_PANIC_BROADCASTER_HPP_INCLUDED

#include <uavcan/node/publisher.hpp>
#include <uavcan/node/timer.hpp>
#include <uavcan/protocol/Panic.hpp>

namespace uavcan
{
/**
 * Helper for broadcasting the message uavcan.protocol.Panic.
 */
class UAVCAN_EXPORT PanicBroadcaster : private TimerBase
{
    Publisher<protocol::Panic> pub_;
    protocol::Panic msg_;

    void publishOnce();

    virtual void handleTimerEvent(const TimerEvent&);

public:
    explicit PanicBroadcaster(INode& node)
        : TimerBase(node)
        , pub_(node)
    {
        pub_.setTxTimeout(MonotonicDuration::fromMSec(protocol::Panic::BROADCASTING_INTERVAL_MS - 10));
    }

    /**
     * Begin broadcasting at the standard interval (see BROADCASTING_INTERVAL_MS).
     * This method does not block and can't fail.
     * @param short_reason Short ASCII string that describes the reason of the panic, 7 characters max.
     *                     If the string exceeds 7 characters, it will be truncated.
     */
    void panic(const char* short_reason_description);

    /**
     * Stop broadcasting immediately.
     */
    void dontPanic();    // Where's my towel

    bool isPanicking() const;

    const typename protocol::Panic::FieldTypes::reason_text& getReason() const;
};

}

#endif // UAVCAN_PROTOCOL_PANIC_BROADCASTER_HPP_INCLUDED
