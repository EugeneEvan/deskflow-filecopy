/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferIndicatorTests.h"
#include "gui/FileTransferIndicator.h"

#include <QPainter>
#include <QSignalSpy>

using deskflow::gui::FileTransferIndicator;
using deskflow::gui::FileTransferProgressModel;
using State = FileTransferIndicator::VisualState;
using TaskbarState = FileTransferIndicator::TaskbarState;

namespace {
void apply(FileTransferIndicator &indicator, const QString &state, quint64 done = 0, quint64 total = 10000)
{
  FileTransferProgressModel model;
  const QJsonObject json{{"id", "a"}, {"state", state}, {"received", qint64(done)}, {"total", qint64(total)}};
  QVERIFY(model.update(json, 0));
  indicator.apply(model.status(), model.metrics(0), QStringLiteral("Shared status"), QStringLiteral("Shared detail"));
}

QIcon baseIcon()
{
  QPixmap pixmap(32, 32);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.fillRect(QRect(8, 8, 12, 12), QColor(40, 90, 140));
  return QIcon(pixmap);
}
} // namespace

void FileTransferIndicatorTests::preparingAndVerifyingDoNotClaimPercentage()
{
  FileTransferIndicator indicator;
  apply(indicator, "preparing");
  QCOMPARE(indicator.visualState(), State::Preparing);
  QCOMPARE(indicator.taskbarState(), TaskbarState::Indeterminate);
  QCOMPARE(indicator.progress(), 0);
  QVERIFY(indicator.animationRunning());
  apply(indicator, "receiving", 10000);
  QCOMPARE(indicator.visualState(), State::Verifying);
  QCOMPARE(indicator.taskbarState(), TaskbarState::Indeterminate);
  QCOMPARE(indicator.progress(), 0);
  QVERIFY(indicator.animationRunning());
  apply(indicator, "sending", 0, 0);
  QCOMPARE(indicator.taskbarState(), TaskbarState::Indeterminate);
  QVERIFY(indicator.animationRunning());
}

void FileTransferIndicatorTests::transferUsesRealProgressAndClampsBeforeSuccess()
{
  FileTransferIndicator indicator;
  apply(indicator, "sending", 2500);
  QCOMPARE(indicator.visualState(), State::Transferring);
  QCOMPARE(indicator.taskbarState(), TaskbarState::Normal);
  QCOMPARE(indicator.progress(), 250);
  FileTransferProgressModel::Status status;
  status.id = "a";
  status.state = "receiving";
  status.bytesDone = 9000;
  status.bytesTotal = 10000;
  FileTransferProgressModel::Metrics metrics;
  metrics.progress = 1000;
  indicator.apply(status, metrics, {}, {});
  QCOMPARE(indicator.progress(), 999);
  apply(indicator, "ready", 10000);
  QCOMPARE(indicator.visualState(), State::Ready);
  QCOMPARE(indicator.progress(), 1000);
  QCOMPARE(indicator.taskbarState(), TaskbarState::None);
}

void FileTransferIndicatorTests::onlyActiveStatesAnimate()
{
  FileTransferIndicator indicator;
  QVERIFY(!indicator.animationRunning());
  apply(indicator, "receiving", 1000);
  QSignalSpy icons(&indicator, &FileTransferIndicator::iconChanged);
  QTRY_VERIFY_WITH_TIMEOUT(icons.count() > 0, 500);
  for (const auto &state : {"ready", "completed", "failed", "cancelled"}) {
    apply(indicator, state, 10000);
    QVERIFY(!indicator.animationRunning());
    icons.clear();
    QTest::qWait(120);
    QCOMPARE(icons.count(), 0);
  }
}

void FileTransferIndicatorTests::failureWaitsForAcknowledgementAndRetryRecovers()
{
  FileTransferIndicator indicator;
  apply(indicator, "failed", 3000);
  QCOMPARE(indicator.visualState(), State::Failed);
  QCOMPARE(indicator.taskbarState(), TaskbarState::Error);
  indicator.clipboardChanged();
  QCOMPARE(indicator.visualState(), State::Failed);
  indicator.acknowledgeFailure();
  QCOMPARE(indicator.visualState(), State::Idle);
  apply(indicator, "failed", 3000);
  QCOMPARE(indicator.visualState(), State::Idle);
  apply(indicator, "sending", 1000);
  QCOMPARE(indicator.visualState(), State::Transferring);
  QVERIFY(indicator.animationRunning());
  apply(indicator, "failed", 2000);
  QCOMPARE(indicator.visualState(), State::Failed);
  QVERIFY(!indicator.animationRunning());
}

void FileTransferIndicatorTests::clipboardChangeClearsSuccessWithoutResurrectingIt()
{
  FileTransferIndicator indicator;
  for (const auto &state : {"ready", "completed"}) {
    apply(indicator, "receiving", 1000);
    apply(indicator, state, 10000);
    indicator.clipboardChanged();
    QCOMPARE(indicator.visualState(), State::Idle);
    apply(indicator, state, 10000);
    QCOMPARE(indicator.visualState(), State::Idle);
  }
  apply(indicator, "receiving", 1000);
  indicator.clipboardChanged();
  QCOMPARE(indicator.visualState(), State::Transferring);
}

void FileTransferIndicatorTests::disconnectInvalidatesOldStatus()
{
  FileTransferIndicator indicator;
  apply(indicator, "receiving", 1000);
  indicator.setConnected(false);
  QCOMPARE(indicator.visualState(), State::Idle);
  QVERIFY(!indicator.animationRunning());
  apply(indicator, "ready", 10000);
  QCOMPARE(indicator.visualState(), State::Idle);
  indicator.setConnected(true);
  apply(indicator, "receiving", 1000);
  QCOMPARE(indicator.visualState(), State::Idle);
  apply(indicator, "preparing");
  QCOMPARE(indicator.visualState(), State::Preparing);
  indicator.invalidate();
  QCOMPARE(indicator.visualState(), State::Idle);
  QVERIFY(!indicator.animationRunning());
}

void FileTransferIndicatorTests::cancelledAndIdleRestoreBaseIcon()
{
  FileTransferIndicator indicator;
  const auto base = baseIcon();
  indicator.setBaseIcon(base);
  apply(indicator, "receiving", 2000);
  apply(indicator, "cancelled", 2000);
  QCOMPARE(indicator.visualState(), State::Idle);
  QCOMPARE(indicator.taskbarState(), TaskbarState::None);
  QCOMPARE(indicator.icon().cacheKey(), base.cacheKey());
  apply(indicator, "receiving", 1000);
  indicator.clear();
  QCOMPARE(indicator.icon().cacheKey(), base.cacheKey());
  QVERIFY(!indicator.animationRunning());
}

void FileTransferIndicatorTests::disconnectedFailureRemainsUntilAcknowledged()
{
  FileTransferIndicator indicator;
  apply(indicator, "receiving", 1000);
  apply(indicator, "failed", 1000);
  indicator.setConnected(false);
  QCOMPARE(indicator.visualState(), State::Failed);
  QCOMPARE(indicator.taskbarState(), TaskbarState::Error);
  QVERIFY(!indicator.animationRunning());
  apply(indicator, "receiving", 1000);
  QCOMPARE(indicator.visualState(), State::Failed);
  indicator.acknowledgeFailure();
  QCOMPARE(indicator.visualState(), State::Idle);
  indicator.setConnected(true);
  apply(indicator, "failed", 1000);
  QCOMPARE(indicator.visualState(), State::Idle);
}

void FileTransferIndicatorTests::hoverTextUsesSharedPresentationWithoutAnimationUpdates()
{
  FileTransferIndicator indicator;
  indicator.setConnectionToolTip("Deskflow FileCopy\nConnected");
  QCOMPARE(indicator.toolTip(), QStringLiteral("Deskflow FileCopy\nConnected"));
  apply(indicator, "sending", 2500);
  QVERIFY(indicator.toolTip().contains("Shared status"));
  QVERIFY(indicator.toolTip().contains("Shared detail"));
  QVERIFY(indicator.toolTip().contains("25%"));
  QSignalSpy toolTips(&indicator, &FileTransferIndicator::toolTipChanged);
  QTest::qWait(180);
  QCOMPARE(toolTips.count(), 0);
  apply(indicator, "receiving", 10000);
  QVERIFY(!indicator.toolTip().contains('%'));
  indicator.clear();
  QCOMPARE(indicator.toolTip(), QStringLiteral("Deskflow FileCopy\nConnected"));
}

void FileTransferIndicatorTests::rendersAtTraySizesAndPreservesBaseIcon()
{
  FileTransferIndicator indicator;
  indicator.setBaseIcon(baseIcon());
  apply(indicator, "receiving", 2500);
  for (int size : {16, 20, 24, 32}) {
    for (qreal ratio : {1.0, 1.25, 1.5, 2.0}) {
      const auto rendered = indicator.renderPixmap(size, ratio);
      QCOMPARE(rendered.size(), QSize(qRound(size * ratio), qRound(size * ratio)));
      QCOMPARE(rendered.devicePixelRatio(), ratio);
      const auto image = rendered.toImage();
      QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), QColor(40, 90, 140));
    }
  }
}

void FileTransferIndicatorTests::terminalBadgesHaveDistinctColors()
{
  FileTransferIndicator indicator;
  indicator.setBaseIcon(baseIcon());
  for (const auto &state : {"ready", "completed", "failed"}) {
    apply(indicator, state, 10000);
    const auto image = indicator.renderPixmap(32).toImage();
    int coloredPixels = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        const bool matches =
            QString(state) == "failed" ? color.red() > color.green() * 1.5 : color.green() > color.red() * 1.5;
        if (color.alpha() > 128 && matches)
          ++coloredPixels;
      }
    }
    QVERIFY(coloredPixels > 12);
  }
}

QTEST_MAIN(FileTransferIndicatorTests)
