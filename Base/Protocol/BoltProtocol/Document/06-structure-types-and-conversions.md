好的，这是第六个文档文件 `06-structure-types-and-conversions.md` 的完整内容。

---

**`Base/Protocol/BoltProtocol/Document/06-structure-types-and-conversions.md`**

```markdown
# 特定 PackStream 结构类型与转换

Bolt 协议不仅使用 PackStream 传输顶层的请求和响应消息，还在这些消息的字段中（例如，`RECORD` 消息的数据，或 `RUN` 消息的参数）使用 PackStream Structure 来表示图数据库中的核心实体（如节点、关系、路径）以及特定的数据类型（如日期、时间、空间点等）。

为了方便在 C++ 中以类型安全和面向对象的方式操作这些常见的结构化数据，BoltProtocol 库提供了：

1.  **强类型的 C++ 结构体**: 为每种标准的 PackStream Structure 定义了对应的 C++ `struct`。
2.  **转换函数**: 用于在通用的 `boltprotocol::PackStreamStructure` (通常通过 `std::shared_ptr` 持有，并包含在 `boltprotocol::Value` 中) 与这些强类型 C++ 结构体之间进行双向转换。

所有强类型结构体定义在 `#include "boltprotocol/bolt_structure_types.h"` (它被聚合头文件 `boltprotocol/message_defs.h` 包含)。
所有转换函数声明在 `#include "boltprotocol/bolt_structure_serialization.h"`。

## 1. 支持的强类型 C++ 结构体

以下是本库目前支持的强类型 C++ 结构体及其对应的 PackStream 标签和关键成员。请注意，某些结构的字段会随 Bolt 版本变化（例如 `element_id` 在 Bolt 5.0+ 中为图元添加）。转换函数会考虑这些版本差异。

### 图元 (Graph Primitives)

*   **`boltprotocol::BoltNode`** (PackStream Tag: `0x4E 'N'`)
    *   `int64_t id;`
    *   `std::vector<std::string> labels;`
    *   `std::map<std::string, Value> properties;`
    *   `std::optional<std::string> element_id;` (Bolt 5.0+)

*   **`boltprotocol::BoltRelationship`** (PackStream Tag: `0x52 'R'`)
    *   `int64_t id;`
    *   `int64_t start_node_id;`
    *   `int64_t end_node_id;`
    *   `std::string type;`
    *   `std::map<std::string, Value> properties;`
    *   `std::optional<std::string> element_id;` (Bolt 5.0+)
    *   `std::optional<std::string> start_node_element_id;` (Bolt 5.0+)
    *   `std::optional<std::string> end_node_element_id;` (Bolt 5.0+)

*   **`boltprotocol::BoltUnboundRelationship`** (PackStream Tag: `0x72 'r'`)
    *   *用于 `BoltPath` 内部，表示不含端点的关系。*
    *   `int64_t id;`
    *   `std::string type;`
    *   `std::map<std::string, Value> properties;`
    *   `std::optional<std::string> element_id;` (Bolt 5.0+)

*   **`boltprotocol::BoltPath`** (PackStream Tag: `0x50 'P'`)
    *   `std::vector<BoltNode> nodes;`
    *   `std::vector<BoltUnboundRelationship> rels;`
    *   `std::vector<int64_t> indices;` (描述路径如何由节点和关系构成)

### 时间类型 (Temporal Types)

*   **`boltprotocol::BoltDate`** (PackStream Tag: `0x44 'D'`)
    *   `int64_t days_since_epoch;` (自 Unix 纪元以来的天数)

*   **`boltprotocol::BoltTime`** (PackStream Tag: `0x54 'T'`)
    *   `int64_t nanoseconds_since_midnight;` (相对于给定偏移量的午夜以来的纳秒数)
    *   `int32_t tz_offset_seconds;` (距 UTC 的秒数偏移量)

*   **`boltprotocol::BoltLocalTime`** (PackStream Tag: `0x74 't'`)
    *   `int64_t nanoseconds_since_midnight;` (本地午夜以来的纳秒数，无时区信息)

*   **`boltprotocol::BoltDateTime`** (PackStream Tag: `0x49 'I'` (现代) / `0x46 'F'` (遗留))
    *   `int64_t seconds_epoch_utc;` (自 Unix 纪元以来的 UTC 秒数)
    *   `int32_t nanoseconds_of_second;` (秒内的纳秒部分, 0-999,999,999)
    *   `int32_t tz_offset_seconds;` (原始时间的 UTC 秒数偏移量)

*   **`boltprotocol::BoltDateTimeZoneId`** (PackStream Tag: `0x69 'i'` (现代) / `0x66 'f'` (遗留))
    *   `int64_t seconds_epoch_utc;` (自 Unix 纪元以来的 UTC 秒数。**注意**: 对于从遗留 'f' 格式反序列化，此字段可能包含已调整的秒数，而非纯 UTC，需上层结合 TZDB 处理。)
    *   `int32_t nanoseconds_of_second;`
    *   `std::string tz_id;` (时区标识符，如 "Europe/Paris")

*   **`boltprotocol::BoltLocalDateTime`** (PackStream Tag: `0x64 'd'`)
    *   `int64_t seconds_epoch_local;` (自 Unix 纪元以来的秒数，解释为本地日期时间)
    *   `int32_t nanoseconds_of_second;`

*   **`boltprotocol::BoltDuration`** (PackStream Tag: `0x45 'E'`)
    *   `int64_t months;`
    *   `int64_t days;`
    *   `int64_t seconds;`
    *   `int32_t nanoseconds;` (秒的纳秒调整部分)

### 空间类型 (Spatial Types)

*   **`boltprotocol::BoltPoint2D`** (PackStream Tag: `0x58 'X'`)
    *   `uint32_t srid;` (空间参考系统标识符)
    *   `double x;`
    *   `double y;`

*   **`boltprotocol::BoltPoint3D`** (PackStream Tag: `0x59 'Y'`)
    *   `uint32_t srid;`
    *   `double x;`
    *   `double y;`
    *   `double z;`

## 2. 转换函数

这些函数用于在通用的 `boltprotocol::PackStreamStructure` 和上述强类型 C++ 结构体之间进行转换。

### A. 从 `PackStreamStructure` 转换为强类型 (`from_packstream`)

这些函数通常在接收到包含特定结构的数据后（例如，在 `RECORD` 消息的字段中）调用。

*   **通用函数签名模式**:
    ```cpp
    boltprotocol::BoltError from_packstream(
        const boltprotocol::PackStreamStructure& pss, 
        TypedStruct& out_struct, 
        /* 可选参数: const boltprotocol::versions::Version& bolt_version (用于版本依赖的结构) */
    );
    ```
    *   `pss`: 输入的、从 PackStream 反序列化得到的通用结构。
    *   `out_struct`: 输出参数，用于填充转换后的强类型结构数据。
    *   `bolt_version`: 对于字段随 Bolt 版本变化的结构（如 `BoltNode`, `BoltRelationship`, `BoltDateTime`），需要提供此参数。
    *   **返回值**: `BoltError::SUCCESS` 或错误码（如 `BoltError::INVALID_MESSAGE_FORMAT` 如果标签或字段不匹配）。

*   **示例函数**:
    *   `from_packstream(const PackStreamStructure& pss, BoltNode& out_node, const versions::Version& bolt_version);`
    *   `from_packstream(const PackStreamStructure& pss, BoltDate& out_date);`
    *   `from_packstream(const PackStreamStructure& pss, BoltDateTime& out_datetime, const versions::Version& bolt_version);`
    *   ... (其他类型的对应函数)

### B. 将强类型转换为 `PackStreamStructure` (`to_packstream`)

这些函数用于客户端需要构造特定类型的结构（例如，作为 Cypher 查询的参数）并将其发送给服务器的场景。

*   **通用函数签名模式**:
    ```cpp
    boltprotocol::BoltError to_packstream(
        const TypedStruct& typed_struct, 
        /* 可选参数: const boltprotocol::versions::Version& bolt_version, */
        /* 可选参数: bool utc_patch_active_for_4_4, (仅用于 BoltDateTime/ZoneId 和 Bolt 4.4) */
        std::shared_ptr<PackStreamStructure>& out_pss_sptr 
    );
    ```
    *   `typed_struct`: 输入的强类型 C++ 结构体实例。
    *   `bolt_version`, `utc_patch_active_for_4_4`: 用于确保序列化为目标 Bolt 版本兼容的格式，特别是对于 DateTime 和 DateTimeZoneId。
    *   `out_pss_sptr`: 输出参数，将被填充为一个新创建的、包含序列化数据的 `std::shared_ptr<PackStreamStructure>`。这个 `shared_ptr` 可以包装在 `Value` 中用于更高层的消息构造。
    *   **返回值**: `BoltError::SUCCESS` 或错误码。

*   **示例函数**:
    *   `to_packstream(const BoltNode& node, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);`
    *   `to_packstream(const BoltDate& date, std::shared_ptr<PackStreamStructure>& out_pss);`
    *   `to_packstream(const BoltDateTime& datetime, const versions::Version& bolt_version, bool utc_patch_active_for_4_4, std::shared_ptr<PackStreamStructure>& out_pss);`
    *   ... (其他类型的对应函数)

### C. 从 `Value` 到强类型的便捷模板 (`value_to_typed_struct`)

为了简化从一个通用的 `boltprotocol::Value`（它可能持有一个 `std::shared_ptr<PackStreamStructure>`）到强类型结构的转换，提供了一个模板函数：

*   **函数签名**:
    ```cpp
    // 主要版本，用于需要版本和/或补丁信息的类型
    template<typename T_StrongType>
    boltprotocol::BoltError value_to_typed_struct(
        const boltprotocol::Value& value, 
        T_StrongType& out_typed_struct, 
        const boltprotocol::versions::Version& bolt_version, 
        bool utc_patch_active_for_4_4 = false // 默认为false
    );

    // 重载版本，用于不需要版本或补丁信息的简单类型 (如 BoltDate)
    template<typename T_StrongType>
    boltprotocol::BoltError value_to_typed_struct(
        const boltprotocol::Value& value, 
        T_StrongType& out_typed_struct
    );
    ```
*   **行为**:
    1.  检查 `value` 是否确实持有一个 `std::shared_ptr<PackStreamStructure>`。
    2.  如果是，并且指针非空，则调用相应的 `from_packstream` 重载。
    3.  返回 `from_packstream` 的结果。
*   **使用示例**:
    ```cpp
    // 假设 record_field 是从 RecordMessageParams::fields 中获取的一个 Value
    // boltprotocol::Value record_field = ...;
    // boltprotocol::versions::Version current_bolt_version = ...;
    // bool is_utc_patch_active_for_4_4 = ...; // (例如从会话状态获取)

    boltprotocol::BoltNode node;
    boltprotocol::BoltError err_node = boltprotocol::value_to_typed_struct(record_field, node, current_bolt_version);
    if (err_node == boltprotocol::BoltError::SUCCESS) {
        // 使用 node
    }

    boltprotocol::BoltDate date_obj;
    boltprotocol::BoltError err_date = boltprotocol::value_to_typed_struct(record_field, date_obj); // Date转换不需要版本
    if (err_date == boltprotocol::BoltError::SUCCESS) {
        // 使用 date_obj
    }

    boltprotocol::BoltDateTime datetime_obj;
    // DateTime 转换需要版本，并且如果 bolt_version 是 4.4，还需要 utc_patch_active_for_4_4
    boltprotocol::BoltError err_dt = boltprotocol::value_to_typed_struct(record_field, datetime_obj, current_bolt_version, is_utc_patch_active_for_4_4);
    if (err_dt == boltprotocol::BoltError::SUCCESS) {
        // 使用 datetime_obj (注意，from_packstream for DateTime/ZoneId 本身是基于标签的，utc_patch主要影响to_packstream)
        // 但提供版本信息给from_packstream总是个好习惯，以防未来有更细微的版本依赖。
    }
    ```

## 版本和补丁的注意事项

*   **图元版本**: `BoltNode`, `BoltRelationship`, `BoltUnboundRelationship` 的字段（特别是 `element_id` 相关的）在 Bolt 5.0 中有变化。它们的转换函数需要 `bolt_version` 参数。
*   **DateTime 和 DateTimeZoneId**:
    *   Bolt 5.0 引入了新的结构 (tag 'I' 和 'i') 取代了遗留的结构 (tag 'F' 和 'f')。
    *   Bolt 4.4 版本可以通过 `HELLO` 消息协商 `"utc"` 补丁来使用新的（现代的）结构。
    *   `from_packstream` 函数会根据读取到的 PackStream Structure 的**标签**来决定按现代还是遗留格式解析。
    *   `to_packstream` 函数则需要 `bolt_version` 和 `utc_patch_active_for_4_4` (当版本为4.4时) 参数来决定序列化为哪种格式（现代还是遗留）。
    *   **遗留 `DateTimeZoneId` (tag 'f') 的复杂性**: 将其 `seconds` 字段（它是本地调整过的秒数）正确转换为纯 UTC 秒数，或者从纯 UTC 秒数和 `tz_id` 正确构造出遗留格式所需的本地调整秒数，都需要访问时区数据库 (TZDB)。本库不包含 TZDB，因此在处理遗留 `DateTimeZoneId` 时存在固有的不精确性，相关函数实现中已包含注释说明。

通过使用这些强类型结构和转换函数，可以极大地简化应用程序与 Bolt 协议中这些常见数据结构交互的复杂性，并提高代码的类型安全性和可读性。
```

---

**7. `Base/Protocol/BoltProtocol/Document/07-error-handling.md`** (已在上一轮提供，保持不变)
**8. `Base/Protocol/BoltProtocol/Document/08-versioning.md`** (已在上一轮提供，保持不变)

这些文档应该能让用户在不直接查阅头文件的情况下，理解如何使用库的核心功能，包括特定结构类型的处理。