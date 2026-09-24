/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QGridLayout;
class QLabel;
class QGroupBox;

namespace deskflow::gui {

/** A display-only view of the known devices. It never connects to the core. */
class DeviceOverviewWidget : public QWidget
{
  Q_OBJECT

public:
  struct Device
  {
    QString name;
    QString address;
    QString role;
    QString status;
    int row = 0;
    int column = 0;
    bool local = false;
    bool connected = false;
    bool operator==(const Device &) const = default;
  };

  explicit DeviceOverviewWidget(QWidget *parent = nullptr);
  void setDevices(const QList<Device> &devices, bool layoutKnown);

protected:
  void changeEvent(QEvent *event) override;

private:
  void updateText();
  QGroupBox *m_group;
  QGridLayout *m_grid;
  QLabel *m_hint;
  QList<Device> m_devices;
  bool m_layoutKnown = false;
};

} // namespace deskflow::gui
