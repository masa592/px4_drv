#ifndef TS_FRAMER_HPP_
#define TS_FRAMER_HPP_

#include <cstdint>
#include <vector>
#include <mutex>
#include <cstring>
#include <algorithm>

namespace px_s1ud {

constexpr size_t TS_PACKET_SIZE = 188;
constexpr uint8_t TS_SYNC_BYTE = 0x47;

class TsFramer {
public:
    explicit TsFramer(size_t max_packets = 32768)
        : max_packets_(max_packets)
        , buffer_(max_packets * TS_PACKET_SIZE)
        , read_pos_(0)
        , write_pos_(0)
        , packet_count_(0)
        , in_sync_(false)
        , consecutive_syncs_(0)
        , discard_null_packets_(false)
    {
        leftover_.reserve(TS_PACKET_SIZE * 4);
    }

    void SetDiscardNullPackets(bool discard)
    {
        discard_null_packets_ = discard;
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        read_pos_ = 0;
        write_pos_ = 0;
        packet_count_ = 0;
        in_sync_ = false;
        consecutive_syncs_ = 0;
        leftover_.clear();
    }

    // Feed raw bytes (can be arbitrary chunk size from WinUSB)
    void FeedRawData(const uint8_t *data, size_t length)
    {
        if (!data || length == 0) return;

        // Combine leftover from previous chunks
        std::vector<uint8_t> work;
        if (!leftover_.empty()) {
            work.reserve(leftover_.size() + length);
            work.insert(work.end(), leftover_.begin(), leftover_.end());
            work.insert(work.end(), data, data + length);
            leftover_.clear();
        }

        const uint8_t *stream = work.empty() ? data : work.data();
        size_t stream_len = work.empty() ? length : work.size();

        size_t offset = 0;
        while (offset + TS_PACKET_SIZE <= stream_len) {
            if (stream[offset] == TS_SYNC_BYTE) {
                // If already in sync, or if next packet also starts with sync byte
                if (in_sync_) {
                    ProcessPacket(&stream[offset]);
                    offset += TS_PACKET_SIZE;
                    continue;
                } else if (offset + TS_PACKET_SIZE < stream_len && stream[offset + TS_PACKET_SIZE] == TS_SYNC_BYTE) {
                    // Confirmed sync on at least 2 consecutive packets
                    in_sync_ = true;
                    ProcessPacket(&stream[offset]);
                    offset += TS_PACKET_SIZE;
                    continue;
                }
            }
            // Loss of sync or not yet synchronized
            in_sync_ = false;
            offset++;
        }

        // Store unparsed tail for the next FeedRawData call
        if (offset < stream_len) {
            leftover_.assign(stream + offset, stream + stream_len);
            if (leftover_.size() > TS_PACKET_SIZE * 8) {
                leftover_.erase(leftover_.begin(), leftover_.end() - TS_PACKET_SIZE);
            }
        }
    }

    size_t GetReadyCount()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return packet_count_;
    }

    size_t PopTsPackets(uint8_t *dst, size_t max_bytes, size_t *remain_bytes)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (packet_count_ == 0 || !dst || max_bytes < TS_PACKET_SIZE) {
            if (remain_bytes) *remain_bytes = packet_count_ * TS_PACKET_SIZE;
            return 0;
        }

        size_t requested_packets = max_bytes / TS_PACKET_SIZE;
        size_t packets_to_copy = (std::min)(requested_packets, packet_count_);
        size_t bytes_to_copy = packets_to_copy * TS_PACKET_SIZE;

        size_t first_chunk = (std::min)(packets_to_copy, max_packets_ - read_pos_);
        std::memcpy(dst, &buffer_[read_pos_ * TS_PACKET_SIZE], first_chunk * TS_PACKET_SIZE);
        read_pos_ = (read_pos_ + first_chunk) % max_packets_;

        size_t second_chunk = packets_to_copy - first_chunk;
        if (second_chunk > 0) {
            std::memcpy(dst + first_chunk * TS_PACKET_SIZE, &buffer_[0], second_chunk * TS_PACKET_SIZE);
            read_pos_ = second_chunk;
        }

        packet_count_ -= packets_to_copy;

        if (remain_bytes) {
            *remain_bytes = packet_count_ * TS_PACKET_SIZE;
        }
        return bytes_to_copy;
    }

private:
    void ProcessPacket(const uint8_t *packet)
    {
        if (discard_null_packets_) {
            uint16_t pid = ((packet[1] & 0x1F) << 8) | packet[2];
            if (pid == 0x1FFF) {
                return;
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (packet_count_ >= max_packets_) {
            // Drop oldest packet on overflow
            read_pos_ = (read_pos_ + 1) % max_packets_;
            packet_count_--;
        }

        std::memcpy(&buffer_[write_pos_ * TS_PACKET_SIZE], packet, TS_PACKET_SIZE);
        write_pos_ = (write_pos_ + 1) % max_packets_;
        packet_count_++;
    }

    size_t max_packets_;
    std::vector<uint8_t> buffer_;
    size_t read_pos_;
    size_t write_pos_;
    size_t packet_count_;
    bool in_sync_;
    int consecutive_syncs_;
    bool discard_null_packets_;
    std::vector<uint8_t> leftover_;
    std::mutex mutex_;
};

} // namespace px_s1ud

#endif // TS_FRAMER_HPP_
