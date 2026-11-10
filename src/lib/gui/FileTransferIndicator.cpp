/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferIndicator.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <qt_windows.h>

#include <ShObjIdl.h>
#endif

namespace deskflow::gui {
namespace {
void paintBadge(QPainter &painter, const QRectF &bounds, bool failed)
{
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(32, 37, 43, 180), bounds.width() * 0.07));
  painter.setBrush(failed ? QColor(154, 64, 72) : QColor(68, 137, 92));
  painter.drawEllipse(bounds);
  painter.translate(bounds.topLeft());
  painter.scale(bounds.width(), bounds.height());
  painter.setPen(QPen(Qt::white, 0.13, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  if (failed) {
    painter.drawLine(QPointF(0.32, 0.32), QPointF(0.68, 0.68));
    painter.drawLine(QPointF(0.68, 0.32), QPointF(0.32, 0.68));
  } else {
    QPainterPath check;
    check.moveTo(0.24, 0.51);
    check.lineTo(0.43, 0.69);
    check.lineTo(0.76, 0.32);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(check);
  }
  painter.restore();
}
} // namespace

struct FileTransferIndicator::NativeTaskbar
{
#ifdef Q_OS_WIN
  HRESULT apartmentResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ITaskbarList3 *taskbar = nullptr;

  NativeTaskbar()
  {
    if (FAILED(apartmentResult) && apartmentResult != RPC_E_CHANGED_MODE) {
      qWarning("could not initialize file transfer taskbar COM apartment");
      return;
    }
    const auto result = CoCreateInstance(
        __uuidof(TaskbarList), nullptr, CLSCTX_INPROC_SERVER, __uuidof(ITaskbarList3),
        reinterpret_cast<void **>(&taskbar)
    );
    if (FAILED(result)) {
      qWarning("could not create file transfer taskbar interface");
      return;
    }
    if (FAILED(taskbar->HrInit())) {
      qWarning("could not initialize file transfer taskbar interface");
      taskbar->Release();
      taskbar = nullptr;
    }
  }

  ~NativeTaskbar()
  {
    if (taskbar)
      taskbar->Release();
    if (SUCCEEDED(apartmentResult))
      CoUninitialize();
  }
#endif
};

FileTransferIndicator::FileTransferIndicator(QObject *parent) : QObject(parent)
{
  m_timer.setInterval(40);
  m_timer.setTimerType(Qt::PreciseTimer);
  connect(&m_timer, &QTimer::timeout, this, &FileTransferIndicator::updateIcon);
}

FileTransferIndicator::~FileTransferIndicator()
{
  setWindowId(0);
}

void FileTransferIndicator::apply(
    const FileTransferProgressModel::Status &status, const FileTransferProgressModel::Metrics &metrics,
    const QString &summary, const QString &detail
)
{
  if (!m_connected)
    return;
  if (!m_suppressedState.isEmpty() && status.id == m_suppressedId &&
      (status.state == m_suppressedState || (m_suppressTask && !status.active())))
    return;

  auto state = VisualState::Idle;
  if (status.state == "preparing")
    state = VisualState::Preparing;
  else if (status.transferring())
    state = status.awaitingCompletion() || status.bytesTotal == 0 ? VisualState::Verifying : VisualState::Transferring;
  else if (status.state == "ready")
    state = VisualState::Ready;
  else if (status.state == "completed")
    state = VisualState::Completed;
  else if (status.state == "failed")
    state = VisualState::Failed;

  const int progress = state == VisualState::Ready || state == VisualState::Completed ? 1000
                       : state == VisualState::Transferring ? std::clamp(metrics.progress, 0, 999)
                                                            : 0;
  const bool appearanceChanged = m_state != state || m_progress != progress;
  if (status.active() && (!m_animationClock.isValid() || m_status.id != status.id))
    m_animationClock.start();
  m_status = status;
  m_state = state;
  m_progress = progress;
  m_summary = summary;
  m_detail = detail;
  m_suppressedId.clear();
  m_suppressedState.clear();
  m_suppressTask = false;

  if (status.active()) {
    if (!m_timer.isActive())
      m_timer.start();
  } else {
    m_timer.stop();
    m_animationClock.invalidate();
  }
  if (appearanceChanged) {
    updateIcon();
    updateTaskbar();
  }
  updateToolTip();
}

void FileTransferIndicator::setBaseIcon(const QIcon &icon)
{
  m_baseIcon = icon;
  updateIcon();
}

void FileTransferIndicator::setConnectionToolTip(const QString &text)
{
  m_connectionToolTip = text;
  updateToolTip();
}

void FileTransferIndicator::setConnected(bool connected)
{
  if (m_connected == connected)
    return;
  m_connected = connected;
  if (!connected && m_state != VisualState::Failed)
    invalidate();
}

void FileTransferIndicator::clipboardChanged()
{
  if (m_state == VisualState::Ready || m_state == VisualState::Completed)
    clear();
}

void FileTransferIndicator::acknowledgeFailure()
{
  if (m_state == VisualState::Failed)
    clear();
}

void FileTransferIndicator::clear()
{
  if (!m_status.state.isEmpty()) {
    m_suppressedId = m_status.id;
    m_suppressedState = m_status.state;
  }
  m_status = {};
  m_state = VisualState::Idle;
  m_progress = 0;
  m_animationClock.invalidate();
  m_summary.clear();
  m_detail.clear();
  m_timer.stop();
  updateIcon();
  updateToolTip();
  updateTaskbar();
}

void FileTransferIndicator::invalidate()
{
  clear();
  m_suppressTask = true;
}

void FileTransferIndicator::setWindowId(WId windowId)
{
  if (m_windowId == windowId)
    return;
#ifdef Q_OS_WIN
  if (m_nativeTaskbar && m_nativeTaskbar->taskbar && IsWindow(reinterpret_cast<HWND>(m_windowId))) {
    m_nativeTaskbar->taskbar->SetProgressState(reinterpret_cast<HWND>(m_windowId), TBPF_NOPROGRESS);
    m_nativeTaskbar->taskbar->SetOverlayIcon(reinterpret_cast<HWND>(m_windowId), nullptr, L"");
  }
#endif
  m_windowId = windowId;
  updateTaskbar();
}

void FileTransferIndicator::refreshTaskbar()
{
  m_nativeTaskbar.reset();
  updateTaskbar();
}

FileTransferIndicator::VisualState FileTransferIndicator::visualState() const
{
  return m_state;
}

FileTransferIndicator::TaskbarState FileTransferIndicator::taskbarState() const
{
  switch (m_state) {
  case VisualState::Preparing:
  case VisualState::Verifying:
    return TaskbarState::Indeterminate;
  case VisualState::Transferring:
    return TaskbarState::Normal;
  case VisualState::Failed:
    return TaskbarState::Error;
  default:
    return TaskbarState::None;
  }
}

int FileTransferIndicator::progress() const
{
  return m_progress;
}

bool FileTransferIndicator::animationRunning() const
{
  return m_timer.isActive();
}

QIcon FileTransferIndicator::icon() const
{
  return m_icon;
}

QString FileTransferIndicator::toolTip() const
{
  return m_toolTip;
}

QPixmap FileTransferIndicator::renderPixmap(int logicalSize, qreal devicePixelRatio) const
{
  if (logicalSize <= 0 || devicePixelRatio <= 0 || !std::isfinite(devicePixelRatio))
    return {};
  const auto physicalSize = qRound(logicalSize * devicePixelRatio);
  QPixmap result(physicalSize, physicalSize);
  result.setDevicePixelRatio(devicePixelRatio);
  result.fill(Qt::transparent);
  QPainter painter(&result);
  painter.setRenderHint(QPainter::Antialiasing);
  m_baseIcon.paint(&painter, QRect(0, 0, logicalSize, logicalSize));
  if (m_state == VisualState::Idle)
    return result;

  const qreal size = logicalSize;
  if (m_state == VisualState::Ready || m_state == VisualState::Completed || m_state == VisualState::Failed) {
    paintBadge(painter, QRectF(size * 0.53, size * 0.53, size * 0.44, size * 0.44), m_state == VisualState::Failed);
    return result;
  }

  const qreal stroke = std::max(1.6, size * 0.075);
  const qreal outline = std::max(0.85, size * 0.025);
  const qreal margin = (stroke + outline) / 2 + 0.5;
  const QRectF ring(margin, margin, size - 2 * margin, size - 2 * margin);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(112, 125, 140, 65), stroke, Qt::SolidLine, Qt::RoundCap));
  painter.drawEllipse(ring);

  // Elapsed time keeps the motion steady even when the GUI skips a timer tick.
  const qreal phase = m_animationClock.isValid() ? (m_animationClock.elapsed() % 1400) / 1400.0 : 0;
  const auto arc = [&](qreal start, qreal span, const QColor &color, qreal width) {
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(ring, qRound((90 - start) * 16), -qRound(span * 16));
  };
  const auto silverArc = [&](qreal start, qreal span) {
    // A graphite edge keeps silver readable on light and dark taskbars.
    arc(start, span, QColor(52, 62, 74, 220), stroke + outline);
    arc(start, span, QColor(192, 204, 218), stroke);
  };
  if (m_state == VisualState::Transferring && m_progress > 0) {
    const qreal span = 360.0 * m_progress / 1000.0;
    silverArc(0, span);
    // Sweep within the completed arc only; the actual percentage never advances
    // because of animation. The highlight enters and leaves at the arc's ends.
    const qreal length = std::min(span, 55.0);
    const qreal head = phase * (span + length);
    const qreal from = std::max(0.0, head - length);
    const qreal to = std::min(span, head);
    if (to > from)
      arc(from, to - from, QColor(250, 252, 255), stroke * 0.7);
  } else {
    // Preparation, verification, and the first 0% all show clear activity.
    const qreal span = 95 + 30 * std::sin(phase * 2 * 3.141592653589793);
    const qreal start = phase * 360;
    silverArc(start, span);
    arc(start + span * 0.65, span * 0.35, QColor(250, 252, 255), stroke * 0.7);
  }
  return result;
}

void FileTransferIndicator::updateIcon()
{
  if (m_state == VisualState::Idle) {
    m_icon = m_baseIcon;
  } else {
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48, 64})
      icon.addPixmap(renderPixmap(size));
    m_icon = icon;
  }
  Q_EMIT iconChanged(m_icon);
}

void FileTransferIndicator::updateToolTip()
{
  QStringList lines;
  if (!m_connectionToolTip.isEmpty())
    lines.append(m_connectionToolTip);
  if (m_state != VisualState::Idle) {
    auto summary = m_summary;
    if (m_state == VisualState::Transferring)
      summary += QStringLiteral(" · %1%").arg(m_progress / 10);
    if (!summary.isEmpty())
      lines.append(summary);
    if (!m_detail.isEmpty())
      lines.append(m_detail);
  }
  auto text = lines.join(QLatin1Char('\n'));
  // Windows notification-area hover text is limited to 128 UTF-16 characters.
  if (text.size() > 127)
    text = text.left(126) + QChar(0x2026);
  if (m_toolTip == text)
    return;
  m_toolTip = text;
  Q_EMIT toolTipChanged(m_toolTip);
}

void FileTransferIndicator::updateTaskbar()
{
#ifdef Q_OS_WIN
  const auto window = reinterpret_cast<HWND>(m_windowId);
  if (!m_windowId || !IsWindow(window))
    return;
  if (!m_nativeTaskbar)
    m_nativeTaskbar = std::make_unique<NativeTaskbar>();
  auto *taskbar = m_nativeTaskbar->taskbar;
  if (!taskbar)
    return;

  HRESULT progressResult = S_OK;
  switch (taskbarState()) {
  case TaskbarState::Indeterminate:
    progressResult = taskbar->SetProgressState(window, TBPF_INDETERMINATE);
    break;
  case TaskbarState::Normal:
    progressResult = taskbar->SetProgressState(window, TBPF_NORMAL);
    if (SUCCEEDED(progressResult))
      progressResult = taskbar->SetProgressValue(window, m_progress, 1000);
    break;
  case TaskbarState::Error:
    progressResult = taskbar->SetProgressState(window, TBPF_ERROR);
    break;
  case TaskbarState::None:
    progressResult = taskbar->SetProgressState(window, TBPF_NOPROGRESS);
    break;
  }

  HICON overlay = nullptr;
  if (m_state == VisualState::Ready || m_state == VisualState::Completed || m_state == VisualState::Failed) {
    const auto dpi = GetDpiForWindow(window);
    const int size = MulDiv(16, dpi ? dpi : 96, 96);
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
      QPainter painter(&image);
      paintBadge(painter, QRectF(1, 1, size - 2, size - 2), m_state == VisualState::Failed);
    }
    overlay = image.toHICON();
  }
  const auto overlayResult = taskbar->SetOverlayIcon(window, overlay, reinterpret_cast<LPCWSTR>(m_summary.utf16()));
  if (overlay)
    DestroyIcon(overlay);
  if (FAILED(progressResult) || FAILED(overlayResult))
    qWarning("could not update file transfer taskbar status");
#endif
}

} // namespace deskflow::gui
