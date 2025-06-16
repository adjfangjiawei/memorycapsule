#ifndef BOLTPROTOCOL_CHUNKING_H
#define BOLTPROTOCOL_CHUNKING_H

#include <cstdint>
#include <functional>  // For std::function
#include <iosfwd>      // For std::istream, std::ostream
#include <vector>

#include "boltprotocol/message_defs.h"  // For BoltError, MAX_CHUNK_PAYLOAD_SIZE, CHUNK_HEADER_SIZE

namespace boltprotocol {

    /**
     * @brief ChunkedWriter 用于将完整的 Bolt 消息分块写入输出流。
     */
    class ChunkedWriter {
      public:
        explicit ChunkedWriter(std::ostream& stream);

        // 禁止拷贝和移动，因为其持有流的引用
        ChunkedWriter(const ChunkedWriter&) = delete;
        ChunkedWriter& operator=(const ChunkedWriter&) = delete;
        ChunkedWriter(ChunkedWriter&&) = delete;
        ChunkedWriter& operator=(ChunkedWriter&&) = delete;

        /**
         * @brief 将提供的完整消息数据分块写入流。
         *        会自动添加块头和末尾的空块。
         * @param message_data 包含单个完整 Bolt 消息的字节向量。
         * @return BoltError::SUCCESS 如果所有块都成功写入。
         *         BoltError::NETWORK_ERROR 如果流写入失败。
         *         BoltError::SERIALIZATION_ERROR 如果内部逻辑错误（例如块大小计算）。
         */
        BoltError write_message(const std::vector<uint8_t>& message_data);

        /**
         * @brief 获取最后一次操作的错误状态。
         */
        BoltError get_error() const {
            return last_error_;
        }

        /**
         * @brief 检查是否发生了错误。
         */
        bool has_error() const {
            return last_error_ != BoltError::SUCCESS;
        }

      private:
        BoltError write_chunk(const uint8_t* data, uint16_t size);
        BoltError write_chunk_header(uint16_t chunk_payload_size);
        BoltError write_end_of_message_marker();  // Writes a zero-size chunk

        void set_error(BoltError err);

        std::ostream& stream_;
        BoltError last_error_ = BoltError::SUCCESS;
    };

    /**
     * @brief ChunkedReader 用于从输入流中读取分块的 Bolt 消息。
     */
    class ChunkedReader {
      public:
        explicit ChunkedReader(std::istream& stream);

        // 禁止拷贝和移动
        ChunkedReader(const ChunkedReader&) = delete;
        ChunkedReader& operator=(const ChunkedReader&) = delete;
        ChunkedReader(ChunkedReader&&) = delete;
        ChunkedReader& operator=(ChunkedReader&&) = delete;

        /**
         * @brief 从流中读取一个完整的 Bolt 消息。
         *        它会持续读取数据块，直到遇到表示消息结束的空块。
         * @param out_message_data 输出参数，用于存储组装好的完整消息字节。
         *                         如果发生错误，此参数的内容未定义。
         * @return BoltError::SUCCESS 如果成功读取并组装了一个完整的消息。
         *         BoltError::NETWORK_ERROR 如果流读取失败。
         *         BoltError::DESERIALIZATION_ERROR 如果块格式无效或消息过大。
         *         BoltError::CHUNK_TOO_LARGE 如果单个块的声明大小超过 MAX_CHUNK_PAYLOAD_SIZE.
         */
        BoltError read_message(std::vector<uint8_t>& out_message_data);

        /**
         * @brief 获取最后一次操作的错误状态。
         */
        BoltError get_error() const {
            return last_error_;
        }
        /**
         * @brief 检查是否发生了错误。
         */
        bool has_error() const {
            return last_error_ != BoltError::SUCCESS;
        }

      private:
        BoltError read_chunk_header(uint16_t& out_chunk_payload_size);
        BoltError read_chunk_payload(uint16_t payload_size, std::vector<uint8_t>& buffer_to_append_to);

        void set_error(BoltError err);

        std::istream& stream_;
        BoltError last_error_ = BoltError::SUCCESS;
        // 可以在这里添加一个内部缓冲区来优化小块的读取，但为了简单起见，暂时直接追加到输出向量
    };

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_CHUNKING_H