// cpporm_sqldriver/sql_connection_parameters.h
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>  // Not directly used here, but often associated with parameter sets

#include "sql_value.h"  // For SqlValue used as map value

namespace cpporm_sqldriver {

    struct ConnectionParameters : public std::map<std::string, SqlValue> {
        // 定义键常量 (声明)
        static const std::string KEY_DRIVER_TYPE;
        static const std::string KEY_DB_NAME;
        static const std::string KEY_USER_NAME;
        static const std::string KEY_PASSWORD;
        static const std::string KEY_HOST_NAME;
        static const std::string KEY_PORT;
        static const std::string KEY_CONNECT_OPTIONS;
        static const std::string KEY_CLIENT_CHARSET;
        static const std::string KEY_APPLICATION_NAME;
        static const std::string KEY_CONNECTION_TIMEOUT_SECONDS;
        static const std::string KEY_READ_TIMEOUT_SECONDS;
        static const std::string KEY_WRITE_TIMEOUT_SECONDS;
        static const std::string KEY_SSL_MODE;
        static const std::string KEY_SSL_CERT_PATH;
        static const std::string KEY_SSL_KEY_PATH;
        static const std::string KEY_SSL_CA_PATH;
        static const std::string KEY_SSL_CIPHER;
        static const std::string KEY_POOL_MAX_SIZE;
        static const std::string KEY_POOL_MIN_SIZE;
        static const std::string KEY_POOL_ACQUIRE_TIMEOUT_MS;
        static const std::string KEY_POOL_CONNECTION_LIFETIME_MS;
        static const std::string KEY_POOL_IDLE_TIMEOUT_MS;

        // Setters (声明)
        void setDriverType(const std::string& v);
        void setDbName(const std::string& v);
        void setUserName(const std::string& v);
        void setPassword(const std::string& v);
        void setHostName(const std::string& v);
        void setPort(int v);
        void setConnectOptions(const std::string& v);
        void setClientCharset(const std::string& v);
        void setApplicationName(const std::string& v);
        void setConnectionTimeoutSeconds(int v);
        void setReadTimeoutSeconds(int v);
        void setWriteTimeoutSeconds(int v);
        void setSslMode(const std::string& v);
        void setSslCertPath(const std::string& v);
        void setSslKeyPath(const std::string& v);
        void setSslCaPath(const std::string& v);
        void setSslCipher(const std::string& v);
        void setPoolMaxSize(int v);
        void setPoolMinSize(int v);
        void setPoolAcquireTimeoutMs(long long v);
        void setPoolConnectionLifetimeMs(long long v);
        void setPoolIdleTimeoutMs(long long v);

        // Getters (声明)
        template <typename T>
        std::optional<T> get(const std::string& key) const;

        std::optional<std::string> driverType() const;
        std::optional<std::string> dbName() const;
        std::optional<std::string> userName() const;
        std::optional<std::string> password() const;
        std::optional<std::string> hostName() const;
        std::optional<int> port() const;
        std::optional<std::string> connectOptions() const;
        std::optional<std::string> clientCharset() const;
        std::optional<std::string> applicationName() const;
        std::optional<int> connectionTimeoutSeconds() const;
        std::optional<int> readTimeoutSeconds() const;
        std::optional<int> writeTimeoutSeconds() const;
        std::optional<std::string> sslMode() const;
        std::optional<std::string> sslCertPath() const;
        std::optional<std::string> sslKeyPath() const;
        std::optional<std::string> sslCaPath() const;
        std::optional<std::string> sslCipher() const;
        std::optional<int> poolMaxSize() const;
        std::optional<int> poolMinSize() const;
        std::optional<long long> poolAcquireTimeoutMs() const;
        std::optional<long long> poolConnectionLifetimeMs() const;
        std::optional<long long> poolIdleTimeoutMs() const;
    };

    template <typename T>
    std::optional<T> ConnectionParameters::get(const std::string& key) const {
        auto it = find(key);
        if (it != end() && !it->second.isNull()) {
            bool ok = false;
            T result{};
            if constexpr (std::is_same_v<T, std::string>) {
                result = it->second.toString(&ok);
            } else if constexpr (std::is_same_v<T, int>) {
                result = it->second.toInt32(&ok);
            } else if constexpr (std::is_same_v<T, unsigned int>) {
                result = it->second.toUInt32(&ok);
            } else if constexpr (std::is_same_v<T, long long>) {
                result = it->second.toInt64(&ok);
            } else if constexpr (std::is_same_v<T, unsigned long long>) {
                result = it->second.toUInt64(&ok);
            } else if constexpr (std::is_same_v<T, bool>) {
                result = it->second.toBool(&ok);
            } else if constexpr (std::is_same_v<T, float>) {
                result = it->second.toFloat(&ok);
            } else if constexpr (std::is_same_v<T, double>) {
                result = it->second.toDouble(&ok);
            } else if constexpr (std::is_same_v<T, long double>) {
                result = it->second.toLongDouble(&ok);
            } else if constexpr (std::is_same_v<T, QByteArray>) {
                result = it->second.toByteArray(&ok);
            } else if constexpr (std::is_same_v<T, QDate>) {
                result = it->second.toDate(&ok);
            } else if constexpr (std::is_same_v<T, QTime>) {
                result = it->second.toTime(&ok);
            } else if constexpr (std::is_same_v<T, QDateTime>) {
                result = it->second.toDateTime(&ok);
            } else if constexpr (std::is_same_v<T, SqlValue::ChronoDate>) {
                result = it->second.toChronoDate(&ok);
            } else if constexpr (std::is_same_v<T, SqlValue::ChronoTime>) {
                result = it->second.toChronoTime(&ok);
            } else if constexpr (std::is_same_v<T, SqlValue::ChronoDateTime>) {
                result = it->second.toChronoDateTime(&ok);
            } else if constexpr (std::is_same_v<T, std::vector<unsigned char>>) {
                result = it->second.toStdVectorUChar(&ok);
            }
            // else if constexpr (std::is_same_v<T, SqlDecimal>) { result = it->second.toDecimal(&ok); } // Example for custom types
            // else if constexpr (std::is_same_v<T, SqlJsonDocument>) { result = it->second.toJsonDocument(&ok); }
            else {
                // For std::any or other unlisted types, this path would be taken.
                // Consider if a static_assert(false, "Unsupported type T for ConnectionParameters::get") is appropriate,
                // or if it should attempt a toStdAny() and std::any_cast, which is more risky.
                // For now, ok will remain false if no specific conversion is matched.
            }
            if (ok) return result;
        }
        return std::nullopt;
    }

}  // namespace cpporm_sqldriver