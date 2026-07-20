#include "BatchFileHelper.h"

#include "FileHelper.h"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <set>
#include <stdexcept>

using std::string;
using std::vector;

namespace {

string joinAll(const vector<string>& parts, const string& sep = {}) {
    string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += sep;
        }
        out += parts[i];
    }
    return out;
}

bool isAllDigits(const string& s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

string rtrimSpaces(const string& s) {
    size_t e = s.find_last_not_of(' ');
    return e == string::npos ? string{} : s.substr(0, e + 1);
}

string padLeftZeros(const string& s, int length) {
    if (static_cast<int>(s.size()) >= length) {
        return s;
    }
    return string(size_t(length) - s.size(), '0') + s;
}

string replaceAll(string s, const string& from, const string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

int numDigitsInInteger(int v) {
    int digits = 1;
    while (v >= 10) {
        v /= 10;
        ++digits;
    }
    return digits;
}

} // namespace

/* ------------------------------- BatchFileHelper ------------------------------- */

BatchFileHelper::BatchFileHelper(const vector<string>& filePathStrings) {
    newFileList = filePathStrings;

    collectFiles();
}

vector<string> BatchFileHelper::getFilesToBeCreated() {
    std::map<int, string> byIndex;

    for (auto& folder : folderFileMap)
        for (auto& obj : folder.second)
            for (auto& fND : obj.second)
                if (fND.originalFile.empty())
                    byIndex[fND.idx] = fND.newPath;

    vector<string> sA;
    for (auto& kv : byIndex)
        sA.push_back(kv.second);

    return sA;
}

bool BatchFileHelper::createNewFiles() {
    for (auto& folder : folderFileMap) {
        for (auto& obj : folder.second) {
            for (auto& fND : obj.second) {
                if (!fND.originalFile.empty()) // only creating listed files and not renaming existing files
                    continue;

                try {
                    FileHelper::createFile(fND.newPath, false);
                    fileNameChanges.push_back({ {}, fND.newPath, true });
                } catch (std::exception&) {
                    attemptToUndoFileRenames();
                    return false;
                }
            }
        }
    }

    return true;
}

bool BatchFileHelper::createNewFilesAndAddDupeSuffixToExistingFiles(bool doNotModifyNonDuplicates) {
    addDuplicateSuffixToCollection(doNotModifyNonDuplicates);

    renameExistingFiles(); // rename existing files in preparation to create new files

    for (auto& folder : folderFileMap) {
        for (auto& obj : folder.second) {
            for (auto& fND : obj.second) {
                if (fND.originalFile.empty()) // only create non-existent files here
                {
                    try {
                        FileHelper::createFile(fND.newPath, false);
                        fileNameChanges.push_back({ {}, fND.newPath, true });
                    } catch (std::exception&) {
                        attemptToUndoFileRenames();
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

void BatchFileHelper::clearDupeNumbers() {
    for (auto& folder : folderFileMap)
        for (auto& obj : folder.second)
            for (auto& fND : obj.second)
                fND.clearDupeNumber();
}

vector<int> BatchFileHelper::getUnusedDupeNumbers(const vector<string>& files) {
    // Pass 1: parse every file's suffix and collect the numbers already in use.
    struct Parsed {
        bool hasNumber;
        int number;
    };
    vector<Parsed> parsed;
    parsed.reserve(files.size());

    std::set<int> used;

    for (const auto& f : files) {
        int openParenIndex;
        int closeParenIndex;
        int dupeNumber;

        bool has = fileNameDupeStruct::hasDupeSuffix(FileHelper::getName(f), openParenIndex, closeParenIndex, dupeNumber);
        parsed.push_back({ has, dupeNumber });
        if (has)
            used.insert(dupeNumber);
    }

    // Pass 2: suffixed files keep their number; the rest get the smallest
    // number not used anywhere in the list and not already handed out.
    vector<int> ret;
    ret.reserve(files.size());

    int next = 1;
    for (const auto& p : parsed) {
        if (p.hasNumber) {
            ret.push_back(p.number);
            continue;
        }
        while (used.count(next))
            ++next;
        used.insert(next);
        ret.push_back(next);
    }

    return ret;
}

string BatchFileHelper::printRenameList(string seperator, bool printOnlyIfRenaming, bool printDuplicatesOnly) {
    vector<string> sA;

    for (auto& folder : folderFileMap) {
        for (auto& obj : folder.second) {
            if (!printDuplicatesOnly || obj.second.size() > 1) {
                for (auto& fND : obj.second)
                    // ported fix: original compared with == so "only if renaming" printed non-renames
                    if (!printOnlyIfRenaming || fND.originalName != fND.newName)
                        sA.push_back(fND.originalName + seperator + fND.newName + "\n");

                sA.push_back("\n\n\n");
            }
        }
    }

    return joinAll(sA);
}

string BatchFileHelper::printFileList() {
    vector<string> sA;

    for (auto& folder : folderFileMap) {
        for (auto& obj : folder.second) {
            for (auto& fND : obj.second)
                if (fND.originalFile.empty())
                    sA.push_back(fND.newName + "\n");

            sA.push_back("\n\n\n");
        }
    }

    return joinAll(sA);
}

bool BatchFileHelper::checkForDuplicates(AccessMethod method) {
    // ported fix: original returned after inspecting only the first entry
    if (method == originalNameWithoutSuffix) {
        for (auto& folder : folderFileMap)
            for (auto& obj : folder.second)
                if (obj.second.size() > 1)
                    return true;
        return false;
    }

    std::map<string, std::map<string, vector<fileNameDupeStruct>>> tempFileMap;
    collectFiles(newFileList, tempFileMap, method);

    for (auto& folder : tempFileMap)
        for (auto& obj : folder.second)
            if (obj.second.size() > 1)
                return true;

    return false;
}

bool BatchFileHelper::fileListContainsIllegalChars(AccessMethod method) {
    // ported fix: original could never return true
    for (auto& folder : folderFileMap) {
        if (!FileHelper::isValidFolderPath(folder.first)) {
            return true;
        }

        for (auto& obj : folder.second) {
            for (auto& fND : obj.second) {
                string nameToCheck;

                switch (method) {
                case originalNameWithoutSuffix:
                    nameToCheck = fND.originalNameNoSuffix;
                    break;
                case originalNameWithSuffix:
                    nameToCheck = fND.originalName;
                    break;
                case newNameWithoutSuffix:
                    nameToCheck = fileNameDupeStruct::removeDupeSuffix(FileHelper::removeExt(fND.newName)) + fND.ext;
                    break;
                case newNameWithSuffix:
                    nameToCheck = fND.newName;
                    break;
                }

                if (!FileHelper::isValidFileName(nameToCheck)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void BatchFileHelper::solveIllegalCharacterIssues() {
    successfulUndoActions = 0;
    fileNameChanges.clear();

    for (auto& s : newFileList)
        s = FileHelper::createLegalString(s);

    collectFiles();
}

void BatchFileHelper::addDuplicateSuffixToCollection(bool doNotModifyNonDuplicates) {
    for (auto& folder : folderFileMap) {
        for (auto& obj : folder.second) {
            if (obj.second.size() == 1) // no duplicate found, remove duplicate suffix if desired
            {
                auto& only   = obj.second[0];
                only.newName = doNotModifyNonDuplicates ? only.originalName : only.originalNameNoSuffix;
                only.newPath = only.dir + "/" + only.newName;
                continue;
            }

            vector<int> usedDupeNumbers;

            for (auto& fND : obj.second)
                if (fND.dupeNumber != -1)
                    if (std::find(usedDupeNumbers.begin(), usedDupeNumbers.end(), fND.dupeNumber) == usedDupeNumbers.end())
                        usedDupeNumbers.push_back(fND.dupeNumber);

            int i = 1;
            for (auto& fND : obj.second) {
                if (fND.originalFile.empty() || !fND.hasDupeNumber()) {
                    while (std::find(usedDupeNumbers.begin(), usedDupeNumbers.end(), i) != usedDupeNumbers.end())
                        ++i;

                    fND.newName = fND.originalNameNoSuffixNoExt + "(" + std::to_string(i) + ")" + fND.ext;
                    fND.newPath = fND.dir + "/" + fND.newName;
                    ++i;
                } else {
                    fND.newName = fND.originalNameNoSuffixNoExt + "(" + std::to_string(fND.dupeNumber) + ")" + fND.ext;
                    fND.newPath = fND.dir + "/" + fND.newName;
                }
            }
        }
    }

    refreshFileFolderMap();
}

bool BatchFileHelper::trashListedExistingFiles(AccessMethod method) {
    vector<string> foldersToDelete;

    for (auto& folder : folderFileMap) {
        string tempFolderForDeletion = folder.first + "/saltTempDelete";

        for (auto& obj : folder.second) {
            for (auto& fND : obj.second) {
                if (!fND.originalFile.empty()) // we are only deleting files specified at object creation
                    continue;

                string pathToDelete;

                switch (method) {
                case originalNameWithoutSuffix:
                    pathToDelete = folder.first + "/" + fND.originalNameNoSuffix;
                    break;
                case originalNameWithSuffix:
                    pathToDelete = folder.first + "/" + fND.originalName;
                    break;
                case newNameWithoutSuffix:
                    pathToDelete = folder.first + "/" + fileNameDupeStruct::removeDupeSuffix(FileHelper::removeExt(fND.newName)) + fND.ext;
                    break;
                case newNameWithSuffix:
                    pathToDelete = folder.first + "/" + fND.newName;
                    break;
                }

                if (FileHelper::doesFileExist(pathToDelete)) {
                    try {
                        auto fileMoved = FileHelper::setFileDir(pathToDelete, tempFolderForDeletion, false);
                        fileNameChanges.push_back({ pathToDelete, fileMoved });
                    } catch (std::exception&) {
                        attemptToUndoFileRenames();
                        return false;
                    }
                }
            }
        }

        if (FileHelper::doesDirContainFiles(tempFolderForDeletion))
            foldersToDelete.push_back(tempFolderForDeletion);
    }

    for (const auto& s : foldersToDelete) {
        try {
            FileHelper::moveToTrash(s);
        } catch (std::exception&) {
            return false;
        }
    }

    fileNameChanges.clear();
    newFileList.clear();
    successfulUndoActions = 0;

    return true;
}

bool BatchFileHelper::undoFileChanges() {
    return attemptToUndoFileRenames();
}

void BatchFileHelper::collectFiles() {
    folderFileMap.clear();
    fileNameChanges.clear();
    successfulUndoActions = 0;

    collectFiles(newFileList, folderFileMap, originalNameWithoutSuffix);
}

void BatchFileHelper::collectFiles(const vector<string>& /*newFiles*/, std::map<string, std::map<string, vector<fileNameDupeStruct>>>& fileMapInput, AccessMethod methodForDuplicateCheck) {
    int i;

    // Populate folderFileMap with new files to be created
    i = 0;
    for (const auto& s : newFileList) {
        fileNameDupeStruct fND(s, i);
        fileMapInput[fND.dir][fND.originalNameNoSuffix].push_back(fND);
        ++i;
    }

    // Go through each folder of corresponding new files to be created and populate with existing files
    i = 0;
    vector<string> folderKeys;
    for (auto& folder : fileMapInput)
        folderKeys.push_back(folder.first);

    for (auto& folderKey : folderKeys) {
        if (!FileHelper::doesFolderExist(folderKey))
            continue;

        vector<string> files = FileHelper::getFiles(folderKey);

        for (auto& f : files) {
            fileNameDupeStruct fND(f, i);
            fND.originalFile = f; // this entry exists on disk

            string key;
            switch (methodForDuplicateCheck) {
            case originalNameWithoutSuffix:
                key = fND.originalNameNoSuffix;
                break;
            case originalNameWithSuffix:
                key = fND.originalName;
                break;
            case newNameWithoutSuffix:
                key = fileNameDupeStruct::removeDupeSuffix(FileHelper::removeExt(fND.newName)) + fND.ext;
                break;
            case newNameWithSuffix:
                key = fND.newName;
                break;
            }

            fileMapInput[folderKey][key].push_back(fND);
            ++i;
        }
    }
}

void BatchFileHelper::refreshFileFolderMap() {
    std::map<string, std::map<string, vector<fileNameDupeStruct>>> tempFileFolderMap;

    for (auto& folder : folderFileMap) {
        for (auto& obj : folder.second) {
            for (auto& fND : obj.second) {
                string dir, name, ext;
                FileHelper::getFileParts(fND.newPath, dir, name, ext);
                tempFileFolderMap[dir][name].push_back(fND);
            }
        }
    }

    folderFileMap = std::move(tempFileFolderMap);
}

void BatchFileHelper::swapFileNames(string& file1, string& file2, vector<fileNameChangesStruct>& changes) {
    string initialName2 = FileHelper::getName(file2);
    string initialName1 = FileHelper::getName(file1);

    // move file2 out of the way with a random-suffixed name
    string tempName = initialName2 + "_" + generateRandom4CharString();
    string file2Temp = FileHelper::setFileName(file2, tempName, false);
    changes.push_back({ file2, file2Temp });

    // file1 takes file2's original name
    string file1New = FileHelper::setFileName(file1, initialName2, false);
    changes.push_back({ file1, file1New });

    // file2 takes file1's original name
    string file2New = FileHelper::setFileName(file2Temp, initialName1, false);
    changes.push_back({ file2Temp, file2New });

    file1 = file1New;
    file2 = file2New;
}

string BatchFileHelper::generateRandom4CharString() {
    std::uniform_int_distribution<int> dist('A', 'Z');
    string ret;
    for (int i = 0; i < 4; ++i)
        ret += char(dist(random));
    return ret;
}

bool BatchFileHelper::attemptToUndoFileRenames() {
    for (size_t i = fileNameChanges.size() - size_t(successfulUndoActions); i-- > 0;) {
        // attempt to undo name changes
        try {
            if (fileNameChanges[i].fileWasCreated) {
                FileHelper::permanentDelete(fileNameChanges[i].newFile);
                ++successfulUndoActions;
            } else if (FileHelper::doesFileExist(fileNameChanges[i].newFile)) {
                FileHelper::setFilePath(fileNameChanges[i].newFile, fileNameChanges[i].oldFile, false);
                ++successfulUndoActions;
            } else {
                return false;
            }
        } catch (std::exception&) {
            return false;
        }
    }
    return true;
}

bool BatchFileHelper::renameExistingFiles() {
    for (auto& folder : folderFileMap) {
        for (auto& obj : folder.second) {
            for (auto& fileObj : obj.second) {
                if (fileObj.originalFile.empty()) // file does not yet exist so do not attempt to rename
                    continue;

                if (FileHelper::getName(fileObj.originalFile) == fileObj.newName)
                    continue;

                // attempt to rename files
                try {
                    string newFilePath = fileObj.dir + "/" + fileObj.newName;

                    if (FileHelper::doesFileExist(newFilePath)) {
                        swapFileNames(fileObj.originalFile, newFilePath, fileNameChanges);
                        fileObj.newPath = fileObj.originalFile;
                    } else {
                        string oldPath = fileObj.originalFile;
                        FileHelper::setFileName(fileObj.originalFile, fileObj.newName, false);
                        fileObj.originalFile = newFilePath;
                        fileObj.newPath      = newFilePath;
                        fileNameChanges.push_back({ oldPath, newFilePath });
                    }
                } catch (std::exception&) {
                    attemptToUndoFileRenames();
                    return false;
                }
            }
        }
    }

    return true;
}

BatchFileHelper::fileNameDupeStruct::fileNameDupeStruct() {}

BatchFileHelper::fileNameDupeStruct::fileNameDupeStruct(const string& s, int addIndex) {
    dir = FileHelper::getDir(s);

    string name               = FileHelper::getName(s);
    ext                       = FileHelper::getExt(name); // WITH dot ("" if none)
    originalNameNoSuffixNoExt = removeDupeSuffix(FileHelper::getNameNoExt(name), &dupeNumber);
    originalNameNoSuffix      = originalNameNoSuffixNoExt + ext;
    originalName              = name;
    newName                   = originalName;
    newPath                   = dir + "/" + newName;
    idx                       = addIndex;
}

bool BatchFileHelper::fileNameDupeStruct::hasDupeNumber() {
    return dupeNumber != -1;
}

void BatchFileHelper::fileNameDupeStruct::clearDupeNumber() {
    dupeNumber = -1;
}

void BatchFileHelper::fileNameDupeStruct::updateNewPath(const string& s) {
    dir     = FileHelper::getDir(s);
    newName = FileHelper::getName(s);
    ext     = FileHelper::getExt(s);
    newPath = dir + "/" + newName;
}

bool BatchFileHelper::fileNameDupeStruct::hasDupeSuffix(const string& s, int& indexOfOpenParenOut, int& indexOfCloseParenOut, int& dupeNumberOut) {
    size_t open  = s.find_last_of('(');
    size_t close = s.find_last_of(')');

    indexOfOpenParenOut  = open == string::npos ? -1 : int(open);
    indexOfCloseParenOut = close == string::npos ? -1 : int(close);

    if (open == string::npos || close == string::npos || close <= open + 1) {
        dupeNumberOut = -1;
        return false;
    }

    string dupeNumberStr = s.substr(open + 1, close - open - 1);

    dupeNumberOut = isAllDigits(dupeNumberStr) ? std::stoi(dupeNumberStr) : -1;

    return dupeNumberOut != -1;
}

string BatchFileHelper::fileNameDupeStruct::removeDupeSuffix(const string& fileNameNoExt, int* dupeNumberOut) {
    int indexOfOpenParen;
    int indexOfCloseParen;
    int dupeNumber;

    bool hasDupe = hasDupeSuffix(fileNameNoExt, indexOfOpenParen, indexOfCloseParen, dupeNumber);

    if (dupeNumberOut != nullptr)
        *dupeNumberOut = dupeNumber;

    if (hasDupe)
        return rtrimSpaces(fileNameNoExt.substr(0, size_t(indexOfOpenParen)));

    return fileNameNoExt;
}

string BatchFileHelper::fileNameDupeStruct::addDupeSuffix(const string& s, int dupeNumberToUse) {
    int indexOfOpenParen;
    int indexOfCloseParen;
    int dupeNumber;

    if (hasDupeSuffix(s, indexOfOpenParen, indexOfCloseParen, dupeNumber))
        return s;

    return FileHelper::removeExt(s) + "(" + std::to_string(dupeNumberToUse) + ")" + FileHelper::getExt(s);
}

/* ------------------------------- BatchFileChanger ------------------------------- */

BatchFileChanger::Task* BatchFileChanger::addTask(string originalPath, string newPath, Operation operation) {
    if (runTasksWasCalled)
        throw std::invalid_argument("In order to protect file integrity, tasks cannot be added once tasks have been run. Create a new object in order to make more tasks.");

    if (operation == Operation::none) {
        assert(false);
        return nullptr;
    }

    didCheckForConflicts = false;

    TASKS.push_back(std::make_unique<Task>());
    auto& t     = *TASKS.back();
    t.operation = operation;
    t.origFile  = originalPath;
    t.userInput = newPath;
    t.random    = generateRandom7CharString();

    switch (operation) {
    case invalid:
    case none:
        break;
    case Operation::move:
        // existing folder (or extension-less nonexistent path) = folder destination
        if (FileHelper::isFileOrFolder(newPath) == 2)
            t.newFile = newPath + "/" + FileHelper::getName(originalPath);
        else if (FileHelper::isValidFilePath(newPath))
            t.newFile = newPath;
        else
            throw std::invalid_argument("MOVE task ->: " + newPath + " Path is not a valid folder path or file path.");
        break;
    case Operation::rename:
        if (FileHelper::isValidFilePath(newPath))
            t.newFile = newPath;
        else if (FileHelper::isValidFileName(newPath))
            t.newFile = FileHelper::getDir(originalPath) + "/" + newPath;
        else
            throw std::invalid_argument("RENAME task ->: " + newPath + " Path is not a valid file path or file name.");
        break;
    case Operation::copy:
        if (newPath.empty())
            t.newFile = t.origFile;
        else if (FileHelper::isFileOrFolder(newPath) == 2)
            t.newFile = newPath + "/" + FileHelper::getName(t.origFile);
        else if (FileHelper::isValidFilePath(newPath))
            t.newFile = newPath;
        else
            throw std::invalid_argument("COPY task: " + newPath + " Path is not a valid folder path or file path.");
        break;
    case Operation::temporary: {
        if (!FileHelper::isValidFolderPath(newPath))
            throw std::invalid_argument("TEMPORARY task: " + newPath + " Path is not a valid folder path.");
#ifdef _WIN32
        // preserve the original folder structure under newPath, drive letter becomes a folder
        string universal = FileHelper::toUniversalSlash(originalPath);
        if (universal.size() > 2 && universal[1] == ':')
            t.newFile = newPath + "/" + universal.substr(0, 1) + universal.substr(2);
        else
            t.newFile = newPath + "/" + universal;
#else
        t.newFile = newPath + "/" + originalPath;
#endif
        break;
    }
    }

    t.newFile  = FileHelper::toUniversalSlash(t.newFile);
    t.origFile = FileHelper::toUniversalSlash(t.origFile);

    if (taskMapOriginal[FileHelper::getDir(t.origFile)][t.origFile].size() == 0)
        taskMapOriginal[FileHelper::getDir(t.origFile)][t.origFile].push_back(&t);
    else
        throw std::invalid_argument("There cannot be more than one task for a file.");

    FileHelper::ensureValidFilePath(t.origFile);
    FileHelper::ensureValidFilePath(t.newFile);

    taskMapNew[FileHelper::getDir(t.newFile)][t.newFile].push_back(&t);

    return &*TASKS.back();
}

void BatchFileChanger::addTasks(const vector<std::pair<string, string>>& fromToList, Operation operation) {
    for (const auto& fromTo : fromToList)
        addTask(fromTo.first, fromTo.second, operation);
}

void BatchFileChanger::setDuplicateFormat(int startingNumber, string regexMatch, string tagScriptReplace, int padFlag, int orderFlag, bool onlyRenameDuplicates) {
    duplicateFormat.startingNumber       = startingNumber;
    duplicateFormat.tagScriptReplace     = tagScriptReplace;
    duplicateFormat.padFlag              = padFlag;
    duplicateFormat.orderFlag            = orderFlag;
    duplicateFormat.onlyRenameDuplicates = onlyRenameDuplicates;

    if (tagScriptReplace.find("<num>") == string::npos)
        throw std::invalid_argument("tagScriptReplace parameter must contain '<num>' in order to place duplicate number.");

    try {
        duplicateFormat.regexMatch = std::regex(regexMatch, std::regex_constants::icase);
    } catch (std::exception& e) {
        throw std::invalid_argument("Regex expression failed: " + string(e.what()));
    }
}

void BatchFileChanger::applyDuplicateFormat() {
    std::map<string, std::map<string, vector<Task*>>> taskMapNewReplacement;

    if (runTasksWasCalled)
        throw std::invalid_argument("In order to protect file integrity, editing tasks is not allowed after tasks have been run. Create a new object in order to make more tasks.");

    for (auto& directory : taskMapNew) {
        for (auto& fullpath : directory.second) {
            size_t size   = fullpath.second.size();
            int padlength = numDigitsInInteger(static_cast<int>(size));

            // ported fix: original tested size > 0 (always true), defeating onlyRenameDuplicates
            if (size > 1 || !duplicateFormat.onlyRenameDuplicates) {
                vector<int> numbers(size);
                std::iota(numbers.begin(), numbers.end(), 0);
                switch (duplicateFormat.orderFlag) {
                case Order::forward:
                    break;
                case Order::reverse:
                    std::reverse(numbers.begin(), numbers.end());
                    break; // ported fix: original fell through into random shuffle
                case Order::random:
                    std::shuffle(numbers.begin(), numbers.end(), randomGen);
                    break;
                }

                for (size_t i = 0; i < size; ++i) {
                    auto& t = *fullpath.second[size_t(numbers[i])];

                    int dupNumber       = int(i) + duplicateFormat.startingNumber;
                    string dupNumberStr = std::to_string(dupNumber);
                    if (duplicateFormat.padFlag == Padding::automatic)
                        dupNumberStr = padLeftZeros(dupNumberStr, padlength);
                    else if (duplicateFormat.padFlag >= 2)
                        dupNumberStr = padLeftZeros(dupNumberStr, duplicateFormat.padFlag);

                    string dir, name;
                    FileHelper::getFileParts(t.newFile, dir, name);
                    string substituted = std::regex_replace(name, duplicateFormat.regexMatch, duplicateFormat.tagScriptReplace, std::regex_constants::format_first_only);
                    string newName     = replaceAll(substituted, "<num>", dupNumberStr);

                    try {
                        FileHelper::ensureValidFileName(newName);
                        FileHelper::ensureValidFilePath(dir + "/" + newName);
                    } catch (std::exception& e) {
                        throw std::invalid_argument("Bad path after applying duplicate formatting. Check format parameters -> " + string(e.what()));
                    }

                    t.newFileWithDupID = dir + "/" + newName;
                    t.dupNumber        = dupNumber;
                    taskMapNewReplacement[FileHelper::getDir(t.getNewPathStr())][t.getNewPathStr()].push_back(&t);
                }
            } else {
                for (size_t i = 0; i < size; ++i) {
                    auto& t = *fullpath.second[i];
                    taskMapNewReplacement[FileHelper::getDir(t.getNewPathStr())][t.getNewPathStr()].push_back(&t);
                    t.dupNumber = duplicateFormat.startingNumber;
                }
            }
        }
    }

    taskMapNew = std::move(taskMapNewReplacement);
}

string BatchFileChanger::checkForConflicts() {
    vector<string> ret;
    ret.push_back("ErrorType\tOperation\tOriginalFile\tNewFile\tOriginalUserInput");

    // create temp task map for checking for errors that includes all files that exist in all folders that have associated tasks
    std::map<string, std::map<string, vector<Task>>> tempTaskMap;

    for (auto& directory : taskMapNew)
        for (auto& fullpath : directory.second)
            for (auto& t : fullpath.second)
                tempTaskMap[directory.first][t->getNewPathStr()].push_back(*t);

    // add files to temp task map, this will increase the size to above 1 (the number of tasks for a given full path) if there is a file conflict
    for (auto& directory : tempTaskMap) {
        vector<string> files;

        if (FileHelper::doesFolderExist(directory.first))
            files = FileHelper::getFiles(directory.first);

        for (auto& f : files) {
            Task t;
            t.origFile  = f;
            t.newFile   = f;
            t.operation = Operation::none;

            auto it = taskMapOriginal[directory.first].find(t.origFile);
            if (it == taskMapOriginal[directory.first].end())
                tempTaskMap[directory.first][t.getNewPathStr()].push_back(t); // the size is increased here
        }
    }

    // check size for full path (number of tasks for given path)
    for (auto& directory : tempTaskMap) {
        for (auto& fullpath : directory.second) {
            if (fullpath.second.size() > 1) {
                for (auto& t : fullpath.second) {
                    if (t.operation == Operation::none)
                        ret.push_back("File already exists\t" + getOperationName(t.operation) + "\t" + t.origFile);
                    else
                        ret.push_back("File shares new name with another file targeted for manipulation\t" + getOperationName(t.operation) + "\t" + t.origFile + "\t" + t.getNewPathStr() + "\t" + t.userInput);
                }
            }
        }
    }

    didCheckForConflicts = true;

    if (ret.size() >= 2)
        return joinAll(ret, "\n");

    return {};
}

void BatchFileChanger::runTasks() {
    if (!didCheckForConflicts && !checkForConflicts().empty())
        throw std::invalid_argument("Cannot run tasks, there are file conflicts. Call checkForConflicts() to review the issues.");

    // initial rename with random characters
    for (auto& directory : taskMapNew) {
        for (auto& fullpath : directory.second) {
            for (auto& t : fullpath.second) {
                if (t->completedTask)
                    continue;

                const string& o = t->origFile;
                const string n  = t->getNewPathStr();
                const string& r = t->random;

                try {
                    switch (t->operation) {
                    case Operation::invalid:
                    case Operation::none:
                        break;
                    case Operation::move:
                    case Operation::rename:
                    case Operation::temporary:
                        FileHelper::setFilePath(o, n + r, false);
                        break;
                    case Operation::copy:
                        FileHelper::copyFile(o, n + r, false);
                        break;
                    }
                } catch (std::exception& e) {
                    throw std::invalid_argument("Failed at completing a task. You can re-run this function to continue where it left off. -> " + string(e.what()));
                }
                t->completedTask  = true;
                runTasksWasCalled = true;
                orderOfOperations.push_back(t);
            }
        }
    }

    // remove random characters
    for (auto& directory : taskMapNew) {
        for (auto& fullpath : directory.second) {
            for (auto& t : fullpath.second) {
                if (t->completedRandomRemove)
                    continue;

                const string n  = t->getNewPathStr();
                const string& r = t->random;
                try {
                    switch (t->operation) {
                    case Operation::rename:
                    case Operation::move:
                    case Operation::copy:
                    case Operation::temporary:
                        FileHelper::setFilePath(n + r, n, false);
                        break;
                    case Operation::invalid:
                    case Operation::none:
                        break;
                    }
                } catch (std::exception& e) {
                    throw std::invalid_argument("Failed at running tasks / removing random characters. You can re-run this function to continue where it left off. -> " + string(e.what()));
                }
                t->completedRandomRemove = true;
                orderOfOperations.push_back(t);
            }
        }
    }
}

void BatchFileChanger::undoTasks() {
    // put the random suffix back (reverse of the "remove random characters" pass)
    for (auto it = orderOfOperations.rbegin(); it != orderOfOperations.rend(); ++it) {
        Task* t = *it;
        if (!t->completedRandomRemove)
            continue;

        try {
            switch (t->operation) {
            case Operation::rename:
            case Operation::move:
            case Operation::copy:
            case Operation::temporary:
                // ported fix: original trashed the final file here, destroying what the second pass needed
                FileHelper::setFilePath(t->getNewPathStr(), t->getNewPathStr() + t->random, false);
                break;
            case Operation::invalid:
            case Operation::none:
                break;
            }
        } catch (std::exception& e) {
            throw std::invalid_argument("Failed at undoing / restoring the random string. You can re-run the undo function to continue where it left off. -> " + string(e.what()));
        }

        t->completedRandomRemove = false;
    }

    // reverse the original task operation
    for (auto it = orderOfOperations.rbegin(); it != orderOfOperations.rend(); ++it) {
        Task* t = *it;
        if (!t->completedTask)
            continue;

        try {
            switch (t->operation) {
            case Operation::rename:
            case Operation::move:
            case Operation::temporary:
                FileHelper::setFilePath(t->getNewPathStr() + t->random, t->origFile, false);
                break;
            case Operation::copy:
                FileHelper::moveToTrash(t->getNewPathStr() + t->random);
                break;
            case Operation::invalid:
            case Operation::none:
                break;
            }
        } catch (std::exception& e) {
            throw std::invalid_argument("Failed at undoing a task. You can re-run the undo function to continue where it left off. -> " + string(e.what()));
        }

        t->completedTask = false;
    }

    runTasksWasCalled = false;
}

vector<string> BatchFileChanger::getRenameList() {
    vector<string> ret;

    // as-added order
    for (auto& t : TASKS)
        ret.push_back(t->origFile + "," + t->getNewPathStr());

    return ret;
}

vector<std::pair<string, string>> BatchFileChanger::getRenamePairs() {
    vector<std::pair<string, string>> ret;

    // as-added order
    for (auto& t : TASKS)
        ret.push_back({ t->origFile, t->getNewPathStr() });

    return ret;
}

string BatchFileChanger::getOperationName(Operation operation) {
    switch (operation) {
    case Operation::none:
        return "none";
    case Operation::move:
        return "move";
    case Operation::rename:
        return "rename";
    case Operation::copy:
        return "copy";
    case Operation::temporary:
        return "temporary";
    case Operation::invalid:
        return "invalid";
    }
    return {};
}

string BatchFileChanger::generateRandom7CharString() {
    // 8,031,810,176 possible combinations
    std::uniform_int_distribution<int> dist('A', 'Z');

    for (;;) {
        string str;
        for (int i = 0; i < 7; ++i)
            str += char(dist(randomGen));

        if (++randomStrings[str] == 1)
            return str;

        assert(false); // how could this happen with one in eight billion chances?
    }
}
