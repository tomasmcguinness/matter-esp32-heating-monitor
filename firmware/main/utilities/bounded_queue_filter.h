#pragma once

#include <atomic>

#include <esp_log.h>
#include <inet/IPPacketInfo.h>
#include <platform/ESP32/ESP32EndpointQueueFilter.h>

// Stops incoming UDP packets filling the CHIP event queue.
//
// lwIP's task hands each packet to the CHIP task through that queue, and CHIP aborts if the post
// fails, so a CHIP task held up for a second or two by CASE crypto restarts the board if enough
// packets arrive meanwhile. ESP32EndpointQueueFilter only drops mDNS packets that mention neither
// "matter" nor our hostname, which on a LAN of Matter devices is hardly any of them, and it does
// not count what it lets through.
//
// This counts the packets queued and drops new ones past a limit, leaving the rest of the queue
// for everything else that posts to it. A dropped Matter message is retransmitted by its sender,
// and a dropped mDNS answer is asked for again, so the worst outcome is a delay. The limits have to
// stay below CONFIG_MAX_EVENT_QUEUE_SIZE.
class BoundedQueueFilter : public chip::Inet::ESP32EndpointQueueFilter
{
public:
    static constexpr int kMaxQueuedPackets = 96;
    static constexpr int kMaxQueuedMdnsPackets = 24;

    FilterOutcome FilterBeforeEnqueue(const void *endpoint, const chip::Inet::IPPacketInfo &pktInfo,
                                      const chip::System::PacketBufferHandle &pktPayload) override
    {
        if (ESP32EndpointQueueFilter::FilterBeforeEnqueue(endpoint, pktInfo, pktPayload) != FilterOutcome::kAllowPacket)
        {
            return FilterOutcome::kDropPacket;
        }

        bool is_mdns = IsMdns(pktInfo);

        if (is_mdns && !Reserve(mQueuedMdns, kMaxQueuedMdnsPackets))
        {
            mDropped.fetch_add(1);
            return FilterOutcome::kDropPacket;
        }

        if (!Reserve(mQueued, kMaxQueuedPackets))
        {
            if (is_mdns)
            {
                mQueuedMdns.fetch_sub(1);
            }

            mDropped.fetch_add(1);
            return FilterOutcome::kDropPacket;
        }

        return FilterOutcome::kAllowPacket;
    }

    FilterOutcome FilterAfterDequeue(const void *endpoint, const chip::Inet::IPPacketInfo &pktInfo,
                                     const chip::System::PacketBufferHandle &pktPayload) override
    {
        if (IsMdns(pktInfo))
        {
            mQueuedMdns.fetch_sub(1);
        }

        // Logged here rather than where the packet is dropped, which is on lwIP's task during the
        // flood, and once it has drained rather than for every packet.
        if (mQueued.fetch_sub(1) == 1)
        {
            int dropped = mDropped.exchange(0);

            if (dropped > 0)
            {
                ESP_LOGW("queue_filter", "Dropped %d UDP packets while the CHIP event queue was backed up", dropped);
            }
        }

        return ESP32EndpointQueueFilter::FilterAfterDequeue(endpoint, pktInfo, pktPayload);
    }

private:
    static bool IsMdns(const chip::Inet::IPPacketInfo &pktInfo) { return pktInfo.DestPort == 5353; }

    static bool Reserve(std::atomic<int> &count, int limit)
    {
        if (count.fetch_add(1) >= limit)
        {
            count.fetch_sub(1);
            return false;
        }

        return true;
    }

    std::atomic<int> mQueued{0};
    std::atomic<int> mQueuedMdns{0};
    std::atomic<int> mDropped{0};
};
