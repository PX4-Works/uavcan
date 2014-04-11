/*
 * Copyright (C) 2014 Pavel Kirienko <pavel.kirienko@gmail.com>
 */

#include <cstdlib>
#include <cassert>
#include <uavcan/protocol/global_time_sync_master.hpp>
#include <uavcan/debug.hpp>

namespace uavcan
{
/*
 * GlobalTimeSyncMaster::IfaceMaster
 */
int GlobalTimeSyncMaster::IfaceMaster::init()
{
    const int res = pub_.init();
    if (res >= 0)
    {
        TransferSender* const ts = pub_.getTransferSender();
        assert(ts != NULL);
        ts->setIfaceMask(1 << iface_index_);
        ts->setCanIOFlags(CanIOFlagLoopback);
    }
    return res;
}

void GlobalTimeSyncMaster::IfaceMaster::setTxTimestamp(UtcTime ts)
{
    if (ts.isZero())
    {
        assert(0);
        pub_.getNode().registerInternalFailure("GlobalTimeSyncMaster got zero UTC TX timestamp");
        return;
    }
    if (!prev_tx_utc_.isZero())
    {
        prev_tx_utc_ = UtcTime(); // Reset again, because there's something broken in the driver and we don't trust it
        pub_.getNode().registerInternalFailure("GlobalTimeSyncMaster publication conflict");
        return;
    }
    prev_tx_utc_ = ts;
}

int GlobalTimeSyncMaster::IfaceMaster::publish(TransferID tid, MonotonicTime current_time)
{
    assert(pub_.getTransferSender()->getCanIOFlags() == CanIOFlagLoopback);
    assert(pub_.getTransferSender()->getIfaceMask() == (1 << iface_index_));

    const MonotonicDuration since_prev_pub = current_time - prev_pub_mono_;
    prev_pub_mono_ = current_time;
    assert(since_prev_pub.isPositive());
    const bool long_period = since_prev_pub.toMSec() >= protocol::GlobalTimeSync::MAX_PUBLICATION_PERIOD_MS;

    protocol::GlobalTimeSync msg;
    msg.prev_utc_usec = long_period ? 0 : prev_tx_utc_.toUSec();
    prev_tx_utc_ = UtcTime();

    UAVCAN_TRACE("GlobalTimeSyncMaster", "Publishing %llu iface=%i tid=%i",
                 static_cast<unsigned long long>(msg.prev_utc_usec), int(iface_index_), int(tid.get()));
    return pub_.broadcast(msg, tid);
}

/*
 * GlobalTimeSyncMaster
 */
void GlobalTimeSyncMaster::handleLoopbackFrame(const RxFrame& frame)
{
    const uint8_t iface = frame.getIfaceIndex();
    if (initialized_ && iface < MaxCanIfaces)
    {
        if (frame.getDataTypeID() == dtid_ &&
            frame.getTransferType() == TransferTypeMessageBroadcast &&
            frame.isLast() && frame.isFirst() &&
            frame.getSrcNodeID() == node_.getNodeID())
        {
            iface_masters_[iface]->setTxTimestamp(frame.getUtcTimestamp());
        }
    }
    else
    {
        assert(0);
    }
}

int GlobalTimeSyncMaster::getNextTransferID(TransferID& tid)
{
    const MonotonicDuration max_transfer_interval =
        MonotonicDuration::fromMSec(protocol::GlobalTimeSync::PUBLISHER_TIMEOUT_MS);

    const OutgoingTransferRegistryKey otr_key(dtid_, TransferTypeMessageBroadcast, NodeID::Broadcast);
    const MonotonicTime otr_deadline = node_.getMonotonicTime() + max_transfer_interval;
    TransferID* const tid_ptr =
        node_.getDispatcher().getOutgoingTransferRegistry().accessOrCreate(otr_key, otr_deadline);
    if (tid_ptr == NULL)
    {
        return -ErrMemory;
    }

    tid = *tid_ptr;
    tid_ptr->increment();
    return 0;
}

int GlobalTimeSyncMaster::init()
{
    if (initialized_)
    {
        return 0;
    }

    // Data type ID
    const DataTypeDescriptor* const desc =
        GlobalDataTypeRegistry::instance().find(DataTypeKindMessage, protocol::GlobalTimeSync::getDataTypeFullName());
    if (desc == NULL)
    {
        return -ErrUnknownDataType;
    }
    dtid_ = desc->getID();

    // Iface master array
    int res = -ErrLogic;
    for (uint8_t i = 0; i < MaxCanIfaces; i++)
    {
        if (!iface_masters_[i].isConstructed())
        {
            iface_masters_[i].construct<INode&, uint8_t>(node_, i);
        }
        res = iface_masters_[i]->init();
        if (res < 0)
        {
            break;
        }
    }

    // Loopback listener
    initialized_ = res >= 0;
    if (initialized_)
    {
        LoopbackFrameListenerBase::startListening();
    }
    return res;
}

int GlobalTimeSyncMaster::publish()
{
    if (!initialized_)
    {
        const int res = init();
        if (res < 0)
        {
            return res;
        }
    }

    /*
     * Enforce max frequency
     */
    const MonotonicTime current_time = node_.getMonotonicTime();
    {
        const MonotonicDuration since_prev_pub = current_time - prev_pub_mono_;
        assert(since_prev_pub.isPositive());
        if (since_prev_pub.toMSec() < protocol::GlobalTimeSync::MIN_PUBLICATION_PERIOD_MS)
        {
            UAVCAN_TRACE("GlobalTimeSyncMaster", "Publication skipped");
            return 0;
        }
        prev_pub_mono_ = current_time;
    }

    /*
     * Obtain common Transfer ID for all masters
     */
    TransferID tid;
    {
        const int tid_res = getNextTransferID(tid);
        if (tid_res < 0)
        {
            return tid_res;
        }
    }

    for (uint8_t i = 0; i < node_.getDispatcher().getCanIOManager().getNumIfaces(); i++)
    {
        const int res = iface_masters_[i]->publish(tid, current_time);
        if (res < 0)
        {
            return res;
        }
    }
    return 0;
}

}