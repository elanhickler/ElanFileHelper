// Smoke test for the std-string port of FileHelper / BatchFileHelper.
// Creates a sandbox in the system temp folder, exercises the main paths,
// prints PASS/FAIL per check, exits nonzero on any failure.

#include "../include/BatchFileHelper.h"
#include "../include/FileHelper.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using std::string;

static int failures = 0;

#define CHECK(expr)                                                        \
    do {                                                                   \
        if (expr) {                                                        \
            std::cout << "PASS  " << #expr << "\n";                        \
        } else {                                                           \
            std::cout << "FAIL  " << #expr << "\n";                        \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        auto va = (a);                                                     \
        auto vb = (b);                                                     \
        if (va == vb) {                                                    \
            std::cout << "PASS  " << #a << " == " << #b << "\n";           \
        } else {                                                           \
            std::cout << "FAIL  " << #a << " == " << #b << "  (got \"" << va << "\", expected \"" << vb << "\")\n"; \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

#define CHECK_THROWS(expr)                                                 \
    do {                                                                   \
        bool threw = false;                                                \
        try {                                                              \
            expr;                                                          \
        } catch (std::exception&) {                                        \
            threw = true;                                                  \
        }                                                                  \
        if (threw) {                                                       \
            std::cout << "PASS  throws: " << #expr << "\n";                \
        } else {                                                           \
            std::cout << "FAIL  did not throw: " << #expr << "\n";         \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

int runAllChecks();

int main() {
    try {
        return runAllChecks();
    } catch (std::exception& e) {
        std::cout << "UNCAUGHT EXCEPTION: " << e.what() << "\n";
        return 2;
    }
}

int runAllChecks() {
    string tempBase = FileHelper::toUniversalSlash(fs::temp_directory_path().string());
    while (!tempBase.empty() && tempBase.back() == '/')
        tempBase.pop_back();
    string sandbox = tempBase + "/ElanFileHelperTest";
    std::error_code ec;
    fs::remove_all(sandbox, ec);
    fs::create_directories(sandbox);

    std::cout << "sandbox: " << sandbox << "\n\n";

    /* --- string helpers, no disk --- */

    CHECK(FileHelper::hasExt("C:/a/b.txt"));
    CHECK(!FileHelper::hasExt("C:/a.dir/b"));
    CHECK(!FileHelper::hasExt("noext"));
    CHECK_EQ(FileHelper::getExt("C:/a/b.txt"), ".txt");
    CHECK_EQ(FileHelper::getExt("C:/a/noext"), "");
    CHECK_EQ(FileHelper::setExt("C:/a/b.txt", "wav"), "C:/a/b.wav");
    CHECK_EQ(FileHelper::setExt("C:/a/b", "wav"), "C:/a/b.wav");
    CHECK_EQ(FileHelper::removeExt("C:/a/b.txt"), "b");
    CHECK_EQ(FileHelper::getName("C:/a/b.txt"), "b.txt");
    CHECK_EQ(FileHelper::getName("C:/a/b/"), "b");
    CHECK_EQ(FileHelper::getNameNoExt("C:/a/b.txt"), "b");
    CHECK_EQ(FileHelper::getDir("C:/a/b.txt"), "C:/a");
    CHECK_EQ(FileHelper::getDir("C:/a"), "C:/");
    CHECK_EQ(FileHelper::addSuffix("C:/a/b.txt", "_backup"), "C:/a/b_backup.txt");
    CHECK_EQ(FileHelper::addSuffix("b.txt", "_v2"), "b_v2.txt");
    CHECK_EQ(FileHelper::assumeExt("b", "txt"), "b.txt");
    CHECK_EQ(FileHelper::assumeExt("b.wav", "txt"), "b.wav");
    CHECK_EQ(FileHelper::assumeFolder("b.txt", "C:/base"), "C:/base/b.txt");
    CHECK_EQ(FileHelper::assumeFolder("C:/x/b.txt", "C:/base"), "C:/x/b.txt");
    CHECK_EQ(FileHelper::toUniversalSlash("C:\\a\\b"), "C:/a/b");
    CHECK_EQ(FileHelper::createLegalString("C:/bad*name?.txt"), "C:/bad_name_.txt");
    CHECK_EQ(FileHelper::createLegalString("C://double//slash/file.txt"), "C:/double/slash/file.txt");
    CHECK_EQ(FileHelper::getParent(1, "C:/a/b/c"), "C:/a/b");
    CHECK_EQ(FileHelper::getParent(2, "C:/a/b/c"), "C:/a");
    CHECK_EQ(FileHelper::getParent(0, "C:/a/b/file.txt"), "C:/a/b");

    CHECK(FileHelper::isAbsolutePath("C:/a"));
    CHECK(FileHelper::isRelativePath("a/b.txt"));
    CHECK(FileHelper::isValidFileName("good name.txt"));
    CHECK(!FileHelper::isValidFileName("bad:name.txt"));
    CHECK(!FileHelper::isValidFileName("trailingdot."));
    CHECK(!FileHelper::isValidFileName(" leading.txt"));
    CHECK(!FileHelper::isValidFileName("space .txt"));
    CHECK(FileHelper::isValidFilePath("C:/a/b.txt"));
    CHECK(!FileHelper::isValidFilePath("relative/b.txt"));
    CHECK(!FileHelper::isValidFilePath("C:/a/b/"));
    CHECK_THROWS(FileHelper::ensureValidFileName("ill*egal"));
    CHECK_THROWS(FileHelper::ensureValidDir("C:/a//b"));

    string drive, dir, nameNoExt, ext;
    FileHelper::getFileParts(string("C:/music/song.wav"), drive, dir, nameNoExt, ext);
    CHECK_EQ(drive, "C");
    CHECK_EQ(nameNoExt, "song");
    CHECK_EQ(ext, "wav");

    string dir3, name3, ext3;
    FileHelper::getFileParts(string("C:/music/song.wav"), dir3, name3, ext3);
    CHECK_EQ(dir3, "C:/music");
    CHECK_EQ(name3, "song");
    CHECK_EQ(ext3, "wav");

    /* --- disk operations --- */

    string fileA = sandbox + "/alpha.txt";
    FileHelper::createTextFile(fileA, "hello", false);
    CHECK(FileHelper::doesFileExist(fileA));
    CHECK_EQ(FileHelper::readFile(fileA), "hello");
    CHECK_THROWS(FileHelper::createFile(fileA, false));
    FileHelper::createTextFile(fileA, "rewritten", true);
    CHECK_EQ(FileHelper::readFile(fileA), "rewritten");

    string subDir = sandbox + "/sub/deeper";
    FileHelper::createFolder(subDir);
    CHECK(FileHelper::doesFolderExist(subDir));

    string fileB = FileHelper::copyFile(fileA, subDir + "/beta.txt", false);
    CHECK(FileHelper::doesFileExist(fileB));
    CHECK(FileHelper::isDataEqual(fileA, fileB));

    string renamed = FileHelper::setFileName(fileB, "gamma.txt", false);
    CHECK_EQ(FileHelper::getName(renamed), "gamma.txt");
    CHECK(!FileHelper::doesFileExist(fileB));

    string moved = FileHelper::setFileDir(renamed, sandbox + "/moved", false);
    CHECK(FileHelper::doesFileExist(moved));
    CHECK_EQ(FileHelper::getDir(moved), sandbox + "/moved");

    CHECK_EQ(FileHelper::getUnusedFilePath(fileA, false), sandbox + "/alpha(2).txt");

    auto files = FileHelper::getFiles(sandbox);
    CHECK(files.size() == 1); // just alpha.txt at top level
    auto filesRec = FileHelper::getFilesRecursive(sandbox);
    CHECK(filesRec.size() == 2); // alpha.txt + moved/gamma.txt
    auto txtOnly = FileHelper::getFilesRecursive(sandbox, "*.txt");
    CHECK(txtOnly.size() == 2);
    auto wavOnly = FileHelper::getFilesRecursive(sandbox, "*.wav");
    CHECK(wavOnly.empty());
    auto folders = FileHelper::getFolders(sandbox);
    CHECK(folders.size() == 2); // sub, moved

    auto removedFolders = FileHelper::removeEmptyFoldersRecursive(sandbox + "/sub");
    CHECK(removedFolders.size() == 2); // deeper then sub itself
    CHECK(!FileHelper::doesFolderExist(sandbox + "/sub"));

    string temp = FileHelper::createTempFile();
    CHECK(FileHelper::doesFileExist(temp));
    FileHelper::permanentDelete(temp);
    CHECK(!FileHelper::doesFileExist(temp));

    /* --- BatchFileHelper: duplicate suffixes + creation --- */

    string batchDir = sandbox + "/batch";
    FileHelper::createFolder(batchDir);
    FileHelper::createTextFile(batchDir + "/track.txt", "existing", false);

    std::vector<string> wanted = {
        batchDir + "/track.txt", // collides with existing file
        batchDir + "/track.txt", // and with itself
        batchDir + "/fresh.txt",
    };

    BatchFileHelper bfh(wanted);
    CHECK(bfh.checkForDuplicates());
    CHECK(bfh.createNewFilesAndAddDupeSuffixToExistingFiles());

    auto batchFiles = FileHelper::getFiles(batchDir);
    CHECK(batchFiles.size() == 4); // track(1..3) + fresh
    CHECK(FileHelper::doesFileExist(batchDir + "/fresh.txt"));
    int trackDupes = 0, withContent = 0;
    for (auto& f : batchFiles) {
        if (FileHelper::getName(f).rfind("track(", 0) == 0) {
            ++trackDupes;
            if (FileHelper::readFile(f) == "existing")
                ++withContent;
        }
    }
    CHECK(trackDupes == 3);
    CHECK(withContent == 1); // exactly one of them is the original, content intact

    /* --- BatchFileChanger: move + rename + copy, then undo --- */

    string changerDir = sandbox + "/changer";
    FileHelper::createFolder(changerDir);
    FileHelper::createTextFile(changerDir + "/one.txt", "1", false);
    FileHelper::createTextFile(changerDir + "/two.txt", "2", false);
    FileHelper::createTextFile(changerDir + "/three.txt", "3", false);
    FileHelper::createFolder(changerDir + "/dest");

    BatchFileChanger changer;
    changer.addTask(changerDir + "/one.txt", changerDir + "/dest", BatchFileChanger::Operation::move);
    changer.addTask(changerDir + "/two.txt", "two_renamed.txt", BatchFileChanger::Operation::rename);
    changer.addTask(changerDir + "/three.txt", changerDir + "/dest/three_copy.txt", BatchFileChanger::Operation::copy);

    CHECK(changer.checkForConflicts().empty());
    changer.runTasks();

    CHECK(FileHelper::doesFileExist(changerDir + "/dest/one.txt"));
    CHECK(!FileHelper::doesFileExist(changerDir + "/one.txt"));
    CHECK(FileHelper::doesFileExist(changerDir + "/two_renamed.txt"));
    CHECK(FileHelper::doesFileExist(changerDir + "/dest/three_copy.txt"));
    CHECK(FileHelper::doesFileExist(changerDir + "/three.txt")); // copy leaves source

    changer.undoTasks();

    CHECK(FileHelper::doesFileExist(changerDir + "/one.txt"));
    CHECK(!FileHelper::doesFileExist(changerDir + "/dest/one.txt"));
    CHECK(FileHelper::doesFileExist(changerDir + "/two.txt"));
    CHECK(!FileHelper::doesFileExist(changerDir + "/two_renamed.txt"));
    CHECK(!FileHelper::doesFileExist(changerDir + "/dest/three_copy.txt"));
    CHECK(FileHelper::doesFileExist(changerDir + "/three.txt"));

    /* cleanup */
    fs::remove_all(sandbox, ec);

    std::cout << "\n" << (failures == 0 ? "ALL PASS" : std::to_string(failures) + " FAILURE(S)") << "\n";
    return failures == 0 ? 0 : 1;
}
