#ifndef _FAT16_H_INCLUDED
#define _FAT16_H_INCLUDED

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include "spi.h"

struct fileZipArchive;

struct fileTYPE
{
	fileTYPE();
	~fileTYPE();
	int opened();

	FILE           *filp;
	int             mode;
	int             type;
	fileZipArchive *zip;
	__off64_t       size;
	__off64_t       offset;
	char            path[1024];
	char            name[261];
};

struct direntext_t
{
	dirent de;
	int  cookie;
	char datecode[16];
	char altname[256];
};

/*
struct fileTextReader
{
	fileTextReader();
	~fileTextReader();

	size_t size;
	char *buffer;
	char *pos;
};
*/

int  FileOpen(fileTYPE *file, const char *name, char mute = 0);
int  FileOpenEx(fileTYPE *file, const char *name, int mode, char mute = 0, int use_zip = 1);
void FileClose(fileTYPE *file);

__off64_t FileGetSize(fileTYPE *file);

int FileSeek(fileTYPE *file, __off64_t offset, int origin);
int FileSeekLBA(fileTYPE *file, uint32_t offset);

int FileReadAdv(fileTYPE *file, void *pBuffer, int length, int failres = 0);
int FileReadSec(fileTYPE *file, void *pBuffer);
//int FileWriteAdv(fileTYPE *file, void *pBuffer, int length, int failres = 0);
//int FileWriteSec(fileTYPE *file, void *pBuffer);
//int FileCreatePath(const char *dir);

int FileExists(const char *name, int use_zip = 1);
//int FileCanWrite(const char *name);
//int PathIsDir(const char *name, int use_zip = 1);
//struct stat64* getPathStat(const char *path);

/*
#define SAVE_DIR "saves"
void FileGenerateSavePath(const char *name, char* out_name, int ext_replace = 1);

#define SAVESTATE_DIR "savestates"
void FileGenerateSavestatePath(const char *name, char* out_name, int sufx);

#define SCREENSHOT_DIR "screenshots"
#define SCREENSHOT_DEFAULT "screen"
void FileGenerateScreenshotName(const char *name, char *out_name, int buflen);

int FileSave(const char *name, void *pBuffer, int size);
int FileLoad(const char *name, void *pBuffer, int size); // supply pBuffer = 0 to get the file size without loading
int FileDelete(const char *name);
int DirDelete(const char *name);

//save/load from config dir
#define CONFIG_DIR "config"
int FileSaveConfig(const char *name, void *pBuffer, int size);
int FileLoadConfig(const char *name, void *pBuffer, int size); // supply pBuffer = 0 to get the file size without loading
int FileDeleteConfig(const char *name);

void AdjustDirectory(char *path);
int ScanDirectory(char* path, int mode, const char *extension, int options, const char *prefix = NULL, const char *filter = NULL);

void prefixGameDir(char *dir, size_t dir_len);
int findPrefixDir(char *dir, size_t dir_len);

*/
const char *getStorageDir(int dev);
const char *getRootDir();
const char *getFullPath(const char *name);
/*
uint32_t getFileType(const char *name);
int isXmlName(const char *path); // 1 - MRA, 2 - MGL

bool FileOpenTextReader(fileTextReader *reader, const char *path);
const char* FileReadLine(fileTextReader *reader);
*/
#define LOADBUF_SZ (1024*1024)

#define COEFF_DIR "filters"
#define GAMMA_DIR "gamma"
#define AFILTER_DIR "filters_audio"
#define SMASK_DIR "shadow_masks"
#define PRESET_DIR "presets"
#define GAMES_DIR "games"
#define CIFS_DIR "cifs"
#define DOCS_DIR "docs"

#endif
