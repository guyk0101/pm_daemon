#ifndef _PM_DAEMON_
#define _PM_DAEMON_

/* PM Daemon release version number */
#define PM_DAEMON_VERSION "1.0.0"

/* PM Daemon config file path */
#define DAEMON_CONF_FILE                        "pm_daemon.conf"

/* PM Daemon parameter prefix MACRO */
#define PM_DAEMON_PARSE_FILE_TIMER              "PeriodOfTime"
#define PM_DAEMON_DEBUG_FLAG                    "DebugFlag"
#define PM_DAEMON_UPLOAD_TARGET                 "UploadTarget"
#define PM_DAEMON_RECIEVE_DATA_ZONE             "ReceiveZone"
#define PM_DAEMON_BACKUP_DATA_ZONE              "BackupZone"
#define PM_DAEMON_CSV_UPLOAD_ZONE               "CsvUploadZone"
#define PM_DAEMON_DEBUG_FILE                    "DebugFile"
#define PM_DAEMON_KEEP_DAY_BACKUP_DATA          "KeepDayBackup"
#define PM_DAEMON_FTP_SERVER_IP                 "FtpServerIp"
#define PM_DAEMON_FTP_USERNAME                  "FtpUsername"
#define PM_DAEMON_FTP_PASSWORD                  "FtpPassword"
#define PM_DAEMON_FTP_ERROR_LOG_NUMBER          "FtpErrorLogNumber"

/* PM Daemon default parameter MACRO */
#define PM_DAEMON_DEFAULT_PARSE_FILE_TIMER      (15)
#define PM_DAEMON_DEFAULT_DEBUG_FLAG            (0)
#define PM_DAEMON_DEFAULT_RECIEVE_DATA_ZONE     "/export/home/pmDaemon/rec"
#define PM_DAEMON_DEFAULT_BACKUP_DATA_ZONE      "/export/home/pmDaemon/bak"
#define PM_DAEMON_DEFAULT_CSV_UPLOAD_ZONE       "/export/home/pmDaemon/csv"
#define PM_DAEMON_DEFAULT_DAEMON_DEBUG_FILE     "/export/home/pmDaemon/debug_pm_daemon.txt"
#define PM_DAEMON_DEFAULT_KEEP_DAY_BACKUP_DATA  (1)
#define PM_DAEMON_DEFAULT_FTP_SERVER_IP         "localhost"
#define PM_DAEMON_DEFAULT_FTP_USERNAME          ""
#define PM_DAEMON_DEFAULT_FTP_PASSWORD          ""
#define PM_DAEMON_DEFAULT_FTP_ERROR_LOG_NUMBER  (1000)

/* Structure for PM Daemon parameters */
#define PM_DAEMON_MAX_PATH_LENGTH               128
typedef struct pm_daemon_param_s{
    int     PeriodOfTime;
    int     DebugFlag;
    char    ReceiveZone[PM_DAEMON_MAX_PATH_LENGTH];
    char    BackupZone[PM_DAEMON_MAX_PATH_LENGTH];
    char    CsvUploadZone[PM_DAEMON_MAX_PATH_LENGTH];
    char    DebugFile[PM_DAEMON_MAX_PATH_LENGTH];
    int     KeepDayBackup;
    char    FtpServerIp[PM_DAEMON_MAX_PATH_LENGTH];
    char    FtpUsername[PM_DAEMON_MAX_PATH_LENGTH];
    char    FtpPassword[PM_DAEMON_MAX_PATH_LENGTH];
    int     FtpErrorLogNumber;
}pm_daemon_param_t;

extern pm_daemon_param_t pmDaemonParam;

/* Debug macro and variable definition */
#define PM_DBG(x,...) if(pmDaemonParam.DebugFlag){\
    FILE * fp_debug;\
    if(access(pmDaemonParam.DebugFile, R_OK)){\
        fprintf(stderr,x,##__VA_ARGS__);}\
    else if( (fp_debug = fopen(pmDaemonParam.DebugFile,"a+")) == NULL ){\
        fprintf(stderr,x,##__VA_ARGS__);}\
    else{\
        fprintf(fp_debug,x,##__VA_ARGS__);\
        fclose(fp_debug);}\
}

#endif /* End of _PM_DAEMON_ */
