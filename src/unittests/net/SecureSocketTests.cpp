/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SecureSocketTests.h"

#include "../deskflow/MockEventQueue.h"
#include "net/SecureSocket.h"
#include "net/SocketMultiplexer.h"

#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <memory>
#include <string>

namespace {

class ReadEvents : public MockEventQueue
{
public:
  void addEvent(Event &&event) override
  {
    if (event.getType() == EventTypes::StreamInputReady)
      ++ready;
    Event::deleteData(event);
  }
  int ready = 0;
};

long countTransportReads(BIO *bio, int operation, const char *, size_t, int, long, int result, size_t *)
{
  if (operation == BIO_CB_READ)
    ++*reinterpret_cast<int *>(BIO_get_callback_arg(bio));
  return result;
}

bool installTestCertificate(SSL_CTX *context)
{
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> generator(
      EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), &EVP_PKEY_CTX_free
  );
  EVP_PKEY *rawKey = nullptr;
  if (!generator || EVP_PKEY_keygen_init(generator.get()) <= 0 ||
      EVP_PKEY_CTX_set_ec_paramgen_curve_nid(generator.get(), NID_X9_62_prime256v1) <= 0 ||
      EVP_PKEY_keygen(generator.get(), &rawKey) <= 0)
    return false;
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(rawKey, &EVP_PKEY_free);
  std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), &X509_free);
  if (!certificate || X509_set_version(certificate.get(), 2) != 1 ||
      ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) != 1 ||
      !X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -60) ||
      !X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 3600) ||
      X509_set_pubkey(certificate.get(), key.get()) != 1)
    return false;
  auto *name = X509_get_subject_name(certificate.get());
  if (X509_NAME_add_entry_by_txt(
          name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("Deskflow isolated TLS test"), -1, -1, 0
      ) != 1 ||
      X509_set_issuer_name(certificate.get(), name) != 1 || X509_sign(certificate.get(), key.get(), EVP_sha256()) <= 0)
    return false;
  return SSL_CTX_use_certificate(context, certificate.get()) == 1 && SSL_CTX_use_PrivateKey(context, key.get()) == 1;
}

bool advanceHandshake(SSL *ssl)
{
  const int result = SSL_do_handshake(ssl);
  if (result == 1)
    return true;
  const auto error = SSL_get_error(ssl, result);
  return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE;
}

} // namespace

void SecureSocketTests::initTestCase()
{
#ifdef Q_OS_WIN
  m_arch.init();
#endif
  m_log.setFilter(LogLevel::Level::Warning);
}

void SecureSocketTests::boundedReadDrainsCurrentTlsRecord_data()
{
  QTest::addColumn<int>("prefillBytes");
  QTest::addColumn<int>("tlsVersion");
  for (const auto version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
    for (const auto prefill : {1024 * 1024 - 4096, 1024 * 1024}) {
      const auto row = QByteArray::number(version) + '-' + QByteArray::number(prefill);
      QTest::newRow(row.constData()) << prefill << version;
    }
  }
}

void SecureSocketTests::boundedReadDrainsCurrentTlsRecord()
{
  QFETCH(int, prefillBytes);
  QFETCH(int, tlsVersion);
  constexpr size_t budget = 1024 * 1024;
  constexpr size_t recordSize = 16 * 1024;
  int transportReads = 0;
  ReadEvents events;
  SocketMultiplexer multiplexer;
  // An unconnected transport is never registered with the multiplexer. TLS
  // runs entirely over memory BIOs, without user settings, files or clipboard.
  SecureSocket receiver(&events, &multiplexer, IArchNetwork::AddressFamily::INet);
  receiver.initSsl(true);
  QVERIFY(receiver.m_ssl && receiver.m_ssl->m_context);
  QVERIFY(installTestCertificate(receiver.m_ssl->m_context));
  QVERIFY(SSL_CTX_set_min_proto_version(receiver.m_ssl->m_context, tlsVersion) == 1);
  QVERIFY(SSL_CTX_set_max_proto_version(receiver.m_ssl->m_context, tlsVersion) == 1);
  receiver.createSSL();
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> clientContext(SSL_CTX_new(TLS_client_method()), &SSL_CTX_free);
  QVERIFY(clientContext);
  SSL_CTX_set_verify(clientContext.get(), SSL_VERIFY_NONE, nullptr);
  std::unique_ptr<SSL, decltype(&SSL_free)> client(SSL_new(clientContext.get()), &SSL_free);
  QVERIFY(client);
  BIO *serverBio = nullptr;
  BIO *clientBio = nullptr;
  QVERIFY(BIO_new_bio_pair(&serverBio, 128 * 1024, &clientBio, 128 * 1024) == 1);
  SSL_set_bio(receiver.m_ssl->m_ssl, serverBio, serverBio);
  SSL_set_bio(client.get(), clientBio, clientBio);
  SSL_set_accept_state(receiver.m_ssl->m_ssl);
  SSL_set_connect_state(client.get());
  for (int attempt = 0;
       attempt < 100 && (!SSL_is_init_finished(client.get()) || !SSL_is_init_finished(receiver.m_ssl->m_ssl));
       ++attempt) {
    QVERIFY(advanceHandshake(client.get()));
    QVERIFY(advanceHandshake(receiver.m_ssl->m_ssl));
  }
  QVERIFY(SSL_is_init_finished(client.get()));
  QVERIFY(SSL_is_init_finished(receiver.m_ssl->m_ssl));
  receiver.m_secureReady = true;

  const std::string payload(recordSize * 3, '\x4a');
  QCOMPARE(SSL_write(client.get(), payload.data(), static_cast<int>(payload.size())), static_cast<int>(payload.size()));
  BIO_set_callback_arg(serverBio, reinterpret_cast<char *>(&transportReads));
  BIO_set_callback_ex(serverBio, &countTransportReads);
  const std::string prefill(static_cast<size_t>(prefillBytes), '\x7f');
  receiver.m_inputBuffer.write(prefill.data(), static_cast<uint32_t>(prefill.size()));

  receiver.doRead();
  QVERIFY(transportReads > 0);
  // A subsequent FD_READ must reach recv/BIO again. Leaving decrypted bytes
  // here can consume that notification without rearming Winsock's FD_READ.
  QCOMPARE(SSL_pending(receiver.m_ssl->m_ssl), 0);
  QVERIFY(receiver.m_inputBuffer.getSize() > budget);
  QVERIFY(receiver.m_inputBuffer.getSize() <= budget + recordSize);
  const auto before = receiver.m_inputBuffer.getSize();
  transportReads = 0;
  receiver.doRead();
  QVERIFY(transportReads > 0);
  QCOMPARE(SSL_pending(receiver.m_ssl->m_ssl), 0);
  QVERIFY(receiver.m_inputBuffer.getSize() - before <= recordSize);

  // Let the event loop consume its buffer, then receive the remaining TLS
  // record with no further writes from the peer.
  std::string received(receiver.getSize(), '\0');
  QCOMPARE(receiver.read(received.data(), static_cast<uint32_t>(received.size())), received.size());
  receiver.doRead();
  std::string tail(receiver.getSize(), '\0');
  QCOMPARE(receiver.read(tail.data(), static_cast<uint32_t>(tail.size())), tail.size());
  QCOMPARE(received + tail, prefill + payload);
  QCOMPARE(events.ready, 1);
  BIO_set_callback_ex(serverBio, nullptr);
  receiver.close();
}

QTEST_GUILESS_MAIN(SecureSocketTests)
