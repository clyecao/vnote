// Sync URL canonicalization and validation before vault or backend work.
#include <QSignalSpy>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <core/services/syncsettings.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {
namespace {
class UnexpectedVault final : public SyncCredentialsStore {
public:
  explicit UnexpectedVault(ServiceLocator &p_services) : SyncCredentialsStore(p_services) {}
  void storeCredentials(const QString &, const SyncCredential &) override { ++writes; }
  int writes = 0;
};
} // namespace

class TestSyncService : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanupTestCase();
  void gitRemoteUrl_data();
  void gitRemoteUrl();
  void invalidSettings_data();
  void invalidSettings();
  void webdavCanonicalRemoteUrl_data();
  void webdavCanonicalRemoteUrl();

private:
  VxCoreContextHandle m_context = nullptr;
};

void TestSyncService::initTestCase() {
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
}

void TestSyncService::cleanupTestCase() { vxcore_context_destroy(m_context); }

void TestSyncService::gitRemoteUrl_data() {
  QTest::addColumn<QString>("remote");
  QTest::addColumn<QString>("canonical");
  QTest::newRow("http-trimmed")
      << QStringLiteral("  http://writer@example.invalid:8080/repo.git  ")
      << QStringLiteral("http://writer@example.invalid:8080/repo.git");
  QTest::newRow("https") << QStringLiteral("https://example.invalid/repo.git")
                         << QStringLiteral("https://example.invalid/repo.git");
  QTest::newRow("github-enterprise-server")
      << QStringLiteral("https://code.company.test/engineering/notes.git")
      << QStringLiteral("https://code.company.test/engineering/notes.git");
  QTest::newRow("github-enterprise-port-and-username")
      << QStringLiteral("  https://writer%40company.test@code.company.test:8443/team/notes.git  ")
      << QStringLiteral("https://writer%40company.test@code.company.test:8443/team/notes.git");
  QTest::newRow("github-enterprise-cloud")
      << QStringLiteral("https://company.ghe.com/team/notes.git")
      << QStringLiteral("https://company.ghe.com/team/notes.git");
  QTest::newRow("intranet-host") << QStringLiteral("https://code/team/notes")
                                 << QStringLiteral("https://code/team/notes");
  QTest::newRow("ipv6-port") << QStringLiteral("https://[::1]:8443/team/notes.git")
                             << QStringLiteral("https://[::1]:8443/team/notes.git");
  QTest::newRow("file") << QStringLiteral("file:///tmp/repo.git")
                        << QStringLiteral("file:///tmp/repo.git");
  QTest::newRow("ssh") << QStringLiteral("ssh://git@example.invalid/repo.git") << QString();
  QTest::newRow("scp") << QStringLiteral("git@example.invalid:repo.git") << QString();
  QTest::newRow("git") << QStringLiteral("git://example.invalid/repo.git") << QString();
  QTest::newRow("empty") << QString() << QString();
  QTest::newRow("whitespace") << QStringLiteral("http://example.invalid/repo name.git")
                             << QString();
  QTest::newRow("missing-host") << QStringLiteral("https:///team/notes.git") << QString();
  QTest::newRow("missing-project") << QStringLiteral("https://code.company.test/") << QString();
  QTest::newRow("invalid-port") << QStringLiteral("https://code.company.test:70000/team/notes.git")
                                << QString();
  QTest::newRow("embedded-token")
      << QStringLiteral("https://writer:secret@code.company.test/team/notes.git") << QString();
  QTest::newRow("empty-password-component")
      << QStringLiteral("https://writer:@code.company.test/team/notes.git") << QString();
  QTest::newRow("query-token")
      << QStringLiteral("https://code.company.test/team/notes.git?access_token=secret")
      << QString();
  QTest::newRow("fragment")
      << QStringLiteral("https://code.company.test/team/notes.git#main") << QString();
  QTest::newRow("malformed-escape")
      << QStringLiteral("https://code.company.test/team/notes%ZZ.git") << QString();
}

void TestSyncService::gitRemoteUrl() {
  QFETCH(QString, remote);
  QFETCH(QString, canonical);
  SyncSettings settings;
  settings.m_backend = QStringLiteral("git");
  settings.m_remoteUrl = remote;
  settings.m_credentials.m_backend = QStringLiteral("git");
  settings.m_credentials.m_secret = QStringLiteral("test-token");
  QCOMPARE(canonicalSyncRemoteUrl(settings), canonical);
  QCOMPARE(validateSyncSettings(settings, true).isEmpty(), !canonical.isEmpty());
}

void TestSyncService::invalidSettings_data() {
  QTest::addColumn<QString>("backend");
  QTest::addColumn<QString>("remote");
  QTest::addColumn<QString>("credentialBackend");
  QTest::addColumn<QString>("username");
  QTest::addColumn<QString>("secret");
  QTest::addColumn<int>("code");
  QTest::newRow("git-empty-token")
      << QStringLiteral("git") << QStringLiteral("https://example.com/repo.git")
      << QStringLiteral("git") << QString() << QString() << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("git-enterprise-embedded-token")
      << QStringLiteral("git")
      << QStringLiteral("https://writer:secret@code.company.test/team/notes.git")
      << QStringLiteral("git") << QString() << QStringLiteral("token")
      << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("git-empty-url") << QStringLiteral("git") << QString() << QStringLiteral("git")
                                 << QString() << QStringLiteral("token")
                                 << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("webdav-empty-username")
      << QStringLiteral("webdav") << QStringLiteral("https://example.com/notebook/")
      << QStringLiteral("webdav") << QString() << QStringLiteral("secret")
      << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("webdav-empty-password")
      << QStringLiteral("webdav") << QStringLiteral("https://example.com/notebook/")
      << QStringLiteral("webdav") << QStringLiteral("user") << QString()
      << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("webdav-unsupported-scheme")
      << QStringLiteral("webdav") << QStringLiteral("ftp://example.com/notebook/")
      << QStringLiteral("webdav") << QStringLiteral("user") << QStringLiteral("secret")
      << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("webdav-rejects-git-credential")
      << QStringLiteral("webdav") << QStringLiteral("https://example.com/notebook/")
      << QStringLiteral("git") << QStringLiteral("user") << QStringLiteral("token")
      << int(VXCORE_ERR_INVALID_PARAM);
  QTest::newRow("unknown-backend")
      << QStringLiteral("unsupported") << QStringLiteral("https://example.com/notebook/")
      << QStringLiteral("unsupported") << QStringLiteral("user") << QStringLiteral("secret")
      << int(VXCORE_ERR_UNKNOWN_BACKEND);
}

void TestSyncService::invalidSettings() {
  QFETCH(QString, backend);
  QFETCH(QString, remote);
  QFETCH(QString, credentialBackend);
  QFETCH(QString, username);
  QFETCH(QString, secret);
  QFETCH(int, code);
  ServiceLocator services;
  NotebookCoreService notebooks(m_context);
  UnexpectedVault vault(services);
  services.registerService<NotebookCoreService>(&notebooks);
  services.registerService<SyncCredentialsStore>(&vault);
  SyncService sync(services);
  const QString id = QStringLiteral("invalid-settings-notebook");
  QSignalSpy finished(&sync, &SyncService::enableFinished);
  sync.enableSyncForNotebook(id, {backend, remote, {credentialBackend, username, secret}});
  QTRY_COMPARE(finished.count(), 1);
  QCOMPARE(finished.first().at(1).toInt(), code);
  QCOMPARE(vault.writes, 0);
  QVERIFY(!sync.isSyncRegistered(id));
  QVERIFY(!sync.isSyncInProgress(id));
}

void TestSyncService::webdavCanonicalRemoteUrl_data() {
  QTest::addColumn<QString>("remote");
  QTest::addColumn<QString>("canonical");
  QTest::newRow("http-default-port") << QStringLiteral("http://example.com:80/notebook/")
                                     << QStringLiteral("http://example.com/notebook/");
  QTest::newRow("http-retains-https-port") << QStringLiteral("http://example.com:443/notebook/")
                                           << QStringLiteral("http://example.com:443/notebook/");
  QTest::newRow("https-default-port") << QStringLiteral("https://example.com:443/notebook/")
                                      << QStringLiteral("https://example.com/notebook/");
  QTest::newRow("https-retains-http-port") << QStringLiteral("https://example.com:80/notebook/")
                                           << QStringLiteral("https://example.com:80/notebook/");
}

void TestSyncService::webdavCanonicalRemoteUrl() {
  QFETCH(QString, remote);
  QFETCH(QString, canonical);
  SyncSettings settings;
  settings.m_backend = QStringLiteral("webdav");
  settings.m_remoteUrl = remote;
  QCOMPARE(canonicalSyncRemoteUrl(settings), canonical);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncService)
#include "test_syncservice.moc"
