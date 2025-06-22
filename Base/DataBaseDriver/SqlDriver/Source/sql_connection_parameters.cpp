// Source/sql_connection_parameters.cpp
#include "sqldriver/sql_connection_parameters.h"

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    // 定义静态常量成员
    const std::string ConnectionParameters::KEY_DRIVER_TYPE = "driver_type";
    const std::string ConnectionParameters::KEY_DB_NAME = "db_name";
    const std::string ConnectionParameters::KEY_USER_NAME = "user_name";
    const std::string ConnectionParameters::KEY_PASSWORD = "password";
    const std::string ConnectionParameters::KEY_HOST_NAME = "host_name";
    const std::string ConnectionParameters::KEY_PORT = "port";
    const std::string ConnectionParameters::KEY_CONNECT_OPTIONS = "connect_options";
    const std::string ConnectionParameters::KEY_CLIENT_CHARSET = "client_charset";
    const std::string ConnectionParameters::KEY_APPLICATION_NAME = "application_name";
    const std::string ConnectionParameters::KEY_CONNECTION_TIMEOUT_SECONDS = "connection_timeout_seconds";
    const std::string ConnectionParameters::KEY_READ_TIMEOUT_SECONDS = "read_timeout_seconds";
    const std::string ConnectionParameters::KEY_WRITE_TIMEOUT_SECONDS = "write_timeout_seconds";
    const std::string ConnectionParameters::KEY_SSL_MODE = "ssl_mode";
    const std::string ConnectionParameters::KEY_SSL_CERT_PATH = "ssl_cert_path";
    const std::string ConnectionParameters::KEY_SSL_KEY_PATH = "ssl_key_path";
    const std::string ConnectionParameters::KEY_SSL_CA_PATH = "ssl_ca_path";
    const std::string ConnectionParameters::KEY_SSL_CIPHER = "ssl_cipher";
    const std::string ConnectionParameters::KEY_POOL_MAX_SIZE = "pool_max_size";
    const std::string ConnectionParameters::KEY_POOL_MIN_SIZE = "pool_min_size";
    const std::string ConnectionParameters::KEY_POOL_ACQUIRE_TIMEOUT_MS = "pool_acquire_timeout_ms";
    const std::string ConnectionParameters::KEY_POOL_CONNECTION_LIFETIME_MS = "pool_connection_lifetime_ms";
    const std::string ConnectionParameters::KEY_POOL_IDLE_TIMEOUT_MS = "pool_idle_timeout_ms";

    // Setters (实现)
    void ConnectionParameters::setDriverType(const std::string& v) {
        (*this)[KEY_DRIVER_TYPE] = SqlValue(v);
    }
    void ConnectionParameters::setDbName(const std::string& v) {
        (*this)[KEY_DB_NAME] = SqlValue(v);
    }
    void ConnectionParameters::setUserName(const std::string& v) {
        (*this)[KEY_USER_NAME] = SqlValue(v);
    }
    void ConnectionParameters::setPassword(const std::string& v) {
        (*this)[KEY_PASSWORD] = SqlValue(v);
    }
    void ConnectionParameters::setHostName(const std::string& v) {
        (*this)[KEY_HOST_NAME] = SqlValue(v);
    }
    void ConnectionParameters::setPort(int v) {
        (*this)[KEY_PORT] = SqlValue(static_cast<int32_t>(v));
    }
    void ConnectionParameters::setConnectOptions(const std::string& v) {
        (*this)[KEY_CONNECT_OPTIONS] = SqlValue(v);
    }
    void ConnectionParameters::setClientCharset(const std::string& v) {
        (*this)[KEY_CLIENT_CHARSET] = SqlValue(v);
    }
    void ConnectionParameters::setApplicationName(const std::string& v) {
        (*this)[KEY_APPLICATION_NAME] = SqlValue(v);
    }
    void ConnectionParameters::setConnectionTimeoutSeconds(int v) {
        (*this)[KEY_CONNECTION_TIMEOUT_SECONDS] = SqlValue(static_cast<int32_t>(v));
    }
    void ConnectionParameters::setReadTimeoutSeconds(int v) {
        (*this)[KEY_READ_TIMEOUT_SECONDS] = SqlValue(static_cast<int32_t>(v));
    }
    void ConnectionParameters::setWriteTimeoutSeconds(int v) {
        (*this)[KEY_WRITE_TIMEOUT_SECONDS] = SqlValue(static_cast<int32_t>(v));
    }
    void ConnectionParameters::setSslMode(const std::string& v) {
        (*this)[KEY_SSL_MODE] = SqlValue(v);
    }
    void ConnectionParameters::setSslCertPath(const std::string& v) {
        (*this)[KEY_SSL_CERT_PATH] = SqlValue(v);
    }
    void ConnectionParameters::setSslKeyPath(const std::string& v) {
        (*this)[KEY_SSL_KEY_PATH] = SqlValue(v);
    }
    void ConnectionParameters::setSslCaPath(const std::string& v) {
        (*this)[KEY_SSL_CA_PATH] = SqlValue(v);
    }
    void ConnectionParameters::setSslCipher(const std::string& v) {
        (*this)[KEY_SSL_CIPHER] = SqlValue(v);
    }
    void ConnectionParameters::setPoolMaxSize(int v) {
        (*this)[KEY_POOL_MAX_SIZE] = SqlValue(static_cast<int32_t>(v));
    }
    void ConnectionParameters::setPoolMinSize(int v) {
        (*this)[KEY_POOL_MIN_SIZE] = SqlValue(static_cast<int32_t>(v));
    }
    void ConnectionParameters::setPoolAcquireTimeoutMs(long long v) {
        (*this)[KEY_POOL_ACQUIRE_TIMEOUT_MS] = SqlValue(static_cast<int64_t>(v));
    }
    void ConnectionParameters::setPoolConnectionLifetimeMs(long long v) {
        (*this)[KEY_POOL_CONNECTION_LIFETIME_MS] = SqlValue(static_cast<int64_t>(v));
    }
    void ConnectionParameters::setPoolIdleTimeoutMs(long long v) {
        (*this)[KEY_POOL_IDLE_TIMEOUT_MS] = SqlValue(static_cast<int64_t>(v));
    }

    // Getters (实现 - 使用模板的 get<T>)
    std::optional<std::string> ConnectionParameters::driverType() const {
        return get<std::string>(KEY_DRIVER_TYPE);
    }
    std::optional<std::string> ConnectionParameters::dbName() const {
        return get<std::string>(KEY_DB_NAME);
    }
    std::optional<std::string> ConnectionParameters::userName() const {
        return get<std::string>(KEY_USER_NAME);
    }
    std::optional<std::string> ConnectionParameters::password() const {
        return get<std::string>(KEY_PASSWORD);
    }
    std::optional<std::string> ConnectionParameters::hostName() const {
        return get<std::string>(KEY_HOST_NAME);
    }
    std::optional<int> ConnectionParameters::port() const {
        return get<int>(KEY_PORT);
    }
    std::optional<std::string> ConnectionParameters::connectOptions() const {
        return get<std::string>(KEY_CONNECT_OPTIONS);
    }
    std::optional<std::string> ConnectionParameters::clientCharset() const {
        return get<std::string>(KEY_CLIENT_CHARSET);
    }
    std::optional<std::string> ConnectionParameters::applicationName() const {
        return get<std::string>(KEY_APPLICATION_NAME);
    }
    std::optional<int> ConnectionParameters::connectionTimeoutSeconds() const {
        return get<int>(KEY_CONNECTION_TIMEOUT_SECONDS);
    }
    std::optional<int> ConnectionParameters::readTimeoutSeconds() const {
        return get<int>(KEY_READ_TIMEOUT_SECONDS);
    }
    std::optional<int> ConnectionParameters::writeTimeoutSeconds() const {
        return get<int>(KEY_WRITE_TIMEOUT_SECONDS);
    }
    std::optional<std::string> ConnectionParameters::sslMode() const {
        return get<std::string>(KEY_SSL_MODE);
    }
    std::optional<std::string> ConnectionParameters::sslCertPath() const {
        return get<std::string>(KEY_SSL_CERT_PATH);
    }
    std::optional<std::string> ConnectionParameters::sslKeyPath() const {
        return get<std::string>(KEY_SSL_KEY_PATH);
    }
    std::optional<std::string> ConnectionParameters::sslCaPath() const {
        return get<std::string>(KEY_SSL_CA_PATH);
    }
    std::optional<std::string> ConnectionParameters::sslCipher() const {
        return get<std::string>(KEY_SSL_CIPHER);
    }
    std::optional<int> ConnectionParameters::poolMaxSize() const {
        return get<int>(KEY_POOL_MAX_SIZE);
    }
    std::optional<int> ConnectionParameters::poolMinSize() const {
        return get<int>(KEY_POOL_MIN_SIZE);
    }
    std::optional<long long> ConnectionParameters::poolAcquireTimeoutMs() const {
        return get<long long>(KEY_POOL_ACQUIRE_TIMEOUT_MS);
    }
    std::optional<long long> ConnectionParameters::poolConnectionLifetimeMs() const {
        return get<long long>(KEY_POOL_CONNECTION_LIFETIME_MS);
    }
    std::optional<long long> ConnectionParameters::poolIdleTimeoutMs() const {
        return get<long long>(KEY_POOL_IDLE_TIMEOUT_MS);
    }

}  // namespace cpporm_sqldriver