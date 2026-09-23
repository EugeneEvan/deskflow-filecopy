/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

// important: this is used for settings paths on some platforms,
// and must not be a url. qt automatically converts this to reverse domain
// notation (rdn), e.g. org.deskflow
const auto kOrgDomain = QStringLiteral("deskflow.org");

const auto kUrlSourceQuery = QStringLiteral("source=gui");
// Keep the settings domain above stable; this derivative has its own releases.
const auto kUrlApp = QStringLiteral("https://github.com/EugeneEvan/deskflow-filecopy");
const auto kUrlHelp = QStringLiteral("%1/blob/main/README.md").arg(kUrlApp);
const auto kUrlDownload = QStringLiteral("%1/releases").arg(kUrlApp);
const auto kUrlWiki = kUrlHelp;
const auto kUrlUpdateCheck = QStringLiteral(
    "https://raw.githubusercontent.com/EugeneEvan/deskflow-filecopy/main/deploy/windows/latest-version.txt"
);

#if defined(Q_OS_LINUX)
const auto kUrlGnomeTrayFix = QStringLiteral("https://extensions.gnome.org/extension/615/appindicator-support/");
#endif
