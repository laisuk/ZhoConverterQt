/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#pragma once

#include "nscore.h"

#define ENOUGH_DATA_THRESHOLD 1024
#define MINIMUM_DATA_THRESHOLD 4

class CharDistributionAnalysis {
public:
    virtual ~CharDistributionAnalysis() = default;

    CharDistributionAnalysis() { Reset(PR_FALSE); }

    void HandleOneChar(const char *aStr, PRUint32 aCharLen) {
        if (const PRInt32 order = (aCharLen == 2) ? GetOrder(aStr) : -1; order >= 0) {
            ++mTotalChars;
            if (static_cast<PRUint32>(order) < mTableSize &&
                512 > mCharToFreqOrder[order])
                ++mFreqChars;
        }
    }

    [[nodiscard]] float GetConfidence() const;

    void Reset(const PRBool aIsPreferredLanguage) {
        mDone = PR_FALSE;
        mTotalChars = 0;
        mFreqChars = 0;
        mDataThreshold = aIsPreferredLanguage ? 0 : MINIMUM_DATA_THRESHOLD;
    }

    [[nodiscard]] PRBool GotEnoughData() const { return mTotalChars > ENOUGH_DATA_THRESHOLD; }

protected:
    virtual PRInt32 GetOrder(const char *str) { return -1; }

    PRBool mDone;
    PRUint32 mFreqChars;
    PRUint32 mTotalChars;
    PRUint32 mDataThreshold;
    const PRInt16 *mCharToFreqOrder;
    PRUint32 mTableSize;
    float mTypicalDistributionRatio;
};

class GB2312DistributionAnalysis : public CharDistributionAnalysis {
public:
    GB2312DistributionAnalysis();

protected:
    PRInt32 GetOrder(const char *str) override {
        if (static_cast<unsigned char>(str[0]) >= 0xb0 &&
            static_cast<unsigned char>(str[1]) >= 0xa1)
            return 94 * (static_cast<unsigned char>(str[0]) - 0xb0) +
                   static_cast<unsigned char>(str[1]) - 0xa1;
        return -1;
    }
};

class Big5DistributionAnalysis : public CharDistributionAnalysis {
public:
    Big5DistributionAnalysis();

protected:
    PRInt32 GetOrder(const char *str) override {
        if (static_cast<unsigned char>(str[0]) < 0xa4)
            return -1;

        if (static_cast<unsigned char>(str[1]) >= 0xa1)
            return 157 * (static_cast<unsigned char>(str[0]) - 0xa4) +
                   static_cast<unsigned char>(str[1]) - 0xa1 + 63;

        return 157 * (static_cast<unsigned char>(str[0]) - 0xa4) +
               static_cast<unsigned char>(str[1]) - 0x40;
    }
};


class SJISDistributionAnalysis : public CharDistributionAnalysis {
public:
    SJISDistributionAnalysis();

protected:
    //for sjis encoding, we are interested
    //  first  byte range: 0x81 -- 0x9f , 0xe0 -- 0xfe
    //  second byte range: 0x40 -- 0x7e,  0x81 -- oxfe
    //no validation needed here. State machine has done that
    PRInt32 GetOrder(const char *str) override {
        PRInt32 order;
        if (static_cast<unsigned char>(*str) >= static_cast<unsigned char>(0x81) && static_cast<unsigned char>(*str) <= static_cast<unsigned char>(0x9f))
            order = 188 * (static_cast<unsigned char>(str[0]) - static_cast<unsigned char>(0x81));
        else if (static_cast<unsigned char>(*str) >= static_cast<unsigned char>(0xe0) && static_cast<unsigned char>(*str) <= static_cast<unsigned char>(0xef))
            order = 188 * (static_cast<unsigned char>(str[0]) - static_cast<unsigned char>(0xe0) + 31);
        else
            return -1;
        order += static_cast<unsigned char>(*(str + 1)) - 0x40;
        if (static_cast<unsigned char>(str[1]) > static_cast<unsigned char>(0x7f))
            order--;
        return order;
    }
};
