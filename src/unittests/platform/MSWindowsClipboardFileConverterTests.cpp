/*
 * SPDX-FileCopyrightText: (C) 2026 Deskflow FileCopy contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"
#include "deskflow/FileTransferFormat.h"
#include "platform/MSWindowsClipboardFileConverter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

class MSWindowsClipboardFileConverterTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void extractsPathsWithoutFileMetadata()
  {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("\u6587\u4ef6.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("private user content"), qint64(20));
    file.close();

    const auto nativePath = QDir::toNativeSeparators(path).toUtf8().toStdString();
    MSWindowsClipboardFileConverter converter;
    const std::unique_ptr<void, decltype(&GlobalFree)> memory(
        converter.fromIClipboard(FileTransferFormat::marshall({{nativePath, 123}})), &GlobalFree
    );
    QVERIFY(memory != nullptr);
    const auto refs = FileTransferFormat::unmarshall(converter.toIClipboard(memory.get()));
    QCOMPARE(refs.size(), size_t(1));
    QCOMPARE(refs.front().path, nativePath);
    // Metadata belongs to the user file worker. Converting a clipboard in
    // the SYSTEM input process must not inspect the referenced filesystem.
    QCOMPARE(refs.front().size, uint64_t(0));
  }

private:
  Log m_log;
};

QTEST_GUILESS_MAIN(MSWindowsClipboardFileConverterTests)
#include "MSWindowsClipboardFileConverterTests.moc"
