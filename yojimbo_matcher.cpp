/*
    Yojimbo Network Library.
    
    Copyright © 2016 - 2017, The Network Protocol Company, Inc.

    Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

        1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

        2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer 
           in the documentation and/or other materials provided with the distribution.

        3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived 
           from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "yojimbo_config.h"
#include "yojimbo_matcher.h"

#include <mbedtls/config.h>
#include <mbedtls/platform.h>
#include <mbedtls/net.h>
#include <mbedtls/debug.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#include <mbedtls/certs.h>

#if 0 // todo

#include "sodium.h"

#define SERVER_PORT "8080"
#define SERVER_NAME "localhost"

namespace yojimbo
{
    struct MatcherInternal
    {
        mbedtls_net_context server_fd;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;
        mbedtls_x509_crt cacert;
    };

    Matcher::Matcher( Allocator & allocator )
    {
        m_allocator = &allocator;
        m_initialized = false;
        m_matchStatus = MATCH_IDLE;
        m_internal = YOJIMBO_NEW( allocator, MatcherInternal );
    }

    Matcher::~Matcher()
    {
        mbedtls_net_free( &m_internal->server_fd );
        mbedtls_x509_crt_free( &m_internal->cacert );
        mbedtls_ssl_free( &m_internal->ssl );
        mbedtls_ssl_config_free( &m_internal->conf );
        mbedtls_ctr_drbg_free( &m_internal->ctr_drbg );
        mbedtls_entropy_free( &m_internal->entropy );

        YOJIMBO_DELETE( *m_allocator, MatcherInternal, m_internal );
    }

    bool Matcher::Initialize()
    {
        const char * pers = "yojimbo_client";

        mbedtls_net_init( &m_internal->server_fd );
        mbedtls_ssl_init( &m_internal->ssl );
        mbedtls_ssl_config_init( &m_internal->conf );
        mbedtls_x509_crt_init( &m_internal->cacert );
        mbedtls_ctr_drbg_init( &m_internal->ctr_drbg );
        mbedtls_entropy_init( &m_internal->entropy );

        int result;

        if ( ( result = mbedtls_ctr_drbg_seed( &m_internal->ctr_drbg, mbedtls_entropy_func, &m_internal->entropy, (const unsigned char *) pers, strlen( pers ) ) ) != 0 )
        {
            debug_printf( "mbedtls_ctr_drbg_seed failed - error code = %d\n", result );
            return false;
        }

        if ( mbedtls_x509_crt_parse( &m_internal->cacert, (const unsigned char *) mbedtls_test_cas_pem, mbedtls_test_cas_pem_len ) < 0 )
        {
            debug_printf( "mbedtls_x509_crt_parse failed - error code = %d\n", result );
            return false;
        }

        m_initialized = true;

        return true;
    }

    void Matcher::RequestMatch( uint64_t protocolId, uint64_t clientId )
    {
        assert( m_initialized );

        uint32_t flags;
        char buf[4*1024];
        char request[1024];
        int bytesRead = 0;
        const char * json;

        int result;

        if ( ( result = mbedtls_net_connect( &m_internal->server_fd, SERVER_NAME, SERVER_PORT, MBEDTLS_NET_PROTO_TCP ) ) != 0 )
        {
            debug_printf( "mbedtls_net_connect failed - error code = %d\n", result );
            m_matchStatus = MATCH_FAILED;
            goto cleanup;
        }

        if ( ( result = mbedtls_ssl_config_defaults( &m_internal->conf,
                        MBEDTLS_SSL_IS_CLIENT,
                        MBEDTLS_SSL_TRANSPORT_STREAM,
                        MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 )
        {
            debug_printf( "mbedtls_net_connect failed - error code = %d\n", result );
            m_matchStatus = MATCH_FAILED;
            goto cleanup;
        }

#if YOJIMBO_SECURE_MODE
        mbedtls_ssl_conf_authmode( &m_internal->conf, MBEDTLS_SSL_VERIFY_REQUIRED );
#else // #if YOJIMBO_SECURE_MODE
        mbedtls_ssl_conf_authmode( &m_internal->conf, MBEDTLS_SSL_VERIFY_OPTIONAL );
#endif // #if YOJMIBO_SECURE_MODE
        mbedtls_ssl_conf_ca_chain( &m_internal->conf, &m_internal->cacert, NULL );
        mbedtls_ssl_conf_rng( &m_internal->conf, mbedtls_ctr_drbg_random, &m_internal->ctr_drbg );

        if ( ( result = mbedtls_ssl_setup( &m_internal->ssl, &m_internal->conf ) ) != 0 )
        {
            debug_printf( "mbedtls_ssl_setup failed - error code = %d\n", result );
            m_matchStatus = MATCH_FAILED;
            goto cleanup;
        }

        if ( ( result = mbedtls_ssl_set_hostname( &m_internal->ssl, "yojimbo" ) ) != 0 )
        {
            debug_printf( "mbedtls_ssl_set_hostname failed - error code = %d\n", result );
            m_matchStatus = MATCH_FAILED;
            goto cleanup;
        }

        mbedtls_ssl_set_bio( &m_internal->ssl, &m_internal->server_fd, mbedtls_net_send, mbedtls_net_recv, NULL );

        while ( ( result = mbedtls_ssl_handshake( &m_internal->ssl ) ) != 0 )
        {
            if ( result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE )
            {
                debug_printf( "mbedtls_ssl_handshake failed - error code = %d\n", result );
                m_matchStatus = MATCH_FAILED;
                goto cleanup;
            }
        }

        if ( ( flags = mbedtls_ssl_get_verify_result( &m_internal->ssl ) ) != 0 )
        {
#if YOJIMBO_SECURE_MODE
            // IMPORTANT: In secure mode you must use a valid certificate, not a self signed one!
            debug_printf( "mbedtls_ssl_get_verify_result failed - flags = %x\n", flags );
            m_matchStatus = MATCH_FAILED;
            goto cleanup;
#endif // #if YOJIMBO_SECURE_MODE
        }

        sprintf( request, "GET /match/%" PRIu64 "/%" PRIu64 " HTTP/1.0\r\n\r\n", protocolId, clientId );

        debug_printf( "match request:\n" );
        debug_printf( "%s\n", request );

        while ( ( result = mbedtls_ssl_write( &m_internal->ssl, (uint8_t*) request, strlen( request ) ) ) <= 0 )
        {
            if ( result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE )
            {
                debug_printf( "mbedtls_ssl_write failed - error code = %d\n", result );
                m_matchStatus = MATCH_FAILED;
                goto cleanup;
            }
        }

        memset( buf, 0, sizeof( buf ) );

        do
        {
            result = mbedtls_ssl_read( &m_internal->ssl, (uint8_t*) ( buf + bytesRead ), sizeof( buf ) - bytesRead - 1 );

            if ( result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE )
                continue;

            if ( result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY )
                break;

            if ( result <= 0 )
                break;

            bytesRead += result;
        }
        while( 1 );

        json = strstr( (const char*)buf, "\r\n\r\n" );

        if ( json && ParseMatchResponse( json, m_matchResponse ) )
        {
            m_matchStatus = MATCH_READY;
        }
        else
        {
            debug_printf( "failed to parse match response json:\n%s\n", json );
            m_matchStatus = MATCH_FAILED;
        }

    cleanup:

        mbedtls_ssl_close_notify( &m_internal->ssl );
    }

    MatchStatus Matcher::GetMatchStatus()
    {
        return m_matchStatus;
    }

    void Matcher::GetMatchResponse( MatchResponse & matchResponse )
    {
        matchResponse = ( m_matchStatus == MATCH_READY ) ? m_matchResponse : MatchResponse();
    }

    static bool exists_and_is_string( Document & doc, const char * key )
    {
        return doc.HasMember( key ) && doc[key].IsString();
    }

    static bool exists_and_is_array( Document & doc, const char * key )
    {
        return doc.HasMember( key ) && doc[key].IsArray();
    }

    bool Matcher::ParseMatchResponse( const char * json, MatchResponse & matchResponse )
    {
        Document doc;
        doc.Parse( json );
        if ( doc.HasParseError() )
            return false;

        if ( !exists_and_is_string( doc, "connectTokenData" ) )
            return false;

        if ( !exists_and_is_string( doc, "connectTokenNonce" ) )
            return false;

        if ( !exists_and_is_array( doc, "serverAddresses" ) )
            return false;

        if ( !exists_and_is_string( doc, "clientToServerKey" ) )
            return false;

        if ( !exists_and_is_string( doc, "serverToClientKey" ) )
            return false;

        const char * encryptedConnectTokenBase64 = doc["connectTokenData"].GetString();

        int encryptedLength = base64_decode_data( encryptedConnectTokenBase64, matchResponse.connectTokenData, ConnectTokenBytes );

        if ( encryptedLength != ConnectTokenBytes )
            return false;        

        uint64_t connectTokenNonce = atoll( doc["connectTokenNonce"].GetString() );

        memcpy( &matchResponse.connectTokenNonce, &connectTokenNonce, 8 );

        uint64_t connectTokenExpireTimestamp = atoll( doc["connectTokenExpireTimestamp"].GetString() );

        memcpy( &matchResponse.connectTokenExpireTimestamp, &connectTokenExpireTimestamp, 8 );

        matchResponse.numServerAddresses = 0;

        const Value & serverAddresses = doc["serverAddresses"];

        if ( !serverAddresses.IsArray() )
            return false;

        for ( SizeType i = 0; i < serverAddresses.Size(); ++i )
        {
            if ( i >= MaxServersPerConnect )
                return false;

            if ( !serverAddresses[i].IsString() )
                return false;

            char serverAddress[MaxAddressLength];

            base64_decode_string( serverAddresses[i].GetString(), serverAddress, sizeof( serverAddress ) );

            matchResponse.serverAddresses[i] = Address( serverAddress );

            if ( !matchResponse.serverAddresses[i].IsValid() )
                return false;

            matchResponse.numServerAddresses++;
        }

        const char * clientToServerKeyBase64 = doc["clientToServerKey"].GetString();

        const char * serverToClientKeyBase64 = doc["serverToClientKey"].GetString();

        if ( base64_decode_data( clientToServerKeyBase64, matchResponse.clientToServerKey, KeyBytes ) != KeyBytes )
            return false;

        if ( base64_decode_data( serverToClientKeyBase64, matchResponse.serverToClientKey, KeyBytes ) != KeyBytes )
            return false;

        return true;
    }
}

#endif
