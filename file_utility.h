#ifndef _FILE_UTILITY_
#define _FILE_UTILITY_

#define CSV_FILENAME_PATH_LENGTH 128
#define RECIEVE_FILE_PATH_LENGTH 256

typedef struct csv_fileName_path_s{
    char gsxFileNamePath[CSV_FILENAME_PATH_LENGTH];
    char dslFileNamePath[CSV_FILENAME_PATH_LENGTH];
    char fxsFileNamePath[CSV_FILENAME_PATH_LENGTH];
    char voipFileNamePath[CSV_FILENAME_PATH_LENGTH];
    char mgmtFileNamePath[CSV_FILENAME_PATH_LENGTH];
}csv_fileName_path_t;

extern csv_fileName_path_t csv_fileName_path;

extern char backup_dir_path[];

typedef struct csvFilePtr_s{
    FILE* gsx;
    FILE* dsl;
    FILE* fxs;
    FILE* voip;
    FILE* mgmt;
}csvFilePtr_t;

char* get_setting(char * prefix, char * path);
int getFilelist(char *path);
char* getFileName(int index);
void arrayNodeFree();
void set_CSV_filenamePath(time_t* timep);
int fopen_CSV_Files(csvFilePtr_t*);
int fclose_CSV_Files(csvFilePtr_t*);
int parseRowData_OutputCSV(char*, csvFilePtr_t*);
void make_backup_dir(struct tm *p);
void moveRowDataFiles_to_backupDir(char *filePath);
int checkAndDelete_backupDIRs(time_t *time);
int checkAndDelete_csvFiles(time_t *time);
void compress_backupDir();
void moveRowDataFiles_to_backupDir_inRec(char *filePath);
void make_backup_dir_inRec(struct tm *p);
void compress_backupDir_inRec();
void compress_folders_inCsv();
int upload_csv_to_target(time_t time);
int reupload_csv_to_target();

#endif /* End of _FILE_UTILITY_ */
