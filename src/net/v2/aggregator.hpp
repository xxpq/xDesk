/**
 * DeskX Data Aggregator
 * 
 * 数据聚合器 - 负责分片数据的重组与FEC纠错
 */

#ifndef DESKX_NET_V2_AGGREGATOR_HPP
#define DESKX_NET_V2_AGGREGATOR_HPP

#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <array>
#include <chrono>
#include <memory>
#include <functional>
#include <mutex>

namespace deskx {
namespace net {

// FEC 配置
struct FECConfig {
    uint32_t data_shards = 4;   // 数据分片数
    uint32_t parity_shards = 2;  // 校验分片数
    uint32_t max_fec_k = 20;     // 最大 K 值
    
    FECConfig() = default;
    FECConfig(uint32_t d, uint32_t p) : data_shards(d), parity_shards(p) {}
};

// 分片状态
enum class ShardStatus : uint8_t {
    EMPTY = 0,
    DATA  = 1,
    PARITY = 2,
    COMPLETE = 3
};

// 单个分片数据
struct Shard {
    uint32_t index;
    uint32_t total;      // 总分片数
    ShardStatus status;
    std::vector<uint8_t> data;
    
    Shard() : index(0), total(0), status(ShardStatus::EMPTY) {}
    Shard(uint32_t idx, uint32_t tot, const uint8_t* d, size_t len) 
        : index(idx), total(tot), status(ShardStatus::DATA) {
        data.assign(d, d + len);
    }
};

// 数据聚合器类
class DataAggregator {
public:
    using CompleteCallback = std::function<void(uint32_t frame_id, 
                                                 std::vector<uint8_t>&& data)>;
    using LossCallback = std::function<void(float loss_rate)>;

private:
    // 待重组的帧缓冲区: frame_id -> shards vector
    std::unordered_map<uint32_t, std::vector<std::unique_ptr<Shard>>> pending_frames_;
    
    // 已完成但待交付的帧
    std::unordered_map<uint32_t, std::vector<uint8_t>> complete_frames_;
    
    // FEC 配置
    FECConfig fec_config_;
    
    // 统计信息
    struct Stats {
        uint64_t frames_received = 0;
        uint64_t frames_completed = 0;
        uint64_t frames_dropped = 0;
        uint64_t bytes_received = 0;
        uint64_t shards_lost = 0;
        uint64_t shards_recovered = 0;
    } stats_;
    
    // 回调函数
    CompleteCallback complete_callback_;
    LossCallback loss_callback_;
    
    // 配置参数
    uint32_t max_pending_frames_ = 64;
    uint32_t frame_timeout_ms_ = 5000;
    bool fec_enabled_ = true;
    
    // 内部状态 - 使用 mutable 以支持 const 方法
    mutable std::mutex mutex_;

public:
    DataAggregator();
    ~DataAggregator();
    
    // 配置
    void setFECConfig(const FECConfig& config);
    void setMaxPendingFrames(uint32_t max);
    void setFrameTimeout(uint32_t timeout_ms);
    void enableFEC(bool enable);
    
    // 回调设置
    void setCompleteCallback(CompleteCallback cb);
    void setLossCallback(LossCallback cb);
    
    // 添加数据分片
    bool addDataShard(uint32_t frame_id, uint32_t index, uint32_t total,
                      const uint8_t* data, size_t len);
    
    // 添加校验分片 (FEC)
    bool addParityShard(uint32_t frame_id, uint32_t index, uint32_t total,
                        const uint8_t* data, size_t len);
    
    // 添加完整帧数据（不分片）
    bool addFrame(uint32_t frame_id, const uint8_t* data, size_t len);
    
    // 处理接收到的数据
    bool processData(const uint8_t* header, const uint8_t* payload, size_t payload_len);
    
    // 检查并清理超时帧
    void checkTimeout();
    
    // 获取统计信息
    Stats getStats() const;
    void resetStats();
    
    // 清除所有待处理数据
    void clear();
    
private:
    // 尝试重组帧
    bool tryReassemble(uint32_t frame_id);
    
    // 使用 FEC 恢复丢失的分片
    bool recoverWithFEC(uint32_t frame_id);
    
    // 交付完成的帧
    void deliverFrame(uint32_t frame_id, std::vector<uint8_t>&& data);
    
    // 计算丢包率
    float calculateLossRate(uint32_t frame_id) const;
};

} // namespace net
} // namespace deskx

#endif // DESKX_NET_V2_AGGREGATOR_HPP
