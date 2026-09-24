/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DeviceOverviewWidget.h"

#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace deskflow::gui {
namespace {

// Keep unusually long computer names and IPv6 addresses from setting the
// minimum width of the whole application; the full value remains available
// through the tooltip and accessible name.
class DeviceLabel : public QLabel
{
public:
  explicit DeviceLabel(const QString &text, QWidget *parent) : QLabel(parent), m_text(text)
  {
    setTextFormat(Qt::PlainText);
    setToolTip(text);
    setAccessibleName(text);
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setText(text);
  }

protected:
  void resizeEvent(QResizeEvent *event) override
  {
    QLabel::resizeEvent(event);
    setText(fontMetrics().elidedText(m_text, Qt::ElideMiddle, contentsRect().width()));
  }

private:
  QString m_text;
};

QWidget *makeCard(const DeviceOverviewWidget::Device &device, QWidget *parent)
{
  auto *card = new QFrame(parent);
  card->setObjectName(device.local ? QStringLiteral("localDeviceCard") : QStringLiteral("peerDeviceCard"));
  card->setFrameShape(QFrame::StyledPanel);
  card->setBackgroundRole(device.local ? QPalette::AlternateBase : QPalette::Base);
  card->setAutoFillBackground(true);
  card->setMinimumWidth(150);
  auto *layout = new QVBoxLayout(card);
  layout->setContentsMargins(8, 5, 8, 5);
  layout->setSpacing(2);
  layout->setAlignment(Qt::AlignTop);

  auto *heading = new QHBoxLayout;
  auto *role = new DeviceLabel(device.role, card);
  heading->addWidget(role, 1);
  auto *status = new QLabel(device.status, card);
  status->setTextFormat(Qt::PlainText);
  status->setObjectName(QStringLiteral("deviceStatus"));
  heading->addWidget(status);
  layout->addLayout(heading);
  auto *name = new DeviceLabel(device.name, card);
  auto font = name->font();
  font.setBold(true);
  name->setFont(font);
  layout->addWidget(name);
  if (!device.address.isEmpty())
    layout->addWidget(new DeviceLabel(device.address, card));
  return card;
}

} // namespace

DeviceOverviewWidget::DeviceOverviewWidget(QWidget *parent)
    : QWidget(parent),
      m_group(new QGroupBox(this)),
      m_grid(new QGridLayout())
{
  setObjectName(QStringLiteral("deviceOverview"));
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_group);
  auto *groupLayout = new QVBoxLayout(m_group);
  groupLayout->setContentsMargins(6, 5, 6, 5);
  m_grid->setContentsMargins(0, 0, 0, 0);
  m_grid->setSpacing(8);
  groupLayout->addLayout(m_grid);
  // The main page already scrolls. Keep cards at their content height rather
  // than reserving a second scroll viewport with a large minimum height.
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  updateText();
}

void DeviceOverviewWidget::setDevices(const QList<Device> &devices, bool layoutKnown)
{
  if (m_devices == devices && m_layoutKnown == layoutKnown)
    return;
  m_devices = devices;
  while (auto *item = m_grid->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  for (int column = 0; column < m_grid->columnCount(); ++column)
    m_grid->setColumnStretch(column, 0);
  for (int row = 0; row < m_grid->rowCount(); ++row)
    m_grid->setRowStretch(row, 0);

  // Compress empty margins and grid gaps while retaining the configured
  // ordering on both axes. Unknown layouts are explicitly labelled below.
  QList<int> rows;
  QList<int> columns;
  for (const auto &device : devices) {
    if (!rows.contains(device.row))
      rows.append(device.row);
    if (!columns.contains(device.column))
      columns.append(device.column);
  }
  std::sort(rows.begin(), rows.end());
  std::sort(columns.begin(), columns.end());
  for (const auto &device : devices) {
    const auto row = static_cast<int>(rows.indexOf(device.row));
    const auto column = static_cast<int>(columns.indexOf(device.column));
    m_grid->addWidget(makeCard(device, this), row, column);
    m_grid->setColumnStretch(column, 1);
  }
  m_layoutKnown = layoutKnown;
  updateText();
}

void DeviceOverviewWidget::changeEvent(QEvent *event)
{
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    updateText();
}

void DeviceOverviewWidget::updateText()
{
  m_group->setToolTip(
      m_layoutKnown ? tr("Devices follow the configured screen positions. Move the pointer across a shared edge.")
                    : tr("Screen positions are configured on the server; this view only lists the known devices.")
  );
}

} // namespace deskflow::gui
