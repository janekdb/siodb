// Copyright (C) 2019-2020 Siodb GmbH. All rights reserved.
// Use of this source code is governed by a license that can be found
// in the LICENSE file.

#pragma once

// Project headers
#include "../../utils/HelperMacros.h"
#include "../OpenSslError.h"

// OpenSSL headers
#include <openssl/ssl.h>

namespace siodb::crypto {

/** Wrapper for the OpenSSL SSL_CTX. */
class SslContext {
public:
    /**
     * Initializes object of class SslContext.
     * @param method SSL method.
     * @throw OpenSslError if SSL_CTX object could not be created.
     */
    SslContext(const SSL_METHOD* method)
        : m_sslCtx(SSL_CTX_new(method))
    {
        if (!m_sslCtx) throw OpenSslError("SSL_CTX_new failed");
    }

    /**
     * Initializes object of class SslContext.
     * @param other SSL context.
     */
    SslContext(SslContext&& other) noexcept
        : m_sslCtx(other.m_sslCtx)
    {
        other.m_sslCtx = nullptr;
    }

    DECLARE_NONCOPYABLE(SslContext);

    /**
     * Deinitializes object.
     */
    ~SslContext()
    {
        SSL_CTX_free(m_sslCtx);
    }

    /**
     * Converts class into SSL_CTX*.
     * @return SSL_CTX pointer.
     */
    operator SSL_CTX*() noexcept
    {
        return m_sslCtx;
    }

    /**
     * Converts class into const SSL_CTX*.
     * @return Const SSL_CTX pointer.
     */
    operator const SSL_CTX*() const noexcept
    {
        return m_sslCtx;
    }

    /**
     * Releases pointer without freeing memory.
     * @return Released SSL_CTX.
     */
    SSL_CTX* release() noexcept
    {
        auto sslCtx = m_sslCtx;
        m_sslCtx = nullptr;
        return sslCtx;
    }

private:
    /** SSL context */
    SSL_CTX* m_sslCtx;
};

}  // namespace siodb::crypto