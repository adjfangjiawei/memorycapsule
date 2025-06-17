# BoltProtocol 库简介

欢迎使用 BoltProtocol 库！这是一个C++实现的底层库，用于处理 Neo4j Bolt 协议的编码、解码和核心交互逻辑。它旨在为构建功能完善的 Neo4j C++ 驱动程序或直接与 Bolt 协议交互的工具提供坚实的基础。

## 目标

*   **协议合规性**: 严格遵循 Neo4j Bolt 协议规范的各个版本。
*   **灵活性**: 提供底层的序列化和反序列化原语，同时也支持强类型的特定数据结构。
*   **可移植性**: 依赖标准 C++。
*   **错误处理**: 提供清晰的错误码和状态反馈。

## 主要功能概述

本库提供以下核心功能：

*   **PackStream 序列化与反序列化**:
    *   支持 Bolt 协议使用的 PackStream V1 数据格式的所有类型（Null, Boolean, Integer, Float, String, List, Map, Structure）。
    *   通过 `boltprotocol::Value` 类型在 C++ 中统一表示 PackStream 数据。
*   **Bolt 握手 (Handshake)**:
    *   实现客户端与服务器之间的 Bolt 协议版本协商。
    *   提供 `boltprotocol::versions::Version` 结构体表示版本。
    *   函数 `boltprotocol::perform_handshake` 执行完整握手流程。
*   **消息分块 (Chunking)**:
    *   通过 `boltprotocol::ChunkedWriter` 将完整的 Bolt 消息分块写入输出流。
    *   通过 `boltprotocol::ChunkedReader` 从输入流读取分块数据并重组为完整消息。
*   **Bolt 消息定义与处理**:
    *   **消息标签**: `boltprotocol::MessageTag` 枚举定义了所有标准 Bolt 消息类型。
    *   **消息参数结构体**: 为每种 Bolt 消息（如 HELLO, RUN, SUCCESS 等）定义了相应的 C++ `struct` (例如 `boltprotocol::HelloMessageParams`, `boltprotocol::RunMessageParams`) 来表示其参数。这些结构体通常包含 `std::string`, `int64_t`, `std::optional`, `std::vector`, `std::map<std::string, boltprotocol::Value>` 等成员。
    *   **客户端消息序列化**: 提供函数将 C++ 参数结构体序列化为 Bolt 消息字节流 (例如 `boltprotocol::serialize_hello_message`)。
    *   **服务器响应反序列化**: 提供函数将服务器响应字节流反序列化为 C++ 参数结构体 (例如 `boltprotocol::deserialize_success_message`)。
    *   **服务器端请求反序列化**: 提供函数供服务器解析客户端请求字节流 (例如 `boltprotocol::deserialize_hello_message_request`)。
*   **特定 PackStream 结构类型支持**:
    *   为常见的图元（Node, Relationship, Path）和数据类型（Date, Time, DateTime, Point 等）定义了强类型的 C++ `struct` (例如 `boltprotocol::BoltNode`, `boltprotocol::BoltDate`)。
    *   提供转换函数在这些强类型结构与通用的 `boltprotocol::PackStreamStructure` 之间进行转换 (例如 `boltprotocol::from_packstream`, `boltprotocol::to_packstream`)。

## 如何开始

要开始使用 BoltProtocol 库，您通常需要：

1.  **包含主头文件**:
    *   `#include "boltprotocol/message_defs.h"`: 这是最主要的聚合头文件，它包含了：
        *   核心数据类型 (`Value`, `BoltMap`, `BoltList`, `PackStreamStructure`)
        *   错误枚举 (`BoltError`)
        *   版本结构 (`versions::Version`) 和相关常量
        *   消息标签 (`MessageTag`)
        *   所有消息参数结构体 (`HelloMessageParams` 等)
        *   特定结构类型 (`BoltNode`, `BoltDate` 等，通过包含 `bolt_structure_types.h`)
    *   `#include "boltprotocol/message_serialization.h"`: 获取所有消息序列化和反序列化函数的声明。
    *   `#include "boltprotocol/handshake.h"`: 获取握手相关函数的声明。
    *   `#include "boltprotocol/chunking.h"`: 获取分块读写器的声明。
    *   `#include "boltprotocol/bolt_structure_serialization.h"`: 获取特定 PackStream 结构类型（如 `BoltNode`）与通用 `PackStreamStructure` 之间转换函数的声明。
2.  **理解核心概念**: 熟悉 PackStream、Bolt 消息、握手和分块机制（详见本文档后续章节）。
3.  **查阅后续文档**: 详细了解如何使用特定功能，如消息序列化、反序列化、特定结构转换和错误处理。

## 模块结构概览 (供库开发者参考)

为了方便维护和理解，本库的内部实现分布在不同的目录和文件中：

*   **`Include/boltprotocol/`**: 包含所有公共头文件。
    *   `detail/`: 包含库内部使用的辅助头文件，不应被库用户直接包含。
    *   `message_defs.h`: 聚合了核心类型定义。
    *   其他 `.h` 文件对应各个功能模块。
*   **`Source/`**: 包含所有 `.cpp` 实现文件。
    *   文件按功能模块组织 (例如 `packstream_reader_*.cpp`, `message_serialization_client_*.cpp`, `bolt_structure_*.cpp`)。

让我们深入了解核心概念，开始构建您的 Bolt 应用！