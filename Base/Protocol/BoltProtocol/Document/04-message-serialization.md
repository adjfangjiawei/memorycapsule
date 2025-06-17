好的，这是第四个文档文件 `04-message-serialization.md` 的完整内容。

---

**`Base/Protocol/BoltProtocol/Document/04-message-serialization.md`**

```markdown
# Bolt 客户端消息序列化

本章详细介绍如何使用 BoltProtocol 库将 C++ 中的消息参数结构体序列化为可以通过网络发送给服务器的 Bolt 消息字节流。所有 Bolt 消息都使用 PackStream V1 格式进行序列化，并通过分块机制传输（分块处理见 `03-handshake-and-chunking.md`）。

所有客户端请求消息的序列化函数都在头文件 `#include "boltprotocol/message_serialization.h"` 中声明。

## 通用序列化流程

对于客户端发送的每一个请求消息，序列化过程通常如下：

1.  **包含必要的头文件**:
    ```cpp
    #include "boltprotocol/message_defs.h"     // 提供 XxxMessageParams 结构体, Value, versions::Version, MessageTag 等
    #include "boltprotocol/message_serialization.h" // 提供 serialize_xxx_message 函数声明
    #include "boltprotocol/packstream_writer.h"   // 提供 PackStreamWriter 类
    #include <vector>                           // 用于存储序列化后的字节
    ```
2.  **创建并填充参数结构体**:
    根据您要发送的消息类型，创建并填充相应的 `boltprotocol::XxxMessageParams` 结构体。例如，对于 `RUN` 消息，使用 `boltprotocol::RunMessageParams`。请参考每个消息的具体文档或 `bolt_message_params.h` 中的定义来了解其成员。
3.  **准备 `PackStreamWriter`**:
    实例化一个 `boltprotocol::PackStreamWriter` 对象。它需要一个输出目标来写入序列化后的字节。通常，这是一个 `std::vector<uint8_t>`。
    ```cpp
    std::vector<uint8_t> serialized_message_bytes;
    boltprotocol::PackStreamWriter writer(serialized_message_bytes);
    ```
4.  **调用序列化函数**:
    调用与消息类型对应的 `boltprotocol::serialize_xxx_message()` 函数。将准备好的参数结构体实例和 `PackStreamWriter` 实例传递给它。
    *   **版本依赖性**: 某些消息（如 `HELLO`, `RUN`, `BEGIN`, `ROUTE`）的结构或其 `extra` 字典中的字段会根据 Bolt 协议版本而变化。因此，这些消息的序列化函数需要一个额外的 `const boltprotocol::versions::Version& target_bolt_version` 参数，以指明应按照哪个 Bolt 版本的规范来构造消息。通常，这个版本是客户端与服务器握手后协商得到的版本，或者是客户端在发送 `HELLO` 时意图使用的版本。
5.  **错误检查**:
    所有序列化函数都返回一个 `boltprotocol::BoltError`。务必检查此返回值。如果不是 `boltprotocol::BoltError::SUCCESS`，则表示序列化过程中发生了错误。`PackStreamWriter` 对象内部也会记录错误状态，可以通过其 `has_error()` 和 `get_error()` 方法查询。
6.  **获取序列化数据**:
    如果序列化成功，并且 `PackStreamWriter` 的目标是 `std::vector<uint8_t>`，那么这个向量现在就包含了完整的、PackStream 编码的 Bolt 消息的字节负载。
    ```cpp
    // ... (接上例)
    // serialized_message_bytes 现在包含了可以发送给 ChunkedWriter 的数据。
    ```

## 支持的客户端请求消息及其序列化

以下是本库支持序列化的主要客户端请求消息的列表、它们的参数结构体关键成员以及对应的序列化函数签名。

---

### 1. `HELLO` (MessageTag::HELLO - `0x01`)
*用于初始化连接，进行认证（Bolt < 5.1）和能力协商。*

*   **参数结构体**: `boltprotocol::HelloMessageParams`
    *   `user_agent` (std::string): **必需**. 客户端应用程序的用户代理字符串。
    *   `auth_scheme` (std::optional<std::string>): 认证方案，如 "basic", "none", "kerberos"。主要用于 Bolt < 5.1。
    *   `auth_principal` (std::optional<std::string>): 用户名，用于 "basic" 等方案。
    *   `auth_credentials` (std::optional<std::string>): 凭证（如密码），用于 "basic" 等方案。
    *   `auth_scheme_specific_tokens` (std::optional<std::map<std::string, Value>>): 其他特定认证方案的参数。
    *   `routing_context` (std::optional<std::map<std::string, Value>>): Bolt 4.1+。用于传递路由信息，例如 `{"address": "initial.host:port"}`。
    *   `patch_bolt` (std::optional<std::vector<std::string>>): Bolt 4.3-4.4。用于请求协议补丁，例如 `{"utc"}`。
    *   `notifications_min_severity` (std::optional<std::string>): Bolt 5.2+。设置最小通知级别。
    *   `notifications_disabled_categories` (std::optional<std::vector<std::string>>): Bolt 5.2+。禁用特定类别的通知。
    *   `bolt_agent` (std::optional<BoltAgentInfo>): Bolt 5.3+ **必需**. 包含驱动/库自身的信息。
        *   `BoltAgentInfo::product` (std::string): **必需**. 例如 "MyCppDriver/1.0"。
        *   `BoltAgentInfo::platform` (std::optional<std::string>): 例如 "Linux x86_64"。
        *   `BoltAgentInfo::language` (std::optional<std::string>): 例如 "C++20"。
        *   `BoltAgentInfo::language_details` (std::optional<std::string>): 例如 "GCC 11.3"。
    *   `other_extra_tokens` (std::map<std::string, Value>): 用于任何其他非标准或未来版本的 `extra` 字典参数。
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_hello_message(
        const boltprotocol::HelloMessageParams& params, 
        boltprotocol::PackStreamWriter& writer, 
        const boltprotocol::versions::Version& client_target_version
    );
    ```

---

### 2. `LOGON` (MessageTag::LOGON - `0x6A`)
*用于 Bolt 5.1+ 的身份验证。*

*   **参数结构体**: `boltprotocol::LogonMessageParams`
    *   `auth_tokens` (std::map<std::string, Value>): **必需**. 包含 `"scheme"` (必需, String) 以及该 scheme 所需的其他认证信息，例如：
        *   对于 `"basic"` scheme: `"principal"` (String), `"credentials"` (String).
        *   对于 `"bearer"` scheme: `"credentials"` (String, 包含 token).
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_logon_message(
        const boltprotocol::LogonMessageParams& params, 
        boltprotocol::PackStreamWriter& writer
    );
    ```

---

### 3. `LOGOFF` (MessageTag::LOGOFF - `0x6B`)
*用于 Bolt 5.1+ 注销当前用户。*

*   **参数结构体**: `boltprotocol::LogoffMessageParams` (此结构体为空，因为 LOGOFF 消息没有参数字段。)
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_logoff_message(
        boltprotocol::PackStreamWriter& writer
    );
    ```
    *注意*: 此函数不需要 `LogoffMessageParams` 类型的参数，因为它序列化的是一个没有字段的 PackStream Structure。

---

### 4. `RUN` (MessageTag::RUN - `0x10`)
*用于执行一个 Cypher 查询，可以用于自动提交事务或在显式事务内。*

*   **参数结构体**: `boltprotocol::RunMessageParams`
    *   `cypher_query` (std::string): **必需**. 要执行的 Cypher 查询语句。
    *   `parameters` (std::map<std::string, Value>): Cypher 查询的参数。
    *   `bookmarks` (std::optional<std::vector<std::string>>): Bolt 3+. 用于因果一致性的书签列表。
    *   `tx_timeout` (std::optional<int64_t>): Bolt 3+. 事务超时时间（毫秒）。
    *   `tx_metadata` (std::optional<std::map<std::string, Value>>): Bolt 3+. 事务相关的元数据。
    *   `mode` (std::optional<std::string>): Bolt 3+. 事务访问模式 ("r" 表示只读，"w" 表示读写，默认为 "w")。仅用于自动提交事务。
    *   `db` (std::optional<std::string>): Bolt 4.0+. 指定要操作的数据库名称。
    *   `imp_user` (std::optional<std::string>): Bolt 4.4+. 模拟用户（仅用于自动提交的 RUN）。
    *   `notifications_min_severity` (std::optional<std::string>): Bolt 5.2+.
    *   `notifications_disabled_categories` (std::optional<std::vector<std::string>>): Bolt 5.2+.
    *   `other_extra_fields` (std::map<std::string, Value>): 其他自定义的 `extra` 字典字段。
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_run_message(
        const boltprotocol::RunMessageParams& params, 
        boltprotocol::PackStreamWriter& writer, 
        const boltprotocol::versions::Version& target_bolt_version
    );
    ```
*   **注意**: `target_bolt_version` 用于确定 `extra` 字典中应包含哪些版本特定的字段。显式事务中的 `RUN` 消息的 `extra` 字段为空。

---

### 5. `BEGIN` (MessageTag::BEGIN - `0x11`)
*用于开始一个显式事务 (Bolt 3+)。*

*   **参数结构体**: `boltprotocol::BeginMessageParams`
    *   `bookmarks` (std::optional<std::vector<std::string>>): Bolt 3+.
    *   `tx_timeout` (std::optional<int64_t>): Bolt 3+.
    *   `tx_metadata` (std::optional<std::map<std::string, Value>>): Bolt 3+.
    *   `mode` (std::optional<std::string>): Bolt 3+ (默认为 "w" 如果不指定)。
    *   `db` (std::optional<std::string>): Bolt 4.0+.
    *   `imp_user` (std::optional<std::string>): Bolt 4.0+.
    *   `notifications_min_severity` (std::optional<std::string>): Bolt 5.2+.
    *   `notifications_disabled_categories` (std::optional<std::vector<std::string>>): Bolt 5.2+.
    *   `other_extra_fields` (std::map<std::string, Value>): 其他自定义 `extra` 字段。
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_begin_message(
        const boltprotocol::BeginMessageParams& params, 
        boltprotocol::PackStreamWriter& writer, 
        const boltprotocol::versions::Version& target_bolt_version
    );
    ```

---

### 6. `PULL` (MessageTag::PULL - `0x3F`)
*用于从服务器拉取由 `RUN` 启动的查询结果流。在 Bolt 4.0 之前被称为 `PULL_ALL` 且无参数。*

*   **参数结构体**: `boltprotocol::PullMessageParams`
    *   `n` (std::optional<int64_t>): **必需 (对于 Bolt 4.0+)**. 要获取的记录数。`-1` 表示获取所有剩余记录。
    *   `qid` (std::optional<int64_t>): **必需 (对于 Bolt 4.0+ 显式事务中的 PULL)**. 查询ID，用于标识要从哪个结果流拉取数据。对于自动提交事务或 Bolt 4.0 之前的 `PULL_ALL`，此字段可能不那么关键或默认为 `-1`。
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_pull_message(
        const boltprotocol::PullMessageParams& params, 
        boltprotocol::PackStreamWriter& writer
    );
    ```
*   **注意**: 此函数序列化为 Bolt 4.0+ 的 PULL 格式（包含一个 `extra` map）。对于 Bolt < 4.0 的 `PULL_ALL`（没有字段），客户端需要发送一个空的 PSS (tag PULL, 0 fields)，这需要一个不同的序列化逻辑或此函数的版本判断。目前此函数假定 Bolt 4.0+ 格式。

---

### 7. `DISCARD` (MessageTag::DISCARD - `0x2F`)
*用于丢弃服务器上由 `RUN` 启动的查询结果流中剩余的记录。在 Bolt 4.0 之前被称为 `DISCARD_ALL` 且无参数。*

*   **参数结构体**: `boltprotocol::DiscardMessageParams`
    *   `n` (std::optional<int64_t>): **必需 (对于 Bolt 4.0+)**. 要丢弃的记录数。`-1` 表示丢弃所有剩余记录。
    *   `qid` (std::optional<int64_t>): **必需 (对于 Bolt 4.0+ 显式事务中的 DISCARD)**. 查询ID。
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_discard_message(
        const boltprotocol::DiscardMessageParams& params, 
        boltprotocol::PackStreamWriter& writer
    );
    ```
*   **注意**: 同 PULL。

---

### 8. `COMMIT` (MessageTag::COMMIT - `0x12`)
*用于提交一个显式事务 (Bolt 3+)。*

*   **参数结构体**: `boltprotocol::CommitMessageParams` (空结构体)
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_commit_message(
        boltprotocol::PackStreamWriter& writer
    );
    ```
*   **注意**: COMMIT 消息的 PackStream Structure 包含一个字段，该字段是一个空的 PackStream Map `{}`。

---

### 9. `ROLLBACK` (MessageTag::ROLLBACK - `0x13`)
*用于回滚一个显式事务 (Bolt 3+)。*

*   **参数结构体**: `boltprotocol::RollbackMessageParams` (空结构体)
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_rollback_message(
        boltprotocol::PackStreamWriter& writer
    );
    ```
*   **注意**: 与 COMMIT 类似，包含一个空 map 字段。

---

### 10. `RESET` (MessageTag::RESET - `0x0F`)
*用于将连接重置回初始状态（通常是 `READY` 或 `AUTHENTICATION` 状态，取决于版本）。*

*   **参数结构体**: 无
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_reset_message(
        boltprotocol::PackStreamWriter& writer
    );
    ```
*   **注意**: RESET 消息的 PackStream Structure 没有字段。

---

### 11. `GOODBYE` (MessageTag::GOODBYE - `0x02`)
*客户端通知服务器它将优雅地关闭连接 (Bolt 3+)。服务器通常不响应此消息。*

*   **参数结构体**: 无
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_goodbye_message(
        boltprotocol::PackStreamWriter& writer
    );
    ```
*   **注意**: GOODBYE 消息的 PackStream Structure 没有字段。

---

### 12. `ROUTE` (MessageTag::ROUTE - `0x66`)
*用于从服务器获取路由信息 (Bolt 4.3+)。*

*   **参数结构体**: `boltprotocol::RouteMessageParams`
    *   `routing_table_context` (std::map<std::string, Value>): 包含路由上下文信息，如客户端的初始连接地址。对于 ROUTE V2，此 map 可能包含 `"db"` 和 `"imp_user"`。
    *   `bookmarks` (std::vector<std::string>): 用于会话一致性的书签。
    *   `db_name_for_v43` (std::optional<std::string>): **仅用于 Bolt 4.3**。作为 PackStream Structure 的第三个顶级字段。
    *   `extra_for_v44_plus` (std::optional<std::map<std::string, Value>>): **用于 Bolt 4.4+**。作为 PackStream Structure 的第三个顶级字段，此 map 可包含 `"db"` 和/或 `"imp_user"`。
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_route_message(
        const boltprotocol::RouteMessageParams& params, 
        boltprotocol::PackStreamWriter& writer, 
        const boltprotocol::versions::Version& negotiated_bolt_version
    );
    ```
*   **注意**: `negotiated_bolt_version` 用于决定第三个顶级字段的结构（是 `db_name_for_v43` 还是 `extra_for_v44_plus`）。

---

### 13. `TELEMETRY` (MessageTag::TELEMETRY - `0x54`)
*用于向服务器发送驱动 API 使用情况的遥测数据 (Bolt 5.4+)。*

*   **参数结构体**: `boltprotocol::TelemetryMessageParams`
    *   `metadata` (std::map<std::string, Value>): **必需**. 必须包含一个键为 `"api"`，值为 `Integer` 的条目，表示使用的 API 类型。
*   **序列化函数**:
    ```cpp
    boltprotocol::BoltError serialize_telemetry_message(
        const boltprotocol::TelemetryMessageParams& params, 
        boltprotocol::PackStreamWriter& writer
    );
    ```

## 示例：序列化 HELLO 消息

```cpp
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/message_defs.h" // For HelloMessageParams, Value, versions::Version etc.
#include "boltprotocol/packstream_writer.h"
#include <vector>
#include <iostream>

int main() {
    boltprotocol::HelloMessageParams hello_p;
    // 假设客户端目标版本是 Bolt 5.3
    boltprotocol::versions::Version target_version(5, 3);

    hello_p.user_agent = "MyAwesomeCppApp/1.0";
    
    // Bolt 5.3+ 需要 bolt_agent
    boltprotocol::HelloMessageParams::BoltAgentInfo agent;
    agent.product = "MyDriver/0.1";
    agent.platform = "Linux x64";
    agent.language = "C++20";
    hello_p.bolt_agent = agent;

    // Bolt 5.3 HELLO 中不包含认证信息 (应使用 LOGON)
    // hello_p.auth_scheme = "basic"; 
    // hello_p.auth_principal = "user";
    // hello_p.auth_credentials = "pass";

    std::map<std::string, boltprotocol::Value> routing_ctx;
    routing_ctx["address"] = boltprotocol::Value(std::string("client.initial.host:7687"));
    hello_p.routing_context = routing_ctx; // For Bolt 4.1+

    std::vector<uint8_t> serialized_bytes;
    boltprotocol::PackStreamWriter writer(serialized_bytes);

    boltprotocol::BoltError err = boltprotocol::serialize_hello_message(hello_p, writer, target_version);

    if (err == boltprotocol::BoltError::SUCCESS) {
        std::cout << "HELLO message serialized. Size: " << serialized_bytes.size() << std::endl;
        // serialized_bytes 可以传递给 ChunkedWriter
    } else {
        std::cerr << "HELLO serialization failed: " << static_cast<int>(err) << std::endl;
    }

    return 0;
}

```

通过使用这些函数和参数结构体，您可以方便地为各种 Bolt 客户端请求构建符合协议规范的 PackStream 字节流。务必根据您正在交互的服务器所支持的 Bolt 版本以及消息本身的规范来正确填充参数。
```