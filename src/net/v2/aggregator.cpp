/**
 * DeskX Data Aggregator Implementation
 */

#include "aggregator.hpp"
#include "frame.hpp"
#include <algorithm>
#include <cmath>

namespace deskx {
namespace net {

DataAggregator::DataAggregator() {
    fec_config_ = FECConfig(4, 2);
}

DataAggregator::~DataAggregator() {
    clear();
}

void DataAggregator::setFECConfig(const FECConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    fec_config_ = config;
}

void DataAggregator::setMaxPendingFrames(uint32_t max) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_pending_frames_ = max;
}

void DataAggregator::setFrameTimeout(uint32_t timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    frame_timeout_ms_ = timeout_ms;
}

void DataAggregator::enableFEC(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    fec_enabled_ = enable;
}

void DataAggregator::setCompleteCallback(CompleteCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    complete_callback_ = std::move(cb);
}

void DataAggregator::setLossCallback(LossCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    loss_callback_ = std::move(cb);
}

bool DataAggregator::addDataShard(uint32_t frame_id, uint32_t index, uint32_t total,
                                   const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.frames_received++;
    stats_.bytes_received += len;
    
    auto it = pending_frames_.find(frame_id);
    if (it == pending_frames_.end()) {
        auto result = pending_frames_.emplace(frame_id, std::vector<std::unique_ptr<Shard>>());
        it = result.first;
        it->second.reserve(total);
    }
    
    auto& shards = it->second;
    if (index >= shards.size()) {
        shards.resize(index + 1);
    }
    
    shards[index] = std::make_unique<Shard>(index, total, data, len);
    return tryReassemble(frame_id);
}

bool DataAggregator::addParityShard(uint32_t frame_id, uint32_t index, uint32_t total,
                                     const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = pending_frames_.find(frame_id);
    if (it == pending_frames_.end()) {
        return false;
    }
    
    auto& shards = it->second;
    if (index >= shards.size()) {
        shards.resize(index + 1);
    }
    
    shards[index] = std::make_unique<Shard>();
    shards[index]->index = index;
    shards[index]->total = total;
    shards[index]->status = ShardStatus::PARITY;
    shards[index]->data.assign(data, data + len);
    
    return recoverWithFEC(frame_id);
}

bool DataAggregator::addFrame(uint32_t frame_id, const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.frames_received++;
    stats_.bytes_received += len;
    
    std::vector<uint8_t> frame_data(data, data + len);
    deliverFrame(frame_id, std::move(frame_data));
    return true;
}

bool DataAggregator::processData(const uint8_t* header, const uint8_t* payload, size_t payload_len) {
    v2::FrameHeader fh;
    std::memcpy(&fh, header, sizeof(v2::FrameHeader));
    fh.magic = ntohl(fh.magic);
    fh.payload_size = ntohl(fh.payload_size);
    fh.sequence = ntohl(fh.sequence);
    fh.timestamp = ntohl(fh.timestamp);
    
    if (fh.magic != v2::MAGIC_DESK) {
        return false;
    }
    
    if (fh.type == static_cast<uint8_t>(v2::FrameType::FRAME_VIDEO) ||
        fh.type == static_cast<uint8_t>(v2::FrameType::FRAME_RAW)) {
        return addDataShard(fh.sequence, 0, 1, payload, payload_len);
    }
    return true;
}

void DataAggregator::checkTimeout() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    while (pending_frames_.size() > max_pending_frames_) {
        pending_frames_.erase(pending_frames_.begin());
        stats_.frames_dropped++;
    }
    
    while (complete_frames_.size() > max_pending_frames_) {
        complete_frames_.erase(complete_frames_.begin());
        stats_.frames_dropped++;
    }
}

DataAggregator::Stats DataAggregator::getStats() const {
    // 复制 stats 到临时变量
    Stats result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = stats_;
    }
    return result;
}

void DataAggregator::resetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = Stats();
}

void DataAggregator::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_frames_.clear();
    complete_frames_.clear();
}

bool DataAggregator::tryReassemble(uint32_t frame_id) {
    auto it = pending_frames_.find(frame_id);
    if (it == pending_frames_.end()) return false;
    
    auto& shards = it->second;
    if (shards.empty()) return false;
    
    bool has_data = false;
    for (const auto& shard : shards) {
        if (shard && shard->status == ShardStatus::DATA) {
            has_data = true;
            break;
        }
    }
    if (!has_data) return false;
    
    size_t shard_size = 0;
    uint32_t total_shards = 0;
    for (const auto& shard : shards) {
        if (shard && shard->status == ShardStatus::DATA) {
            shard_size = shard->data.size();
            total_shards = shard->total;
            break;
        }
    }
    
    uint32_t received = 0;
    for (const auto& shard : shards) {
        if (shard && (shard->status == ShardStatus::DATA || shard->status == ShardStatus::COMPLETE)) {
            received++;
        }
    }
    
    if (received >= total_shards) {
        std::vector<uint8_t> frame_data;
        frame_data.reserve(shard_size * total_shards);
        
        for (uint32_t i = 0; i < total_shards; i++) {
            if (i < shards.size() && shards[i] && shards[i]->status == ShardStatus::DATA) {
                frame_data.insert(frame_data.end(), shards[i]->data.begin(), shards[i]->data.end());
            }
        }
        
        pending_frames_.erase(it);
        deliverFrame(frame_id, std::move(frame_data));
        return true;
    }
    return false;
}

bool DataAggregator::recoverWithFEC(uint32_t frame_id) {
    if (!fec_enabled_) return false;
    
    auto it = pending_frames_.find(frame_id);
    if (it == pending_frames_.end()) return false;
    
    auto& shards = it->second;
    uint32_t data_count = 0, parity_count = 0;
    for (const auto& shard : shards) {
        if (shard) {
            if (shard->status == ShardStatus::DATA) data_count++;
            else if (shard->status == ShardStatus::PARITY) parity_count++;
        }
    }
    
    if (data_count + parity_count >= shards.size() && data_count < shards.size() && parity_count > 0) {
        stats_.shards_recovered++;
        for (auto& shard : shards) {
            if (shard && shard->status == ShardStatus::PARITY) {
                shard->status = ShardStatus::COMPLETE;
                break;
            }
        }
        return tryReassemble(frame_id);
    }
    return false;
}

void DataAggregator::deliverFrame(uint32_t frame_id, std::vector<uint8_t>&& data) {
    stats_.frames_completed++;
    if (complete_callback_) {
        complete_callback_(frame_id, std::move(data));
    }
}

float DataAggregator::calculateLossRate(uint32_t frame_id) const {
    auto it = pending_frames_.find(frame_id);
    if (it == pending_frames_.end()) return 0.0f;
    
    const auto& shards = it->second;
    if (shards.empty()) return 0.0f;
    
    uint32_t total = 0, lost = 0;
    for (const auto& shard : shards) {
        total++;
        if (!shard || shard->status == ShardStatus::EMPTY) lost++;
    }
    return total > 0 ? static_cast<float>(lost) / total : 0.0f;
}

} // namespace net
} // namespace deskx
