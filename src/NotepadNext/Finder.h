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


#ifndef FINDER_H
#define FINDER_H

#include "ScintillaNext.h"

class Finder
{
public:
    explicit Finder(ScintillaNext *edit);

    void setEditor(ScintillaNext *edit) { this->editor = edit; }
    void setSearchFlags(int flags);
    void setWrap(bool wrap);
    void setSearchText(const QString &text);

    Sci_CharacterRange findNext(int startPos = INVALID_POSITION);
    Sci_CharacterRange findPrev();
    int count();

    Sci_CharacterRange replaceSelectionIfMatch(const QString &replaceText);
    int replaceAll(const QString &replaceText);

private:
    ScintillaNext *editor;

    bool wrap = false;
    QString text;
};

#endif // FINDER_H
