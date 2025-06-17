好的，这是第七个文档文件 `07-error-handling.md` 的完整内容。

---

**`Base/Protocol/BoltProtocol/Document/07-error-handling.md`**

```markdown
# BoltProtocol 库错误处理

BoltProtocol 库采用基于返回值的错误处理机制，以确保与各种项目（包括不使用或有特定异常处理策略的项目）的良好集成性。所有可能失败的操作都会返回一个 `boltprotocol::BoltError` 枚举类型的值。

## 1. `boltprotocol::BoltError` 枚举

`BoltError` 枚举定义了库中可能发生的各种错误情况。它在头文件 `boltprotocol/bolt_errors_versions.h` 中定义（该头文件由聚合头文件 `boltprotocol/message_defs.h` 包含）。

以下是一些关键的错误码及其含义：

*   **`BoltError::SUCCESS` (值为 0)**:
    *   表示操作成功完成，没有发生错误。

*   **通用错误**:
    *   `BoltError::UNKNOWN_ERROR`: 发生了未分类或意外的错误。
    *   `BoltError::INVALID_ARGUMENT`: 提供给函数的参数无效（例如，空指针、超出范围的值）。
    *   `BoltError::OUT_OF_MEMORY`: 在操作过程中无法分配所需的内存。

*   **序列化/反序列化错误**:
    *   `BoltError::SERIALIZATION_ERROR`: 将 C++ 对象序列化为 PackStream 字节流时出错（例如，字符串过长、数据结构不符合规范）。
    *   `BoltError::DESERIALIZATION_ERROR`: 从 PackStream 字节流反序列化为 C++ 对象时出错（例如，数据格式损坏、意外的字节序列、数据不完整）。
    *   `BoltError::INVALID_MESSAGE_FORMAT`: 接收到的消息的 PackStream Structure 不符合预期（例如，消息标签错误、字段数量不正确、字段类型不匹配）。
    *   `BoltError::RECURSION_DEPTH_EXCEEDED`: 在解析或序列化深度嵌套的 PackStream 结构（如 List, Map, Structure）时，超出了库设定的最大递归深度，以防止栈溢出。
    *   `BoltError::MESSAGE_TOO_LARGE`: 尝试处理的消息（或其组成部分，如字符串）的大小超出了实现或协议的限制。

*   **网络与连接错误**:
    *   `BoltError::NETWORK_ERROR`: 底层网络流操作（读/写）失败。这可能表示连接已关闭、网络中断或其他 I/O 问题。

*   **握手 (Handshake) 错误**:
    *   `BoltError::HANDSHAKE_FAILED`: 握手过程总体失败。
    *   `BoltError::HANDSHAKE_NO_COMMON_VERSION`: 服务器不支持客户端提议的任何 Bolt 协议版本。
    *   `BoltError::HANDSHAKE_MAGIC_MISMATCH`: （理论上，如果实现此检查）客户端发送的魔法序列与 Bolt 协议不符。
    *   `BoltError::UNSUPPORTED_PROTOCOL_VERSION`: 服务器返回了一个无法识别或不支持的协议版本格式。

*   **分块 (Chunking) 错误**:
    *   `BoltError::CHUNK_TOO_LARGE`: （理论上）单个数据块声明的大小超过了允许的最大值 (65535 字节)。
    *   `BoltError::CHUNK_ENCODING_ERROR`: 在将消息编码为分块时发生内部错误。
    *   `BoltError::CHUNK_DECODING_ERROR`: 在从分块数据重组消息时发生错误（例如，块大小与实际数据不符）。

## 2. 检查函数返回值

库中绝大多数执行实际操作的函数（如序列化、反序列化、握手、分块读写）都会返回一个 `BoltError` 类型的值。**使用者必须检查这些函数的返回值，以确定操作是否成功。**

**标准用法**:
```cpp
#include "boltprotocol/message_defs.h" // For BoltError
#include <iostream>

// 假设 some_bolt_operation 是库中的一个函数
// boltprotocol::BoltError result = some_bolt_operation(args...);

// if (result != boltprotocol::BoltError::SUCCESS) {
//     std::cerr << "Bolt operation failed with error code: " 
//               << static_cast<int>(result) << std::endl;
//     // 在此根据具体的 result 值采取相应的错误处理措施，
//     // 例如记录日志、重试、关闭连接或向上传递错误。
// } else {
//     // 操作成功，可以继续。
// }
```

## 3. `PackStreamReader` 和 `PackStreamWriter` 的内部错误状态

`PackStreamReader` 和 `PackStreamWriter` 对象（以及 `ChunkedReader` 和 `ChunkedWriter`）内部维护一个错误状态。一旦在其操作过程中发生错误，它们的内部错误状态会被设置。后续对同一对象的读/写操作通常会立即失败，并返回之前记录的错误码（或一个新的相关错误码）。

您可以使用以下成员函数来查询这些对象的错误状态：

*   **`bool has_error() const;`**:
    *   如果对象内部已记录错误，则返回 `true`，否则返回 `false`。
*   **`boltprotocol::BoltError get_error() const;`**:
    *   返回对象内部记录的 `BoltError` 值。如果 `has_error()` 为 `false`，则此函数返回 `BoltError::SUCCESS`。

**使用示例**:
```cpp
#include "boltprotocol/packstream_writer.h"
#include "boltprotocol/message_defs.h" // For Value, BoltError
#include <vector>
#include <string>
#include <iostream>

// std::vector<uint8_t> buffer;
// boltprotocol::PackStreamWriter writer(buffer);
// boltprotocol::Value val1(std::string("test"));
// boltprotocol::Value val2(12345LL);

// writer.write(val1); // 假设第一次写入成功
// writer.write(val2); // 假设第二次写入也成功

// // 可以在一系列操作后检查最终状态
// if (writer.has_error()) {
//     boltprotocol::BoltError final_error = writer.get_error();
//     std::cerr << "PackStreamWriter encountered an error during operations: " 
//               << static_cast<int>(final_error) << std::endl;
// } else {
//     std::cout << "All PackStream writes successful." << std::endl;
// }
```
虽然每个单独的 `write` 或 `read` 操作都会返回错误码，但在执行了一系列操作后检查对象的整体错误状态也是一种有用的模式。

## 4. 关于 C++ 异常

BoltProtocol 库的设计目标是**不主动向上层调用者抛出 C++ 异常**。它通过返回 `BoltError` 枚举值来报告所有已知的操作失败。

然而，库的内部实现依赖于标准 C++ 库，例如 `std::vector`, `std::string`, `std::map` 的内存分配，以及流操作。在极端情况下（例如系统内存耗尽），这些标准库组件可能会抛出异常（最常见的是 `std::bad_alloc`）。

*   **库内部的异常捕获**: 本库会尽力在其内部实现中捕获由标准库操作（特别是内存分配）直接引起的常见异常，如 `std::bad_alloc`，并将其转换为相应的 `BoltError`（例如 `BoltError::OUT_OF_MEMORY`）返回给调用者。
*   **未捕获的异常**: 如果发生了库未能预见或捕获的异常，或者异常源于用户代码（例如，在提供给库的回调函数中，尽管本库目前不采用回调模式），则这些异常可能会传播到库的调用者之外。应用程序应具备处理此类情况的常规异常处理机制。

总而言之，您应该主要依赖检查 `BoltError` 返回值来处理本库的错误，而不是期望捕获来自本库的特定 C++ 异常。

## 5. 错误恢复策略 (上层应用的职责)

本库提供错误信息，但具体的错误恢复策略由使用本库的上层应用程序或驱动程序来决定。

*   **可恢复错误**: 某些错误可能是暂时的（例如，某些类型的 `BoltError::NETWORK_ERROR` 可能通过重试解决）。
*   **致命错误**: 许多错误，特别是与协议格式、序列化/反序列化相关的错误（如 `BoltError::INVALID_MESSAGE_FORMAT`, `BoltError::DESERIALIZATION_ERROR`）或严重的连接问题，通常被认为是致命的，至少对于当前的 Bolt 会话/连接而言。在这种情况下，推荐的策略通常是：
    1.  记录详细的错误信息。
    2.  安全地关闭当前的 Bolt 连接。
    3.  （可选）尝试建立一个新的连接和会话。
    4.  向上层或用户报告错误。

## 总结

*   **检查返回值**: 这是使用 BoltProtocol 库时最重要的错误处理步骤。
*   **理解错误码**: 熟悉 `BoltError` 枚举中不同值的含义。
*   **读写器状态**: 可以使用 `has_error()` 和 `get_error()` 检查读写器对象的累积错误状态。
*   **异常**: 库本身不抛出异常，但依赖的标准库在极端情况下可能抛出。

通过仔细处理 `BoltError` 返回值，您可以构建出能够稳健处理各种通信和数据处理问题的应用程序。
```