/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferWidgetTests.h"
#include "gui/widgets/FileTransferWidget.h"

#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>

using deskflow::gui::FileTransferWidget;

namespace {
QString
status(const QString &state, quint64 done = 0, quint64 total = 10000, const QString &id = "a", quint64 filesTotal = 0)
{
  const QJsonObject json{
      {"id", id},
      {"state", state},
      {"received", QString::number(done)},
      {"total", QString::number(total)},
      {"filesDone", "3"},
      {"filesTotal", QString::number(filesTotal)},
  };
  return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

QLabel *statusLabel(FileTransferWidget &widget)
{
  return widget.findChild<QLabel *>(QStringLiteral("fileTransferStatus"));
}

QPushButton *cancelButton(FileTransferWidget &widget)
{
  return widget.findChild<QPushButton *>(QStringLiteral("cancelFileTransfer"));
}

QProgressBar *progress(FileTransferWidget &widget)
{
  return widget.findChild<QProgressBar *>(QStringLiteral("fileTransferProgress"));
}
} // namespace

void FileTransferWidgetTests::idleAndDisabledShowInstructions()
{
  FileTransferWidget widget;
  auto *detail = widget.findChild<QLabel *>(QStringLiteral("fileTransferDetail"));
  QVERIFY(!detail->text().isEmpty());
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("File copying is disabled"));
  QVERIFY(cancelButton(widget)->isHidden());
  const auto disabledInstruction = detail->text();
  widget.setAvailable(true);
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("No active transfer"));
  QVERIFY(detail->text() != disabledInstruction);
  QCOMPARE(progress(widget)->value(), 0);
  widget.setAvailable(false);
  QCOMPARE(detail->text(), disabledInstruction);
}

void FileTransferWidgetTests::cancelWaitsForStateChange()
{
  FileTransferWidget widget;
  QSignalSpy requests(&widget, &FileTransferWidget::cancelRequested);
  widget.setStatus(status("sending", 1000));
  auto *cancel = cancelButton(widget);
  QVERIFY(!cancel->isHidden());
  QVERIFY(cancel->isEnabled());
  cancel->click();
  QCOMPARE(requests.count(), 1);
  QVERIFY(!cancel->isEnabled());
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("Cancelling file transfer..."));
  // An in-flight ACK must not re-enable cancellation or hide its pending state.
  widget.setStatus(status("sending", 2000));
  QTRY_COMPARE(progress(widget)->value(), 200);
  QVERIFY(!cancel->isEnabled());
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("Cancelling file transfer..."));
  cancel->click();
  QCOMPARE(requests.count(), 1);
  widget.setStatus(status("cancelled", 2000));
  QVERIFY(cancel->isHidden());
  widget.setStatus(status("sending", 0, 10000, "next"));
  QVERIFY(cancel->isEnabled());
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("Sending files"));
}

void FileTransferWidgetTests::terminalNotificationsAreNotRepeated()
{
  FileTransferWidget widget;
  QSignalSpy notifications(&widget, &FileTransferWidget::notification);
  QSignalSpy caches(&widget, &FileTransferWidget::cacheChanged);
  widget.setStatus(status("ready", 10000));
  widget.setStatus(status("ready", 10000));
  QCOMPARE(notifications.count(), 1);
  QCOMPARE(notifications.first().at(2).toBool(), false);
  QCOMPARE(caches.count(), 1);
  widget.setCoreStopped();
  widget.setStatus(status("ready", 10000));
  QCOMPARE(notifications.count(), 1);
  QCOMPARE(caches.count(), 1);
  widget.setStatus(status("completed", 10000, 10000, "sent"));
  widget.setStatus(status("completed", 10000, 10000, "sent"));
  QCOMPARE(notifications.count(), 2);
  QCOMPARE(caches.count(), 1);
  widget.setStatus(status("failed", 0, 10000, "failed"));
  widget.setStatus(status("failed", 0, 10000, "failed"));
  QCOMPARE(notifications.count(), 3);
  QCOMPARE(notifications.last().at(2).toBool(), true);
  widget.setStatus(status("ready", 10000, 10000, "another"));
  QCOMPARE(caches.count(), 2);
}

void FileTransferWidgetTests::stoppedCoreInterruptsActiveTransfer()
{
  FileTransferWidget widget;
  widget.setStatus(status("receiving", 5000));
  cancelButton(widget)->click();
  widget.setCoreStopped();
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("File transfer interrupted"));
  QCOMPARE(progress(widget)->value(), 0);
  QVERIFY(cancelButton(widget)->isHidden());
  QVERIFY(widget.findChild<QLabel *>(QStringLiteral("fileTransferSpeed"))->isHidden());
  // A previously scheduled render must not resurrect the cancelled task.
  QTest::qWait(300);
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("File transfer interrupted"));
  widget.setStatus(status("preparing", 0, 0, "new"));
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("Preparing files..."));
  QVERIFY(cancelButton(widget)->isEnabled());
  widget.setStatus(status("ready", 10000, 10000, "new"));
  widget.setCoreStopped();
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("Files ready to paste"));
}

void FileTransferWidgetTests::unknownFileTotalIsNotShownAsZero()
{
  FileTransferWidget widget;
  widget.setStatus(status("receiving", 0, 0));
  auto *files = widget.findChild<QLabel *>(QStringLiteral("fileTransferFiles"));
  QVERIFY(!files->isHidden());
  QCOMPARE(files->text(), FileTransferWidget::tr("Files received: %1").arg(QLocale().toString(3)));
  QCOMPARE(progress(widget)->value(), 0);
  widget.setStatus(status("ready", 0, 0, "a", 3));
  QCOMPARE(files->text(), FileTransferWidget::tr("Files: %1 / %2").arg(QLocale().toString(3), QLocale().toString(3)));
  QCOMPARE(progress(widget)->value(), 1000);
}

void FileTransferWidgetTests::confirmationMustArriveBeforeFullProgress()
{
  FileTransferWidget widget;
  widget.setStatus(status("receiving", 10000));
  QCOMPARE(progress(widget)->value(), 999);
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("Checking received files..."));
  QVERIFY(widget.findChild<QLabel *>(QStringLiteral("fileTransferSpeed"))->isHidden());
  QVERIFY(widget.findChild<QLabel *>(QStringLiteral("fileTransferRemaining"))->isHidden());
  QVERIFY(!cancelButton(widget)->isHidden());
  widget.setStatus(status("ready", 10000));
  QCOMPARE(progress(widget)->value(), 1000);
  QVERIFY(cancelButton(widget)->isHidden());
}

void FileTransferWidgetTests::invalidStatusKeepsCurrentTransfer()
{
  FileTransferWidget widget;
  widget.setStatus(status("sending", 5000));
  QTest::ignoreMessage(QtWarningMsg, "invalid file transfer status from core ipc");
  widget.setStatus(QStringLiteral("not json"));
  QTest::ignoreMessage(QtWarningMsg, "invalid file transfer state or counters from core ipc");
  widget.setStatus(QStringLiteral(R"({"state":"sending","received":"invalid"})"));
  QCOMPARE(statusLabel(widget)->text(), FileTransferWidget::tr("Sending files"));
  QCOMPARE(progress(widget)->value(), 500);
  QVERIFY(cancelButton(widget)->isEnabled());
}

void FileTransferWidgetTests::stoppedProgressExpiresSpeedEstimate()
{
  FileTransferWidget widget;
  widget.setStatus(status("sending"));
  auto *speed = widget.findChild<QLabel *>(QStringLiteral("fileTransferSpeed"));
  auto *remaining = widget.findChild<QLabel *>(QStringLiteral("fileTransferRemaining"));
  const auto noSpeed = FileTransferWidget::tr("Speed: --");
  const auto noRemaining = FileTransferWidget::tr("Remaining: --");
  QTest::qWait(150);
  widget.setStatus(status("sending", 1000));
  QTRY_VERIFY_WITH_TIMEOUT(speed->text() != noSpeed, 1000);
  QVERIFY(remaining->text() != noRemaining);
  // The core sends no more ACKs. The widget's own timer must expire the estimate.
  QTRY_COMPARE_WITH_TIMEOUT(speed->text(), noSpeed, 3000);
  QCOMPARE(remaining->text(), noRemaining);
  QCOMPARE(progress(widget)->value(), 100);
}

void FileTransferWidgetTests::errorsArePlainText()
{
  FileTransferWidget widget;
  const QString error = QStringLiteral("<b>filename</b> could not be read");
  const QJsonObject json{{"id", "error"}, {"state", "failed"}, {"error", error}};
  widget.setStatus(QString::fromUtf8(QJsonDocument(json).toJson()));
  auto *detail = widget.findChild<QLabel *>(QStringLiteral("fileTransferDetail"));
  QCOMPARE(detail->text(), error);
  QCOMPARE(detail->textFormat(), Qt::PlainText);
}

QTEST_MAIN(FileTransferWidgetTests)
