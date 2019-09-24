#include "socket.h"

#ifdef ARLIB_SSL_OPENSSL
#include "../thread.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

static SSL_CTX* ctx;

RUN_ONCE_FN(initialize)
{
	//SSL_load_error_strings(); // TODO
	SSL_library_init();
	ctx = SSL_CTX_new(SSLv23_client_method());
	SSL_CTX_set_default_verify_paths(ctx);
	SSL_CTX_set_cipher_list(ctx, "HIGH:!DSS:!aNULL@STRENGTH");
}

#if OPENSSL_VERSION_NUMBER < 0x10002000
static bool validate_hostname(const char * hostname, const X509 * server_cert);
#endif

class socketssl_impl : public socket {
public:
	runloop* loop;
	autoptr<socket> sock;
	SSL* ssl;
	bool connected = false;
	bool is_readable = false;
	
	BIO* to_sock;
	BIO* from_sock;
	
	function<void()> cb_read;
	function<void()> cb_write;
	
	DECL_TIMER(timer, socketssl_impl);
	MAKE_DESTRUCTIBLE_FROM_CALLBACK();
	
#if OPENSSL_VERSION_NUMBER < 0x10002000 // < 1.0.2
	bool permissive;
	string domain;
#endif
	
	socketssl_impl(socket* parent, cstring domain, runloop* loop, bool permissive)
	{
		this->loop = loop;
		
		sock = parent;
		ssl = SSL_new(ctx);
		
		to_sock = BIO_new(BIO_s_mem());
		from_sock = BIO_new(BIO_s_mem());
		
		SSL_set_tlsext_host_name(ssl, (const char*)domain.c_str());
		SSL_set_bio(ssl, from_sock, to_sock);
		
		//the fuck kind of drugs is SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER,
		// what possible excuse could there be for that to not always be on
		//partial writes are perfectly fine with me, and seem mandatory on nonblocking BIOs, enable that too
		SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
		
		SSL_set_verify(ssl, permissive ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, NULL);
		
#if OPENSSL_VERSION_NUMBER < 0x10002000
		this->permissive = permissive;
		this->domain = domain;
#endif
		
#if OPENSSL_VERSION_NUMBER >= 0x10002000 && OPENSSL_VERSION_NUMBER < 0x10100000 // >= 1.0.2, < 1.1.0
		if (!permissive)
		{
			X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
			X509_VERIFY_PARAM_set1_host(param, domain.c_str(), 0);
		}
#endif
		
#if OPENSSL_VERSION_NUMBER >= 0x10100000 // >= 1.1.0
//#error test, especially [gs]et0 vs [gs]et1
//#error also check whether set_tlsext_host_name is needed
		SSL_set1_host(ssl, domain.c_str());
#endif
		
		update();
	}
	
	/*private*/ int fixret(int val)
	{
		if (val > 0) return val;
		//if (val == 0) return -1;
		if (val <= 0)
		{
			int sslerror = SSL_get_error(ssl, val);
			if (sslerror == SSL_ERROR_WANT_READ || sslerror == SSL_ERROR_WANT_WRITE) return 0;
			else return -1;
		}
		return -1; // unreachable, just to shut up a warning
	}
	
	/*private*/ void do_connect()
	{
		int ret = fixret(SSL_connect(ssl));
		if (ret < 0) sock = NULL;
		if (ret > 0)
		{
			connected = true;
#if OPENSSL_VERSION_NUMBER < 0x10002000 // < 1.0.2
			if (!permissive)
			{
				X509* cert = SSL_get_peer_certificate(ssl);
				if (!validate_hostname(domain.c_str(), cert))
					sock = NULL;
				X509_free(cert);
			}
#endif
		}
	}
	
	/*private*/ void update()
	{
		if (sock && !connected)
		{
			do_connect();
			if (sock && !connected)
			{
				set_child_cb();
				return;
			}
		}
		
	again: ;
		bool again = false;
		if (!sock) is_readable = true;
		
		if (cb_read && is_readable) { again = true; RETURN_IF_CALLBACK_DESTRUCTS(cb_read( )); }
		if (cb_write              ) { again = true; RETURN_IF_CALLBACK_DESTRUCTS(cb_write()); }
		
		if (sock && again) goto again;
		
		set_child_cb();
	}
	
	/*private*/ void set_child_cb()
	{
		if (sock)
		{
			sock->callback(                  bind_this(&socketssl_impl::on_readable),
			          BIO_pending(to_sock) ? bind_this(&socketssl_impl::on_writable) : NULL);
		}
	}
	
	/*private*/ void process_send()
	{
		if (!sock) return;
		
		char* buf;
		long buflen = BIO_get_mem_data(to_sock, &buf);
//printf("SOCKWRITE(%li)=...\n",buflen);
		
		if (buflen > 0)
		{
			if (buflen > 4096) buflen = 4096;
			
			int bytes = sock->send(arrayview<byte>((uint8_t*)buf, buflen));
//printf("SOCKWRITE(%li)=%i\n",buflen,bytes);
			if (bytes < 0) sock = NULL;
			if (bytes > 0)
			{
				uint8_t discard[4096];
				int discarded = BIO_read(to_sock, discard, bytes);
				if (discarded != bytes) abort();
			}
			
			set_child_cb();
		}
		if (buflen < 0) sock = NULL;
	}
	
	/*private*/ void process_recv()
	{
		if (!sock) return;
		
		uint8_t buf[4096];
		int bytes = sock->recv(buf);
//VALGRIND_PRINTF_BACKTRACE
//printf("SOCKREAD(4096)=%i\n",bytes);
		if (bytes < 0) sock = NULL;
		if (bytes > 0)
		{
			int bytes2 = BIO_write(from_sock, buf, bytes);
			if (bytes != bytes2) abort();
		}
		
		is_readable = true; // SSL_pending is unreliable
		
		set_child_cb();
	}
	
	int recv(arrayvieww<byte> data) override
	{
		int ret = fixret(SSL_read(ssl, data.ptr(), data.size()));
//printf("USERREAD(%lu)=%i\n",data.size(),ret);
		if (ret == 0)
		{
			if (!sock) ret = -1;
			else is_readable = false;
		}
		if (ret < 0)
		{
			sock = NULL;
			is_readable = false;
		}
		set_child_cb();
		
		return ret;
	}
	
	int send(arrayview<byte> data) override
	{
		if (!sock) return -1;
		
		int ret = fixret(SSL_write(ssl, data.ptr(), data.size()));
//printf("USERWRITE(%lu)=%i\n",data.size(),ret);
		process_send();
		if (ret < 0)
		{
			sock = NULL;
			return ret;
		}
		if (!sock) return -1;
		return ret;
	}
	
	/*private*/ void on_readable() { process_recv(); update(); }
	/*private*/ void on_writable() { process_send(); update(); }
	void callback(function<void()> cb_read, function<void()> cb_write) override
	{
		this->cb_read = cb_read;
		this->cb_write = cb_write;
		if (cb_write) timer.set_idle(bind_this(&socketssl_impl::update));
	}
	
	~socketssl_impl()
	{
		if (sock)
		{
			SSL_shutdown(ssl);
			process_send();
		}
		
		//the BIOs should not be freed, the SSL* grabs the only reference
		SSL_free(ssl);
	}
};

socket* socket::wrap_ssl_raw(socket* inner, cstring domain, runloop* loop)
{
	initialize();
	if (!ctx) return NULL;
	if (!inner) return NULL;
	return new socketssl_impl(inner, domain, loop, false);
}

socket* socket::wrap_ssl_raw_noverify(socket* inner, runloop* loop)
{
	initialize();
	if (!ctx) return NULL;
	if (!inner) return NULL;
	return new socketssl_impl(inner, "", loop, true);
}

#include "../test.h"
#ifdef ARLIB_TEST
#include "../os.h"
//this is more to initialize this thing before the actual ssl tests than a real test
//most of them are in a runloop, but initialization takes longer (9-33ms) than the runloop watchdog (3ms)
//this is also why it provides 'tcp' rather than 'ssl';
// if it provides 'ssl', it could run after the other SSL tests so they fail the watchdog
test("OpenSSL init", "array,base64", "tcp")
{
	test_skip("kinda slow");
	
	uint64_t begin_us = time_us_ne();
	initialize();
	uint64_t end_us = time_us_ne();
	if (!RUNNING_ON_VALGRIND)
	{
		assert_lt(end_us-begin_us, 5000); // takes about 1ms - no clue why Bear is so much slower
	}
}
#endif


#if OPENSSL_VERSION_NUMBER < 0x10002000
//from TLSe https://github.com/eduardsui/tlse/blob/90bdc5d/tlse.c#L2519
#define bad_certificate -1
static int tls_certificate_valid_subject_name(const unsigned char *cert_subject, const char *subject) {
    // no subjects ...
    if (((!cert_subject) || (!cert_subject[0])) && ((!subject) || (!subject[0])))
        return 0;
    
    if ((!subject) || (!subject[0]))
        return bad_certificate;
    
    if ((!cert_subject) || (!cert_subject[0]))
        return bad_certificate;
    
    // exact match
    if (!strcmp((const char *)cert_subject, subject))
        return 0;
    
    const char *wildcard = strchr((const char *)cert_subject, '*');
    if (wildcard) {
        // 6.4.3 (1) The client SHOULD NOT attempt to match a presented identifier in
        // which the wildcard character comprises a label other than the left-most label
        if (!wildcard[1]) {
            // subject is [*]
            // or
            // subject is [something*] .. invalid
            return bad_certificate;
        }
        wildcard++;
        const char *match = strstr(subject, wildcard);
        if ((!match) && (wildcard[0] == '.')) {
            // check *.domain.com agains domain.com
            wildcard++;
            if (!strcasecmp(subject, wildcard))
                return 0;
        }
        if (match) {
            unsigned long offset = (unsigned long)match - (unsigned long)subject;
            if (offset) {
                // check for foo.*.domain.com against *.domain.com (invalid)
                if (memchr(subject, '.', offset))
                    return bad_certificate;
            }
            // check if exact match
            if (!strcasecmp(match, wildcard))
                return 0;
        }
    }
    
    return bad_certificate;
}

//copypasted from https://wiki.openssl.org/index.php/Hostname_validation
//and modified a bit (for example to add a missing cast)
/*
Copyright (C) 2012, iSEC Partners.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

/*
 * Helper functions to perform basic hostname validation using OpenSSL.
 *
 * Please read "everything-you-wanted-to-know-about-openssl.pdf" before
 * attempting to use this code. This whitepaper describes how the code works,
 * how it should be used, and what its limitations are.
 *
 * Author:  Alban Diquet
 * License: See LICENSE
 *
 */

// Get rid of OSX 10.7 and greater deprecation warnings.
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

//#include <openssl/ssl.h>

//#include "openssl_hostname_validation.h"
//#include "hostcheck.h"

#define HOSTNAME_MAX_SIZE 255
enum HostnameValidationResult { Error, MalformedCertificate, NoSANPresent, MatchFound, MatchNotFound };

/**
* Tries to find a match for hostname in the certificate's Common Name field.
*
* Returns MatchFound if a match was found.
* Returns MatchNotFound if no matches were found.
* Returns MalformedCertificate if the Common Name had a NUL character embedded in it.
* Returns Error if the Common Name could not be extracted.
*/
static HostnameValidationResult matches_common_name(const char *hostname, const X509 *server_cert) {
        int common_name_loc = -1;
        X509_NAME_ENTRY *common_name_entry = NULL;
        ASN1_STRING *common_name_asn1 = NULL;
        char *common_name_str = NULL;

        // Find the position of the CN field in the Subject field of the certificate
        common_name_loc = X509_NAME_get_index_by_NID(X509_get_subject_name((X509 *) server_cert), NID_commonName, -1);
        if (common_name_loc < 0) {
                return Error;
        }

        // Extract the CN field
        common_name_entry = X509_NAME_get_entry(X509_get_subject_name((X509 *) server_cert), common_name_loc);
        if (common_name_entry == NULL) {
                return Error;
        }

        // Convert the CN field to a C string
        common_name_asn1 = X509_NAME_ENTRY_get_data(common_name_entry);
        if (common_name_asn1 == NULL) {
                return Error;
        }
        common_name_str = (char *) ASN1_STRING_data(common_name_asn1);

        // Make sure there isn't an embedded NUL character in the CN
        if ((size_t)ASN1_STRING_length(common_name_asn1) != strlen(common_name_str)) {
                return MalformedCertificate;
        }

        // Compare expected hostname with the CN
        if (tls_certificate_valid_subject_name((uint8_t*)common_name_str, hostname)==0) {
                return MatchFound;
        }
        else {
                return MatchNotFound;
        }
}


/**
* Tries to find a match for hostname in the certificate's Subject Alternative Name extension.
*
* Returns MatchFound if a match was found.
* Returns MatchNotFound if no matches were found.
* Returns MalformedCertificate if any of the hostnames had a NUL character embedded in it.
* Returns NoSANPresent if the SAN extension was not present in the certificate.
*/
static HostnameValidationResult matches_subject_alternative_name(const char *hostname, const X509 *server_cert) {
        HostnameValidationResult result = MatchNotFound;
        int i;
        int san_names_nb = -1;
        STACK_OF(GENERAL_NAME) *san_names = NULL;

        // Try to extract the names within the SAN extension from the certificate
        san_names = (STACK_OF(GENERAL_NAME)*)X509_get_ext_d2i((X509 *) server_cert, NID_subject_alt_name, NULL, NULL);
        if (san_names == NULL) {
                return NoSANPresent;
        }
        san_names_nb = sk_GENERAL_NAME_num(san_names);

        // Check each name within the extension
        for (i=0; i<san_names_nb; i++) {
                const GENERAL_NAME *current_name = sk_GENERAL_NAME_value(san_names, i);

                if (current_name->type == GEN_DNS) {
                        // Current name is a DNS name, let's check it
                        char *dns_name = (char *) ASN1_STRING_data(current_name->d.dNSName);

                        // Make sure there isn't an embedded NUL character in the DNS name
                        if ((size_t)ASN1_STRING_length(current_name->d.dNSName) != strlen(dns_name)) {
                                result = MalformedCertificate;
                                break;
                        }
                        else { // Compare expected hostname with the DNS name
                                if (tls_certificate_valid_subject_name((uint8_t*)dns_name, hostname)==0) {
                                        result = MatchFound;
                                        break;
                                }
                        }
                }
        }
        sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);

        return result;
}


/**
* Validates the server's identity by looking for the expected hostname in the
* server's certificate. As described in RFC 6125, it first tries to find a match
* in the Subject Alternative Name extension. If the extension is not present in
* the certificate, it checks the Common Name instead.
*
* Returns MatchFound if a match was found.
* Returns MatchNotFound if no matches were found.
* Returns MalformedCertificate if any of the hostnames had a NUL character embedded in it.
* Returns Error if there was an error.
*/
static bool validate_hostname(const char *hostname, const X509 *server_cert) {
        HostnameValidationResult result;

        if((hostname == NULL) || (server_cert == NULL))
                return false;

        // First try the Subject Alternative Names extension
        result = matches_subject_alternative_name(hostname, server_cert);
        if (result == NoSANPresent) {
                // Extension was not found: try the Common Name
                result = matches_common_name(hostname, server_cert);
        }

        return (result==MatchFound);
}
#endif
#endif
