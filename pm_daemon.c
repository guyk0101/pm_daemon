#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <wait.h>
#include "pm_daemon.h"
#include "file_utility.h"

/* Variable for PM Daemon parameters */
pm_daemon_param_t pmDaemonParam;

/* POSIX timer callback function declaration */
static void timer_expire(int sig);

/* Get PM Daemon parameter function declaration */
static void get_pm_daemon_param(void);

/* Get local time string function declaration */
static char* get_localtime(time_t *timep, struct tm *p);

static void reaper(int sig);
static void quitHandler(int sig);
static void* signal_sigaction(int signo, void *func);

static void processFiles();

static void reupload_csv();

static int processCount = 0;

static csvFilePtr_t csvFilePtr;
static int fileNum_total = 0;
static time_t time_total = 0;

static int reupload_pid;

int main(int argc, char *argv[]) {

    int pid;
    char * parameter = NULL;
    struct itimerspec        start_tmr;
    struct sigevent          sig_ev;
    timer_t                  tid;
    struct sigaction         sched_sig_act;

    /* Fork to create daemon process */
    if( (pid=fork()) > 0){
        /* Detach parent process */
        exit(EXIT_SUCCESS);
    }
    else if(pid < 0){
        fprintf(stderr, "Fork PM daemon version %s failed!!\n", PM_DAEMON_VERSION);
        exit(EXIT_FAILURE);
    }

    /* Reset PM Daemon parameter */
    memset((void*)&pmDaemonParam, 0, sizeof(pmDaemonParam));

    /* Assign default PM Daemon parameters */
    pmDaemonParam.PeriodOfTime = PM_DAEMON_DEFAULT_PARSE_FILE_TIMER;
    pmDaemonParam.DebugFlag = PM_DAEMON_DEFAULT_DEBUG_FLAG;
    strcpy(pmDaemonParam.ReceiveZone, PM_DAEMON_DEFAULT_RECIEVE_DATA_ZONE);
    strcpy(pmDaemonParam.BackupZone, PM_DAEMON_DEFAULT_BACKUP_DATA_ZONE);
    strcpy(pmDaemonParam.CsvUploadZone, PM_DAEMON_DEFAULT_CSV_UPLOAD_ZONE);
    strcpy(pmDaemonParam.DebugFile, PM_DAEMON_DEFAULT_DAEMON_DEBUG_FILE);
    pmDaemonParam.KeepDayBackup = PM_DAEMON_DEFAULT_KEEP_DAY_BACKUP_DATA;
    strcpy(pmDaemonParam.FtpServerIp, PM_DAEMON_DEFAULT_FTP_SERVER_IP);
    pmDaemonParam.FtpErrorLogNumber = PM_DAEMON_DEFAULT_FTP_ERROR_LOG_NUMBER;

    /* Read timer parameter from DAEMON_CONF_FILE */
    if(parameter = get_setting(PM_DAEMON_PARSE_FILE_TIMER, DAEMON_CONF_FILE)){
        /* Get timer parameter, update it */
        pmDaemonParam.PeriodOfTime = atoi(parameter);
    }

    /* Set service routine POSIX timer */
    start_tmr.it_interval.tv_sec    = 60;
    start_tmr.it_interval.tv_nsec   = 0;
    start_tmr.it_value.tv_sec       = 60;
    start_tmr.it_value.tv_nsec      = 0;

    /* Update PM Daemon parameters from DAEMON_CONF_FILE */
    get_pm_daemon_param();

    signal_sigaction(SIGCHLD, reaper);
    signal_sigaction(SIGTERM, quitHandler);
    signal_sigaction(SIGINT, quitHandler);
    signal_sigaction(SIGKILL, quitHandler);

    PM_DBG("\nLaunch PM daemon version %s [Process ID %d]\n", PM_DAEMON_VERSION, getpid());
    PM_DBG("PM daemon service routine will output csv files per %d minutes\n", pmDaemonParam.PeriodOfTime);

    /* Create a POSIX timer for PM daemon service routine and Initialize signal handler and signal mask. */
    memset(&sig_ev, 0x00, sizeof(sig_ev));
    sig_ev.sigev_notify = SIGEV_SIGNAL;
    sig_ev.sigev_signo = SIGUSR2;
    timer_create(CLOCK_REALTIME, &sig_ev, &tid);

    memset(&sched_sig_act, 0x00, sizeof(sched_sig_act));
    sched_sig_act.sa_handler = timer_expire;
    sigemptyset(&sched_sig_act.sa_mask);
    sigaddset(&sched_sig_act.sa_mask, SIGUSR2);
    sched_sig_act.sa_flags = SA_RESTART;

    /* Set signal handler. */
    sigaction(SIGUSR2, &sched_sig_act, NULL);

    /* Start timer. */
    timer_settime(tid, 0, &start_tmr, NULL);

    /* Fork to create upload process */
    if( (reupload_pid=fork()) == 0){ // child process
        reupload_csv();
    }
    else if(reupload_pid < 0){
        PM_DBG("%d Fork upload process failed!!\n", __LINE__);
        exit(EXIT_FAILURE);
    }

    /* Keep running for POSIX timer callback */
    while(1) sleep(1);

    return 0;
}

static time_t fileTimeStamp;
static void processFiles(time_t *curTime, struct tm *p) {
    int i, fileNum;
    char filePath[RECIEVE_FILE_PATH_LENGTH];
    time_t time_s, time_t;
    int pid;

    time(&time_s);

    /* Parse/summarize PM file and convert it to Excel CSV format per period of time */
    if ((fileNum = getFilelist(pmDaemonParam.ReceiveZone)) <= 0) {
        /*
            There is no any file in receive zone,
            we check Backup data zone, some folders may need to be remove
        */
        if (processCount == 0) {
            checkAndDelete_backupDIRs(curTime);
            checkAndDelete_csvFiles(curTime);
        }
        return;

    }

    if (processCount == 0) {
        set_CSV_filenamePath(curTime);

        /* Keep file creating time */
        fileTimeStamp = *curTime;

        //make_backup_dir(p);
        make_backup_dir_inRec(p);
    }

    fopen_CSV_Files(&csvFilePtr);

    for (i = 0; i < fileNum; i++) {

        memset(filePath, 0, RECIEVE_FILE_PATH_LENGTH);
        sprintf(filePath, "%s/%s", pmDaemonParam.ReceiveZone, getFileName(i));

        parseRowData_OutputCSV(filePath, &csvFilePtr);

        //moveRowDataFiles_to_backupDir(filePath);
        moveRowDataFiles_to_backupDir_inRec(filePath);
    }

    arrayNodeFree();

    if (processCount == 0) {

        /* Check Backup data zone, some folders may need to be remove */
        checkAndDelete_backupDIRs(curTime);

        checkAndDelete_csvFiles(curTime);
    }

    time(&time_t);

    //PM_DBG("Process files: %d, Process time: %lds\n", fileNum, (time_t - time_s));
    fileNum_total +=  fileNum;
    time_total += (time_t - time_s);

    if (++processCount >= pmDaemonParam.PeriodOfTime) {

        //PM_DBG("Process files total: %d, Process time total: %lds\n", fileNum_total, time_total);
        processCount = 0;
        fileNum_total = 0;
        time_total = 0;

        compress_folders_inCsv();

        upload_csv_to_target(fileTimeStamp);

        pid = fork();
        
        if (pid == -1) {
            PM_DBG("%d Fork fail!!\n", __LINE__);
        }
        else if (pid == 0) {
            compress_backupDir_inRec();

            _exit(EXIT_SUCCESS);
        }
    }

    return;
}

/*
 *  POSIX Timer Expired function
 */
static void timer_expire(int sig) {
    int i, fileNum;
    time_t curTime;
    struct tm p;

    if (sig) {
        ;
    }

    //PM_DBG("\nRun PM daemon service routine at %s via Process ID %d\n", get_localtime(&curTime, &p), getpid());

    /* Update PM Daemon parameters from DAEMON_CONF_FILE */
    get_pm_daemon_param();

    get_localtime(&curTime, &p);

    processFiles(&curTime, &p);

    return;
}

/*
 * Get PM Daemon parameter function
 */
static void get_pm_daemon_param(void) {
    char *parameter = NULL;

    /* Get Period Of Time Flag */
    if (parameter = get_setting(PM_DAEMON_PARSE_FILE_TIMER, DAEMON_CONF_FILE)) {
        if (atoi(parameter) != pmDaemonParam.PeriodOfTime) {
            PM_DBG("PM daemon ftp upload peroid changed to %d from %d\n", atoi(parameter), pmDaemonParam.PeriodOfTime);
            pmDaemonParam.PeriodOfTime = atoi(parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        pmDaemonParam.PeriodOfTime = PM_DAEMON_DEFAULT_PARSE_FILE_TIMER;
    }

    /* Get Debug file */
    if (parameter = get_setting(PM_DAEMON_DEBUG_FILE, DAEMON_CONF_FILE)) {
        if (strcmp(parameter, pmDaemonParam.DebugFile)) {
            PM_DBG("PM daemon debug file changed to %s from %s\n", parameter, pmDaemonParam.DebugFile);
            memset(pmDaemonParam.DebugFile, 0, sizeof(pmDaemonParam.DebugFile));
            strcpy(pmDaemonParam.DebugFile, parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        memset(pmDaemonParam.DebugFile, 0, sizeof(pmDaemonParam.DebugFile));
        strcpy(pmDaemonParam.DebugFile, PM_DAEMON_DEFAULT_DAEMON_DEBUG_FILE);
    }

    /* Get Debug Flag */
    if (parameter = get_setting(PM_DAEMON_DEBUG_FLAG, DAEMON_CONF_FILE)) {
        if (atoi(parameter) != pmDaemonParam.DebugFlag) {
            PM_DBG("PM daemon debug flag changed to %d from %d\n", atoi(parameter), pmDaemonParam.DebugFlag);
            pmDaemonParam.DebugFlag = atoi(parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        pmDaemonParam.DebugFlag = PM_DAEMON_DEFAULT_DEBUG_FLAG;
    }

    /* Get Receive Zone */
    if (parameter = get_setting(PM_DAEMON_RECIEVE_DATA_ZONE, DAEMON_CONF_FILE)) {
        if (strcmp(parameter, pmDaemonParam.ReceiveZone)) {
            PM_DBG("PM daemon receive zone changed to %s from %s\n", parameter, pmDaemonParam.ReceiveZone);
            memset(pmDaemonParam.ReceiveZone, 0, sizeof(pmDaemonParam.ReceiveZone));
            strcpy(pmDaemonParam.ReceiveZone, parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        memset(pmDaemonParam.ReceiveZone, 0, sizeof(pmDaemonParam.ReceiveZone));
        strcpy(pmDaemonParam.ReceiveZone, PM_DAEMON_DEFAULT_RECIEVE_DATA_ZONE);
    }

    /* Get Backup Zone */
    if (parameter = get_setting(PM_DAEMON_BACKUP_DATA_ZONE, DAEMON_CONF_FILE)) {
        if (strcmp(parameter, pmDaemonParam.BackupZone)) {
            PM_DBG("PM daemon backup zone changed to %s from %s\n", parameter, pmDaemonParam.BackupZone);
            memset(pmDaemonParam.BackupZone, 0, sizeof(pmDaemonParam.BackupZone));
            strcpy(pmDaemonParam.BackupZone, parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        memset(pmDaemonParam.BackupZone, 0, sizeof(pmDaemonParam.BackupZone));
        strcpy(pmDaemonParam.BackupZone, PM_DAEMON_DEFAULT_BACKUP_DATA_ZONE);
    }

    /* Get CSV upload Zone */
    if (parameter = get_setting(PM_DAEMON_CSV_UPLOAD_ZONE, DAEMON_CONF_FILE)) {
        if (strcmp(parameter, pmDaemonParam.CsvUploadZone)) {
            PM_DBG("PM daemon CSV upload zone changed to %s from %s\n", parameter, pmDaemonParam.CsvUploadZone);
            memset(pmDaemonParam.CsvUploadZone, 0, sizeof(pmDaemonParam.CsvUploadZone));
            strcpy(pmDaemonParam.CsvUploadZone, parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        memset(pmDaemonParam.CsvUploadZone, 0, sizeof(pmDaemonParam.CsvUploadZone));
        strcpy(pmDaemonParam.CsvUploadZone, PM_DAEMON_DEFAULT_CSV_UPLOAD_ZONE);
    }

    /* Get Keep days for Backup data zone */
    if (parameter = get_setting(PM_DAEMON_KEEP_DAY_BACKUP_DATA, DAEMON_CONF_FILE)) {
        if (atoi(parameter) != pmDaemonParam.KeepDayBackup) {
            PM_DBG("PM daemon keep day changed to %d from %d\n", atoi(parameter), pmDaemonParam.KeepDayBackup);
            pmDaemonParam.KeepDayBackup = atoi(parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        pmDaemonParam.KeepDayBackup = PM_DAEMON_DEFAULT_KEEP_DAY_BACKUP_DATA;
    }

    /* Get FTP Server IP address */
    if (parameter = get_setting(PM_DAEMON_FTP_SERVER_IP, DAEMON_CONF_FILE)) {
        if (strcmp(parameter, pmDaemonParam.FtpServerIp)) {
            PM_DBG("PM daemon FTP server IP changed to %s from %s\n", parameter, pmDaemonParam.FtpServerIp);
            memset(pmDaemonParam.FtpServerIp, 0, sizeof(pmDaemonParam.FtpServerIp));
            strcpy(pmDaemonParam.FtpServerIp, parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        memset(pmDaemonParam.FtpServerIp, 0, sizeof(pmDaemonParam.FtpServerIp));
        strcpy(pmDaemonParam.FtpServerIp, PM_DAEMON_DEFAULT_FTP_SERVER_IP);
    }

    /* Get FTP User Name */
    if (parameter = get_setting(PM_DAEMON_FTP_USERNAME, DAEMON_CONF_FILE)) {
        if (strcmp(parameter, pmDaemonParam.FtpUsername)) {
            PM_DBG("PM daemon FTP username changed to %s from %s\n", parameter, pmDaemonParam.FtpUsername);
            memset(pmDaemonParam.FtpUsername, 0, sizeof(pmDaemonParam.FtpUsername));
            strcpy(pmDaemonParam.FtpUsername, parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        memset(pmDaemonParam.FtpUsername, 0, sizeof(pmDaemonParam.FtpUsername));
    }

    /* Get FTP Password */
    if (parameter = get_setting(PM_DAEMON_FTP_PASSWORD, DAEMON_CONF_FILE)) {
        if (strcmp(parameter, pmDaemonParam.FtpPassword)) {
            PM_DBG("PM daemon FTP password changed to %s from %s\n", parameter, pmDaemonParam.FtpPassword);
            memset(pmDaemonParam.FtpPassword, 0, sizeof(pmDaemonParam.FtpPassword));
            strcpy(pmDaemonParam.FtpPassword, parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        memset(pmDaemonParam.FtpPassword, 0, sizeof(pmDaemonParam.FtpPassword));
    }

    /* Get max FTP error log number */
    if (parameter = get_setting(PM_DAEMON_FTP_ERROR_LOG_NUMBER, DAEMON_CONF_FILE)) {
        if (atoi(parameter) != pmDaemonParam.FtpErrorLogNumber) {
            PM_DBG("PM daemon FTP error log number changed to %d from %d\n", atoi(parameter), pmDaemonParam.FtpErrorLogNumber);
            pmDaemonParam.FtpErrorLogNumber = atoi(parameter);
        }
    }
    else {
        /* Cannot get parameter, apply default value */
        pmDaemonParam.FtpErrorLogNumber = PM_DAEMON_DEFAULT_FTP_ERROR_LOG_NUMBER;
    }

    return;
}

/*
 * Get local time string function
 */
static char* get_localtime(time_t *timep, struct tm *p) {
    static char buf[32];
    char *wday[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    time(timep);
    memcpy(p, localtime(timep), sizeof(struct tm));

    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%d/%02d/%02d %s %02d:%02d:%02d",
            (1900 + p->tm_year),
            (1 + p->tm_mon),
            (p->tm_mday),
            wday[p->tm_wday],
            p->tm_hour,
            p->tm_min,
            p->tm_sec
           );

    return buf;
}

/*------------------------------------------------------------------------
 * reaper - clean up zombie children
 *------------------------------------------------------------------------
 */
static void reaper(int sig) {
    int	status, pid;

    while ((pid = wait3(&status, WNOHANG, (struct rusage *) 0)) > 0) {
        //printf("%s() pid = %d status = %x\n", __func__, pid, status);
    }
}

static void quitHandler(int sig) {

    /* Parent will do*/
    if (reupload_pid > 0) {
        kill(reupload_pid, SIGTERM);
    }
    /* child will do */
    else {
        ;
    }
    exit(EXIT_SUCCESS);
}
 
static void* signal_sigaction(int signo, void *func) {
    struct sigaction act, oact;
    act.sa_handler = func;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (signo == SIGALRM) {
#ifdef SA_INTERRUPT
        act.sa_flags |= SA_INTERRUPT;
#endif
    }
    else {
#ifdef SA_RESTART
        act.sa_flags |= SA_RESTART;
#endif
    }
    if (sigaction(signo, &act, &oact) < 0) {
        return (SIG_ERR);
    }
    return (oact.sa_handler);
}

static void reupload_csv()
{
    while (1) {
        reupload_csv_to_target();
        sleep(pmDaemonParam.PeriodOfTime * 60);
    }
}

