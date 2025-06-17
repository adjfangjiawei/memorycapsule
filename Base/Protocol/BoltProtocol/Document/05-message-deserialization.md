好的，这是第五个文档文件 `05-message-deserialization.md` 的完整内容。

---

**`Base/Protocol/BoltProtocol/Document/05-message-deserialization.md`**

```markdown
# Bolt 消息反序列化

本章详细介绍如何使用 BoltProtocol 库将从网络接收到的 Bolt 消息字节流（已通过 `ChunkedReader` 重组为完整的 PackStream 消息）反序列化为 C++ 中的参数结构体。这包括客户端解析服务器的响应消息，以及服务器端解析客户端的请求消息。

所有消息的反序列化函数都在头文件 `#include "boltprotocol/message_serialization.h"` 中声明。

## 通用反序列化流程

无论是客户端接收服务器响应，还是服务器接收客户端请求，反序列化一条 Bolt 消息通常遵循以下步骤：

1.  **包含必要的头文件**:
    ```cpp
    #include "boltprotocol/message_defs.h"     // 提供 XxxMessageParams 结构体, Value, versions::Version, MessageTag 等
    #include "boltprotocol/message_serialization.h" // 提供 deserialize_xxx_message 函数声明
    #include "boltprotocol/packstream_reader.h"   // 提供 PackStreamReader 类
    #include <vector>                           // 通常用于存储从 ChunkedReader 获取的字节
    ```
2.  **获取完整的消息字节**:
    使用 `boltprotocol::ChunkedReader` 从输入流中读取一个完整的、已去分块的 Bolt 消息，这将得到一个 `std::vector<uint8_t>` 类型的 `received_message_bytes`。
3.  **准备 `PackStreamReader`**:
    实例化一个 `boltprotocol::PackStreamReader` 对象，将其绑定到包含完整消息字节的向量。
    ```cpp
    // std::vector<uint8_t> received_message_bytes = ... (from ChunkedReader)
    boltprotocol::PackStreamReader reader(received_message_bytes);
    ```
4.  **确定消息类型并调用反序列化函数**:
    *   **如果已知道期望的消息类型**: 直接调用相应的 `boltprotocol::deserialize_xxx_message()` (用于客户端解析响应) 或 `boltprotocol::deserialize_xxx_message_request()` (用于服务器解析请求) 函数。这些函数内部会验证消息标签是否匹配。
    *   **如果需要动态判断消息类型**:
        a.  先读取通用的 `boltprotocol::Value`，并检查它是否为一个 `std::shared_ptr<boltprotocol::PackStreamStructure>`。
        b.  如果是，获取其 `tag` 字段，转换为 `boltprotocol::MessageTag`。
        c.  根据 `tag` 分发到对应的反序列化函数。
        d.  **重要**: 如果采用这种方式，在调用特定消息的反序列化函数之前，需要**重新**用原始的 `received_message_bytes` 构建一个新的 `PackStreamReader`，因为第一次读取 `Value` 会消耗流。或者，`deserialize_message_structure_prelude` 可以被用来更安全地提取顶层结构而不完全消耗，但这主要用于反序列化函数内部。对于外部判断，通常是读一次，然后根据类型决定下一步。更稳健的方式是有一个通用的消息分派机制。
    *   **版本依赖性**: 某些消息的反序列化函数（如 `HELLO`, `RUN`, `BEGIN`, `ROUTE` 的请求反序列化，以及特定结构如 `BoltDateTime` 的转换）需要传递当前连接协商的 Bolt 版本 (`const boltprotocol::versions::Version& negotiated_version`)，因为消息的字段或其解释可能依赖于此版本。

5.  **错误检查**:
    所有反序列化函数都返回一个 `boltprotocol::BoltError`。务必检查此返回值。如果不是 `boltprotocol::BoltError::SUCCESS`，则表示反序列化过程中发生了错误（例如，格式错误、数据不完整、标签不匹配、字段数量不对等）。`PackStreamReader` 对象内部也会记录错误状态。
6.  **访问反序列化后的参数**:
    如果反序列化成功，传递给函数的输出参数结构体（例如 `SuccessMessageParams& out_params`）现在就包含了从消息字节中解析出来的数据。

## A. 客户端解析服务器响应消息

这些函数由客户端用于解析从服务器接收到的响应。

---

### 1. `SUCCESS` (MessageTag::SUCCESS - `0x70`)
*表示请求已成功处理。*

*   **参数结构体 (输出)**: `boltprotocol::SuccessMessageParams`
    *   `metadata` (std::map<std::string, Value>): 包含服务器返回的各种元数据。具体内容取决于产生此 `SUCCESS` 的原始请求和 Bolt 版本。常见的键包括：
        *   `"fields"` (List<String>): 对于 `RUN` 的响应，列出结果集的字段名。
        *   `"qid"` (Integer): 对于显式事务中的 `RUN`，返回查询ID。
        *   `"t_first"` (Integer): 第一个记录可用的时间 (毫秒)。
        *   `"server"` (String): 服务器代理字符串 (例如 "Neo4j/5.10.0")。通常在 `HELLO` 的响应中。
        *   `"connection_id"` (String): 服务器端的连接标识符。通常在 `HELLO` 的响应中。
        *   `"bookmark"` (String): 对于自动提交事务或 `COMMIT` 的响应，返回事务书签。
        *   `"has_more"` (Boolean): 对于 `PULL`/`DISCARD` 的响应 (Bolt 4.0+)，指示是否还有更多记录。
        *   `"patch_bolt"` (List<String>): 对于 `HELLO` 的响应 (Bolt 4.3-4.4)，确认服务器接受的协议补丁。
        *   `"hints"` (Map): 对于 `HELLO` 的响应 (Bolt 4.3+)，服务器配置提示。
        *   ... 以及其他如 `type`, `db`, `plan`, `profile`, `stats` 等。
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_success_message(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::SuccessMessageParams& out_params
    );
    ```

---

### 2. `FAILURE` (MessageTag::FAILURE - `0x7F`)
*表示请求处理失败。*

*   **参数结构体 (输出)**: `boltprotocol::FailureMessageParams`
    *   `metadata` (std::map<std::string, Value>): 包含错误信息。常见的键包括：
        *   `"code"` (String): Neo4j 错误码 (Bolt < 5.7)。
        *   `"message"` (String): 人类可读的错误描述。
        *   `"neo4j_code"` (String): Neo4j 错误码 (Bolt 5.7+)。
        *   `"gql_status"` (String): GQL 状态码 (Bolt 5.7+)。
        *   `"description"` (String): GQL 状态描述 (Bolt 5.7+)。
        *   `"diagnostic_record"` (Map): 诊断信息 (Bolt 5.7+)。
        *   `"cause"` (Map): 错误的内部原因 (Bolt 5.7+)。
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_failure_message(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::FailureMessageParams& out_params
    );
    ```

---

### 3. `RECORD` (MessageTag::RECORD - `0x71`)
*包含查询结果集中的一条记录。*

*   **参数结构体 (输出)**: `boltprotocol::RecordMessageParams`
    *   `fields` (std::vector<Value>): 一个包含该记录所有字段值的列表。值的顺序与 `RUN` 响应的 `SUCCESS` 消息中 `"fields"` 元数据定义的顺序一致。
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_record_message(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::RecordMessageParams& out_params
    );
    ```

---

### 4. `IGNORED` (MessageTag::IGNORED - `0x7E`)
*表示服务器忽略了对应的请求（例如，当连接处于 `FAILED` 或 `INTERRUPTED` 状态时）。*

*   **参数结构体**: 无。
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_ignored_message(
        boltprotocol::PackStreamReader& reader
    );
    ```
*   **注意**: IGNORED 消息的 PackStream Structure 可以有0个字段，或者1个包含元数据 map 的字段。此函数主要验证消息结构。

---

## B. 服务器解析客户端请求消息

这些函数由服务器端用于解析从客户端接收到的请求。

---

### 1. `HELLO` (MessageTag::HELLO - `0x01`)

*   **参数结构体 (输出)**: `boltprotocol::HelloMessageParams` (成员已在客户端序列化部分描述)
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_hello_message_request(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::HelloMessageParams& out_params, 
        const boltprotocol::versions::Version& server_negotiated_version // 服务器期望或已协商的版本
    );
    ```

---

### 2. `LOGON` (MessageTag::LOGON - `0x6A`)

*   **参数结构体 (输出)**: `boltprotocol::LogonMessageParams`
    *   `auth_tokens` (std::map<std::string, Value>)
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_logon_message_request(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::LogonMessageParams& out_params
    );
    ```

---

### 3. `LOGOFF` (MessageTag::LOGOFF - `0x6B`)

*   **参数结构体**: 无。
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_logoff_message_request(
        boltprotocol::PackStreamReader& reader
    );
    ```

---

### 4. `RUN` (MessageTag::RUN - `0x10`)

*   **参数结构体 (输出)**: `boltprotocol::RunMessageParams` (成员已在客户端序列化部分描述)
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_run_message_request(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::RunMessageParams& out_params, 
        const boltprotocol::versions::Version& server_negotiated_version
    );
    ```

---

### 5. `BEGIN` (MessageTag::BEGIN - `0x11`)

*   **参数结构体 (输出)**: `boltprotocol::BeginMessageParams` (成员已在客户端序列化部分描述)
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_begin_message_request(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::BeginMessageParams& out_params, 
        const boltprotocol::versions::Version& server_negotiated_version
    );
    ```

---

### 6. `PULL` (MessageTag::PULL - `0x3F`)

*   **参数结构体 (输出)**: `boltprotocol::PullMessageParams`
    *   `n` (std::optional<int64_t>)
    *   `qid` (std::optional<int64_t>)
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_pull_message_request(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::PullMessageParams& out_params, 
        const boltprotocol::versions::Version& server_negotiated_version
    );
    ```
*   **注意**: 此函数会根据 `server_negotiated_version` 判断是 Bolt < 4.0 的 `PULL_ALL` (0个字段) 还是 Bolt 4.0+ 的 `PULL` (1个 `extra` map 字段)。

---

### 7. `DISCARD` (MessageTag::DISCARD - `0x2F`)

*   **参数结构体 (输出)**: `boltprotocol::DiscardMessageParams`
    *   `n` (std::optional<int64_t>)
    *   `qid` (std::optional<int64_t>)
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_discard_message_request(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::DiscardMessageParams& out_params, 
        const boltprotocol::versions::Version& server_negotiated_version
    );
    ```
*   **注意**: 同 PULL。

---

### 8. `COMMIT` (MessageTag::COMMIT - `0x12`)

*   **参数结构体**: 无。
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_commit_message_request(
        boltprotocol::PackStreamReader& reader
    );
    ```
*   **注意**: 验证 PackStream Structure 为1个字段（空 map）。

---

### 9. `ROLLBACK` (MessageTag::ROLLBACK - `0x13`)

*   **参数结构体**: 无。
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_rollback_message_request(
        boltprotocol::PackStreamReader& reader
    );
    ```
*   **注意**: 同 COMMIT。

---

### 10. `RESET` (MessageTag::RESET - `0x0F`)

*   **参数结构体**: 无。
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_reset_message_request(
        boltprotocol::PackStreamReader& reader
    );
    ```
*   **注意**: 验证 PackStream Structure 为0个字段。

---

### 11. `GOODBYE` (MessageTag::GOODBYE - `0x02`)

*   **参数结构体**: 无。
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_goodbye_message_request(
        boltprotocol::PackStreamReader& reader
    );
    ```
*   **注意**: 验证 PackStream Structure 为0个字段。

---

### 12. `ROUTE` (MessageTag::ROUTE - `0x66`)

*   **参数结构体 (输出)**: `boltprotocol::RouteMessageParams` (成员已在客户端序列化部分描述)
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_route_message_request(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::RouteMessageParams& out_params, 
        const boltprotocol::versions::Version& server_negotiated_version
    );
    ```
*   **注意**: 根据 `server_negotiated_version` 解析第三个字段是 `db_name` (Bolt 4.3) 还是 `extra` map (Bolt 4.4+)。

---

### 13. `TELEMETRY` (MessageTag::TELEMETRY - `0x54`)

*   **参数结构体 (输出)**: `boltprotocol::TelemetryMessageParams`
    *   `metadata` (std::map<std::string, Value>): 应包含 `"api"` (Integer) 键。
*   **反序列化函数**:
    ```cpp
    boltprotocol::BoltError deserialize_telemetry_message_request(
        boltprotocol::PackStreamReader& reader, 
        boltprotocol::TelemetryMessageParams& out_params
    );
    ```

## 反序列化示例 (客户端解析 RECORD 消息)

```cpp
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/message_defs.h"
#include "boltprotocol/packstream_reader.h"
#include <vector>
#include <iostream>

// 假设 received_record_bytes 是从 ChunkedReader 获取的 RECORD 消息的字节
// std::vector<uint8_t> received_record_bytes = ...; 

void process_record_message(const std::vector<uint8_t>& received_record_bytes) {
    boltprotocol::PackStreamReader reader(received_record_bytes);
    boltprotocol::RecordMessageParams record_data;
    boltprotocol::BoltError err = boltprotocol::deserialize_record_message(reader, record_data);

    if (err == boltprotocol::BoltError::SUCCESS) {
        std::cout << "RECORD message deserialized. Number of fields: " << record_data.fields.size() << std::endl;
        for (size_t i = 0; i < record_data.fields.size(); ++i) {
            const auto& fieldValue = record_data.fields[i];
            // 根据 fieldValue 的实际类型 (通过 std::holds_alternative 和 std::get) 进行处理
            if (std::holds_alternative<std::string>(fieldValue)) {
                std::cout << "  Field " << i << " (String): " << std::get<std::string>(fieldValue) << std::endl;
            } else if (std::holds_alternative<int64_t>(fieldValue)) {
                std::cout << "  Field " << i << " (Integer): " << std::get<int64_t>(fieldValue) << std::endl;
            } // ... 其他类型 ...
        }
    } else {
        std::cerr << "Failed to deserialize RECORD message, error: " << static_cast<int>(err) << std::endl;
    }
}
```
正确使用这些反序列化函数，并结合版本信息（如果需要），可以帮助您的应用程序或服务器正确地解析和理解 Bolt 协议消息。
```