#pragma once

#include <map>
#include <string>
#include <vector>

/*
A class for manipulating existing files with throw and no-throw functions.
This class is NOT for manipulating strings that are file-like, although
there are a few helper functions for manipulating strings as needed.

std edition: ported from the original JUCE version. All paths are
std::string (system narrow encoding; ASCII-safe everywhere, UTF-8 works on
most setups). Internally backed by std::filesystem. The old StringOrFile
helper class is gone -- everything takes and returns std::string.

NOTE: There will not be an ending slash when retrieving folders/paths/directories

NOMENCLATURE
------------
Name = name of a folder or file
Path = full path to a file or folder
Dir  = full path to folder
Ext  = extension short hand

File     = file full path
FilePath = file full path
FileName = file name
FileDir  = path to file without its name

Folder     = folder full path
FolderPath = folder full path
FolderName = folder name
FolderDir  = path to folder without its name
*/

class FileHelper {
  public:
    FileHelper()  = default;
    ~FileHelper() = default;

    enum class Error {
        empty_string,
        path_is_relative,
        illegal_char,
        path_is_dir,
        begins_with_space,
        space_before_ext,
        ends_with_space,
        ends_with_dot,
        ends_or_begins_with_space,
        file_already_exists,
        folder_already_exists,
        file_already_exists_as_folder,
        directory_with_multi_slash,
        directory_already_exists_as_file,
        path_does_not_exist,
        file_does_not_exist,
        directory_does_not_exist,
        unknown_error_create_dir,
        unknown_error_create_file,
        unknown_error_file_move_or_rename,
        unknown_error_folder_move_or_rename,
        unknown_error_overwrite_file,
        unknown_error_file_copy,
        unknown_error_modify_file,
        unknown_error_file_trash,
        unknown_error_file_delete
    };

    static std::map<FileHelper::Error, std::string> errorMessages;

    static bool isIllegalNameChar(char c);
    static bool isIllegalDirChar(char c);

    /* NO THROW FILE CHECKS */

    // True if path exists as a file. Does not throw exception.
    static bool doesFileExist(const std::string& file);
    // True if path exists as a folder. Does not throw exception.
    static bool doesFolderExist(const std::string& directory);
    // True if path exists as a file or folder. Does not throw exception.
    static bool doesPathExist(const std::string& path);
    // Asks if path contains files. Does not throw exception. Returns false if directory does not exist, no files, or only folders are found.
    static bool doesDirContainFiles(const std::string& directory, const std::string& wildCard = "*");
    // Asks if path contains folders. Does not throw exception. Returns false if directory does not exist, no folders, or only files are found.
    static bool doesDirContainFolders(const std::string& directory, const std::string& wildCard = "*");
    // Checks if given path is an absolute/full path. Does not throw.
    static bool isAbsolutePath(const std::string& path);
    // Checks if given path is a relative/partial path. Does not throw.
    static bool isRelativePath(const std::string& path);
    // Checks if given path is a file. Does not throw.
    static bool isFile(const std::string& path);
    // Checks if given path is a folder. Does not throw.
    static bool isFolder(const std::string& path);
    // Returns 0 if not valid. Returns 1 if path is a file path, 2 if a folder path. If path does not exist, assumes file if has extension, otherwise assumes folder.
    static int isFileOrFolder(const std::string& path);

    /* NO THROW CHECK FOR VALID PATHS */

    // Asks if string is a path with a valid file name, does not throw exception.
    static bool isValidFilePath(const std::string& file);
    // Asks if file is a valid file name without a path, does not throw exception.
    static bool isValidFileName(const std::string& file);
    // Asks if file is a valid folder name without a path, does not throw exception.
    static bool isValidFolderName(const std::string& folderName);
    // Asks if file is a path to a folder or file, does not throw exception.
    static bool isValidPath(const std::string& file);
    // Asks if file is a folder path, does not throw exception.
    static bool isValidFolderPath(const std::string& dir);

    /* VALID PATH CHECKS THAT THROW */

    // Throws exception if path is not a file path or badly formatted.
    static void ensureValidFilePath(const std::string& path);
    // Path may be a file or folder. Throws exception if badly formatted.
    static void ensureValidPath(const std::string& path);
    // Path must be a folder. Throws exception if path is badly formatted.
    static void ensureValidDir(const std::string& directory);
    // Throws exception if badly formatted file name
    static void ensureValidFileName(const std::string& fileName);
    // Throws exception if badly formatted folder name
    static void ensureValidFolderName(const std::string& folderName);
    // Throws exception if badly formatted path or path does not exist.
    static void ensurePathExists(const std::string& path);
    // Throws exception if badly formatted file path or file does not exist.
    static void ensureFileExists(const std::string& file);
    // Throws exception if file already exists.
    static bool ensureFileNotExist(const std::string& file);
    // Throws exception if badly formatted directory or directory does not exist.
    static void ensureFolderExists(const std::string& directory);

    /* RENAME, MOVE, PATH INFO, ASSUME PATH */

    // Throws exception if file is invalid or if file is a folder or fails to modify file
    static std::string setFileDir(const std::string& file, const std::string& fileDir, bool overwrite);
    // Affects the file name and its dot and extension. Throws exception if file is invalid or if file is a folder or fails to modify file
    static std::string setFileName(const std::string& file, const std::string& fileName, bool overwrite);
    // Throws exception if folderName is invalid or fails to modify file's folder
    static std::string setFolderName(const std::string& folder, const std::string& newName, bool overwrite);
    // Returns name of folder or file with dot and extension
    static std::string getName(const std::string& path);
    // Returns name of folder or file without dot and extension
    static std::string getNameNoExt(const std::string& path);
    // Returns file or folder after renaming
    static std::string setName(const std::string& path, const std::string& name, bool overwrite);

    // Throws exception if fails to modify file
    static std::string setFilePath(const std::string& file, const std::string& filePath, bool overwrite);
    // Throws exception if fails to modify file or folder
    static std::string setPath(const std::string& path, const std::string& newPath, bool overwrite);

    // Replaces illegal characters with '_' and removes duplicate slashes
    static std::string createLegalString(const std::string& filePathOrName);

    // Returns true if path has a file extension.
    static bool hasExt(const std::string& filePath);
    // Returns the extension part of file with dot (e.g. ".txt"), or empty string.
    static std::string getExt(const std::string& file);
    // Adds or changes the extension for file string and returns the new string. `ext` is given without dot.
    static std::string setExt(const std::string& file, const std::string& ext);
    // Return string for file name without an extension (name only, path stripped). Does not throw.
    static std::string removeExt(const std::string& file);

    // If file does not have an extension, set to given extension. Does not throw.
    static std::string assumeExt(const std::string& file, const std::string& ext);
    // If file string is a relative path, a base folder is added to the returned string. Does not throw.
    static std::string assumeFolder(const std::string& file, const std::string& folder);
    // Adds a base folder to a relative path and extension to string without an extension. Does not throw.
    static std::string assume(const std::string& file, const std::string& folder, const std::string& ext);

    // Return string with added suffix before the extension (whole input preserved). Does not throw.
    static std::string addSuffix(const std::string& fileName, const std::string& suffix);
    // Extracts the folder path from given file or folder. Does not throw.
    static std::string getFolder(const std::string& f) {
        return getParent(0, f);
    }
    // Extracts the parent folder path from given path. Does not throw.
    static std::string getDir(const std::string& f);
    // Returns file parts: drive letter without punctuation, directory without drive letter, file name without extension, file extension without dot, via 4 output parameters.
    static void getFileParts(const std::string& f, std::string& driveLetterOut, std::string& fileDirOut, std::string& fileNameNoExtOut, std::string& fileExtOut);
    // Returns file parts: directory, file name without extension, file extension without dot, via 3 output parameters.
    static void getFileParts(const std::string& f, std::string& fileDirOut, std::string& fileNameNoExtOut, std::string& fileExtOut);
    // Returns file parts: directory and file name with dot and extension via 2 output parameters.
    static void getFileParts(const std::string& f, std::string& fileDirOut, std::string& fileNameOut);

    /* OPERATING SYSTEM OPEN or REVEAL */

    // Opens folder. Throws exception if badly formatted path, error creating folder, or folder does not exist.
    static void openFolder(const std::string& directory, bool createIfNeeded);
    // Opens or runs file. Throws exception if badly formatted path, error creating file, or file does not exist.
    static void openFile(const std::string& path, bool createIfNeeded);
    // Opens the folder that contains given folder. Throws exception if badly formatted path, error creating folder, or folder does not exist.
    static void revealFolder(const std::string& path, bool createIfNeeded);
    // Opens the folder that contains file. Throws exception if badly formatted path, error creating folder, or file does not exist.
    static void revealFile(const std::string& path, bool createIfNeeded);

    /* GET PATHS, READ, CREATE, DELETE FILE */

    /**
     * 1. Creates any parent directories.
     * 2. Throw if the file path appears to be invalid in some way
     * 3. Throw if file exists and overwrite is not true
     * 4. Throw if the file cannot be created
     * 5. Creates the file.
     */
    static std::string createFile(const std::string& filePath, bool overwrite);
    // Creates a file in operating system's temporary folder and returns the file path.
    static std::string createTempFile();
    // Creates text file. Throws exception for badly formatted path or error creating file.
    static void createTextFile(const std::string& filePath, const std::string& text, bool overwrite);
    // Returns file contents as a string. Throws exception if file does not exist
    static std::string readFile(const std::string& filePath);
    // Creates folder for path. Throws exception if badly formatted path or error creating folder.
    static void createFolder(const std::string& fileDir);
    // Copies file to new path and returns it. Throws exception for badly formatted path or error creating file.
    static std::string copyFile(const std::string& origFile, const std::string& filePath, bool overwrite);
    // Moves file or folder to trash / recycle bin. Throws exception on failure. (Windows implementation; throws elsewhere.)
    static void moveToTrash(const std::string& path);
    // Deletes a file or folder permanently. Throws exception if path is invalid or deletion failure.
    static void permanentDelete(const std::string& path);
    // Recursively deletes folders containing no files (deepest first) including the given folder if it ends up empty. Returns a list of removed folders.
    static std::vector<std::string> removeEmptyFoldersRecursive(const std::string& folder);
    // Recursively deletes files that are zero size. Throws exception if invalid path or failure.
    static std::vector<std::string> removeEmptyFilesRecursive(const std::string& folder);
    // Throws for invalid path. Gets list of folders immediately inside a directory. If given path is a file, function will use its directory.
    static std::vector<std::string> getFolders(const std::string& path, const std::string& wildCard = "*");
    // Throws for invalid path. Gets list of files immediately inside a directory. If given path is a file, function will use its directory.
    static std::vector<std::string> getFiles(const std::string& path, const std::string& wildCard = "*");
    // Throws for invalid path. Gets list of folders and subfolders inside a directory. If given path is a file, function will use its directory.
    static std::vector<std::string> getFoldersRecursive(const std::string& path, const std::string& wildCard = "*");
    // Throws for invalid path. Gets list of files in given directory and subfolders. If given path is a file, function will use its directory.
    static std::vector<std::string> getFilesRecursive(const std::string& path, const std::string& wildCard = "*");

    /* DATE, TIME, AND SLASH CONVERSION */

    // Get parent or 'up' or 'back' folders based on levels "up". Levels are clamped up to the source drive. NOTE: Removes the file from the path. No trailing slash.
    static std::string getParent(int levels, const std::string& path);
    // Gets the parent directory for folder or file.
    static std::string getParent(const std::string& path) {
        return getParent(1, path);
    }

    // Convert all slashes to forward slashes.
    static std::string toUniversalSlash(const std::string& path);

    // Convert all slashes to system native slash.
    static std::string toSystemSlash(const std::string& path);

    // format is Year-Month-Day e.g. 2001-12-01
    static std::string getDateStr(const std::string& separator = "-");
    // format is HourMinute e.g. 0105
    static std::string getTimeStr();

    // 2001-01-01_0101 -> Year-Month-Day HourMinute, separatorA is between date numbers, separatorB is between date and time.
    static std::string getDateAndTimeStr(const std::string& separatorA = "-", const std::string& separatorB = "_");

    /* FILE COMPARISONS */

    // Adds (2) suffix to filename before extension, with number depending on what final name is available to avoid overwriting another file.
    static std::string getUnusedFilePath(std::string filePath, bool replaceIllegalChars);

    static bool isDataEqual(const std::string& path1, const std::string& path2) {
        return FileHelper::readFile(path1) == FileHelper::readFile(path2);
    }

  protected:
    static void throwError(const std::string& path, FileHelper::Error v);

    static bool isSlashChar(char c);
};
