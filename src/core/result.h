// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QString>

#include <utility>
#include <variant>

namespace ft {

/// Broad categories of failures. User-visible text lives in Error::message.
enum class ErrorCode {
    InvalidArgument,
    NotFound,
    Conflict,
    OutOfRange,
    Io,
    Parse,
    UnsupportedVersion,
    InvalidDocument,
    Media,
    Engine,
    Cancelled,
    Internal,
};

struct Error
{
    ErrorCode code = ErrorCode::Internal;
    QString message; ///< concise, user-facing description
    QString detail;  ///< optional technical detail for logs and diagnostics

    [[nodiscard]] QString toString() const
    {
        return detail.isEmpty() ? message : QStringLiteral("%1 (%2)").arg(message, detail);
    }
};

inline Error makeError(ErrorCode code, QString message, QString detail = {})
{
    return Error{code, std::move(message), std::move(detail)};
}

/// Holds either a value or an Error. Used instead of exceptions across module boundaries so
/// failures are explicit at every call site.
template <typename T>
class [[nodiscard]] Result
{
public:
    Result(T value) : m_data(std::move(value)) {}         // NOLINT(google-explicit-constructor)
    Result(Error error) : m_data(std::move(error)) {}     // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool ok() const { return std::holds_alternative<T>(m_data); }
    explicit operator bool() const { return ok(); }

    [[nodiscard]] const T& value() const& { return std::get<T>(m_data); }
    [[nodiscard]] T& value() & { return std::get<T>(m_data); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(m_data)); }
    [[nodiscard]] const Error& error() const { return std::get<Error>(m_data); }

    const T& operator*() const& { return value(); }
    T& operator*() & { return value(); }
    const T* operator->() const { return &value(); }
    T* operator->() { return &value(); }

private:
    std::variant<T, Error> m_data;
};

/// Result specialisation for operations that produce no value.
template <>
class [[nodiscard]] Result<void>
{
public:
    Result() = default;
    Result(Error error) : m_error(std::move(error)), m_ok(false) {} // NOLINT

    [[nodiscard]] bool ok() const { return m_ok; }
    explicit operator bool() const { return m_ok; }
    [[nodiscard]] const Error& error() const { return m_error; }

private:
    Error m_error;
    bool m_ok = true;
};

using Status = Result<void>;

} // namespace ft
