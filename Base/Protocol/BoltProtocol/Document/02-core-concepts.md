# BoltProtocol 核心概念

理解 Bolt 协议的几个核心概念对于有效使用本库至关重要。

## 1. PackStream

PackStream 是 Bolt 协议用于在网络上传输数据的一种二进制序列化格式。它支持多种数据类型，本库对这些类型提供了相应的 C++ 表示。

### C++ 中的 PackStream 类型表示

所有 PackStream 类型最终都可以通过 `boltprotocol::Value` 来表示。这是一个 `std::variant`，能够持有以下C++类型，对应不同的 PackStream 类型：

*   **`std::nullptr_t`**:
    *   对应 PackStream `Null`。
*   **`bool`**:
    *   对应 PackStream `Boolean` (`true` 或 `false`)。
*   **`int64_t`**:
    *   对应所有 PackStream `Integer` 类型 (TinyInt, Int8, Int16, Int32, Int64)。库会自动处理不同大小整数的编码和解码。
*   **`double`**:
    *   对应 PackStream `Float` (64位浮点数)。
*   **`std::string`**:
    *   对应 PackStream `String` (UTF-8 编码)。
*   **`std::shared_ptr<boltprotocol::BoltList>`**:
    *   对应 PackStream `List`。
    *   `boltprotocol::BoltList` 结构体包含一个成员：
        *   `std::vector<boltprotocol::Value> elements;`
*   **`std::shared_ptr<boltprotocol::BoltMap>`**:
    *   对应 PackStream `Map`。
    *   `boltprotocol::BoltMap` 结构体包含一个成员：
        *   `std::map<std::string, boltprotocol::Value> pairs;`
*   **`std::shared_ptr<boltprotocol::PackStreamStructure>`**:
    *   对应 PackStream `Structure`。这是 Bolt 协议中用于表示消息和复杂数据类型（如图节点、日期时间等）的基础。
    *   `boltprotocol::PackStreamStructure` 结构体包含成员：
        *   `uint8_t tag;`：一个字节的标签，用于标识该结构的具体类型（例如，消息类型或特定数据如 Node、Date 的类型）。
        *   `std::vector<boltprotocol::Value> fields;`：一个包含该结构所有字段值的列表。

**注意**: 您通常不需要手动创建或解析底层的 `Value`、`BoltList`、`BoltMap` 或 `PackStreamStructure` 对象来构造或解析 Bolt 消息。库提供了更高级别的消息参数结构体 (`XxxMessageParams`) 和针对特定 PackStream 结构（如 `BoltNode`）的强类型 C++ `struct`，以及相应的序列化/反序列化函数来处理这些转换。

## 2. Bolt 消息

Bolt 协议通过客户端和服务器之间的请求-响应消息交换进行通信。

*   **请求消息 (Request Message)**: 由客户端发送给服务器。
*   **响应消息 (Response Message)**: 由服务器发送给客户端。一个完整的响应通常由以下组成：
    *   零个或多个 **详情消息 (Detail Message)**: 例如 `RECORD` 消息，用于流式传输结果数据。
    *   恰好一个 **总结消息 (Summary Message)**: 例如 `SUCCESS` 或 `FAILURE`，标志着请求处理的结束和最终状态。

每条 Bolt 消息（无论是请求还是响应）都被编码为一个 **PackStream Structure**。

### 消息标签 (`boltprotocol::MessageTag`)

`boltprotocol::MessageTag` 是一个 `enum class`，定义了所有标准 Bolt 消息的唯一类型标签 (一个字节的整数)。这个标签值被用作相应消息的 `PackStreamStructure` 的 `tag` 字段。

*   **一些重要的消息标签示例**:
    *   `MessageTag::HELLO` (0x01): 客户端发送，用于初始化连接。
    *   `MessageTag::LOGON` (0x6A): 客户端发送，用于认证 (Bolt 5.1+)。
    *   `MessageTag::RUN` (0x10): 客户端发送，用于执行 Cypher 查询。
    *   `MessageTag::PULL` (0x3F): 客户端发送，用于拉取 `RUN` 查询的结果。
    *   `MessageTag::BEGIN` (0x11): 客户端发送，用于开始一个显式事务。
    *   `MessageTag::COMMIT` (0x12): 客户端发送，用于提交一个显式事务。
    *   `MessageTag::SUCCESS` (0x70): 服务器发送，表示请求成功。
    *   `MessageTag::FAILURE` (0x7F): 服务器发送，表示请求失败。
    *   `MessageTag::RECORD` (0x71): 服务器发送，包含一条结果记录。
    *   `MessageTag::IGNORED` (0x7E): 服务器发送，表示请求被忽略。
    *   ... (更多标签请参见规范或后续文档)

### 消息参数结构体 (`boltprotocol::*MessageParams`)

为了方便在 C++ 中构造和处理消息的参数，本库为大多数 Bolt 消息定义了对应的参数结构体。这些结构体以 `MessageParams` 结尾，例如 `boltprotocol::HelloMessageParams`, `boltprotocol::RunMessageParams`, `boltprotocol::SuccessMessageParams`。

*   这些结构体通常包含与消息字段对应的成员，使用标准的 C++ 类型如 `std::string`, `int64_t`, `std::vector`, `std::map`, 以及 `std::optional` (用于可选字段) 和 `boltprotocol::Value` (用于可变类型字段)。
*   **例如, `boltprotocol::RunMessageParams` 可能包含**:
    *   `std::string cypher_query;`
    *   `std::map<std::string, boltprotocol::Value> parameters;`
    *   `std::optional<std::vector<std::string>> bookmarks;`
    *   `std::optional<std::string> db;`
    *   ... 等等。
*   您可以通过填充这些结构体的成员来准备要发送的消息，或者在反序列化服务器响应（或客户端请求）后从这些结构体中读取参数。

## 3. 特定 PackStream 结构 (图元和数据类型)

除了 Bolt 消息本身，PackStream 还用于在消息字段中序列化 Neo4j 的特定数据类型，例如图节点、关系、路径、日期、时间、空间点等。

本库为这些常见的 PackStream Structure 定义了强类型的 C++ `struct`，例如：

*   `boltprotocol::BoltNode` (对应 PackStream Structure tag `0x4E 'N'`)
*   `boltprotocol::BoltRelationship` (tag `0x52 'R'`)
*   `boltprotocol::BoltPath` (tag `0x50 'P'`)
*   `boltprotocol::BoltDate` (tag `0x44 'D'`)
*   `boltprotocol::BoltTime` (tag `0x54 'T'`)
*   `boltprotocol::BoltDateTime` (tag `0x49 'I'` 或遗留的 `0x46 'F'`)
*   ... 等等。

这些强类型结构及其与通用 `PackStreamStructure` (`Value` 中持有的 `std::shared_ptr<PackStreamStructure>`) 之间的转换函数将在 `06-structure-types-and-conversions.md` 中详细介绍。这些转换函数通常会考虑不同 Bolt 版本对这些结构字段定义的影响。

## 总结

*   Bolt 协议的数据交换基于 **PackStream** 二进制序列化格式。
*   在 C++ 中，通用的 PackStream 数据由 `boltprotocol::Value` (一个 `std::variant`) 表示，它可以包含基本类型、`BoltList`、`BoltMap` 或 `PackStreamStructure`。
*   **Bolt 消息** 本质上是带有特定 `MessageTag` 的 `PackStreamStructure`。
*   使用库定义的 **`XxxMessageParams`** 结构体来方便地构造和访问消息的参数。
*   常见的图元和数据类型也有对应的 **强类型 C++ `struct`** (如 `BoltNode`)，并提供了与通用 `PackStreamStructure` 之间的转换机制。

理解这些核心概念是高效使用本库进行 Bolt 协议编程的基础。