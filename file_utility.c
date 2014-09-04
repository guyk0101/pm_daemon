#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <libgen.h>
#include "pm_daemon.h"
#include "file_utility.h"
#include "ftpclient.h"

#define MAX_PREFIX             128
#define MAX_BACKUP_DIR_LENGTH  256
#define SED_TEMP_FILE          "/tmp/pm_daemon_sed_reslut.txt"

enum {
    GSX,
    DSL,
    FXS,
    VOIP,
    MGMT
};

typedef struct arrayNode *arrayNodePtr;
typedef struct arrayNode {
    arrayNodePtr next;
    int size;     // array size
    int count;    // current element number
    char **item;  // pointer of array elements
}arrayNode_t;

static arrayNodePtr createArrayNode(int depth);
static void itemAdd(char *filename);

static arrayNodePtr head = NULL;
static arrayNodePtr current;

static int getIfType(char *lineData);
static void getIpAddr(char *filePath, char *ipAddr);
static void write_csv_header(FILE *fptr, char **csv_file_header);
static void write_csv_data(char *ipAddr, char *lineData, csvFilePtr_t *csvFilePtr, int ifType);
static int check_backupDIR(char *dirName, time_t *time);
static void delete_backupDIR(char *dirName);
static int check_csvFiles(char *fileName, time_t *time);
static void delete_csvFiles(char *fileName);
static int read_file_into_buf(char *path, char **buf);

csv_fileName_path_t csv_fileName_path;

char backup_dir_path[MAX_BACKUP_DIR_LENGTH];
char backup_dir_path_rec[MAX_BACKUP_DIR_LENGTH];

static char *gsx_csv_file_header[] = {
    "ipAdress,",
    "IF identifier,",
    "enetPmTxOctets,",
    "enetPmTxPkts,",
    "enetPmTxBroadcastPkts,",
    "enetPmTxMulticastPkts,",
    "enetPmRxOctets,",
    "enetPmRxPkts,",
    "enetPmRxBroadcastPkts,",
    "enetPmRxMulticastPkts,",
    "enetPmRxCRCAlignErrors,",
    "enetPmRxUndersizePkts,",
    "enetPmRxOversizePkts,",
    "enetPmRxFragments,",
    "enetPmCollisions,",
    "enetPmPkts64Octets,",
    "enetPmPkts65to127Octets,",
    "enetPmPkts128to255Octets,",
    "enetPmPkts256to511Octets,",
    "enetPmPkts512to1023Octets,",
    "enetPmPkts1024to1518Octets",
    NULL
};

static char *dsl_csv_file_header[] = {
    "ipAdress,",
    "IF identifier,",
    "xdsl2PMLFecs_xtuc,",
    "xdsl2PMLEs_xtuc,",
    "xdsl2PMLSes_xtuc,",
    "xdsl2PMLLoss_xtuc,",
    "xdsl2PMLUas_xtuc,",
    "xdsl2PMLFecs_xtur,",
    "xdsl2PMLEs_xtur,",
    "xdsl2PMLSes_xtur,",
    "xdsl2PMLLoss_xtur,",
    "xdsl2PMLUas_xtur,",
    "enetPmRxOversizePkts,",
    "xdsl2PMChCodingViolations_xtuc,",
    "xdsl2PMChCorrectedBlocks_xtuc,",
    "xdsl2PMChCodingViolations_xtur,",
    "xdsl2PMChCorrectedBlocks_xtur",
    NULL
};

static char *fxs_csv_file_header[] = {
    "ipAdress,",
    "IF identifier,",
    "fxsPMRtpElapsedTime,",
    "fxsPMRtpTxBytes,",
    "fxsPMRtpRxBytes,",
    "fxsPMRtpTxPackets,",
    "fxsPMRtpTxLostPackets,",
    "fxsPMRtpRxLostPackets,",
    "fxsPMInAbandonAfterRing,",
    "fxsPMInCallAttempt,",
    "fxsPMInCallConnected,",
    "fxsPMOutCallAttempt,",
    "fxsPMOutCallConnected,",
    "fxsPMOutIncompleteDialing,",
    "fxsPMOutPartialDialingAbandon,",
    "fxsPMOutPartialDialingTimeout,",
    "fxsPMOutNoDialingAbandon,",
    "fxsPMOutNoDialingTimeout",
    NULL
};

static char *voip_csv_file_header[] = {
    "ipAdress,",
    "IF identifier,",
    "voipPMIfTxBytes,",
    "voipPMIfRxBytes,",
    "voipPMIfTxUniPackets,",
    "voipPMIfRxUniPackets,",
    "voipPMIfTxNonuniPackets,",
    "voipPMIfRxNonuniPackets,",
    "voipPMH248TxPackets,",
    "voipPMH248RxPackets,",
    "voipPMH248TxResentPackets,",
    "voipPMH248TxLostPackets,",
    "voipPMH248TxErrorPackets,",
    "voipPMH248RxUnidentifiedPackets",
    NULL
};

static char *mgmt_csv_file_header[] = {
    "ipAdress,",
    "IF identifier,",
    "mgmtPMIfTxBytes,",
    "mgmtPMIfRxBytes,",
    "mgmtPMIfTxUniPackets,",
    "mgmtPMIfRxUniPackets,",
    "mgmtPMIfTxNonuniPackets,",
    "mgmtPMIfRxNonuniPackets",
    NULL
};


/*
 *  Get parameter from specified file
 */
char* get_setting(char *prefix, char *path) {
    static char buf[MAX_PREFIX];
    FILE *fp_log = NULL;
    char *log_ptr = NULL;
    int len, i;

    /* Check path does exist or not */
    if (access(path, R_OK)) {
        PM_DBG("%s does not exist! Fail to parse [%s]\n", path, prefix);
        goto _exit;
    }

    memset(buf, 0, MAX_PREFIX);
    len = snprintf(buf, MAX_PREFIX, "sed -n /'%s/p' %s > %s", prefix, path, SED_TEMP_FILE);
    if (len >= MAX_PREFIX) {
        PM_DBG("[%s() buffer is too small]\n", __func__);
        goto _exit;
    }
    system(buf);

    /* Open WC_TEMP_FILE to know current log number */
    if ((fp_log = fopen(SED_TEMP_FILE, "r")) == NULL) {
        PM_DBG("[%s() fopen(SED_TEMP_FILE) failed]\n", __func__);
        goto _exit;
    }
    memset(buf, 0, MAX_PREFIX);
    if (fgets(buf, MAX_PREFIX, fp_log) != NULL) {
        if ((log_ptr = strstr(buf, prefix)) != NULL) {
            /* Read out current_index */
            if ((strlen(buf) - strlen(prefix)) < 3) {
                log_ptr = NULL;
                goto _exit;
            }
            else {
                log_ptr += (strlen(prefix) + 1);
                while (*log_ptr == '=' ||
                       *log_ptr == ' ') {
                    log_ptr++;
                }
                goto _exit;
            }
        }
    }
_exit:
    if (fp_log != NULL) {
        fclose(fp_log);
        unlink(SED_TEMP_FILE);
    }

    /* Ignore "carriage return" and "NL line feed" */
    if (log_ptr) {
        for (i = 0, len = strlen(log_ptr); i < len; i++) {
            if ((*(log_ptr + i) == '\n') || (*(log_ptr + i) == '\r')) {
                *(log_ptr + i) = '\0';
            }
        }
    }

    return log_ptr;
}

int getFilelist(char *path) {
    DIR *dir;
    struct dirent *ptr;
    int count = 0;

    dir = opendir(path);
    if (dir == NULL) {
        PM_DBG("[%s() opendir(%s) failed]\n", __func__, path);
        return -1;
    }

    if (head == NULL) {
        head = createArrayNode(2500);
        current = head;
    }

/* In solaris, the implicit directory is readed first. But in linux it is read at last */
#if 0
    ptr = readdir(dir); // skip ptr->d_name = "."
    ptr = readdir(dir); // skip ptr->d_name = ".."
#endif

    while ((ptr = readdir(dir)) != NULL) {
        if (strncmp("0.7_", ptr->d_name, 4) == 0) {
            itemAdd(ptr->d_name);
            count++;
        }
    }

    closedir(dir);

    return count;
}

char* getFileName(int index) {
    arrayNodePtr curPtr = head;

    if (curPtr == NULL) {
        return NULL;
    }

    while (index > curPtr->size - 1) {
        index = index - curPtr->size;
        if (curPtr->next == NULL) {
            return NULL;
        }
        curPtr = curPtr->next;
    }

    return  curPtr->item[index];
}

static arrayNodePtr createArrayNode(int depth) {
    arrayNodePtr ptr;

    ptr = malloc(sizeof(*ptr));
    ptr->item = malloc(depth * sizeof(char *));
    memset(ptr->item, 0, depth * sizeof(char *));
    ptr->size = depth;
    ptr->count = 0;
    ptr->next = NULL;

    return ptr;
}

static void itemAdd(char *filename) {
    arrayNodePtr tmp;

    if (current->size == current->count) {
        tmp = createArrayNode(current->size * 2);
        current->next = tmp;
        current = current->next;
    }

    current->item[current->count] = malloc(sizeof(char) * strlen(filename) + 1);
    strcpy(current->item[current->count], filename);
    current->count++;

    return;
}

void arrayNodeFree() {
    int i;
    arrayNodePtr tmp, ptr = head;

    while (ptr != NULL) {
        for (i = 0; i < ptr->count; i++) {
            free(ptr->item[i]);
        }
        free(ptr->item);
        tmp = ptr;
        ptr = ptr->next;
        free(tmp);
    }
    head = NULL;

    return;
}

void set_CSV_filenamePath(time_t *timep) {

    char mkdirCmd[64];

    sprintf(mkdirCmd, "mkdir %s/%ld_csv", pmDaemonParam.CsvUploadZone, *timep);

    system(mkdirCmd);

    sprintf(csv_fileName_path.gsxFileNamePath, "%s/%ld_csv/gsx-%ld.csv", pmDaemonParam.CsvUploadZone, *timep, *timep);

    sprintf(csv_fileName_path.dslFileNamePath, "%s/%ld_csv/dsl-%ld.csv", pmDaemonParam.CsvUploadZone, *timep, *timep);

    sprintf(csv_fileName_path.fxsFileNamePath, "%s/%ld_csv/fxs-%ld.csv", pmDaemonParam.CsvUploadZone, *timep, *timep);

    sprintf(csv_fileName_path.voipFileNamePath, "%s/%ld_csv/voip-%ld.csv", pmDaemonParam.CsvUploadZone, *timep, *timep);

    sprintf(csv_fileName_path.mgmtFileNamePath, "%s/%ld_csv/mgmt-%ld.csv", pmDaemonParam.CsvUploadZone, *timep, *timep);

    return;
}

void make_backup_dir(struct tm *p) {
    char make_backup_dir[MAX_BACKUP_DIR_LENGTH + 6]; // extend 6 space size for "mkdir ".

    memset(backup_dir_path, 0, MAX_BACKUP_DIR_LENGTH);

    sprintf(backup_dir_path, "%s/%d-%d-%d-%d-%d", pmDaemonParam.BackupZone,
            (1900 + p->tm_year),
            (1 + p->tm_mon),
            (p->tm_mday),
            p->tm_hour,
            p->tm_min
            );

    sprintf(make_backup_dir, "mkdir %s", backup_dir_path);

    system(make_backup_dir);

    return;
}

void moveRowDataFiles_to_backupDir(char *filePath) {
    char moveCmd[512];

    sprintf(moveCmd, "mv %s %s", filePath, backup_dir_path);
    //sprintf(moveCmd, "rm %s", filePath);
    system(moveCmd);

    return;
}

static int check_backupDIR(char *dirName, time_t *time) {
    struct tm t;
    char dirNameTmp[32];
    time_t dirTime;
    long period;

    strcpy(dirNameTmp, dirName);

    if (!strcmp(dirNameTmp, ".") || !strcmp(dirNameTmp, ".."))
        return 0;

    t.tm_year = atoi(strtok(dirNameTmp, "-")) - 1900;
    t.tm_mon = atoi(strtok(NULL, "-")) - 1;
    t.tm_mday = atoi(strtok(NULL, "-"));
    t.tm_hour = atoi(strtok(NULL, "-"));
    t.tm_min = atoi(strtok(NULL, "-"));
    t.tm_sec = 0;
    t.tm_isdst = 0;

    dirTime = mktime(&t);

    period = (pmDaemonParam.KeepDayBackup * 1800 + 59); // Exclude extra seconds of the directory time. (ex: 1837)

    //PM_DBG("%s: dirName = %s, *time = %ld dirTime = %ld (*time - dirTime) = %ld\n", __FUNCTION__, dirName, *time, dirTime, (*time - dirTime));
    if ((*time - dirTime) > period)
        return 1;
    else
        return 0;

}

static void delete_backupDIR(char *dirName) {
    char rmDirCmd[128];

    sprintf(rmDirCmd, "rm %s/%s", pmDaemonParam.BackupZone, dirName);

    system(rmDirCmd);
}

int checkAndDelete_backupDIRs(time_t *time) {
    DIR *dir;
    struct dirent *ptr;
    int count = 0;

    dir = opendir(pmDaemonParam.BackupZone);
    if (dir == NULL) {
        PM_DBG("[%s() opendir(%s) failed]\n", __func__, pmDaemonParam.BackupZone);
        return -1;
    }

    while ((ptr = readdir(dir)) != NULL) {
        if (check_backupDIR(ptr->d_name, time))
            delete_backupDIR(ptr->d_name);
    }

    closedir(dir);

    return 0;
}

static int check_csvFiles(char *fileName, time_t *time) {
    char fileNameTmp[32];
    time_t fileTime;
    long period;

    strcpy(fileNameTmp, fileName);

    if (!strcmp(fileNameTmp, ".") || !strcmp(fileNameTmp, ".."))
        return 0;

    fileTime = atol(strtok(fileNameTmp, "_"));

    period = (pmDaemonParam.KeepDayBackup * 1800);

    if ((*time - fileTime) > period)
        return 1;
    else
        return 0;

}

static void delete_csvFiles(char *fileName) {
    char rmFileCmd[128];

    sprintf(rmFileCmd, "rm %s/%s", pmDaemonParam.CsvUploadZone, fileName);

    system(rmFileCmd);
}

int checkAndDelete_csvFiles(time_t *time) {
    DIR *dir;
    struct dirent *ptr;
    int count = 0;

    dir = opendir(pmDaemonParam.CsvUploadZone);
    if (dir == NULL) {
        PM_DBG("[%s() opendir(%s) failed]\n", __func__, pmDaemonParam.CsvUploadZone);
        return -1;
    }

    while ((ptr = readdir(dir)) != NULL) {
        if (check_csvFiles(ptr->d_name, time))
            delete_csvFiles(ptr->d_name);
    }

    closedir(dir);

    return 0;
}

int fopen_CSV_Files(csvFilePtr_t *csvFilePtr) {
    if ((csvFilePtr->gsx = fopen(csv_fileName_path.gsxFileNamePath, "a+")) == NULL) {
        PM_DBG("[%s() open(%s) failed]\n", __func__, csv_fileName_path.gsxFileNamePath);
        goto fail;
    }
    write_csv_header(csvFilePtr->gsx, gsx_csv_file_header);

    if ((csvFilePtr->dsl = fopen(csv_fileName_path.dslFileNamePath, "a+")) == NULL) {
        PM_DBG("[%s() open(%s) failed]\n", __func__, csv_fileName_path.dslFileNamePath);
        goto fail;
    }
    write_csv_header(csvFilePtr->dsl, dsl_csv_file_header);

    if ((csvFilePtr->fxs = fopen(csv_fileName_path.fxsFileNamePath, "a+")) == NULL) {
        PM_DBG("[%s() open(%s) failed]\n", __func__, csv_fileName_path.fxsFileNamePath);
        goto fail;
    }
    write_csv_header(csvFilePtr->fxs, fxs_csv_file_header);

    if ((csvFilePtr->voip = fopen(csv_fileName_path.voipFileNamePath, "a+")) == NULL) {
        PM_DBG("[%s() open(%s) failed]\n", __func__, csv_fileName_path.voipFileNamePath);
        goto fail;
    }
    write_csv_header(csvFilePtr->voip, voip_csv_file_header);

    if ((csvFilePtr->mgmt = fopen(csv_fileName_path.mgmtFileNamePath, "a+")) == NULL) {
        PM_DBG("[%s() open(%s) failed]\n", __func__, csv_fileName_path.mgmtFileNamePath);
        goto fail;
    }

    setvbuf(csvFilePtr->gsx, NULL, _IOFBF, 1024 * 24 * 1000);
    setvbuf(csvFilePtr->dsl, NULL, _IOFBF, 1024 * 24 * 1000);
    setvbuf(csvFilePtr->fxs, NULL, _IOFBF, 1024 * 24 * 1000);
    setvbuf(csvFilePtr->voip, NULL, _IOFBF, 1024 * 24 * 1000);
    setvbuf(csvFilePtr->mgmt, NULL, _IOFBF, 1024 * 24 * 1000);

    write_csv_header(csvFilePtr->mgmt, mgmt_csv_file_header);

    return 0;
fail:
    return -1;
}

int fclose_CSV_Files(csvFilePtr_t *csvFilePtr) {
    if (fclose(csvFilePtr->gsx)) {
        PM_DBG("[%s() close(%s) failed]\n", __func__, csv_fileName_path.gsxFileNamePath);
        goto fail;
    }

    if (fclose(csvFilePtr->dsl)) {
        PM_DBG("[%s() close(%s) failed]\n", __func__, csv_fileName_path.dslFileNamePath);
        goto fail;
    }

    if (fclose(csvFilePtr->fxs)) {
        PM_DBG("[%s() close(%s) failed]\n", __func__, csv_fileName_path.fxsFileNamePath);
        goto fail;
    }

    if (fclose(csvFilePtr->voip)) {
        PM_DBG("[%s() close(%s) failed]\n", __func__, csv_fileName_path.voipFileNamePath);
        goto fail;
    }

    if (fclose(csvFilePtr->mgmt)) {
        PM_DBG("[%s() close(%s) failed]\n", __func__, csv_fileName_path.mgmtFileNamePath);
        goto fail;
    }

    return 0;
fail:
    return -1;
}


static int getIfType(char *lineData) {

    if (!strncmp(lineData, "GSX", 3))
        return GSX;
    if (!strncmp(lineData, "DSL", 3))
        return DSL;
    if (!strncmp(lineData, "FXS", 3))
        return FXS;
    if (!strncmp(lineData, "VOIP", 4))
        return VOIP;
    if (!strncmp(lineData, "MGMT", 4))
        return MGMT;

    return -1;
}

static void getIpAddr(char *filePath, char *ipAddr) {
    char filePath_tmp[RECIEVE_FILE_PATH_LENGTH], *ip;

    strcpy(filePath_tmp, filePath);

    strtok(filePath_tmp, "_"); // version
    ip = strtok(NULL, "_");

    strcpy(ipAddr, ip);

    return;
}

static void write_csv_header(FILE *fptr, char **csv_file_header) {
    int i;
    char **aPtr;

    for (aPtr = csv_file_header, i = 0; aPtr[i] != NULL; i++) {
        fprintf(fptr, "%s", aPtr[i]);
    }

    fprintf(fptr, "\n");
}

static void write_csv_data(char *ipAddr, char *lineData, csvFilePtr_t *csvFilePtr, int ifType) {
    switch (ifType) {
    case GSX:
        fprintf(csvFilePtr->gsx, "%s,%s", ipAddr, lineData);
        break;
    case DSL:
        fprintf(csvFilePtr->dsl, "%s,%s", ipAddr, lineData);
        break;
    case FXS:
        fprintf(csvFilePtr->fxs, "%s,%s", ipAddr, lineData);
        break;
    case VOIP:
        fprintf(csvFilePtr->voip, "%s,%s", ipAddr, lineData);
        break;
    case MGMT:
        fprintf(csvFilePtr->mgmt, "%s,%s", ipAddr, lineData);
        break;
    default:
        break;
    }

    return;
}

int parseRowData_OutputCSV(char *filePath, csvFilePtr_t *csvFilePtr) {
    int ifType;
    char lineData[2048], ipAddr[16];
    FILE *fptr = NULL;

    memset(lineData, 0, 2048);
    memset(ipAddr, 0, 16);

    if (!(fptr = fopen(filePath, "r"))) {
        PM_DBG("[%s() open(%s) failed]\n", __func__, filePath);
        return -1;
    }

    getIpAddr(filePath, ipAddr);

    while (fgets(lineData, 2048, fptr)) {
        lineData[strlen(lineData)] = '\0';
        ifType = getIfType(lineData);
        write_csv_data(ipAddr, lineData, csvFilePtr, ifType);
    }

    fclose(fptr);

    return 0;
}

void compress_backupDir() {
    char compressCmd[256];
    char rmDirCmd[128];

    sprintf(compressCmd, "tar pcf %s.tar %s ; bzip2 %s.tar", backup_dir_path, backup_dir_path, backup_dir_path);

    sprintf(rmDirCmd, "rm -rf %s", backup_dir_path);

    system(compressCmd);
    system(rmDirCmd);

    return;
}

void moveRowDataFiles_to_backupDir_inRec(char *filePath) {
    char moveCmd[512];

    sprintf(moveCmd, "mv %s %s", filePath, backup_dir_path_rec);
    system(moveCmd);

    return;
}

void make_backup_dir_inRec(struct tm *p) {
    char make_backup_dir[MAX_BACKUP_DIR_LENGTH + 6]; // extend 6 space size for "mkdir ".

    memset(backup_dir_path_rec, 0, MAX_BACKUP_DIR_LENGTH);

    sprintf(backup_dir_path_rec, "%s/%d-%d-%d-%d-%d", pmDaemonParam.ReceiveZone,
            (1900 + p->tm_year),
            (1 + p->tm_mon),
            (p->tm_mday),
            p->tm_hour,
            p->tm_min
            );

    sprintf(make_backup_dir, "mkdir %s", backup_dir_path_rec);

    system(make_backup_dir);

    return;
}

void compress_backupDir_inRec() {
    char backup_dir_path_rec_tmp[MAX_BACKUP_DIR_LENGTH];
    char compressCmd[256];
    char rmDirCmd[128];
    char mvCmd[128];

    // Prevent dirname() to add NULL symbol on backup_dir_path_rec after dir path.
    memcpy(backup_dir_path_rec_tmp, backup_dir_path_rec, sizeof(backup_dir_path_rec_tmp));

    sprintf(compressCmd, "tar pcf %s.tar -C %s %s; gzip %s.tar", 
            backup_dir_path_rec, dirname(backup_dir_path_rec_tmp), basename(backup_dir_path_rec), backup_dir_path_rec);

    sprintf(rmDirCmd, "rm -rf %s", backup_dir_path_rec);

    sprintf(mvCmd, "mv %s.* %s", backup_dir_path_rec, pmDaemonParam.BackupZone);

    system(compressCmd);
    system(rmDirCmd);
    system(mvCmd);

    return;
}

const char *keyword = "_reupload";

int upload_csv_to_target(time_t time) {
    int s, len, ret, count = 0;
    DIR *dir;
    struct dirent *ptr;
    struct stat buf;
    char pathName[128], uploadPathName[128], reUploadName[128];
    char rmCmd[128];
    char fileTimeStamp[32];
    char *upload;

    sprintf(fileTimeStamp, "%ld", time);

    dir = opendir(pmDaemonParam.CsvUploadZone);
    if (dir == NULL) {
        PM_DBG("[%s() opendir(%s) failed]\n", __func__, pmDaemonParam.CsvUploadZone);
        return -1;
    }

    /* First, get the upload file path name */
    while ((ptr = readdir(dir)) != NULL) {
        memset(pathName, 0, sizeof(pathName));
        sprintf(pathName,"%s/%s", pmDaemonParam.CsvUploadZone, ptr->d_name);
        stat(pathName, &buf);

        if(S_ISREG(buf.st_mode) && (strstr(ptr->d_name, "tar") != NULL) && (strstr(ptr->d_name, fileTimeStamp) != NULL)) {
            sprintf(uploadPathName, "%s", pathName);
        }
    }

    /* If uploading fail, it will be renamed for other uploading process */
    sprintf(reUploadName, "%s%s", uploadPathName, keyword);
    
    s = ftp_connect(pmDaemonParam.FtpServerIp, pmDaemonParam.FtpUsername, pmDaemonParam.FtpPassword, ftpclient_printf);
    if (s < 0) {
        PM_DBG("[%s() ftp_connect(%s) failed]\n", __func__, pmDaemonParam.FtpServerIp);
        rename(uploadPathName, reUploadName);
        return -1;
    }

    len = read_file_into_buf(uploadPathName, &upload);

    ret = ftp_put_file(basename(uploadPathName), upload, len, s, ftpclient_printf);

    if (ret < 0) {
        PM_DBG("[%s() ftp_put_file(%s) failed. ret = %d]\n", __func__, uploadPathName, ret);
        rename(uploadPathName, reUploadName);
    }
    else {
        sprintf(rmCmd, "rm %s", uploadPathName);
        system(rmCmd);
    }

    closedir(dir);

    if (s > 0) {
        ftp_close(s, ftpclient_printf);
    }

    free(upload);

    return 0;

}

int reupload_csv_to_target() {
    int s = 0, len, ret;
    DIR *dir;
    struct dirent *ptr;
    char pathName[128], uploadFileName[128];
    char rmCmd[128];
    struct stat buf;
    char *upload, *pch;

    dir = opendir(pmDaemonParam.CsvUploadZone);
    if (dir == NULL) {
        PM_DBG("[%s() opendir(%s) failed]\n", __func__, pmDaemonParam.CsvUploadZone);
        return -1;
    }

    while ((ptr = readdir(dir)) != NULL) {
        memset(pathName, 0, sizeof(pathName));
        sprintf(pathName,"%s/%s", pmDaemonParam.CsvUploadZone, ptr->d_name);
        stat(pathName, &buf);

        if(S_ISREG(buf.st_mode) && (strstr(ptr->d_name, keyword) != NULL)) {

            /* Avoid reopening the ftp connection */
            if (s == 0) {
                s = ftp_connect(pmDaemonParam.FtpServerIp, pmDaemonParam.FtpUsername, pmDaemonParam.FtpPassword, ftpclient_printf);
                if (s < 0) {
                    PM_DBG("[%s() ftp_connect(%s) failed]\n", __func__, pmDaemonParam.FtpServerIp);
                    return -1;
                }
            }
            
            len = read_file_into_buf(pathName, &upload);

            /* Rename the uploaded file (remove keyword) */
            sprintf(uploadFileName, "%s", basename(pathName));
            pch = strstr (uploadFileName, keyword);
            *pch = '\0';
            
            ret = ftp_put_file(uploadFileName, upload, len, s, ftpclient_printf);

            free(upload);

            if (ret < 0) {
                PM_DBG("[%s() ftp_put_file(%s) failed. ret = %d]\n", __func__, pathName, ret);
            }
            else {
                sprintf(rmCmd, "rm %s", pathName);
                system(rmCmd);
            }
        }
    }

    closedir(dir);

    if (s > 0) {
        ftp_close(s, ftpclient_printf);
    }

    return 0;
}

void compress_folders_inCsv() {
    DIR *dir;
    struct dirent *ptr;
    struct stat buf;
    char pathName[128];
    char compressCmd[256];
    char rmDirCmd[128];
    int count = 0;

    dir = opendir(pmDaemonParam.CsvUploadZone);
    if (dir == NULL) {
        PM_DBG("[%s() opendir(%s) failed]\n", __func__, pmDaemonParam.CsvUploadZone);
        return ;
    }

    while ((ptr = readdir(dir)) != NULL) {
        memset(pathName, 0, sizeof(pathName));
        sprintf(pathName,"%s/%s", pmDaemonParam.CsvUploadZone, ptr->d_name);
        stat(pathName, &buf);

        /* Find directory and skip backing up directory */
        if(S_ISDIR(buf.st_mode) && strcmp(ptr->d_name, ".") && strcmp(ptr->d_name, "..")) {

            sprintf(compressCmd, "tar pcf %s.tar -C %s %s; gzip %s.tar", 
            pathName, pmDaemonParam.CsvUploadZone, ptr->d_name, pathName);
            system(compressCmd);
            sprintf(rmDirCmd, "rm -rf %s", pathName);
            system(rmDirCmd);
        }
    }

    closedir(dir);

    return ;
}

static int read_file_into_buf(char *path, char **buf) {
    size_t newLen;
    FILE *fp = fopen(path, "r");
    if (fp != NULL) {
        /* Go to the end of the file. */
        if (fseek(fp, 0L, SEEK_END) == 0) {
            /* Get the size of the file. */
            long bufsize = ftell(fp);
            if (bufsize == -1) {  /* Error */
                return -1;
            }

            /* Allocate our buffer to that size. */
            *buf = malloc(sizeof(char) * (bufsize + 1));

            /* Go back to the start of the file. */
            if (fseek(fp, 0L, SEEK_SET) != 0) { /* Error */
                return -1;
            }

            /* Read the entire file into memory. */
            newLen = fread(*buf, sizeof(char), bufsize, fp);
            if (newLen == 0) {
                fputs("Error reading file", stderr);
            }
            else {
                //(*buf)[++newLen] = '\0'; /* Just to be safe. */
            }
        }
        fclose(fp);
    }

    return newLen;
}

