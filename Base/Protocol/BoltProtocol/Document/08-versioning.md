好的，这是第八个文档文件 `08-versioning.md` 的完整内容。

---

**`Base/Protocol/BoltProtocol/Document/08-versioning.md`**

```markdown
# Bolt 版本处理

Bolt 协议是一个多版本协议，客户端和服务器需要在通信开始时通过握手过程协商一个共同支持的协议版本。这个协商得到的版本将决定后续消息交换的格式和行为。BoltProtocol 库提供了处理版本信息和版本相关逻辑的机制。

## 1. 版本表示 (`boltprotocol::versions::Version`)

Bolt 协议版本由 `boltprotocol::versions::Version` 结构体表示，其定义（在 `boltprotocol/bolt_errors_versions.h` 中，通过 `boltprotocol/message_defs.h` 聚合包含）如下：

```cpp
namespace boltprotocol {
namespace versions {
    struct Version {
        uint8_t major; // 主版本号
        uint8_t minor; // 次版本号

        // 默认构造函数 (通常初始化为 0.0 或无效状态)
        Version(); 

        // 构造特定版本，例如 Version(5, 4) 代表 Bolt 5.4
        constexpr Version(uint8_t maj, uint8_t min);

        // 比较操作符
        bool operator<(const Version& other) const;
        bool operator==(const Version& other) const;
        bool operator!=(const Version& other) const;
        // >=, >, <= 可以通过 < 和 == 组合实现，或显式提供

        // 用于握手：将版本对象转换为4字节大端序数组 (格式通常为 00 00 Major Minor)
        std::array<uint8_t, 4> to_handshake_bytes() const;

        // 用于握手：从服务器响应的4字节数组解析版本对象
        static BoltError from_handshake_bytes(const std::array<uint8_t, 4>& bytes, Version& out_version);
    };

    // 预定义的常见 Bolt 版本常量
    extern const Version V5_4; // (5,4)
    extern const Version V5_3; // (5,3)
    extern const Version V5_2; // (5,2)
    extern const Version V5_1; // (5,1)
    extern const Version V5_0; // (5,0)
    extern const Version V4_4; // (4,4)
    // ... 以及其他可能定义的版本

    // 获取库推荐的客户端提议版本列表
    extern const std::vector<Version>& get_default_proposed_versions();

} // namespace versions
} // namespace boltprotocol
```

您可以使用这些预定义的常量（如 `boltprotocol::versions::V5_4`）或构造自己的 `Version` 对象。比较操作符允许您轻松地判断版本的先后。

## 2. 握手与版本协商

连接建立后的第一步是**握手**，其目的是协商后续通信所使用的 Bolt 协议版本。详细的握手过程和 API 使用方法在 `03-handshake-and-chunking.md` 中有详细描述。

关键函数是 `boltprotocol::perform_handshake()`，它会：
1.  接收客户端希望提议的版本列表 (`std::vector<boltprotocol::versions::Version>`)。
2.  向服务器发送握手请求。
3.  接收服务器的响应。
4.  将服务器选择的、双方共同支持的版本填充到输出参数 `boltprotocol::versions::Version& out_negotiated_version` 中。

这个 `out_negotiated_version` **至关重要**，因为库中的许多后续操作（特别是消息的序列化和反序列化）都需要这个版本信息来确保与服务器的兼容性。

## 3. 版本对消息结构和行为的影响

Bolt 协议的许多方面都会随版本而演变。了解这些差异并使用协商得到的版本信息是正确实现 Bolt 通信的基础。本库在内部处理了许多这样的版本差异，但需要您在调用相关函数时提供正确的版本上下文。

主要受版本影响的方面包括：

*   **消息结构与字段**:
    *   **`HELLO` 消息**:
        *   Bolt 5.1 之前：认证信息（scheme, principal, credentials）在 HELLO 的 `extra` map 中。
        *   Bolt 5.1 及之后：认证信息移至单独的 `LOGON` 消息。
        *   Bolt 4.1+：`extra` map 中可包含 `routing` 字典。
        *   Bolt 4.3-4.4：`extra` map 中可包含 `patch_bolt` 列表 (如 `"utc"`)。
        *   Bolt 5.2+：`extra` map 中可包含 `notifications_minimum_severity` 和 `notifications_disabled_categories`。
        *   Bolt 5.3+：`extra` map 中**必须**包含 `bolt_agent` 字典。
    *   **`RUN` 和 `BEGIN` 消息**: 其 `extra` map 中可包含的字段（如 `db`, `imp_user`, `tx_timeout`, `mode`, `bookmarks`, `notifications_...`）在不同 Bolt 版本中被引入。
    *   **`PULL` 和 `DISCARD` 消息**:
        *   Bolt 4.0 之前：被称为 `PULL_ALL` 和 `DISCARD_ALL`，其 PackStream Structure 没有字段。
        *   Bolt 4.0 及之后：重命名为 `PULL` 和 `DISCARD`，其 PackStream Structure 包含一个 `extra` map 字段，该 map 包含 `n` 和 `qid` 参数。
    *   **`ROUTE` 消息**:
        *   Bolt 4.3：PackStream Structure 的第三个字段是 `db` (String 或 Null)。
        *   Bolt 4.4 及之后：PackStream Structure 的第三个字段是一个 `extra` Map (可包含 `db`, `imp_user`)。
    *   **`LOGON`, `LOGOFF`, `TELEMETRY`**: 这些消息是在特定 Bolt 版本之后才引入的。

*   **PackStream 特定结构体字段**:
    *   例如，`BoltNode`, `BoltRelationship`, `BoltUnboundRelationship` 结构体中的 `element_id` 相关字段是在 Bolt 5.0 中添加的。
    *   `BoltDateTime` 和 `BoltDateTimeZoneId` 结构：Bolt 5.0 引入了新的、修正了问题的结构 (标签 'I', 'i')，取代了旧的遗留结构 (标签 'F', 'f')。Bolt 4.4 版本可以通过协商 `"utc"` patch 来使用新的结构。

*   **服务器行为**:
    *   例如，服务器对 `ROUTE` 消息的响应（特别是 `rt` 路由表中的 `db` 字段）在 Bolt 4.4 中有变化。
    *   服务器对 `SUCCESS` 和 `FAILURE` 消息 `metadata` 中返回的字段也随版本演变（例如 `FAILURE` 中的 `gql_status` 在 Bolt 5.7+ 引入）。

## 4. 在 BoltProtocol 库中使用版本信息

为了处理上述版本差异，本库的许多 API 都要求传递协商得到的 `boltprotocol::versions::Version` 对象。

*   **客户端消息序列化**:
    *   `serialize_hello_message(params, writer, client_target_version)`: `client_target_version` 指示客户端希望或能够支持的最高版本，用于构造符合该版本预期的 HELLO 消息。
    *   `serialize_run_message(params, writer, negotiated_version)`
    *   `serialize_begin_message(params, writer, negotiated_version)`
    *   `serialize_route_message(params, writer, negotiated_version)`
    *   这些函数会使用 `negotiated_version` 来决定序列化哪些可选字段或采用哪种结构。

*   **服务器端请求消息反序列化**:
    *   `deserialize_hello_message_request(reader, out_params, server_negotiated_version)`
    *   `deserialize_run_message_request(reader, out_params, server_negotiated_version)`
    *   `deserialize_begin_message_request(reader, out_params, server_negotiated_version)`
    *   `deserialize_pull_message_request(reader, out_params, server_negotiated_version)`
    *   `deserialize_discard_message_request(reader, out_params, server_negotiated_version)`
    *   `deserialize_route_message_request(reader, out_params, server_negotiated_version)`
    *   这些函数使用 `server_negotiated_version` 来正确解析和填充参数结构体。

*   **特定 PackStream 结构转换**:
    *   `from_packstream(pss, out_node, bolt_version)`
    *   `to_packstream(node, bolt_version, out_pss)`
    *   `to_packstream(datetime, bolt_version, utc_patch_active_for_4_4, out_pss)`
    *   这些函数使用版本信息（以及 `utc_patch_active_for_4_4` 标志）来处理结构字段的版本差异和 DateTime/DateTimeZoneId 的现代/遗留格式选择。

## 5. 上层应用的职责

使用本 BoltProtocol 库的上层应用（如驱动程序或服务器）需要：

1.  **执行握手**: 在连接建立后立即调用 `perform_handshake`。
2.  **存储协商版本**: 保存 `perform_handshake` 返回的 `negotiated_version`。
3.  **传递版本信息**: 在调用本库中依赖版本的序列化、反序列化或结构转换函数时，正确传递存储的 `negotiated_version`。
4.  **处理服务器能力**: 对于 `HELLO` 的 `SUCCESS` 响应，解析其 `metadata` 中的 `patch_bolt` 列表（以确定如 "utc" patch 是否激活）和 `hints`/`configuration_hints`（以了解服务器的特定能力，如对 ROUTE V2 上下文的支持）。这些信息将指导上层应用如何构造后续消息的参数（例如，如何填充 `RouteMessageParams` 的 `routing_table_context`，或在调用 `to_packstream` 时传递 `utc_patch_active_for_4_4` 标志）。

## 总结

Bolt 协议的版本控制是其核心特性之一。本库通过要求在关键 API 中传递协商版本信息，并在内部逻辑中根据此版本进行调整，来支持与不同版本 Bolt 服务器的兼容通信。正确管理和使用版本信息是上层应用程序确保互操作性的关键。
```