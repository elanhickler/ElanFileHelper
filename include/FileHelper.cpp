#include "FileHelper.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>
#endif

using std::invalid_argument;
using std::string;
using std::vector;
namespace fs = std::filesystem;

static const string illegal_filename_chars  = "*?|<>\"\\/:";
static const string illegal_directory_chars = "*?|<>\"";

#ifdef _WIN32
static const string systemSlash = "\\";
#else
static const string systemSlash = "/";
#endif

/* internal helpers */
namespace {

#ifdef _WIN32
// Paths are UTF-8 at the API boundary. Windows' narrow fs::path constructor
// would interpret them in the legacy system codepage, mangling anything
// non-ASCII -- so convert explicitly. Invalid UTF-8 (e.g. a legacy
// ANSI-encoded string) falls back to the system codepage instead of failing.
std::wstring utf8ToWide(const string& s) {
    if (s.empty()) {
        return {};
    }
    UINT codePage = CP_UTF8;
    int wideLen   = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), int(s.size()), nullptr, 0);
    if (wideLen == 0) {
        codePage = CP_ACP;
        wideLen  = MultiByteToWideChar(CP_ACP, 0, s.c_str(), int(s.size()), nullptr, 0);
        if (wideLen == 0) {
            return {};
        }
    }
    std::wstring w(size_t(wideLen), L'\0');
    MultiByteToWideChar(codePage, 0, s.c_str(), int(s.size()), &w[0], wideLen);
    return w;
}

string wideToUtf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    int narrowLen = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    if (narrowLen == 0) {
        return {};
    }
    string s(size_t(narrowLen), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), &s[0], narrowLen, nullptr, nullptr);
    return s;
}

fs::path toPath(const string& s) {
    return fs::path(utf8ToWide(s));
}

string fromPath(const fs::path& p) {
    return wideToUtf8(p.wstring());
}
#else
fs::path toPath(const string& s) {
    return fs::path(s);
}

string fromPath(const fs::path& p) {
    return p.string();
}
#endif

string trimCopy(const string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == string::npos) {
        return {};
    }
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

string stripTrailingSlashes(const string& s) {
    size_t e = s.size();
    // keep a lone root slash / drive root ("C:/") intact
    while (e > 0 && (s[e - 1] == '/' || s[e - 1] == '\\')) {
        if (e == 1) {
            break;
        }
        if (e == 3 && s[1] == ':') {
            break;
        }
        --e;
    }
    return s.substr(0, e);
}

char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

// Case-insensitive wildcard match supporting '*' and '?'.
bool wildcardMatch(const string& text, const string& pattern) {
    if (pattern == "*" || pattern.empty()) {
        return true;
    }
    size_t t = 0, p = 0, starIdx = string::npos, match = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || asciiLower(pattern[p]) == asciiLower(text[t]))) {
            ++t;
            ++p;
        } else if (p < pattern.size() && pattern[p] == '*') {
            starIdx = p++;
            match   = t;
        } else if (starIdx != string::npos) {
            p = starIdx + 1;
            t = ++match;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

string toForwardSlashes(string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

string joinRange(const vector<string>& parts, const string& sep, size_t begin, size_t end) {
    string out;
    for (size_t i = begin; i < end && i < parts.size(); ++i) {
        if (i > begin) {
            out += sep;
        }
        out += parts[i];
    }
    return out;
}

vector<string> splitOnSlashes(const string& s) {
    vector<string> parts;
    string current;
    for (char c : s) {
        if (c == '/' || c == '\\') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

string getRandomAlphaNumericString(int length) {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::mt19937 gen{ std::random_device{}() };
    std::uniform_int_distribution<int> dist(0, int(sizeof(alphabet) - 2));
    string out;
    for (int i = 0; i < length; ++i) {
        out += alphabet[dist(gen)];
    }
    return out;
}

string pad2(int v) {
    string s = std::to_string(v);
    return s.size() < 2 ? "0" + s : s;
}

} // namespace

/* Unknown errors could be a permissions error. */

std::map<FileHelper::Error, string> FileHelper::errorMessages = {
  // Formatting errors
    {                       FileHelper::Error::empty_string,                                                                                                          "Input string is empty."},
    {                       FileHelper::Error::illegal_char,                                                                                                        "Illegal character found."},
    {                   FileHelper::Error::path_is_relative,                                                                                                   "File path cannot be relative."},
    {                        FileHelper::Error::path_is_dir,                                                                                                "File path cannot be a directory."},
    {                   FileHelper::Error::space_before_ext,                                                                                                         "Space before extension."},
    {                  FileHelper::Error::begins_with_space,                                                                                                              "Begins with space."},
    {                    FileHelper::Error::ends_with_space,                                                                                                                "Ends with space."},
    {                      FileHelper::Error::ends_with_dot,                                                                                                                  "Ends with dot."},
    {          FileHelper::Error::ends_or_begins_with_space,                                                                                                      "Ends or begins with space."},
 // Existence errors
    {                FileHelper::Error::file_already_exists,                                                                                                            "File already exists."},
    {              FileHelper::Error::folder_already_exists,                                                                                                          "Folder already exists."},
    {      FileHelper::Error::file_already_exists_as_folder,                                                                                                "File already exists as a folder."},
    {         FileHelper::Error::directory_with_multi_slash,                                                                                   "Directory path has multiple slashes in a row."},
    {   FileHelper::Error::directory_already_exists_as_file,                                                                                               "Directory already exists as file."},
    {                FileHelper::Error::path_does_not_exist,                                                                                        "Path does not exist as a file or folder."},
    {                FileHelper::Error::file_does_not_exist,                                                                                                            "File does not exist."},
    {           FileHelper::Error::directory_does_not_exist,                                                                                                       "Directory does not exist."},
 // Creation errors
    {           FileHelper::Error::unknown_error_create_dir,                            "Unknown error creating directory, check that path is valid, drive letter, or file write permissions."},
    {  FileHelper::Error::unknown_error_file_move_or_rename,                "Unknown error moving or renaming file, may be in use by another program or check file permissions or disk space."},
    {FileHelper::Error::unknown_error_folder_move_or_rename, "Unknown error moving or renaming folder, files inside may be in use by another program or check file permissions or disk space."},
    {           FileHelper::Error::unknown_error_file_trash,                   "Unknown error moving file to trash, may be in use by another program or check file permissions or disk space."},
    {          FileHelper::Error::unknown_error_file_delete,                                        "Unknown error deleting file, may be in use by another program or check file permissions."},
    {          FileHelper::Error::unknown_error_create_file,                           "Unknown error creating file, check that path is valid, drive letter, file permissions, or disk space."},
    {       FileHelper::Error::unknown_error_overwrite_file,                        "Unkown error overwriting file, may be in use by another program or check file permissions or disk space."},
    {            FileHelper::Error::unknown_error_file_copy,                            "Unknown error copying file, check that path is valid, drive letter, file permissions, or disk space."},
    {          FileHelper::Error::unknown_error_modify_file,                         "Unknown error modifying file, may be in use by another program or check file permissions or disk space."},
};

void FileHelper::throwError(const string& path, FileHelper::Error v) {
    throw invalid_argument(path + " -> " + errorMessages[v]);
}

bool FileHelper::isSlashChar(char c) {
    return c == '\\' || c == '/';
}

bool FileHelper::isIllegalNameChar(char c) {
    return (static_cast<unsigned char>(c) <= 31) || illegal_filename_chars.find(c) != string::npos;
}

bool FileHelper::isIllegalDirChar(char c) {
    return (static_cast<unsigned char>(c) <= 31) || illegal_directory_chars.find(c) != string::npos;
}

string FileHelper::createLegalString(const string& filePathOrName) {
    // Split on the last slash: everything before is directory, after is name.
    string input = filePathOrName;
    size_t lastSlash = input.find_last_of("\\/");

    string fileFolder = lastSlash == string::npos ? string{} : trimCopy(input.substr(0, lastSlash + 1));
    string fileName   = trimCopy(lastSlash == string::npos ? input : input.substr(lastSlash + 1));

    string out;

    bool lastWasSlash = false;
    for (char c : fileFolder) {
        if (isSlashChar(c)) {
            if (!lastWasSlash) {
                out += '/';
            }
            lastWasSlash = true;
            continue;
        }
        lastWasSlash = false;
        out += isIllegalDirChar(c) ? '_' : c;
    }

    if (!fileName.empty()) {
        if (!out.empty() && out.back() != '/') {
            out += '/';
        }
        for (char c : fileName) {
            out += isIllegalNameChar(c) ? '_' : c;
        }
    }

    return out;
}

string FileHelper::setFilePath(const string& originalFile, const string& filePath, bool overwrite) {
    if (originalFile == filePath) {
        return originalFile;
    }

    ensureValidFilePath(filePath);

    if (doesFileExist(filePath)) {
        if (!overwrite) {
            throwError(filePath, FileHelper::Error::file_already_exists);
        }
        std::error_code ec;
        if (!fs::remove(toPath(filePath), ec) || ec) {
            throwError(filePath, FileHelper::Error::unknown_error_overwrite_file);
        }
    }

    std::error_code ec;
    fs::create_directories(toPath(getDir(filePath)), ec);
    if (!doesFolderExist(getDir(filePath))) {
        throwError(FileHelper::getDir(filePath), FileHelper::Error::unknown_error_create_dir);
    }

    fs::rename(toPath(originalFile), toPath(filePath), ec);
    if (ec) {
        throwError(originalFile + " -> " + filePath, FileHelper::Error::unknown_error_file_move_or_rename);
    }

    return filePath;
}

string FileHelper::setPath(const string& originalPath, const string& path, bool overwrite) {
    if (originalPath == path) {
        return originalPath;
    }

    ensureValidPath(path);

    if (doesFileExist(path)) {
        if (!overwrite) {
            throwError(path, FileHelper::Error::file_already_exists);
        }
        std::error_code ec;
        if (!fs::remove(toPath(path), ec) || ec) {
            throwError(path, FileHelper::Error::unknown_error_overwrite_file);
        }
    }

    std::error_code ec;
    fs::create_directories(toPath(getDir(path)), ec);
    if (!doesFolderExist(getDir(path))) {
        throwError(FileHelper::getDir(path), FileHelper::Error::unknown_error_create_dir);
    }

    fs::rename(toPath(originalPath), toPath(path), ec);
    if (ec) {
        throwError(originalPath + " -> " + path, FileHelper::Error::unknown_error_file_move_or_rename);
    }

    return path;
}

string FileHelper::getUnusedFilePath(string filePath, bool replaceIllegalChars) {
    if (replaceIllegalChars) {
        filePath = createLegalString(filePath);
    }

    string fileDir, fileName, fileExt;
    getFileParts(filePath, fileDir, fileName, fileExt);

    string dotExt = fileExt.empty() ? string{} : "." + fileExt;

    string candidate = fileDir + "/" + fileName + dotExt;
    int n = 2;
    while (doesPathExist(candidate)) {
        candidate = fileDir + "/" + fileName + "(" + std::to_string(n) + ")" + dotExt;
        ++n;
    }

    return candidate;
}

bool FileHelper::hasExt(const string& filePath) {
    size_t indexOfDot = filePath.find_last_of('.');
    // dot is found && is not first char && is not last char && after the last slash
    if (indexOfDot == string::npos || indexOfDot == 0 || indexOfDot == filePath.size() - 1) {
        return false;
    }
    size_t lastSlash = filePath.find_last_of("\\/");
    if (lastSlash != string::npos && indexOfDot < lastSlash) {
        return false;
    }
    return true;
}

string FileHelper::getExt(const string& f) {
    if (!hasExt(f)) {
        return {};
    }
    return f.substr(f.find_last_of('.'));
}

string FileHelper::setExt(const string& file, const string& ext) {
    if (hasExt(file)) {
        return file.substr(0, file.find_last_of('.')) + "." + ext;
    }
    return file + "." + ext;
}

string FileHelper::assumeExt(const string& file, const string& ext) {
    return hasExt(file) ? file : setExt(file, ext);
}

string FileHelper::assumeFolder(const string& file, const string& folder) {
    return isRelativePath(file) ? folder + "/" + file : file;
}

string FileHelper::assume(const string& file, const string& folder, const string& ext) {
    return assumeFolder(assumeExt(file, ext), folder);
}

string FileHelper::removeExt(const string& file) {
    return getNameNoExt(file);
}

string FileHelper::addSuffix(const string& fileName, const string& suffix) {
    if (!hasExt(fileName)) {
        return fileName + suffix;
    }
    size_t dot = fileName.find_last_of('.');
    return fileName.substr(0, dot) + suffix + fileName.substr(dot);
}

string FileHelper::getDir(const string& f) {
    string s = stripTrailingSlashes(toForwardSlashes(f));
    size_t lastSlash = s.find_last_of('/');
    if (lastSlash == string::npos) {
        return {};
    }
    if (lastSlash == 0) {
        return "/";
    }
    if (lastSlash == 2 && s[1] == ':') {
        return s.substr(0, 3); // "C:/"-style root
    }
    return s.substr(0, lastSlash);
}

string FileHelper::getName(const string& path) {
    string s = stripTrailingSlashes(toForwardSlashes(path));
    size_t lastSlash = s.find_last_of('/');
    return lastSlash == string::npos ? s : s.substr(lastSlash + 1);
}

string FileHelper::getNameNoExt(const string& path) {
    string name = getName(path);
    if (!hasExt(name)) {
        return name;
    }
    return name.substr(0, name.find_last_of('.'));
}

void FileHelper::getFileParts(const string& f, string& driveLetterOut, string& fileDirOut, string& fileNameNoExtOut, string& fileExtOut) {
    if (FileHelper::isValidFileName(f)) {
        driveLetterOut   = "";
        fileDirOut       = "";
        fileNameNoExtOut = FileHelper::getNameNoExt(f);
        fileExtOut       = hasExt(f) ? FileHelper::getExt(f).substr(1) : string{};
        return;
    }

    if (isRelativePath(f)) {
        vector<string> splits = splitOnSlashes(f);
        if (!splits.empty() && isValidFileName(splits.back()) && hasExt(splits.back())) {
            string fileName  = splits.back();
            driveLetterOut   = "";
            fileDirOut       = joinRange(splits, systemSlash, 0, splits.size() - 1);
            fileNameNoExtOut = FileHelper::getNameNoExt(fileName);
            fileExtOut       = FileHelper::getExt(fileName).substr(1);
        } else {
            driveLetterOut   = "";
            fileDirOut       = joinRange(splits, systemSlash, 0, splits.size());
            fileNameNoExtOut = "";
            fileExtOut       = "";
        }
        return;
    }

#ifdef _WIN32
    driveLetterOut = f.substr(0, 1);
    string dir     = getDir(f);
    fileDirOut     = dir.size() > 3 ? dir.substr(3) : string{};
#else
    driveLetterOut = systemSlash;
    string dir     = getDir(f);
    fileDirOut     = dir.size() > 1 ? dir.substr(1) : string{};
#endif
    fileNameNoExtOut = getNameNoExt(f);
    fileExtOut       = hasExt(f) ? getExt(f).substr(1) : string{};
}

void FileHelper::getFileParts(const string& f, string& fileDirOut, string& fileNameNoExtOut, string& fileExtOut) {
    fileDirOut       = FileHelper::isValidFilePath(f) ? getDir(f) : stripTrailingSlashes(toForwardSlashes(f));
    fileNameNoExtOut = getNameNoExt(f);
    fileExtOut       = hasExt(f) ? getExt(f).substr(1) : string{};
}

void FileHelper::getFileParts(const string& f, string& fileDirOut, string& fileNameOut) {
    fileDirOut  = FileHelper::isValidFilePath(f) ? getDir(f) : stripTrailingSlashes(toForwardSlashes(f));
    fileNameOut = getName(f);
}

/* OS OPEN / REVEAL */

namespace {

void osOpen(const string& path) {
#ifdef _WIN32
    fs::path p = fs::absolute(toPath(path));
    ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    #ifdef __APPLE__
    std::system(("open \"" + path + "\"").c_str());
    #else
    std::system(("xdg-open \"" + path + "\"").c_str());
    #endif
#endif
}

void osReveal(const string& path) {
#ifdef _WIN32
    fs::path p    = fs::absolute(toPath(path));
    std::wstring params = L"/select,\"" + p.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
#else
    #ifdef __APPLE__
    std::system(("open -R \"" + path + "\"").c_str());
    #else
    std::system(("xdg-open \"" + FileHelper::getDir(path) + "\"").c_str());
    #endif
#endif
}

} // namespace

void FileHelper::openFolder(const string& directory, bool createIfNeeded) {
    ensureValidDir(directory);

    if (!doesFolderExist(directory)) {
        if (createIfNeeded) {
            createFolder(directory);
        } else {
            throwError(directory, FileHelper::Error::directory_does_not_exist);
        }
    }

    osOpen(directory);
}

void FileHelper::openFile(const string& path, bool createIfNeeded) {
    ensureValidFilePath(path);

    if (!doesFileExist(path)) {
        if (createIfNeeded) {
            createFile(path, false);
        } else {
            throwError(path, FileHelper::Error::file_does_not_exist);
        }
    }

    osOpen(path);
}

void FileHelper::revealFolder(const string& directory, bool createIfNeeded) {
    ensureValidDir(directory);

    if (!doesFolderExist(directory)) {
        if (createIfNeeded) {
            createFolder(directory);
        } else {
            throwError(directory, FileHelper::Error::directory_does_not_exist);
        }
    }

    osReveal(directory);
}

void FileHelper::revealFile(const string& path, bool createIfNeeded) {
    ensureValidFilePath(path);

    if (!doesFileExist(path)) {
        if (createIfNeeded) {
            createFile(path, false);
        } else {
            throwError(path, FileHelper::Error::file_does_not_exist);
        }
    }

    osReveal(path);
}

/* NO THROW VALID CHECKS */

bool FileHelper::isValidFolderName(const string& folderName) {
    try {
        ensureValidFolderName(folderName);
        return true;
    } catch (std::exception&) {
        return false;
    }
}

bool FileHelper::isValidPath(const string& file) {
    try {
        ensureValidPath(file);
        return true;
    } catch (std::exception&) {
        return false;
    }
}

bool FileHelper::isValidFolderPath(const string& dir) {
    try {
        ensureValidDir(dir);
        return true;
    } catch (std::exception&) {
        return false;
    }
}

bool FileHelper::isValidFilePath(const string& file) {
    try {
        ensureValidFilePath(file);
        return true;
    } catch (std::exception&) {
        return false;
    }
}

bool FileHelper::isValidFileName(const string& file) {
    try {
        ensureValidFileName(file);
        return true;
    } catch (std::exception&) {
        return false;
    }
}

/* RENAME / MOVE */

string FileHelper::setFileDir(const string& file, const string& fileDir, bool overwrite) {
    if (getDir(file) == stripTrailingSlashes(toForwardSlashes(fileDir))) {
        return file;
    }

    ensureValidDir(fileDir);

    std::error_code ec;
    fs::create_directories(toPath(fileDir), ec);
    if (!doesFolderExist(fileDir)) {
        throwError(fileDir, FileHelper::Error::unknown_error_create_dir);
    }

    string newFile = stripTrailingSlashes(toForwardSlashes(fileDir)) + "/" + getName(file);

    if (doesFileExist(newFile)) {
        if (!overwrite) {
            throwError(fileDir, FileHelper::Error::file_already_exists);
        }
        if (!fs::remove(toPath(newFile), ec) || ec) {
            throwError(fileDir, FileHelper::Error::unknown_error_overwrite_file);
        }
    }

    fs::rename(toPath(file), toPath(newFile), ec);
    if (ec) {
        throwError(file, FileHelper::Error::unknown_error_file_move_or_rename);
    }

    return newFile;
}

string FileHelper::setFileName(const string& file, const string& fileName, bool overwrite) {
    if (getName(file) == fileName) {
        return file;
    }

    ensureValidFileName(fileName);

    string newFile = getDir(file) + "/" + fileName;

    std::error_code ec;
    if (doesFileExist(newFile)) {
        if (!overwrite) {
            throwError(newFile, FileHelper::Error::file_already_exists);
        }
        if (!fs::remove(toPath(newFile), ec) || ec) {
            throwError(newFile, FileHelper::Error::unknown_error_overwrite_file);
        }
    }

    fs::rename(toPath(file), toPath(newFile), ec);
    if (ec) {
        throwError(file + " :: " + newFile, FileHelper::Error::unknown_error_file_move_or_rename);
    }

    return newFile;
}

string FileHelper::setFolderName(const string& folder, const string& newName, bool /*overwrite*/) {
    if (FileHelper::getName(folder) == newName) {
        return folder;
    }

    ensureValidFolderName(newName);

    string newFolder = getDir(folder) + "/" + newName;

    if (doesFileExist(newFolder)) {
        throwError(newFolder, FileHelper::Error::file_already_exists);
    }

    if (doesPathExist(newFolder)) {
        throwError(newFolder, FileHelper::Error::folder_already_exists);
    }

    std::error_code ec;
    fs::rename(toPath(folder), toPath(newFolder), ec);
    if (ec) {
        throwError(folder + " :: " + newFolder, FileHelper::Error::unknown_error_folder_move_or_rename);
    }

    return newFolder;
}

string FileHelper::setName(const string& path, const string& name, bool overwrite) {
    if (FileHelper::getName(path) == name) {
        return path;
    }

    if (doesFileExist(path)) {
        return setFileName(path, name, overwrite);
    }
    return setFolderName(path, name, overwrite);
}

/* CREATE / READ / DELETE */

string FileHelper::createFile(const string& filePath, bool overwrite) {
    ensureValidFilePath(filePath);

    std::error_code ec;
    if (doesFileExist(filePath)) {
        if (!overwrite) {
            throwError(filePath, FileHelper::Error::file_already_exists);
        }
        if (!fs::remove(toPath(filePath), ec) || ec) {
            throwError(filePath, FileHelper::Error::unknown_error_overwrite_file);
        }
    } else if (doesFolderExist(filePath)) {
        throwError(filePath, FileHelper::Error::file_already_exists_as_folder);
    }

    string dir = getDir(filePath);
    if (!dir.empty()) {
        fs::create_directories(toPath(dir), ec);
    }

    std::ofstream out(toPath(filePath), std::ios::binary);
    if (!out.is_open()) {
        throwError(filePath, FileHelper::Error::unknown_error_create_file);
    }
    out.close();

    return filePath;
}

string FileHelper::createTempFile() {
    std::error_code ec;
    string tempFolder = toForwardSlashes(fromPath(fs::temp_directory_path(ec)));
    string tempPath;

    do {
        tempPath = tempFolder + "/temp_" + getRandomAlphaNumericString(8) + ".tmp";
    } while (doesFileExist(tempPath));

    createFile(tempPath, false);

    return tempPath;
}

void FileHelper::createTextFile(const string& filePath, const string& text, bool overwrite) {
    createFile(filePath, overwrite);

    std::ofstream out(toPath(filePath), std::ios::binary | std::ios::app);
    if (!out.is_open()) {
        throwError(filePath, FileHelper::Error::unknown_error_modify_file);
    }
    out << text;
    if (!out.good()) {
        throwError(filePath, FileHelper::Error::unknown_error_modify_file);
    }
}

string FileHelper::readFile(const string& filePath) {
    ensureFileExists(filePath);

    std::ifstream in(toPath(filePath), std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void FileHelper::createFolder(const string& fileDir) {
    ensureValidDir(fileDir);

    std::error_code ec;
    fs::create_directories(toPath(fileDir), ec);
    if (!doesFolderExist(fileDir)) {
        throwError(fileDir, FileHelper::Error::unknown_error_create_dir);
    }
}

string FileHelper::copyFile(const string& f, const string& filePath, bool overwrite) {
    ensureValidFilePath(filePath);

    std::error_code ec;
    if (doesFileExist(filePath)) {
        if (!overwrite) {
            throwError(filePath, FileHelper::Error::file_already_exists);
        }
        if (!fs::remove(toPath(filePath), ec) || ec) {
            throwError(filePath, FileHelper::Error::unknown_error_overwrite_file);
        }
    }

    string dir = getDir(filePath);
    fs::create_directories(toPath(dir), ec);
    if (!doesFolderExist(dir)) {
        throwError(dir, FileHelper::Error::unknown_error_create_dir);
    }

    fs::copy_file(toPath(f), toPath(filePath), ec);
    if (ec) {
        throwError(f + " :: " + filePath, FileHelper::Error::unknown_error_file_copy);
    }

    return filePath;
}

void FileHelper::moveToTrash(const string& path) {
    ensureValidPath(path);

#ifdef _WIN32
    // SHFileOperation requires an absolute, double-null-terminated, backslashed path.
    std::wstring w = fs::absolute(toPath(path)).wstring();
    w.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc  = FO_DELETE;
    op.pFrom  = w.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted) {
        throwError(path, FileHelper::Error::unknown_error_file_trash);
    }
#else
    // No portable trash on this platform; refuse rather than silently permanent-delete.
    throwError(path, FileHelper::Error::unknown_error_file_trash);
#endif
}

void FileHelper::permanentDelete(const string& path) {
    ensureValidPath(path);

    std::error_code ec;
    fs::remove_all(toPath(path), ec);
    if (ec || doesPathExist(path)) {
        throwError(path, FileHelper::Error::unknown_error_file_delete);
    }
}

vector<string> FileHelper::removeEmptyFoldersRecursive(const string& folder) {
    vector<string> folders = getFoldersRecursive(folder);
    folders.push_back(stripTrailingSlashes(toForwardSlashes(folder)));

    // deepest paths first
    std::sort(folders.begin(), folders.end(), [](const string& a, const string& b) { return a.size() > b.size(); });

    vector<string> removedPaths;
    for (const auto& f : folders) {
        if (!FileHelper::doesDirContainFolders(f) && !FileHelper::doesDirContainFiles(f)) {
            FileHelper::permanentDelete(f);
            removedPaths.push_back(f);
        }
    }

    return removedPaths;
}

vector<string> FileHelper::removeEmptyFilesRecursive(const string& folder) {
    auto files = getFilesRecursive(folder);

    vector<string> removedPaths;
    for (auto& f : files) {
        std::error_code ec;
        if (fs::file_size(toPath(f), ec) == 0 && !ec) {
            FileHelper::permanentDelete(f);
            removedPaths.push_back(f);
        }
    }

    return removedPaths;
}

namespace {

vector<string> listDirectory(const string& directory, const string& wildCard, bool recursive, bool wantFolders) {
    vector<string> v;
    std::error_code ec;

    auto consider = [&](const fs::directory_entry& entry) {
        std::error_code ec2;
        bool isDir = entry.is_directory(ec2);
        if (wantFolders != isDir) {
            return;
        }
        string name = fromPath(entry.path().filename());
        if (!wildcardMatch(name, wildCard)) {
            return;
        }
        v.push_back(toForwardSlashes(fromPath(entry.path())));
    };

    if (recursive) {
        for (fs::recursive_directory_iterator it(toPath(directory), fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                break;
            }
            consider(*it);
        }
    } else {
        for (fs::directory_iterator it(toPath(directory), fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                break;
            }
            consider(*it);
        }
    }

    return v;
}

} // namespace

vector<string> FileHelper::getFolders(const string& f, const string& wildCard) {
    string path = doesFileExist(f) ? getDir(f) : f;

    ensureFolderExists(path);

    return listDirectory(path, wildCard, false, true);
}

vector<string> FileHelper::getFiles(const string& f, const string& wildCard) {
    string path = doesFileExist(f) ? getDir(f) : f;

    ensureFolderExists(path);

    return listDirectory(path, wildCard, false, false);
}

vector<string> FileHelper::getFoldersRecursive(const string& f, const string& wildCard) {
    string path = isFolder(f) ? f : getDir(f);

    ensureFolderExists(path);

    return listDirectory(path, wildCard, true, true);
}

vector<string> FileHelper::getFilesRecursive(const string& f, const string& wildCard) {
    string path = doesFileExist(f) ? getDir(f) : f;

    ensureFolderExists(path);

    return listDirectory(path, wildCard, true, false);
}

/* EXISTENCE */

bool FileHelper::doesFileExist(const string& path) {
    std::error_code ec;
    return fs::is_regular_file(toPath(path), ec);
}

bool FileHelper::doesFolderExist(const string& directory) {
    std::error_code ec;
    return fs::is_directory(toPath(directory), ec);
}

bool FileHelper::doesPathExist(const string& path) {
    return doesFileExist(path) || doesFolderExist(path);
}

bool FileHelper::doesDirContainFiles(const string& directory, const string& wildCard) {
    if (!isValidFolderPath(directory) || !doesFolderExist(directory)) {
        return false;
    }

    return !listDirectory(directory, wildCard, false, false).empty();
}

bool FileHelper::doesDirContainFolders(const string& directory, const string& wildCard) {
    if (!isValidFolderPath(directory) || !doesFolderExist(directory)) {
        return false;
    }

    return !listDirectory(directory, wildCard, false, true).empty();
}

bool FileHelper::isAbsolutePath(const string& path) {
    if (path.size() >= 2 && isSlashChar(path[0]) && isSlashChar(path[1])) {
        return true; // UNC
    }
#ifdef _WIN32
    return path.size() >= 3 && path[1] == ':' && isSlashChar(path[2]);
#else
    return !path.empty() && path[0] == '/';
#endif
}

bool FileHelper::isRelativePath(const string& path) {
    return !isAbsolutePath(path);
}

bool FileHelper::isFile(const string& path) {
    return doesFileExist(path) || isValidFilePath(path);
}

bool FileHelper::isFolder(const string& path) {
    return doesFolderExist(path) || isValidFolderPath(path);
}

int FileHelper::isFileOrFolder(const string& path) {
    if (doesFileExist(path)) {
        return 1;
    }
    if (doesFolderExist(path)) {
        return 2;
    }

    if (isValidPath(path)) {
        if (hasExt(path)) {
            return 1;
        }

        return 2;
    }

    return 0;
}

/* THROWING VALIDATION */

void FileHelper::ensureValidFilePath(const string& path) {
    if (isRelativePath(path)) {
        throwError(path, FileHelper::Error::path_is_relative);
    }

    if (!path.empty() && isSlashChar(path.back())) {
        throwError(path, FileHelper::Error::path_is_dir);
    }

    ensureValidPath(path);
}

void FileHelper::ensureValidPath(const string& path) {
    if (path.empty()) {
        throwError(path, FileHelper::Error::empty_string);
    }

    string dir = getDir(path);
    ensureValidDir(dir.empty() ? path : dir);

    string name = getName(path);
    if (!name.empty()) {
        ensureValidFileName(name);
    }
}

void FileHelper::ensureValidDir(const string& directory) {
    if (directory.empty()) {
        throwError(directory, FileHelper::Error::empty_string);
    }

    if (trimCopy(directory) != directory) {
        throwError(directory, FileHelper::Error::ends_or_begins_with_space);
    }

    for (char c : directory) {
        if (isIllegalDirChar(c)) {
            throwError(directory, FileHelper::Error::illegal_char);
        }
    }

    if (directory.find("\\\\") != string::npos || directory.find("//") != string::npos) {
        throwError(directory, FileHelper::Error::directory_with_multi_slash);
    }
}

void FileHelper::ensureValidFileName(const string& fileName) {
    if (fileName.empty()) {
        throwError({}, FileHelper::Error::empty_string);
    }

    for (char c : fileName) {
        if (FileHelper::isIllegalNameChar(c)) {
            throwError(fileName, FileHelper::Error::illegal_char);
        }
    }

    if (fileName.front() == ' ') {
        throwError(fileName, FileHelper::Error::begins_with_space);
    }
    if (fileName.back() == ' ') {
        throwError(fileName, FileHelper::Error::ends_with_space);
    }
    if (fileName.back() == '.') {
        throwError(fileName, FileHelper::Error::ends_with_dot);
    }

    // make sure no space before extension
    size_t lastDot = fileName.find_last_of('.');
    if (lastDot != string::npos && lastDot > 0 && fileName[lastDot - 1] == ' ') {
        throwError(fileName, FileHelper::Error::space_before_ext);
    }
}

void FileHelper::ensureValidFolderName(const string& folderName) {
    if (folderName.empty()) {
        throwError(folderName, FileHelper::Error::empty_string);
    }

    if (trimCopy(folderName) != folderName) {
        throwError(folderName, FileHelper::Error::ends_or_begins_with_space);
    }

    for (char c : folderName) {
        if (isIllegalDirChar(c)) {
            throwError(folderName, FileHelper::Error::illegal_char);
        }
    }

    size_t slashLocation = folderName.find_first_of("\\/");

    // a single trailing slash is tolerated; any other slash means this is a path, not a name
    if (!(slashLocation == string::npos || slashLocation == folderName.size() - 1)) {
        throwError(folderName, FileHelper::Error::directory_with_multi_slash);
    }
}

void FileHelper::ensurePathExists(const string& path) {
    ensureValidPath(path);

    if (!doesPathExist(path)) {
        throwError(path, FileHelper::Error::path_does_not_exist);
    }
}

void FileHelper::ensureFileExists(const string& file) {
    ensureValidFilePath(file);

    if (!doesFileExist(file)) {
        throwError(file, FileHelper::Error::file_does_not_exist);
    }
}

bool FileHelper::ensureFileNotExist(const string& file) {
    ensureValidFilePath(file);

    if (doesFileExist(file)) {
        throwError(file, FileHelper::Error::file_already_exists);
    }

    return true;
}

void FileHelper::ensureFolderExists(const string& directory) {
    ensureValidDir(directory);

    if (doesFileExist(directory)) {
        throwError(directory, FileHelper::Error::directory_already_exists_as_file);
    }

    if (!doesFolderExist(directory)) {
        throwError(directory, FileHelper::Error::directory_does_not_exist);
    }
}

/* PARENT / SLASHES / DATE-TIME */

string FileHelper::getParent(int levels, const string& path) {
    string current = stripTrailingSlashes(toForwardSlashes(path));

    if (doesFileExist(current) || hasExt(current)) {
        current = getDir(current);
    }

    for (int i = 0; i < levels; ++i) {
        string parent = getDir(current);
        if (parent.empty() || parent == current) {
            break;
        }
        // clamp at drive/filesystem root
        string root = toForwardSlashes(fromPath(toPath(current).root_path()));
        current     = parent;
        if (stripTrailingSlashes(current) == stripTrailingSlashes(root)) {
            break;
        }
    }

    return stripTrailingSlashes(current);
}

string FileHelper::toUniversalSlash(const string& path) {
    return toForwardSlashes(path);
}

string FileHelper::toSystemSlash(const string& path) {
    string out = path;
    for (char& c : out) {
        if (c == '\\' || c == '/') {
            c = systemSlash[0];
        }
    }
    return out;
}

string FileHelper::getDateStr(const string& separator) {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return std::to_string(tmv.tm_year + 1900) + separator + pad2(tmv.tm_mon + 1) + separator + pad2(tmv.tm_mday);
}

string FileHelper::getTimeStr() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return pad2(tmv.tm_hour) + pad2(tmv.tm_min);
}

string FileHelper::getDateAndTimeStr(const string& separatorA, const string& separatorB) {
    return getDateStr(separatorA) + separatorB + getTimeStr();
}
