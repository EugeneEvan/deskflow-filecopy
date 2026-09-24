/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferProgressModelTests.h"
#include "gui/FileTransferProgressModel.h"

#include <QJsonDocument>

using deskflow::gui::FileTransferProgressModel;

namespace {
QJsonObject status(quint64 done, quint64 total = 10000, const QString &state = "receiving", const QString &id = "a")
{
  return {
      {"id", id},         {"state", state},    {"received", QString::number(done)}, {"total", QString::number(total)},
      {"filesDone", "0"}, {"filesTotal", "0"},
  };
}
} // namespace

void FileTransferProgressModelTests::firstSampleHasNoRate()
{
  FileTransferProgressModel model;
  QVERIFY(model.update(status(1024), 100));
  QVERIFY(!model.metrics(100).bytesPerSecond);
  QVERIFY(!model.metrics(100).secondsRemaining);
  QVERIFY(model.update(status(1024), 100));
  QVERIFY(!model.metrics(100).bytesPerSecond);
}

void FileTransferProgressModelTests::shortTransferRateAndEta()
{
  FileTransferProgressModel model;
  QVERIFY(model.update(status(0), 0));
  QVERIFY(model.update(status(250), 50));
  QVERIFY(!model.metrics(50).bytesPerSecond);
  QVERIFY(model.update(status(1000), 200));
  auto metrics = model.metrics(200);
  QVERIFY(metrics.bytesPerSecond);
  QCOMPARE(*metrics.bytesPerSecond, 5000.0);
  QVERIFY(metrics.secondsRemaining);
  QCOMPARE(*metrics.secondsRemaining, quint64(2));
  QCOMPARE(metrics.progress, 100);
}

void FileTransferProgressModelTests::rateUsesRecentSamples()
{
  FileTransferProgressModel model;
  for (qint64 second = 0; second <= 4; ++second)
    QVERIFY(model.update(status(static_cast<quint64>(second * 1000), 100000), second * 1000));
  for (qint64 second = 5; second <= 8; ++second)
    QVERIFY(model.update(status(static_cast<quint64>(4000 + (second - 4) * 2000), 100000), second * 1000));
  const auto metrics = model.metrics(8000);
  QVERIFY(metrics.bytesPerSecond);
  QCOMPARE(*metrics.bytesPerSecond, 2000.0);
  QCOMPARE(*metrics.secondsRemaining, quint64(44));
}

void FileTransferProgressModelTests::stalledTransferClearsEtaAndRecovers()
{
  FileTransferProgressModel model;
  QVERIFY(model.update(status(0), 0));
  QVERIFY(model.update(status(1000), 1000));
  QVERIFY(model.metrics(1000).bytesPerSecond);
  QVERIFY(!model.metrics(3000).bytesPerSecond);
  QVERIFY(!model.metrics(3000).secondsRemaining);
  QVERIFY(model.update(status(2000), 4000));
  QVERIFY(!model.metrics(4000).bytesPerSecond);
  QVERIFY(model.update(status(3000), 4500));
  const auto metrics = model.metrics(4500);
  QVERIFY(metrics.bytesPerSecond);
  QCOMPARE(*metrics.bytesPerSecond, 2000.0);
}

void FileTransferProgressModelTests::newTaskAndDirectionResetRate()
{
  FileTransferProgressModel model;
  QVERIFY(model.update(status(0), 0));
  QVERIFY(model.update(status(1000), 500));
  QVERIFY(model.metrics(500).bytesPerSecond);
  QVERIFY(model.update(status(1000, 10000, "sending"), 600));
  QVERIFY(!model.metrics(600).bytesPerSecond);
  QVERIFY(model.update(status(2000, 10000, "sending"), 1100));
  QVERIFY(model.metrics(1100).bytesPerSecond);
  QVERIFY(model.update(status(2000, 10000, "sending", "b"), 1200));
  QVERIFY(!model.metrics(1200).bytesPerSecond);
}

void FileTransferProgressModelTests::preparingAndTerminalStatesHaveNoRate()
{
  FileTransferProgressModel model;
  QVERIFY(model.update(status(0, 0, "preparing"), 0));
  QVERIFY(model.status().active());
  QVERIFY(!model.metrics(100).bytesPerSecond);
  QCOMPARE(model.metrics(100).progress, 0);
  QVERIFY(model.update(status(0), 1000));
  QVERIFY(model.update(status(1000), 1500));
  QVERIFY(model.metrics(1500).bytesPerSecond);
  for (const auto &state : {"ready", "completed", "cancelled", "failed"}) {
    QVERIFY(model.update(status(10000, 10000, state), 2000));
    QVERIFY(!model.status().active());
    QVERIFY(!model.metrics(2000).bytesPerSecond);
    QVERIFY(!model.metrics(2000).secondsRemaining);
  }
  model.reset();
  QVERIFY(model.status().state.isEmpty());
  QVERIFY(!model.metrics(2500).bytesPerSecond);
}

void FileTransferProgressModelTests::countersPreserve64BitValues()
{
  const auto json =
      QJsonDocument::fromJson(
          R"({"id":"large","state":"sending","received":"9007199254740993","total":"18446744073709551615"})"
      )
          .object();
  FileTransferProgressModel model;
  QVERIFY(model.update(json, 0));
  QCOMPARE(model.status().bytesDone, quint64(9007199254740993ULL));
  QCOMPARE(model.status().bytesTotal, quint64(18446744073709551615ULL));
  // Existing cores may still send signed 64-bit JSON numbers rather than strings.
  auto numeric = status(0);
  numeric["received"] = qint64(5368709120LL);
  numeric["total"] = qint64(10737418240LL);
  QVERIFY(model.update(numeric, 100));
  QCOMPARE(model.status().bytesDone, quint64(5368709120ULL));
  QCOMPARE(model.metrics(100).progress, 500);
}

void FileTransferProgressModelTests::invalidStatusDoesNotReplaceValidStatus()
{
  FileTransferProgressModel model;
  QVERIFY(model.update(status(123), 0));
  auto invalid = status(0);
  invalid["received"] = "not-a-number";
  QVERIFY(!model.update(invalid, 100));
  invalid["received"] = -1;
  QVERIFY(!model.update(invalid, 100));
  invalid["received"] = 1.5;
  QVERIFY(!model.update(invalid, 100));
  QVERIFY(!model.update(status(0, 0, "unknown"), 100));
  QCOMPARE(model.status().bytesDone, quint64(123));
}

void FileTransferProgressModelTests::fileCountsAllowUnknownTotalAndEmptyFiles()
{
  FileTransferProgressModel model;
  auto json = status(0, 0);
  json["filesDone"] = "3";
  QVERIFY(model.update(json, 0));
  QVERIFY(model.status().hasFileCounts);
  QCOMPARE(model.status().filesDone, quint64(3));
  QCOMPARE(model.status().filesTotal, quint64(0));
  QVERIFY(!model.status().awaitingCompletion());
  QCOMPARE(model.metrics(0).progress, 0);
  json["state"] = "ready";
  json["filesTotal"] = "3";
  QVERIFY(model.update(json, 100));
  QCOMPARE(model.status().filesTotal, quint64(3));
  QCOMPARE(model.metrics(100).progress, 1000);
  json.remove("filesDone");
  json.remove("filesTotal");
  QVERIFY(model.update(json, 200));
  QVERIFY(!model.status().hasFileCounts);
}

void FileTransferProgressModelTests::allBytesDoNotMeanReady()
{
  FileTransferProgressModel model;
  QVERIFY(model.update(status(0), 0));
  QVERIFY(model.update(status(10000), 500));
  QVERIFY(model.status().awaitingCompletion());
  QCOMPARE(model.metrics(500).progress, 999);
  QVERIFY(!model.metrics(500).bytesPerSecond);
  QVERIFY(!model.metrics(500).secondsRemaining);
  QVERIFY(model.update(status(10000, 10000, "ready"), 600));
  QCOMPARE(model.metrics(600).progress, 1000);
}

void FileTransferProgressModelTests::counterRollbackStartsFreshSamples()
{
  FileTransferProgressModel model;
  QVERIFY(model.update(status(0), 0));
  QVERIFY(model.update(status(1000), 500));
  QVERIFY(model.metrics(500).bytesPerSecond);
  QVERIFY(model.update(status(500), 750));
  QVERIFY(!model.metrics(750).bytesPerSecond);
  QVERIFY(model.update(status(1000), 1000));
  QCOMPARE(*model.metrics(1000).bytesPerSecond, 2000.0);
}

QTEST_APPLESS_MAIN(FileTransferProgressModelTests)
