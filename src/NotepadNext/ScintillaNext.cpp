/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ScintillaNext.h"
#include "uchardet.h"
#include <cinttypes>

#include <QDir>
#include <QMouseEvent>
#include <QSaveFile>
#include <QTextCodec>


const int CHUNK_SIZE = 1024 * 1024 * 4; // Not sure what is best


static bool writeToDisk(const QByteArray &data, const QString &path)
{
    qInfo(Q_FUNC_INFO);

    QSaveFile file(path);
    file.setDirectWriteFallback(true);

    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        return file.commit();
    }
    else {
        qWarning("writeToDisk() failure: %s", qPrintable(file.errorString()));
        return false;
    }
}


ScintillaNext::ScintillaNext(QString name, QWidget *parent) :
    ScintillaEdit(parent),
    name(name)
{
    this->initialiseCommentsForLanguages();
}

ScintillaNext *ScintillaNext::fromFile(const QString &filePath)
{
    QFileInfo info(filePath);

    // TODO: check file permissions

    if(!info.exists()) {
        return Q_NULLPTR;
    }

    ScintillaNext *editor = new ScintillaNext(info.fileName());

    QFile file(filePath);
    bool readSuccessful = editor->readFromDisk(file);

    if (!readSuccessful) {
        delete editor;
        return Q_NULLPTR;
    }

    editor->setFileInfo(filePath);
    editor->updateTimestamp();

    return editor;
}

bool ScintillaNext::isSavedToDisk() const
{
    return bufferType != ScintillaNext::FileMissing && !modify();
}

bool ScintillaNext::isFile() const
{
    return bufferType == ScintillaNext::File || bufferType == ScintillaNext::FileMissing;;
}

QFileInfo ScintillaNext::getFileInfo() const
{
    Q_ASSERT(isFile());

    return fileInfo;
}

QString ScintillaNext::getPath() const
{
    Q_ASSERT(isFile());

    return QDir::toNativeSeparators(fileInfo.canonicalPath());
}

QString ScintillaNext::getFilePath() const
{
    Q_ASSERT(isFile());

    return QDir::toNativeSeparators(fileInfo.canonicalFilePath());
}

void ScintillaNext::setFoldMarkers(const QString &type)
{
    QMap<QString, QList<int>> map{
        {"simple", {SC_MARK_MINUS, SC_MARK_PLUS, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY}},
        {"arrow",  {SC_MARK_ARROWDOWN, SC_MARK_ARROW, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY}},
        {"circle", {SC_MARK_CIRCLEMINUS, SC_MARK_CIRCLEPLUS, SC_MARK_VLINE, SC_MARK_LCORNERCURVE, SC_MARK_CIRCLEPLUSCONNECTED, SC_MARK_CIRCLEMINUSCONNECTED, SC_MARK_TCORNERCURVE }},
        {"box",    {SC_MARK_BOXMINUS, SC_MARK_BOXPLUS, SC_MARK_VLINE, SC_MARK_LCORNER, SC_MARK_BOXPLUSCONNECTED, SC_MARK_BOXMINUSCONNECTED, SC_MARK_TCORNER }},
    };

    if (!map.contains(type))
        return;

    const auto types = map[type];
    markerDefine(SC_MARKNUM_FOLDEROPEN, types[0]);
    markerDefine(SC_MARKNUM_FOLDER, types[1]);
    markerDefine(SC_MARKNUM_FOLDERSUB, types[2]);
    markerDefine(SC_MARKNUM_FOLDERTAIL, types[3]);
    markerDefine(SC_MARKNUM_FOLDEREND, types[4]);
    markerDefine(SC_MARKNUM_FOLDEROPENMID, types[5]);
    markerDefine(SC_MARKNUM_FOLDERMIDTAIL, types[6]);
}

void ScintillaNext::close()
{
    emit closed();

    deleteLater();
}

bool ScintillaNext::save()
{
    qInfo(Q_FUNC_INFO);

    Q_ASSERT(isFile());

    emit aboutToSave();

    bool writeSuccessful = writeToDisk(QByteArray::fromRawData((char*)characterPointer(), textLength()), fileInfo.filePath());

    if (writeSuccessful) {
        updateTimestamp();
        setSavePoint();

        emit saved();
    }

    return writeSuccessful;
}

void ScintillaNext::reload()
{
    Q_ASSERT(isFile());

    // Ensure the file still exists.
    if (!QFile::exists(fileInfo.canonicalFilePath())) {
        return;
    }

    // Remove all the text
    {
        const QSignalBlocker blocker(this);
        setUndoCollection(false);
        emptyUndoBuffer();
        setText("");
        setUndoCollection(true);
    }

    // NOTE: if the read fails then the buffer will be completely empty...which probably
    // isn't a good thing, but this should be a rare occurrence.
    QFile f(fileInfo.canonicalFilePath());
    bool readSuccessful = readFromDisk(f);

    if (readSuccessful) {
        updateTimestamp();
        setSavePoint();
    }

    return;
}

bool ScintillaNext::saveAs(const QString &newFilePath)
{
    bool isRenamed = bufferType == ScintillaNext::Temporary || fileInfo.canonicalFilePath() != newFilePath;

    emit aboutToSave();

    bool saveSuccessful = writeToDisk(QByteArray::fromRawData((char*)characterPointer(), textLength()), newFilePath);

    if (saveSuccessful) {
        setFileInfo(newFilePath);
        updateTimestamp();
        setSavePoint();

        emit saved();

        if (isRenamed) {
            emit renamed();
        }
    }

    return saveSuccessful;
}

bool ScintillaNext::saveCopyAs(const QString &filePath)
{
    return writeToDisk(QByteArray::fromRawData((char*)characterPointer(), textLength()), filePath);
}

bool ScintillaNext::rename(const QString &newFilePath)
{
    emit aboutToSave();

    // Write out the buffer to the new path
    if (saveCopyAs(newFilePath)) {
        // Remove the old file
        const QString oldPath = fileInfo.canonicalFilePath();
        QFile::remove(oldPath);

        // Everything worked fine, so update the buffer's info
        setFileInfo(newFilePath);
        updateTimestamp();
        setSavePoint();

        emit saved();

        emit renamed();

        return true;
    }

    return false;
}

ScintillaNext::FileStateChange ScintillaNext::checkFileForStateChange()
{
    if (bufferType == BufferType::Temporary) {
        return FileStateChange::NoChange;
    }
    else if (bufferType == BufferType::File) {
        // refresh else exists() fails to notice missing file
        fileInfo.refresh();

        if (!fileInfo.exists()) {
            bufferType = BufferType::FileMissing;

            emit savePointChanged(false);

            return FileStateChange::Deleted;
        }

        // See if the timestamp changed
        if (modifiedTime != fileTimestamp()) {
            return FileStateChange::Modified;
        }
        else {
            return FileStateChange::NoChange;
        }
    }
    else if (bufferType == BufferType::FileMissing) {
        // See if it reappeared
        fileInfo.refresh();

        if (fileInfo.exists()) {
            bufferType = BufferType::File;

            return FileStateChange::Restored;
        }
        else {
            return FileStateChange::NoChange;
        }
    }

    qInfo("type() = %d", bufferType);
    Q_ASSERT(false);

    return FileStateChange::NoChange;
}

bool ScintillaNext::moveToTrash()
{
    if (QFile::exists(fileInfo.canonicalFilePath())) {
        QFile f(fileInfo.canonicalFilePath());

        return f.moveToTrash();
    }

    return false;
}


void ScintillaNext::toggleComment()
{
    //Return if the language doesn't support single-line comments
    QString commentString = singleLineCommentCharacters[this->languageName];
    if (commentString.length() == 0) return;

    //Get the current line text and number
    QString currentLineText = QString(ScintillaEdit::getCurLine(ScintillaEdit::textLength()));
    sptr_t currentLineNumber = ScintillaEdit::lineFromPosition(ScintillaEdit::currentPos());

    //Strip the comment (incase it was indented) and then check if it already has a comment
    if (currentLineText.remove('\t').remove(' ').startsWith(commentString))
    {
        //Delete the comment if it exists
        ScintillaEdit::deleteRange(ScintillaEdit::positionFromLine(currentLineNumber), commentString.length());
    }
    else
    {
        //Add the comment if it doesn't exist
        ScintillaEdit::insertText(ScintillaEdit::positionFromLine(currentLineNumber), commentString.toStdString().c_str());
    }
}

void ScintillaNext::commentLine()
{
    //Return if the language doesn't support single-line comments
    QString commentString = singleLineCommentCharacters[this->languageName];
    if (commentString.length() == 0) return;

    QString currentLineText = QString(ScintillaEdit::getCurLine(ScintillaEdit::textLength()));
    sptr_t currentLineNumber = ScintillaEdit::lineFromPosition(ScintillaEdit::currentPos());

    //Strip the comment (incase it was indented) and then check if it doesn't have a comment
    if (!currentLineText.remove('\t').remove(' ').startsWith(commentString))
    {
        //Insert the comment if it didn't exist
        ScintillaEdit::insertText(ScintillaEdit::positionFromLine(currentLineNumber), commentString.toStdString().c_str());
    }
}

void ScintillaNext::uncommentLine()
{
    //Return if the language doesn't support single-line comments
    QString commentString = singleLineCommentCharacters[this->languageName];
    if (commentString.length() == 0) return;

    QString currentLineText = QString(ScintillaEdit::getCurLine(ScintillaEdit::textLength()));
    sptr_t currentLineNumber = ScintillaEdit::lineFromPosition(ScintillaEdit::currentPos());

    //Strip the comment (incase it was indented) and then check if it has a comment
    if (currentLineText.remove('\t').remove(' ').startsWith(commentString))
    {
        //Remove the comment if it exists
        ScintillaEdit::deleteRange(ScintillaEdit::positionFromLine(currentLineNumber), commentString.length());
    }
}

void ScintillaNext::initialiseCommentsForLanguages()
{
    this->singleLineCommentCharacters["ActionScript"] = "//";
    this->singleLineCommentCharacters["ADA"] = "--";
    this->singleLineCommentCharacters["ASN.1"] = "";
    this->singleLineCommentCharacters["asp"] = "'";
    this->singleLineCommentCharacters["autoIt"] = ";";
    this->singleLineCommentCharacters["AviSynth"] = "#";
    this->singleLineCommentCharacters["BaanC"] = "//";
    this->singleLineCommentCharacters["bash"] = "#";
    this->singleLineCommentCharacters["Batch"] = "REM";
    this->singleLineCommentCharacters["BlitzBasic"] = ";";
    this->singleLineCommentCharacters["C"] = "//";
    this->singleLineCommentCharacters["Caml"] = "";
    this->singleLineCommentCharacters["COBOL"] = "*";
    this->singleLineCommentCharacters["Csound"] = ";";
    this->singleLineCommentCharacters["CoffeeScript"] = "#";
    this->singleLineCommentCharacters["C++"] = "//";
    this->singleLineCommentCharacters["C#"] = "//";
    this->singleLineCommentCharacters["CSS"] = "";
    this->singleLineCommentCharacters["D"] = "//";
    this->singleLineCommentCharacters["DIFF"] = "";
    this->singleLineCommentCharacters["Erlang"] = "%";
    this->singleLineCommentCharacters["ESCRIPT"] = "//";
    this->singleLineCommentCharacters["Forth"] = "\\";
    this->singleLineCommentCharacters["Fortran (free form)"] = "!";
    this->singleLineCommentCharacters["Fortran (fixed form)"] = "C";
    this->singleLineCommentCharacters["FreeBasic"] = "'";
    this->singleLineCommentCharacters["GUI4CLI"] = "//";
    this->singleLineCommentCharacters["Go"] = "//";
    this->singleLineCommentCharacters["Haskell"] = "--";
    this->singleLineCommentCharacters["HTML"] = "";
    this->singleLineCommentCharacters["ini file"] = ";";
    this->singleLineCommentCharacters["InnoSetup"] = ";";
    this->singleLineCommentCharacters["Intel HEX"] = "";
    this->singleLineCommentCharacters["Java"] = "//";
    this->singleLineCommentCharacters["JavaScript (embedded)"] = "//";
    this->singleLineCommentCharacters["JavaScript"] = "//";
    this->singleLineCommentCharacters["JSON"] = "";
    this->singleLineCommentCharacters["KiXtart"] = "";
    this->singleLineCommentCharacters["LISP"] = ";";
    this->singleLineCommentCharacters["LaTeX"] = "%";
    this->singleLineCommentCharacters["Lua"] = "--";
    this->singleLineCommentCharacters["Makefile"] = "#";
    this->singleLineCommentCharacters["Markdown"] = "";
    this->singleLineCommentCharacters["Matlab"] = "%";
    this->singleLineCommentCharacters["MMIXAL"] = "#";
    this->singleLineCommentCharacters["Nimrod"] = "!";
    this->singleLineCommentCharacters["extended crontab"] = "#";
    this->singleLineCommentCharacters["Dos Style"] = "REM";
    this->singleLineCommentCharacters["NSIS"] = ";";
    this->singleLineCommentCharacters["OScript"] = "//";
    this->singleLineCommentCharacters["Objective-C"] = "//";
    this->singleLineCommentCharacters["Pascal"] = "//";
    this->singleLineCommentCharacters["Perl"] = "#";
    this->singleLineCommentCharacters["PHP"] = "//";
    this->singleLineCommentCharacters["Postscript"] = "%";
    this->singleLineCommentCharacters["PowerShell"] = "#";
    this->singleLineCommentCharacters["Properties file"] = "#";
    this->singleLineCommentCharacters["PureBasic"] = ";";
    this->singleLineCommentCharacters["Python"] = "#";
    this->singleLineCommentCharacters["R"] = "#";
    this->singleLineCommentCharacters["REBOL"] = ";";
    this->singleLineCommentCharacters["registry"] = ";";
    this->singleLineCommentCharacters["rc"] = "#";
    this->singleLineCommentCharacters["Ruby"] = "#";
    this->singleLineCommentCharacters["Rust"] = "//";
    this->singleLineCommentCharacters["Scheme"] = ";";
    this->singleLineCommentCharacters["Smalltalk"] = "";
    this->singleLineCommentCharacters["spice"] = "*";
    this->singleLineCommentCharacters["SQL"] = "--";
    this->singleLineCommentCharacters["S-Record"] = "#";
    this->singleLineCommentCharacters["Swift"] = "//";
    this->singleLineCommentCharacters["TCL"] = "#";
    this->singleLineCommentCharacters["Tektronix extended HEX"] = "";
    this->singleLineCommentCharacters["TeX"] = "%";
    this->singleLineCommentCharacters["Text"] = "";
    this->singleLineCommentCharacters["VB / VBS"] = "'";
    this->singleLineCommentCharacters["txt2tags"] = "!";
    this->singleLineCommentCharacters["Verilog"] = "//";
    this->singleLineCommentCharacters["VHDL"] = "--";
    this->singleLineCommentCharacters["Visual Prolog"] = "%";
    this->singleLineCommentCharacters["XML"] = "";
    this->singleLineCommentCharacters["YAML"] = "#";
}

void ScintillaNext::dragEnterEvent(QDragEnterEvent *event)
{
    // Ignore all drag and drop events with urls and let the main application handle it
    if (event->mimeData()->hasUrls()) {
        return;
    }

    ScintillaEdit::dragEnterEvent(event);
}

void ScintillaNext::dropEvent(QDropEvent *event)
{
    // Ignore all drag and drop events with urls and let the main application handle it
    if (event->mimeData()->hasUrls()) {
        return;
    }

    ScintillaEdit::dropEvent(event);
}

bool ScintillaNext::readFromDisk(QFile &file)
{
    if (!file.exists()) {
        qWarning("Cannot read \"%s\": doesn't exist", qUtf8Printable(file.fileName()));
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Something bad happend when opening \"%s\": (%d) %s", qUtf8Printable(file.fileName()), file.error(), qUtf8Printable(file.errorString()));
        return false;
    }

    // TODO: figure out what to do if "size" is too big
    allocate(file.size());

    // Turn off undo collection and block signals during loading
    setUndoCollection(false);
    blockSignals(true);
    // TODO disable notifications
    // modEventMask(SC_MOD_NONE)?

    QByteArray chunk;
    qint64 bytesRead;

    QTextDecoder *decoder = nullptr;
    bool first_read = true;
    do {
        // Try to read as much as possible
        chunk.resize(CHUNK_SIZE);
        bytesRead = file.read(chunk.data(), CHUNK_SIZE);

        chunk.resize(bytesRead);

        qDebug("Read %lld bytes", bytesRead);

        // TODO: this needs moved out of here. Would make much more sense to have a class (or classes)
        // responsible for handling low level situations like this to do things like:
        // - determine encoding
        // - determine space vs tabs
        // - determine indentation size
        if (first_read) {
            first_read = false;

            // Try uchardet library first
            uchardet_t ud = uchardet_new();
            if (uchardet_handle_data(ud, chunk.constData(), chunk.size()) != 0) {
                qWarning("uchardet failed to detect encoding");
            }
            uchardet_data_end(ud);

            QByteArray encoding(uchardet_get_charset(ud));
            uchardet_delete(ud);

            qInfo("Encoding detected as: %s", qUtf8Printable(encoding));

            QTextCodec *codec = QTextCodec::codecForName(encoding);
            if (codec) {
                decoder = codec->makeDecoder();
            } else {
                qWarning("No avialable Codecs for: \"%s\"", qUtf8Printable(encoding));
                qWarning("Falling back to QTextCodec::codecForUtfText()");

                if (chunk.size() >= 2)
                    qWarning("%d %d", chunk.at(0), chunk.at(1));

                codec = QTextCodec::codecForUtfText(chunk);
                decoder = codec->makeDecoder();

                qWarning("Using: %s", qUtf8Printable(codec->name()));
            }
        }

        QByteArray utf8_data = decoder->toUnicode(chunk).toUtf8();
        appendText(utf8_data.size(), utf8_data.constData());
    } while (!file.atEnd() && status() == SC_STATUS_OK);

    delete decoder;

    file.close();

    // Restore it back
    this->blockSignals(false);
    setUndoCollection(true);
    // modEventMask(SC_MODEVENTMASKALL)?

    if (status() != SC_STATUS_OK) {
        qWarning("something bad happend in document->add_data() %ld", status());
        return false;
    }

    if (bytesRead == -1) {
        qWarning("Something bad happend when reading disk %d %s", file.error(), qUtf8Printable(file.errorString()));
        return false;
    }

    return true;
}

QDateTime ScintillaNext::fileTimestamp()
{
    Q_ASSERT(bufferType != ScintillaNext::Temporary);

    fileInfo.refresh();
    qInfo("%s last modified %s", qUtf8Printable(fileInfo.fileName()), qUtf8Printable(fileInfo.lastModified().toString()));
    return fileInfo.lastModified();
}

void ScintillaNext::updateTimestamp()
{
    modifiedTime = fileTimestamp();
}

void ScintillaNext::setFileInfo(const QString &filePath)
{
    fileInfo.setFile(filePath);
    fileInfo.makeAbsolute();

    Q_ASSERT(fileInfo.exists());

    name = fileInfo.fileName();
    bufferType = ScintillaNext::File;
}
