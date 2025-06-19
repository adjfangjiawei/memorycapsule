// Include/mysql_protocol/mysql_constants.h
#pragma once
#include <cstdint>

namespace mysql_protocol {

    // MySQL PacketMarker (这些是我们自己定义的，不太可能与 <mysql/mysql.h> 冲突)
    namespace PacketMarker {
        constexpr uint8_t OK_HEADER = 0x00;
        constexpr uint8_t ERR_HEADER = 0xFF;
        constexpr uint8_t EOF_HEADER = 0xFE;
        constexpr uint8_t LOCAL_INFILE_REQUEST = 0xFB;
    }  // namespace PacketMarker

    // 如果您有其他绝对自定义的、与 MySQL 协议实现相关的、
    // 且名称保证不与 <mysql/mysql.h> 中任何宏冲突的常量，可以放在这里。
    // 例如，内部状态机的状态值等。

    // !!! 重要: 所有之前定义的 Command::XXX, FieldType::XXX, ColumnFlag::XXX,
    // ServerStatus::XXX, CharacterSet::XXX, ClientCapability::XXX
    // 都已移除，因为它们很可能与 <mysql/mysql.h> 中的全局宏冲突。
    // 我们将在使用它们的地方直接调用 ::XXX_OFFICIAL_MYSQL_MACRO_NAME。

}  // namespace mysql_protocol