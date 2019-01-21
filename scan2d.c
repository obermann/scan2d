#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>
#include "usb.h"

#define RUNNING_DIR "/tmp"
#define LOCK_FILE   "/var/run/scan2d.pid"
#define MENU_FILE   "/opt/etc/scan2menu.bin"
#define SCRIPT_FILE "/opt/etc/scan2.sh"
#define TIMEOUT 200

#define VENDORID 0x04E8
#define PRODUCTID 0x3433
#define LP "/dev/usb/lp0"

char CMD_NOSCAN[8] =            {0x00,0x00,0x00,0x00,0x00,0x0A,0x9F,0x00};
char CMD_SEND_MENU[8] =         {0x1B,0x2A,0x00,0x08,0x01,0x02,0x01,0x00};
char CMD_SEND_MENU_ACK[8] =     {0x1B,0x2A,0x00,0x08,0x02,0x02,0x01,0x00};
char CMD_RECEIVE_CHOICE[8] =    {0x1B,0x2A,0x00,0x08,0x03,0x02,0x02,0x00};
char CMD_DELIVER_CHOICE[8] =    {0x1B,0x9A,0x00,0x08,0x04,0x01,0x00,0x00};
char CMD_NOPRINT[8] =           {0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
char CMD_PRINT_FULL[8] =        {0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x01};
char CMD_PRINT_ACTIVE[8] =      {0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x02};

char buf[0xFF];
char buff[0xFF];

int fp;
int lfp;
char *menu;
off_t menusize;
usb_dev_handle *h;
int status = EXIT_SUCCESS;
int quit = 0;

/*
To test signal: kill -HUP `cat /var/run/scan2d.pid`
To terminate:   kill `cat /var/run/scan2d.pid`
*/
void signalHandler(int sig)
{
    switch(sig) {
    case SIGHUP:
        // Many daemons will reload their configuration files and reopen
        // their logfiles instead of exiting when receiving this signal.
        syslog(LOG_DEBUG, "scan2d daemon cought SIGHUP.");
        break;
    case SIGTERM:
        syslog(LOG_INFO, "scan2d daemon is terminating.");
        quit = 1;
        break;
    }
}

int daemonize()
{
    int i;
    char str[10];
    if (getppid() == 1) return EXIT_SUCCESS; /* already a daemon */
    i = fork();
    if (i < 0) {
        syslog(LOG_ERR, "Unable to fork daemon, code=%d (%s)", errno, strerror(errno));
        return EXIT_FAILURE;
    }
    if (i > 0) exit(0); /* parent exits */
    /* child (daemon) continues */
    i = setsid(); /* obtain a new process group */
    if (i < 0) {
        syslog(LOG_ERR, "Unable to create a new session, code %d (%s)", errno, strerror(errno));
        return EXIT_FAILURE;
    }
    /*This one is too egzotic and breaks syslog: makes it skip some or all messages.*/
    //for (i = getdtablesize(); i >= 0; --i) close(i); /* close all descriptors */
    i = open("/dev/null", O_RDWR); dup(i); dup(i); /* handle standart I/O */
    umask(027); /* set newly created file permissions */
    //umask(0);
    chdir(RUNNING_DIR); /* change running directory */
    lfp = open(LOCK_FILE, O_RDWR | O_CREAT, 0640);
    if (lfp < 0) {
        syslog(LOG_ERR, "Unable to create lock file %s, code=%d (%s)", LOCK_FILE, errno, strerror(errno));
        return EXIT_FAILURE;
    }
    if (lockf(lfp, F_TLOCK, 0) < 0) {
        syslog(LOG_ERR, "Unable to lock file %s, code=%d (%s)", LOCK_FILE, errno, strerror(errno));
        return EXIT_FAILURE;
    }
    /* first instance continues */
    sprintf(str, "%d\n", getpid());
    write(lfp, str, strlen(str)); /* record pid to lockfile */
    signal(SIGCHLD, SIG_IGN); /* ignore child */
    signal(SIGTSTP, SIG_IGN); /* ignore tty signals */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGHUP, signalHandler); /* catch hangup signal */
    signal(SIGTERM, signalHandler); /* catch kill signal */
    return EXIT_SUCCESS;
}

int readMenu() {
    fp = open(MENU_FILE, O_RDONLY);
    if (fp < 0) {
        syslog(LOG_ERR, "Unable to read file %s, code=%d (%s)", MENU_FILE, errno, strerror(errno));
        return EXIT_FAILURE;
    }
    /* Get the size of the file. */
    menusize = lseek(fp, 0, SEEK_END);
    if (menusize < 0) {
        syslog(LOG_ERR, "File %s seek end error, code=%d (%s)", MENU_FILE, errno, strerror(errno));
        close(fp);
        return EXIT_FAILURE;
    }
    /* Go back to the start of the file. */
    if (lseek(fp, 0, SEEK_SET) < 0) {
        syslog(LOG_ERR, "File %s seek start error, code=%d (%s)", MENU_FILE, errno, strerror(errno));
        close(fp);
        return EXIT_FAILURE;
    }
    /* Allocate our buffer to that size. */
    menu = malloc(menusize);
    if (!menu && menusize > 0) {
        syslog(LOG_ERR, "Error allocating memory for a file %s (size %ld), code=%d (%s)", MENU_FILE, menusize, errno, strerror(errno));
        close(fp);
        return EXIT_FAILURE;
    }
    /* Read the entire file into memory. */
    if (read(fp, menu, menusize) != menusize) {
        syslog(LOG_ERR, "Error reading file %s, code=%d (%s)", MENU_FILE, errno, strerror(errno));
        free(menu);
        close(fp);
        return EXIT_FAILURE;
    }
    close(fp);
    return EXIT_SUCCESS;
}

void openDevice()
{
    struct usb_bus *bus;
    struct usb_device *dev;
    usb_init();
    usb_find_busses();
    usb_find_devices();
    // Searching for USB device
    for (bus = usb_get_busses(); bus; bus = bus->next) {
        for (dev = bus->devices; dev; dev = dev->next) {
            if (dev->descriptor.idVendor != VENDORID) continue;
            if (dev->descriptor.idProduct != PRODUCTID) continue;
            h = usb_open(dev);
            if (!h) {
                syslog(LOG_ERR, "Found device but unable to open.");
                continue;
            }
        }
    }
}

void worker() {
    int i;
    // For print screen
    i = usb_control_msg(h, 0xC1, 0x23, 0x0100, 0x0100, buf, 0x0008, TIMEOUT);
    if (i != 8) {
        syslog(LOG_ERR, "Error receiving print-screen usb_control_msg, code=%d", i);
        status = EXIT_FAILURE;
    }
    else {
        if ((memcmp(buf, CMD_PRINT_FULL, i) == 0) || (memcmp(buf, CMD_PRINT_ACTIVE, i) == 0)) {
            for (i = 0; i < 8 ; i++) {
                sprintf(&buff[2 * i], "%02X", buf[i]);
            }
            sprintf(buf, "%s %s", SCRIPT_FILE, buff);
            syslog(LOG_DEBUG, "Print-screen: %s", buf);
            system(buf);
            return; // Too soon for a new event check
        }
    }
    // For scan to
    i = usb_control_msg(h, 0xC1, 0x0C, 0x0087, 0x0100, buf, 0x0008, TIMEOUT);
    if (i != 8) {
        syslog(LOG_ERR, "Error receiving scan-to usb_control_msg, code=%d", i);
        status = EXIT_FAILURE;
    }
    else {
        if (memcmp(buf, CMD_SEND_MENU, i) == 0) {
            fp = open(LP, O_WRONLY);
            if (fp < 0) {
                syslog(LOG_ERR, "Unable to open file %s, code=%d (%s)", LP, errno, strerror(errno));
                //status = EXIT_FAILURE;
            }
            else {
                if (write(fp, menu, menusize) != menusize) {
                    syslog(LOG_ERR, "Error writing menu to file %s, code=%d (%s)", LP, errno, strerror(errno));
                }
                close(fp);
            }
        }
        else if (memcmp(buf, CMD_RECEIVE_CHOICE, i) == 0) {
            fp = open(LP, O_RDWR);
            if (fp < 0) {
                syslog(LOG_ERR, "Unable to open file %s, code=%d (%s)", LP, errno, strerror(errno));
                //status = EXIT_FAILURE;
            }
            else {
                if (write(fp, CMD_DELIVER_CHOICE, sizeof(CMD_DELIVER_CHOICE)) != sizeof(CMD_DELIVER_CHOICE)) {
                    syslog(LOG_ERR, "Error writing CMD_DELIVER_CHOICE to file %s, code=%d (%s)", LP, errno, strerror(errno));
                }
                else {
                    i = read(fp, buf, 14);
                    close(fp);
                    if (i != 14) {
                        syslog(LOG_ERR, "Error reading choice from file %s, code=%d (%s)", LP, errno, strerror(errno));
                    }
                    else {
                        for (i = 0; i < 14 ; i++) {
                            sprintf(&buff[2 * i], "%02X", buf[i]);
                        }
                        sprintf(buf, "%s %s", SCRIPT_FILE, buff);
                        syslog(LOG_DEBUG, "Scan-to: %s", buf);
                        system(buf);
                    }
                }
            }
        }
    }
}

int main()
{
    /*
    Set our Logging Mask and open the Log
    setlogmask(LOG_UPTO(LOG_NOTICE));
    http://www.gnu.org/software/libc/manual/html_node/openlog.html
    LOG_PERROR
    */
    openlog("scan2d", LOG_NDELAY | LOG_PID, LOG_USER);
    syslog(LOG_INFO, "scan2d daemon is starting.");
    if (daemonize() == EXIT_SUCCESS && readMenu() == EXIT_SUCCESS)
    //if (readMenu() == EXIT_SUCCESS)
        openDevice();
    if (h) {
        do {
            worker();
            sleep(1);
            if (status == EXIT_FAILURE) {
                usb_close(h);
                syslog(LOG_WARNING, "Trying to reopen USB divice.");
                openDevice();
                if (!h) break;
                    else status = EXIT_SUCCESS;
            }
        } while (!quit);
    }
    else status = EXIT_FAILURE;
    usb_close(h);
    free(menu);
    close(lfp);
    remove(LOCK_FILE);
    closelog();
    return status;
}

/* EOF */
