#include <cctype>  // For std::isalpha, std::isalnum
#include <string>
#include <vector>

#include "sqldriver/mysql/mysql_driver_helper.h"

namespace cpporm_sqldriver {
    namespace mysql_helper {

        NamedPlaceholderInfo processQueryForPlaceholders(const std::string& originalQuery, SqlResultNs::NamedBindingSyntax syntax) {
            NamedPlaceholderInfo info;
            info.hasNamedPlaceholders = false;  // 默认没有命名占位符

            if (syntax == SqlResultNs::NamedBindingSyntax::QuestionMark || originalQuery.empty()) {
                info.processedQuery = originalQuery;  // 问号占位符或空查询，无需处理
                return info;
            }

            char placeholder_char_start = 0;
            switch (syntax) {
                case SqlResultNs::NamedBindingSyntax::Colon:
                    placeholder_char_start = ':';
                    break;
                case SqlResultNs::NamedBindingSyntax::AtSign:
                    placeholder_char_start = '@';
                    break;
                default:  // Should not happen if QuestionMark is handled above
                    info.processedQuery = originalQuery;
                    return info;
            }

            std::string& result_query = info.processedQuery;
            result_query.reserve(originalQuery.length());

            char in_quote_char = 0;  // ' или "
            bool after_backslash = false;

            for (size_t i = 0; i < originalQuery.length(); ++i) {
                char current_char = originalQuery[i];

                if (after_backslash) {
                    result_query += current_char;
                    after_backslash = false;
                    continue;
                }

                if (current_char == '\\') {
                    result_query += current_char;
                    after_backslash = true;
                    continue;
                }

                if (in_quote_char != 0) {  // 如果在引号内
                    result_query += current_char;
                    if (current_char == in_quote_char) {
                        in_quote_char = 0;  // 结束引号
                    }
                    continue;
                }

                if (current_char == '\'' || current_char == '"') {
                    result_query += current_char;
                    in_quote_char = current_char;  // 进入引号
                    continue;
                }

                // 检查是否是占位符开始符 (例如 :)
                if (current_char == placeholder_char_start) {
                    // 检查下一个字符是否是合法的占位符名称开头（字母或下划线）
                    if (i + 1 < originalQuery.length() && (std::isalpha(static_cast<unsigned char>(originalQuery[i + 1])) || originalQuery[i + 1] == '_')) {
                        size_t name_start_idx = i + 1;
                        size_t name_end_idx = name_start_idx;
                        while (name_end_idx < originalQuery.length() && (std::isalnum(static_cast<unsigned char>(originalQuery[name_end_idx])) || originalQuery[name_end_idx] == '_')) {
                            name_end_idx++;
                        }

                        if (name_end_idx > name_start_idx) {  // 找到了一个有效的占位符名称
                            std::string param_name = originalQuery.substr(name_start_idx, name_end_idx - name_start_idx);
                            info.orderedParamNames.push_back(param_name);
                            result_query += '?';  // 替换为问号
                            info.hasNamedPlaceholders = true;
                            i = name_end_idx - 1;  // 更新主循环的索引
                            continue;              // 继续下一个字符的处理
                        }
                    }
                }
                // 如果不是占位符的一部分，或者不是特殊字符，则直接追加
                result_query += current_char;
            }

            // 如果在处理后没有发现任何命名占位符，则处理后的查询应与原始查询相同
            if (!info.hasNamedPlaceholders) {
                info.processedQuery = originalQuery;
                info.orderedParamNames.clear();  // 确保为空
            }

            return info;
        }

    }  // namespace mysql_helper
}  // namespace cpporm_sqldriver