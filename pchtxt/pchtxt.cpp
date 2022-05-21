/**
 * @file pchtxt.cpp
 * @brief Parser for the Patch Text format
 * @author 3096
 *
 * Copyright (c) 2020 3096
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pchtxt.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <set>
#include <unordered_map>
#include <sstream>

namespace pchtxt {

// CONSTANTS

constexpr auto COMMENT_IDENTIFIER = "/";
constexpr auto ECHO_IDENTIFIER = "#";
constexpr auto AUTHOR_IDENTIFIER_OPEN = "[";
constexpr auto AUTHOR_IDENTIFIER_CLOSE = "]";
constexpr auto AMS_CHEAT_IDENTIFIER_OPEN = "[";
constexpr auto AMS_CHEAT_IDENTIFIER_CLOSE = "]";
// meta tags
constexpr auto TITLE_TAG = "@title";
constexpr auto PROGRAM_ID_TAG = "@program";
constexpr auto URL_TAG = "@url";
constexpr auto NSOBID_TAG = "@nsobid";  // legacy
const auto META_TAGS = std::set<std::string_view>{TITLE_TAG, PROGRAM_ID_TAG, URL_TAG, NSOBID_TAG};
// parsing tags
constexpr auto ENABLED_TAG = "@enabled";
constexpr auto DISABLED_TAG = "@disabled";
constexpr auto STOP_PARSING_TAG = "@stop";
constexpr auto FLAG_TAG = "@flag";
// patch type strings
constexpr auto PATCH_TYPE_BIN = "bin";
constexpr auto PATCH_TYPE_HEAP = "heap";
constexpr auto PATCH_TYPE_AMS = "ams";
// flags
constexpr auto BIG_ENDIAN_FLAG = "be";
constexpr auto LITTLE_ENDIAN_FLAG = "le";
constexpr auto NSOBID_FLAG = "nsobid";
constexpr auto NROBID_FLAG = "nrobid";
constexpr auto OFFSET_SHIFT_FLAG = "offset_shift";
constexpr auto DEBUG_INFO_FLAG = "debug_info";
constexpr auto ALT_DEBUG_INFO_FLAG = "print_values";  // legacy

// IPS
constexpr auto IPS32_HEADER_MAGIC = "IPS32";
constexpr auto IPS32_FOOTER_MAGIC = "EEOF";

// utils

inline auto isStartsWith(std::string& checkedStr, std::string_view targetStr) {
    return checkedStr.size() >= targetStr.size() and checkedStr.substr(0, targetStr.size()) == targetStr;
}

inline void ltrim(std::string& str) {
    str.erase(begin(str), std::find_if(begin(str), end(str), [](char ch) { return not std::isspace(ch); }));
}

inline void rtrim(std::string& str) {
    str.erase(std::find_if(rbegin(str), rend(str), [](char ch) { return !std::isspace(ch); }).base(), end(str));
}

inline void trim(std::string& str) {
    ltrim(str);
    rtrim(str);
}

inline auto firstToken(std::string& str) {
    return std::string(begin(str), std::find_if(begin(str), end(str), [](char ch) { return std::isspace(ch); }));
}

inline auto commentPos(std::string& str) {
    auto pos = 0;
    auto isInString = false;
    for (auto ch : str) {
        if (ch == COMMENT_IDENTIFIER[0] and not isInString) {
            break;
        }
        if (ch == '"') {
            isInString = !isInString;
        }
        pos++;
    }
    return pos;
}

inline auto getLineCommentContent(std::string& str) {
    auto commentContentStart = std::find_if(begin(str) + commentPos(str), end(str), [](char ch) {
        return not(std::isspace(ch) or ch == COMMENT_IDENTIFIER[0]);
    });
    auto result = std::string(commentContentStart, end(str));
    return result;
}

inline auto getLineNoComment(std::string& lineStr) {
    auto result = lineStr.substr(0, commentPos(lineStr));
    rtrim(result);
    return result;
}

inline auto getStringToLowerCase(std::string& str) {
    auto result = std::string(str);
    std::transform(begin(result), end(result), begin(result), [](char ch) { return std::tolower(ch); });
    return result;
}

inline auto stringIsHex(std::string& str) {
    return std::find_if(begin(str), end(str), [](char ch) { return not std::isxdigit(ch); }) == end(str);
}

inline void trimZeros(std::string& str) { str.erase(0, std::min(str.find_first_not_of('0'), str.size() - 1)); }

inline void escapeString(std::string& str) {
    auto escapingPos = begin(str);
    auto targetPos = begin(str);

    while (escapingPos != end(str)) {
        if (*escapingPos == '\\' and escapingPos + 1 != end(str)) {
            escapingPos++;

            switch (*escapingPos) {
                case 'a':
                    *targetPos = '\a';
                    break;
                case 'b':
                    *targetPos = '\b';
                    break;
                case 'f':
                    *targetPos = '\f';
                    break;
                case 'n':
                    *targetPos = '\n';
                    break;
                case 'r':
                    *targetPos = '\r';
                    break;
                case 't':
                    *targetPos = '\t';
                    break;
                case 'v':
                    *targetPos = '\v';
                    break;
                default:
                    *targetPos = *escapingPos;
            }
        } else {
            *targetPos = *escapingPos;
        }
        escapingPos++;
        targetPos++;
    }

    if (targetPos != end(str)) str.erase(targetPos, end(str));
}

inline auto getHexCharNibble(char ch) -> uint8_t {
    if (ch >= 'A' and ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' and ch <= 'f') return ch - 'a' + 10;
    return ch - '0';  // this is okay because we already check the string is hex
}

inline auto getHexByte(std::string::iterator& strIter) -> uint8_t {
    return (getHexCharNibble(*strIter) << 4) + getHexCharNibble(*(strIter + 1));
}

// not utils

auto parsePchtxt(std::istream& input) -> PatchTextOutput {
    auto throwAwaySs = std::stringstream{};
    return parsePchtxt(input, throwAwaySs);
}

auto parsePchtxt(std::istream& input, std::ostream& logOs) -> PatchTextOutput {
    auto result = PatchTextOutput{};

    // parse meta
    auto curPos = input.tellg();
    result.meta = getPchtxtMeta(input, logOs);
    input.seekg(curPos);

    // parsing status
    auto curLineNum = 1;
    auto lastCommentLine = std::string{};
    auto curPatch = Patch{};
    auto curPatchCollection = PatchCollection{};
    auto curOffsetShift = 0;
    auto curIsBigEndian = false;
    auto isAcceptingPatch = false;
    auto stopParsing = false;
    auto logDebugInfo = false;

    auto line = std::string{};
    while (true) {
        if (stopParsing) break;

        if (not std::getline(input, line)) {
            logOs << "done parsing patches" << std::endl;
            break;
        }
        trim(line);
        auto lineNoComment = getLineNoComment(line);
        auto lineNoCommentLower = getStringToLowerCase(lineNoComment);

        switch (line[0]) {
            case '@': {  // tags
                auto curTag = firstToken(lineNoCommentLower);

                if (curTag == STOP_PARSING_TAG) {  // stop parsing
                    logOs << "L" << curLineNum << ": done parsing patches (reached tag @stop)" << std::endl;
                    stopParsing = true;
                    break;

                } else if (curTag == ENABLED_TAG or curTag == DISABLED_TAG) {  // start of a new patch
                    // store current
                    if (curPatchCollection.buildId.empty()) {
                        logOs << "L" << curLineNum << ": ERROR: missing build id, abort parsing" << std::endl;
                        return {};
                    }

                    if (not curPatch.contents.empty()) {
                        curPatchCollection.patches.push_back(curPatch);
                        logOs << "L" << curLineNum << ": patch read: " << curPatch.name << std::endl;
                        // start new patch
                        curPatch = Patch{};
                    }

                    if (curTag == ENABLED_TAG) {
                        curPatch.enabled = true;
                    } else {
                        curPatch.enabled = false;
                    }

                    curPatch.lineNum = curLineNum;

                    if (curPatch.type != AMS) {  // don't use last comment on AMS style patch titles
                        // extract name and author from last comment
                        auto authorStartPos = lastCommentLine.rfind(AUTHOR_IDENTIFIER_OPEN);
                        auto authorEndPos = lastCommentLine.rfind(AUTHOR_IDENTIFIER_CLOSE);
                        auto patchName = lastCommentLine.substr(0, authorStartPos);
                        rtrim(patchName);
                        auto author =
                            authorStartPos != std::string::npos
                                ? lastCommentLine.substr(authorStartPos + 1, authorEndPos - authorStartPos - 1)
                                : std::string{};
                        trim(author);
                        curPatch.name = patchName;
                        curPatch.author = author;
                    }

                    // check patch type
                    auto lineAfterTag = lineNoCommentLower.substr(curTag.size());
                    ltrim(lineAfterTag);
                    auto patchType = firstToken(lineAfterTag);
                    if (patchType == PATCH_TYPE_HEAP) {
                        curPatch.type = HEAP;
                    } else if (patchType == PATCH_TYPE_AMS) {
                        curPatch.type = AMS;
                    }

                    isAcceptingPatch = true;

                    if (logDebugInfo) logOs << "L" << curLineNum << ": parsing patch: " << curPatch.name << std::endl;

                } else if (curTag == FLAG_TAG) {  // parse flag
                    auto flagContent = lineNoComment.substr(curTag.size());
                    ltrim(flagContent);
                    auto flagType = firstToken(flagContent);
                    ltrim(flagType);
                    auto flagValue = flagContent.substr(flagType.size());
                    ltrim(flagValue);
                    flagType = getStringToLowerCase(flagType);

                    if (flagType == BIG_ENDIAN_FLAG) {
                        curIsBigEndian = true;

                    } else if (flagType == LITTLE_ENDIAN_FLAG) {
                        curIsBigEndian = false;

                    } else if (flagType == NSOBID_FLAG or flagType == NROBID_FLAG) {
                        // wrap up last bid collection
                        if (not curPatch.contents.empty()) {
                            curPatchCollection.patches.push_back(curPatch);
                            logOs << "L" << curLineNum << ": patch read: " << curPatch.name << std::endl;
                        }
                        curPatch = Patch{};
                        if (not curPatchCollection.patches.empty()) {
                            result.collections.push_back(curPatchCollection);
                            if (logDebugInfo)
                                logOs << "L" << curLineNum << ": parsing stopped for " << curPatchCollection.buildId
                                      << std::endl;
                            curPatchCollection = PatchCollection{};
                        }

                        // check if new bid exist
                        auto existingCollection = std::find_if(
                            begin(result.collections), end(result.collections),
                            [flagValue](PatchCollection& collection) { return collection.buildId == flagValue; });

                        if (existingCollection != end(result.collections)) {  // bid already exist
                            curPatchCollection = *existingCollection;
                            result.collections.erase(existingCollection);
                        } else {
                            // set up patch collection for new bid
                            curPatchCollection.buildId = flagValue;
                            if (flagType == NROBID_FLAG) {
                                curPatchCollection.targetType = NRO;
                            } else {
                                curPatchCollection.targetType = NSO;
                            }
                        }

                        isAcceptingPatch = false;  // don't accept anymore patches since we just started new bid

                        if (logDebugInfo)
                            logOs << "L" << curLineNum << ": parsing started for " << curPatchCollection.buildId
                                  << std::endl;

                    } else if (flagType == OFFSET_SHIFT_FLAG) {
                        curOffsetShift = std::stoi(flagValue, nullptr, 0);
                        if (logDebugInfo)
                            logOs << "L" << curLineNum << ": offset shift is now " << curOffsetShift << std::endl;

                    } else if (flagType == DEBUG_INFO_FLAG or flagType == ALT_DEBUG_INFO_FLAG) {
                        logDebugInfo = true;
                        logOs << "L" << curLineNum << ": additional debug info enabled" << std::endl;

                    } else {
                        logOs << "L" << curLineNum << ": WARNING ignored unrecognized flag type: " << flagType
                              << std::endl;
                    }

                } else if (isStartsWith(lineNoCommentLower, NSOBID_TAG)) {  // legacy style nsobid
                    if (not(lineNoCommentLower.size() > std::string_view(NSOBID_TAG).size() + 1)) {
                        logOs << "L" << curLineNum << ": ERROR: legacy nsobid tag missing value" << std::endl;
                        return {};
                    }
                    curPatchCollection.targetType = NSO;
                    curPatchCollection.buildId = lineNoComment.substr(std::string_view(NSOBID_TAG).size() + 1);
                    ltrim(curPatchCollection.buildId);

                    if (logDebugInfo)
                        logOs << "L" << curLineNum << ": parsing started for " << curPatchCollection.buildId
                              << " (legacy style bid)" << std::endl;

                } else if (META_TAGS.find(curTag) == end(META_TAGS)) {  // check if tag is bad
                    logOs << "L" << curLineNum << ": WARNING ignored unrecognized tag: " << curTag << std::endl;
                }
                break;
            }

            case '#': {  // echo identifier
                logOs << "L" << curLineNum << ": " << line << std::endl;
                break;
            }

            case AMS_CHEAT_IDENTIFIER_OPEN[0]: {  // AMS cheat
                // store current
                if (curPatchCollection.buildId.empty()) {
                    logOs << "L" << curLineNum << ": ERROR: missing build id, abort parsing" << std::endl;
                    return {};
                }

                if (not curPatch.contents.empty()) {
                    curPatchCollection.patches.push_back(curPatch);
                    logOs << "L" << curLineNum << ": patch read: " << curPatch.name << std::endl;
                }

                // start new patch
                auto amsCheatName = lineNoComment.substr(1, lineNoComment.rfind(AMS_CHEAT_IDENTIFIER_CLOSE) - 1);
                trim(amsCheatName);
                curPatch = Patch{amsCheatName, {}, AMS, true, curLineNum, {}};

                if (logDebugInfo) logOs << "L" << curLineNum << ": parsing AMS cheat: " << curPatch.name << std::endl;

                break;
            }

            case '/': {  // comment identifier
                lastCommentLine = getLineCommentContent(line);
                break;
            }

            default: {
                if (not isAcceptingPatch) break;

                // skip empty lines
                if (line.empty()) {
                    break;
                }

                // parse patch contents
                if (curPatch.type == AMS) {  // for AMS cheats, just add line as plain text
                    curPatch.contents.push_back({0, {begin(lineNoComment), end(lineNoComment)}});

                    if (logDebugInfo) logOs << "L" << curLineNum << ": AMS cheat: " << lineNoComment << std::endl;
                    break;
                }

                // parse values
                auto offsetStr = firstToken(lineNoCommentLower);
                auto valueStr = lineNoCommentLower.substr(offsetStr.size());

                // check offset
                if (not stringIsHex(offsetStr)) {
                    if (logDebugInfo)
                        logOs << "L" << curLineNum << ": line ignored: invalid offset: " << line << std::endl;
                    break;
                }
                trimZeros(offsetStr);
                if (offsetStr.size() > 8) {
                    logOs << "L" << curLineNum << ": ERROR: offset: " << offsetStr << " out of range" << std::endl;
                    return {};
                }

                auto offset = static_cast<uint32_t>(std::stoul(offsetStr, nullptr, 16)) + curOffsetShift;
                auto patchContent = PatchContent{offset, {}};

                // parse value
                ltrim(valueStr);
                if (valueStr[0] == '"') {  // string patch
                    auto closingPosSearch = begin(valueStr);
                    while (true) {  // find string closing pos
                        closingPosSearch++;

                        if ((closingPosSearch = std::find(closingPosSearch, end(valueStr), '"')) == end(valueStr)) {
                            logOs << "L" << curLineNum << ": ERROR: cannot find string closing: " << valueStr
                                  << std::endl;
                            return {};
                        }

                        if (*(closingPosSearch - 1) != '\\') {
                            break;
                        }
                    }

                    // escape chars
                    auto stringValueStr = std::string{begin(valueStr) + 1, closingPosSearch};
                    escapeString(stringValueStr);

                    patchContent.value = {begin(stringValueStr), end(stringValueStr)};
                    patchContent.value.push_back('\0');

                } else {            // hex values patch
                    while (true) {  // parse value token by token
                        // get next token
                        auto valueTokenStr = firstToken(valueStr);
                        valueStr = valueStr.substr(valueTokenStr.size());
                        ltrim(valueStr);
                        if (valueTokenStr.empty()) {
                            break;
                        }

                        // check token
                        if (valueTokenStr.size() % 2 != 0) {
                            logOs << "L" << curLineNum << ": ERROR: bad length for hex values: " << valueTokenStr
                                  << std::endl;
                            return {};
                        }
                        if (not stringIsHex(valueTokenStr)) {
                            logOs << "L" << curLineNum << ": ERROR: not valid hex values: " << valueTokenStr
                                  << std::endl;
                            return {};
                        }

                        // parse token value
                        if (curIsBigEndian) {
                            auto curBytePos = end(valueTokenStr);
                            while (curBytePos != begin(valueTokenStr)) {
                                curBytePos -= 2;
                                patchContent.value.push_back(getHexByte(curBytePos));
                            }
                        } else {
                            for (auto curBytePos = begin(valueTokenStr); curBytePos != end(valueTokenStr);
                                 curBytePos += 2) {
                                patchContent.value.push_back(getHexByte(curBytePos));
                            }
                        }
                    }
                }

                curPatch.contents.push_back(patchContent);
                if (logDebugInfo) {
                    logOs << "L" << curLineNum << ": offset: " << std::hex << std::setfill('0') << std::setw(8)
                          << patchContent.offset << " value: ";
                    for (auto byte : patchContent.value) logOs << std::setw(2) << static_cast<int>(byte);
                    logOs << std::dec << " len: " << patchContent.value.size() << std::endl;
                }
            }
        }

        curLineNum++;
    }

    // add last patch and collection
    if (not curPatch.contents.empty()) {
        curPatchCollection.patches.push_back(curPatch);
        logOs << "L" << curLineNum << ": patch read: " << curPatch.name << std::endl;
    }
    if (not curPatchCollection.patches.empty()) {
        result.collections.push_back(curPatchCollection);
        if (logDebugInfo)
            logOs << "L" << curLineNum << ": parsing completed for " << curPatchCollection.buildId << std::endl;
    }

    return result;
}

auto getPchtxtMeta(std::istream& input) -> PatchTextMeta {
    auto throwAwaySs = std::stringstream{};
    return getPchtxtMeta(input, throwAwaySs);
}

auto getPchtxtMeta(std::istream& input, std::ostream& logOs) -> PatchTextMeta {
    auto result = PatchTextMeta{};
    auto tagToValueMap = std::unordered_map<std::string_view, std::string&>{
        {TITLE_TAG, result.title}, {PROGRAM_ID_TAG, result.programId}, {URL_TAG, result.url}};

    auto legacyTitle = std::string{};

    auto curLineNum = 1;
    auto line = std::string{};
    while (true) {
        if (not std::getline(input, line)) {
            logOs << "meta parsing reached end of file" << std::endl;
            break;
        }
        trim(line);

        // meta should stop at an empty line
        if (line.empty()) {
            logOs << "L" << curLineNum << ": done parsing meta" << std::endl;
            break;
        }

        line = getLineNoComment(line);
        auto lineLower = getStringToLowerCase(line);

        if (line[0] == '@') {
            auto curTag = firstToken(lineLower);
            if (curTag == STOP_PARSING_TAG) {
                logOs << "done parsing meta (reached tag @stop)" << std::endl;
                break;
            }

            auto curTagFound = tagToValueMap.find(curTag);
            if (curTagFound != end(tagToValueMap)) {
                auto curTagValue = line.substr(curTag.size());
                ltrim(curTagValue);
                // strip quatation marks if necessary
                if (curTagValue[0] == '"' and curTagValue[curTagValue.size() - 1] == '"') {
                    curTagValue = curTagValue.substr(1, curTagValue.size() - 2);
                }
                curTagFound->second = curTagValue;
                logOs << "L" << curLineNum << ": meta: " << curTag << "=" << curTagValue << std::endl;
            }
        } else if (line[0] == '#') {  // echo identifier
            logOs << "L" << curLineNum << ": " << line << std::endl;
            legacyTitle = line.substr(1);
            ltrim(legacyTitle);
        }

        curLineNum++;
    }

    if (result.title.empty()) {
        result.title = legacyTitle;
        logOs << "using \"" << legacyTitle << "\" as legacy style title" << std::endl;
    }

    return result;
}

void writeIps(PatchCollection& patchCollection, std::ostream& ostream) {
    ostream.write(IPS32_HEADER_MAGIC, std::strlen(IPS32_HEADER_MAGIC));
    for (auto& patch : patchCollection.patches) {
        if (patch.type != BIN or patch.enabled == false) continue;
        for (auto& patchContent : patch.contents) {
            for (auto rightShift : {3, 2, 1, 0}) {
                auto byteToWrite = static_cast<char>((patchContent.offset >> rightShift * 8) & 0xFF);
                ostream.write(&byteToWrite, 1);
            }
            for (auto rightShift : {1, 0}) {
                auto byteToWrite = static_cast<char>((patchContent.value.size() >> rightShift * 8) & 0xFF);
                ostream.write(&byteToWrite, 1);
            }
            ostream.write(reinterpret_cast<char*>(patchContent.value.data()), patchContent.value.size());
        }
    }
    ostream.write(IPS32_FOOTER_MAGIC, std::strlen(IPS32_FOOTER_MAGIC));
}

}  // namespace pchtxt
