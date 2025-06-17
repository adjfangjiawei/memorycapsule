好的，这是第三个文档文件 `03-handshake-and-chunking.md` 的完整内容。

---

**`Base/Protocol/BoltProtocol/Document/03-handshake-and-chunking.md`**

```markdown
# Bolt 握手与消息分块

在任何 Bolt 消息交换开始之前，客户端和服务器必须执行一次握手来协商协议版本。在握手成功后，所有消息的传输都将采用分块编码。本章详细介绍如何使用 BoltProtocol 库处理这两个过程。

## 1. Bolt 握手 (Handshake)

握手是 Bolt 连接的第一个步骤，用于客户端和服务器就后续通信将使用的 Bolt 协议版本达成一致。此过程不依赖于特定的 Bolt 消息版本。

### 版本表示 (`boltprotocol::versions::Version`)

Bolt 协议版本由 `boltprotocol::versions::Version` 结构体表示，其定义（概念性）如下：

```cpp
namespace boltprotocol {
namespace versions {
    struct Version {
        uint8_t major; // 主版本号
        uint8_t minor; // 次版本号

        // 构造函数，例如 Version(5, 4) 代表 Bolt 5.4
        constexpr Version(uint8_t maj, uint8_t min);

        // 比较操作符 (<, ==, !=)
        bool operator<(const Version& other) const;
        bool operator==(const Version& other) const;

        // 将版本转换为握手时发送的4字节数组 (大端序, 00 00 Maj Min)
        std::array<uint8_t, 4> to_handshake_bytes() const;

        // 从服务器响应的4字节数组解析版本
        static BoltError from_handshake_bytes(const std::array<uint8_t, 4>& bytes, Version& out_version);
    };

    // 预定义的版本常量，例如:
    // extern const Version V5_4; // (5,4)
    // extern const Version V5_3; // (5,3)
    // ... 等
} // namespace versions
} // namespace boltprotocol
```
您可以直接使用这些预定义的常量，如 `boltprotocol::versions::V5_4`。

### 握手相关函数

所有握手相关的函数和类型都在 `#include "boltprotocol/handshake.h"` 中声明。

*   **`std::vector<boltprotocol::versions::Version> boltprotocol::versions::get_default_proposed_versions();`**
    *   **描述**: 返回一个库预设的、客户端可以向服务器提议的 Bolt 版本列表。列表按偏好顺序（通常是最新的、最受支持的在前）排列。
    *   **返回值**: 一个包含 `Version` 对象的 `std::vector`。

*   **`boltprotocol::BoltError boltprotocol::perform_handshake(std::ostream& client_output_stream, std::istream& client_input_stream, const std::vector<boltprotocol::versions::Version>& proposed_versions, boltprotocol::versions::Version& out_negotiated_version);`**
    *   **描述**: 执行完整的 Bolt 握手流程。此函数会：
        1.  根据 `proposed_versions` 构建握手请求。
        2.  通过 `client_output_stream` 将20字节的握手请求（包括魔法序列 `6060B017` 和最多4个提议版本）发送给服务器。
        3.  通过 `client_input_stream` 从服务器读取4字节的握手响应。
        4.  解析服务器响应，并将协商成功的版本存储在 `out_negotiated_version` 中。
    *   **参数**:
        *   `client_output_stream`: 一个 `std::ostream&`，代表客户端向服务器发送数据的流（例如，网络套接字的输出流部分）。
        *   `client_input_stream`: 一个 `std::istream&`，代表客户端从服务器接收数据的流（例如，网络套接字的输入流部分）。
        *   `proposed_versions`: 一个 `const std::vector<boltprotocol::versions::Version>&`，包含客户端希望提议的协议版本。通常使用 `get_default_proposed_versions()` 的结果。
        *   `out_negotiated_version`: 一个 `boltprotocol::versions::Version&` 的引用，用于接收握手成功后服务器选择的版本。
    *   **返回值**:
        *   `boltprotocol::BoltError::SUCCESS`: 握手成功，`out_negotiated_version` 已填充。
        *   `boltprotocol::BoltError::INVALID_ARGUMENT`: 如果 `proposed_versions` 为空。
        *   `boltprotocol::BoltError::NETWORK_ERROR`: 在流读写过程中发生错误。
        *   `boltprotocol::BoltError::HANDSHAKE_NO_COMMON_VERSION`: 服务器返回全零 (`00 00 00 00`)，表示不支持客户端提议的任何版本。
        *   `boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION`: 服务器返回的响应格式无法解析为一个有效的 Bolt 版本。
        *   其他可能的错误码。

### 握手流程示例

```cpp
#include "boltprotocol/handshake.h"
#include "boltprotocol/message_defs.h" // For BoltError and versions::Version constants
#include <iostream>
#include <sstream>   // 用于模拟网络流
#include <vector>
#include <array>     // 用于模拟服务器响应

int main() {
    std::stringstream client_to_server_pipe; 
    std::stringstream server_to_client_pipe; 
    boltprotocol::versions::Version negotiated_version;
    boltprotocol::BoltError err;

    // 1. 客户端获取提议版本列表
    std::vector<boltprotocol::versions::Version> client_proposals = 
        boltprotocol::versions::get_default_proposed_versions();

    if (client_proposals.empty()) {
        std::cerr << "Client: No versions to propose." << std::endl;
        return 1;
    }
    std::cout << "Client: Proposing versions: ";
    for (const auto& v : client_proposals) {
        std::cout << (int)v.major << "." << (int)v.minor << " ";
    }
    std::cout << std::endl;

    // --- 模拟服务器端行为 ---
    // 假设服务器支持客户端提议的第一个版本 (例如 Bolt 5.4)
    boltprotocol::versions::Version server_supported_choice = client_proposals[0]; 
    std::array<uint8_t, boltprotocol::HANDSHAKE_RESPONSE_SIZE_BYTES> server_response_data = 
        server_supported_choice.to_handshake_bytes();
    
    // 服务器将响应写入 "server_to_client_pipe"
    server_to_client_pipe.write(
        reinterpret_cast<const char*>(server_response_data.data()), 
        server_response_data.size()
    );
    // 模拟完毕后，确保服务器端的流准备好被客户端读取
    server_to_client_pipe.seekg(0); 
    // --- 服务器端行为模拟结束 ---

    // 2. 客户端执行握手
    std::cout << "Client: Performing handshake..." << std::endl;
    err = boltprotocol::perform_handshake(
        client_to_server_pipe,   // 客户端写数据到这里
        server_to_client_pipe,   // 客户端从这里读数据
        client_proposals,
        negotiated_version
    );

    if (err == boltprotocol::BoltError::SUCCESS) {
        std::cout << "Client: Handshake successful!" << std::endl;
        std::cout << "Client: Negotiated Bolt Protocol Version: "
                  << static_cast<int>(negotiated_version.major) << "."
                  << static_cast<int>(negotiated_version.minor) << std::endl;
        // 后续通信应使用 negotiated_version
    } else {
        std::cerr << "Client: Handshake failed. Error code: " << static_cast<int>(err) << std::endl;
        // 根据错误码进行相应处理
        return 1;
    }

    // 此时，client_to_server_pipe 中包含了客户端发送的20字节握手数据
    // server_to_client_pipe 中的4字节响应数据已被 perform_handshake 读取完毕

    return 0;
}
```

## 2. 消息分块 (Chunking)

在成功的握手之后，所有 Bolt 消息（包括 HELLO 消息）都必须使用分块编码进行传输。分块机制允许将一条逻辑消息分割成一个或多个物理块进行传输。

### 分块规则

*   **块结构**: 每个块以一个 **2字节的块头** 开始。这个头部是一个无符号的、大端序的16位整数，表示紧随其后的数据块负载的大小（以字节为单位）。因此，单个块的最大负载大小是 65,535 字节 (0xFFFF)。
*   **消息结束标记**: 每条完整的 Bolt 消息在其所有数据块之后，必须以一个**零大小的块**作为结束。这个标记由两个字节 `00 00`（即块头声明大小为0）表示。它用于向接收方指示消息的边界。
*   **NOOP Chunk (Bolt 4.1+)**: 一个零大小的块 (`00 00`) 也可以在消息之间作为 NOOP (No Operation) 块发送，通常用于连接保活 (keep-alive)。

### 分块读写器接口

本库在 `#include "boltprotocol/chunking.h"` 中提供了处理分块的类。

*   **`boltprotocol::ChunkedWriter`**: 用于将一条完整的、已经过 PackStream 序列化的 Bolt 消息分块并写入输出流。
    *   **构造函数**: `ChunkedWriter(std::ostream& output_stream);`
        *   `output_stream`: 将分块数据写入的目标流。
    *   **核心方法**: `boltprotocol::BoltError write_message(const std::vector<uint8_t>& full_message_payload);`
        *   `full_message_payload`: 一个包含单条、完整、已 PackStream 序列化的 Bolt 消息的字节向量。
        *   此方法会自动将 `full_message_payload` 分割成一个或多个数据块（每个块不超过最大尺寸），为每个块添加2字节的块头，并在所有数据块之后写入 `00 00` 结束标记。
        *   **返回值**: `BoltError::SUCCESS` 或错误码（如 `BoltError::NETWORK_ERROR`）。

*   **`boltprotocol::ChunkedReader`**: 用于从输入流中读取分块数据，并将它们重新组装成一条完整的 Bolt 消息。
    *   **构造函数**: `ChunkedReader(std::istream& input_stream);`
        *   `input_stream`: 从中读取分块数据的来源流。
    *   **核心方法**: `boltprotocol::BoltError read_message(std::vector<uint8_t>& out_reconstructed_payload);`
        *   此方法会持续读取块（块头 + 块数据），直到遇到零大小的块 (`00 00`)。
        *   所有读取到的块负载数据会被聚合并存储在 `out_reconstructed_payload` 中。
        *   **返回值**: `BoltError::SUCCESS` 或错误码（如 `BoltError::NETWORK_ERROR`, `BoltError::CHUNK_DECODING_ERROR`, `BoltError::MESSAGE_TOO_LARGE`）。
        *   如果连续读取到 `00 00`（例如，一个消息结束标记后紧跟着一个 NOOP chunk），当为 NOOP chunk 调用 `read_message` 时，`out_reconstructed_payload` 将为空。

### 分块使用流程

1.  **准备消息负载**: 使用本库的消息序列化函数 (例如 `serialize_hello_message`) 将消息参数对象转换为 `std::vector<uint8_t>` 形式的 PackStream 字节流。
2.  **发送分块消息**:
    a.  创建 `ChunkedWriter` 实例，绑定到网络输出流。
    b.  调用 `writer.write_message()`，传入准备好的消息负载。
    c.  检查错误。
3.  **接收分块消息**:
    a.  创建 `ChunkedReader` 实例，绑定到网络输入流。
    b.  创建一个空的 `std::vector<uint8_t>` 来存储重组后的消息。
    c.  调用 `reader.read_message()`。
    d.  检查错误。
    e.  如果成功且接收到的向量不为空，这些字节就是完整的 PackStream 编码的 Bolt 消息，可以传递给相应的消息反序列化函数 (例如 `deserialize_success_message`) 进行处理。

**示例代码已在 `01-introduction.md` 中通过模拟流展示，请参考该示例了解具体调用方式。**

正确地使用握手和分块是 Bolt 协议通信的基础。本库的 `perform_handshake`, `ChunkedWriter`, 和 `ChunkedReader` 封装了这些过程的复杂性。
```