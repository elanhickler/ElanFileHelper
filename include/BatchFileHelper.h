#pragma once

#include <map>
#include <memory>
#include <random>
#include <regex>
#include <string>
#include <vector>

/*
std edition: ported from the original JUCE version. All paths are
std::string. See FileHelper.h for path nomenclature.
*/

class BatchFileChanger {
  public:
    BatchFileChanger()  = default;
    ~BatchFileChanger() = default;

    enum Operation {
        none,
        copy,
        move,
        rename,
        temporary,
        invalid
    };

    enum Padding {
        off,
        automatic
    };

    enum Order {
        forward,
        reverse,
        random
    };

    class Task {
      public:
        friend BatchFileChanger;

        // returns newFile or newFileWithDupID based on what is relevant
        std::string getNewPathStr() {
            return newFileWithDupID.empty() ? newFile : newFileWithDupID;
        }

        int getDupNumber() {
            return dupNumber;
        }

      protected:
        std::string origFile;
        std::string userInput;
        std::string random;
        std::string newFile;
        std::string newFileWithDupID;
        int dupNumber = 1;

        Operation operation = Operation::none;

        bool completedTask         = false;
        bool completedRandomRemove = false;
        bool taskFailed            = false;
        bool undoFailed            = false;
    };

    /*
    OPERATIONS
    none: This is reserved for files not being manipulated.
    move: New path can be a directory or a file path.
    rename: New path can be a file name or file path.
    copy: New path can be empty if duplicate files are desired. If a name is provided, the copied file will share the same directory with a new name.
    temporary: Moves file to a new folder where the new path is the given new path plus the original path, essentially preserving its folder structure.
    */
    Task* addTask(std::string originalPath, std::string newPath, Operation operation);

    // Customize the duplicate formatting. `regexMatch` is an ECMAScript regex applied to the
    // file name; `tagScriptReplace` is its replacement string (supports $1, $& etc.) where
    // "<num>" is substituted with the duplicate number. Default turns "name.ext" into "name(<num>).ext".
    void setDuplicateFormat(int startingNumber,
        std::string regexMatch = "(.*)\\.", std::string tagScriptReplace = "$1(<num>).",
        int padFlag = Padding::automatic, int orderFlag = Order::forward,
        bool onlyRenameDuplicates = true);

    // Avoid duplicate filenames before running tasks using this function. By default, it will use fileName(1).ext, fileName(2).ext style of formatting.
    void applyDuplicateFormat();

    // Returns tab and newline separated string displaying file conflicts that will occur if tasks are run. Returns empty string if no issues found.
    std::string checkForConflicts();

    // Runs all tasks. Once tasks are run, tasks can no longer be changed. If fails, can be called again and continues where it left off.
    void runTasks();

    // Runs tasks in reverse attempting to restore files to a state before tasks were run. If fails, can be called again and continues where it left off.
    void undoTasks();

    // Returns comma separated array of strings of oldPath,newPath.
    std::vector<std::string> getRenameList();

    std::vector<std::unique_ptr<Task>> TASKS;

  protected:
    struct DuplicateFormat {
        int startingNumber = 1;
        std::regex regexMatch{ "(.*)\\." };
        std::string tagScriptReplace = "$1(<num>).";
        int padFlag                  = Padding::automatic;
        int orderFlag                = Order::forward;
        bool onlyRenameDuplicates    = true;
    } duplicateFormat;

    std::string getOperationName(Operation operation);

    std::string generateRandom7CharString();

    // map of new path string and task inside a map of directories
    std::map<std::string, std::map<std::string, std::vector<Task*>>> taskMapNew;
    std::map<std::string, std::map<std::string, std::vector<Task*>>> taskMapOriginal;
    std::vector<Task*> orderOfOperations;

    // map of random strings generated to make sure no duplicates
    std::map<std::string, int> randomStrings;

    std::mt19937 randomGen{ std::random_device{}() };
    bool runTasksWasCalled    = false;
    bool didCheckForConflicts = false;
};

/*
Helps create a list of non-existing file paths, checks for duplicates, and adds a duplicate suffix number, and renames existing files with duplicate suffix numbers when needed.
Pass in an array of strings and BatchFileHelper will add those strings as files to a collection as well as add all existing files that are in folders of provided strings.
Call clearDupeNumbers() if you want non-existing and existing files to add dupe suffixes from scratch.
*/
class BatchFileHelper {
  public:
    enum AccessMethod {
        originalNameWithoutSuffix,
        originalNameWithSuffix,
        newNameWithoutSuffix,
        newNameWithSuffix
    };

    BatchFileHelper(const std::vector<std::string>& filePathStrings);

    std::vector<std::string> getFilesToBeCreated();

    /*
    Recommend calling 'checkForDuplicates(AccessMethod::newNameWithSuffix)' before
    running this action. If you want to solve duplicates, call 'addDuplicateSuffixToCollection()'
    */
    bool createNewFiles();

    /*
    This function will rename existing files so they are not overwritten.
    */
    bool createNewFilesAndAddDupeSuffixToExistingFiles(bool doNotModifyNonDuplicates = false);

    /*
    Removes dupe numbers from rename schedule including existing files,
    useful when you need to remove the dupe suffix from existing files
    so that there are no non-consecutive dupe numbers.
    */
    void clearDupeNumbers();

    std::vector<int> getUnusedDupeNumbers(std::vector<std::string>& files);

    /*
    Prints rename list as a string as oldname + seperator + newname + \n and each folder is separated by additional spaces.
    Duplicate file check is based on non-suffixed version of the file names.
    */
    std::string printRenameList(std::string seperator = " -> ", bool printOnlyIfRenaming = false, bool printDuplicatesOnly = false);

    /*
    Prints rename schedule list as "newname + \n" and each folder is separated by additional spaces.
    Duplicate file check is based on non-suffixed version of the file names.
    */
    std::string printFileList();

    /*
    Checks whether BatchFileHelper contains duplicate file names.
    Duplicate file check is based on the method used, either with or without duplicate suffix, either original file names or scheduled new file names
    By default it will perform a duplicate check on files specified at creation and against existing files.
    */
    bool checkForDuplicates(AccessMethod method = originalNameWithoutSuffix);

    /*
    Checks the file rename schedule list for illegal characters in file name and path.
    Returns true if any entry is invalid.
    */
    bool fileListContainsIllegalChars(AccessMethod method = newNameWithSuffix);

    /*
    Replaces all illegal characters with _ and trims whitespace where needed. This overwrites all changes made with other functions! Call this first!
    NOTE: You may want to check for duplicates after running this action or call 'addDuplicateSuffixToCollection' to ensure no duplicates
    NOTE: This will reset the undo functionality.
    */
    void solveIllegalCharacterIssues();

    /*
    Adds a "duplicate" suffix of "(#)" to the string in order to prevent duplicate strings, e.g 'buiscuits' appearing 3 times will be 'buiscuits(1)', 'buiscuits(2)', 'biscuits(3)',
    '(1)' will be added to the first duplicate.
    Suffixes on existing files will be preserved. Existing files will be assigned suffix numbers above and below existing suffixes
    */
    void addDuplicateSuffixToCollection(bool doNotModifyNonDuplicates = false);

    /*
    This cannot be undone unless the action fails at creating a temp folder. Undo will automatically be triggered if needed.
    files are moved to saltTempDelete folder (found in directory of given file) and then the folder is moved to trash.
    By default, files deleted are the ones specified when this object was created. Change method to delete rename schedule files.
    */
    bool trashListedExistingFiles(AccessMethod method = originalNameWithSuffix);

    // Attempts to undo file changes. Returns false if something went wrong. Call this function again to retry the undo, it will pick up where it left off.
    bool undoFileChanges();

  protected:
    struct fileNameChangesStruct {
        std::string oldFile;
        std::string newFile;
        bool fileWasCreated = false;
    };

    struct fileNameDupeStruct {
        fileNameDupeStruct();
        fileNameDupeStruct(const std::string& s, int addIndex);

        bool hasDupeNumber();

        void clearDupeNumber();

        void updateNewPath(const std::string& s);

        // full path of the file on disk; empty if this entry is a to-be-created file
        std::string originalFile;
        std::string dir;
        std::string originalNameNoSuffix;
        std::string originalNameNoSuffixNoExt;
        std::string ext; // WITH dot, e.g. ".txt" (empty if no extension)
        std::string originalName;
        std::string newName;
        std::string newPath;
        int dupeNumber = -1;
        int idx        = -1;

        static bool hasDupeSuffix(const std::string& s, int& indexOfOpenParenOut, int& indexOfCloseParenOut, int& dupeNumberOut);

        // NOTE: Provide file name without extension!!!
        static std::string removeDupeSuffix(const std::string& fileNameNoExt, int* dupeNumberOut = nullptr);

        static std::string addDupeSuffix(const std::string& s, int dupeNumberToUse);
    };

    std::vector<fileNameChangesStruct> fileNameChanges;
    std::vector<std::string> newFileList;
    std::map<std::string, std::map<std::string, std::vector<fileNameDupeStruct>>> folderFileMap;
    std::mt19937 random{ std::random_device{}() };
    int successfulUndoActions = 0;
    std::map<std::string, std::string> renameList;

    void collectFiles();

    void collectFiles(const std::vector<std::string>& newFiles, std::map<std::string, std::map<std::string, std::vector<fileNameDupeStruct>>>& fileMapInput, AccessMethod methodForDuplicateCheck);

    void refreshFileFolderMap();

    void swapFileNames(std::string& file1, std::string& file2, std::vector<fileNameChangesStruct>& changes);

    std::string generateRandom4CharString();

    bool attemptToUndoFileRenames();

    bool renameExistingFiles();
};
