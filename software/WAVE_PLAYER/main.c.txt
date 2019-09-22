/*=========================================================================*/
/*  Includes                                                               */
/*=========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <system.h>
#include <sys/alt_alarm.h>
#include <io.h>

#include "fatfs.h"
#include "diskio.h"

#include "ff.h"
#include "monitor.h"
#include "uart.h"

#include "alt_types.h"

#include <altera_up_avalon_audio.h>
#include <altera_up_avalon_audio_and_video_config.h>

/*=========================================================================*/
/*  DEFINE: All Structures and Common Constants                            */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Macros                                                         */
/*=========================================================================*/

#define PSTR(_a)  _a

/*=========================================================================*/
/*  DEFINE: Prototypes                                                     */
/*=========================================================================*/

/*=========================================================================*/
/*  DEFINE: Definition of all local Data                                   */
/*=========================================================================*/
static alt_alarm alarm;
static unsigned long Systick = 0;
static volatile unsigned short Timer;   /* 1000Hz increment timer */

/*=========================================================================*/
/*  DEFINE: Definition of all local Procedures                             */
/*=========================================================================*/

/***************************************************************************/
/*  TimerFunction                                                          */
/*                                                                         */
/*  This timer function will provide a 10ms timer and                      */
/*  call ffs_DiskIOTimerproc.                                              */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
static alt_u32 TimerFunction (void *context)
{
   static unsigned short wTimer10ms = 0;

   (void)context;

   Systick++;
   wTimer10ms++;
   Timer++; /* Performance counter for this module */

   if (wTimer10ms == 10)
   {
      wTimer10ms = 0;
      ffs_DiskIOTimerproc();  /* Drive timer procedure of low level disk I/O module */
   }

   return(1);
} /* TimerFunction */

/***************************************************************************/
/*  IoInit                                                                 */
/*                                                                         */
/*  Init the hardware like GPIO, UART, and more...                         */
/*                                                                         */
/*  In    : none                                                           */
/*  Out   : none                                                           */
/*  Return: none                                                           */
/***************************************************************************/
static void IoInit(void)
{
   uart0_init(115200);

   /* Init diskio interface */
   ffs_DiskIOInit();

   //SetHighSpeed();

   /* Init timer system */
   alt_alarm_start(&alarm, 1, &TimerFunction, NULL);

} /* IoInit */

/*=========================================================================*/
/*  DEFINE: All code exported                                              */
/*=========================================================================*/

uint32_t acc_size;                 /* Work register for fs command */
uint16_t acc_files, acc_dirs;
FILINFO Finfo;
#if _USE_LFN
char Lfname[512];
#endif

char Line[256];                 /* Console input buffer */

FATFS Fatfs[_VOLUMES];          /* File system object for each logical drive */
FIL File1, File2;               /* File objects */
DIR Dir;                        /* Directory object */
uint8_t Buff[8192] __attribute__ ((aligned(4)));  /* Working buffer */




static
FRESULT scan_files(char *path)
{
    DIR dirs;
    FRESULT res;
    uint8_t i;
    char *fn;


    if ((res = f_opendir(&dirs, path)) == FR_OK) {
        i = (uint8_t)strlen(path);
        while (((res = f_readdir(&dirs, &Finfo)) == FR_OK) && Finfo.fname[0]) {
            if (_FS_RPATH && Finfo.fname[0] == '.')
                continue;
#if _USE_LFN
            fn = *Finfo.lfname ? Finfo.lfname : Finfo.fname;
#else
            fn = Finfo.fname;
#endif
            if (Finfo.fattrib & AM_DIR) {
                acc_dirs++;
                *(path + i) = '/';
                strcpy(path + i + 1, fn);
                res = scan_files(path);
                *(path + i) = '\0';
                if (res != FR_OK)
                    break;
            } else {
                //      xprintf("%s/%s\n", path, fn);
                acc_files++;
                acc_size += Finfo.fsize;
            }
        }
    }

    return res;
}


//                put_rc(f_mount((uint8_t) p1, &Fatfs[p1]));

static
void put_rc(FRESULT rc)
{
    const char *str =
        "OK\0" "DISK_ERR\0" "INT_ERR\0" "NOT_READY\0" "NO_FILE\0" "NO_PATH\0"
        "INVALID_NAME\0" "DENIED\0" "EXIST\0" "INVALID_OBJECT\0" "WRITE_PROTECTED\0"
        "INVALID_DRIVE\0" "NOT_ENABLED\0" "NO_FILE_SYSTEM\0" "MKFS_ABORTED\0" "TIMEOUT\0"
        "LOCKED\0" "NOT_ENOUGH_CORE\0" "TOO_MANY_OPEN_FILES\0";
    FRESULT i;

    for (i = 0; i != rc && *str; i++) {
        while (*str++);
    }
    xprintf("rc=%u FR_%s\n", (uint32_t) rc, str);
}

static
void display_help(void)
{
    xputs("dd <phy_drv#> [<sector>] - Dump sector\n"
          "di <phy_drv#> - Initialize disk\n"
          "ds <phy_drv#> - Show disk status\n"
          "bd <addr> - Dump R/W buffer\n"
          "be <addr> [<data>] ... - Edit R/W buffer\n"
          "br <phy_drv#> <sector> [<n>] - Read disk into R/W buffer\n"
          "bf <n> - Fill working buffer\n"
          "fc - Close a file\n"
          "fd <len> - Read and dump file from current fp\n"
          "fe - Seek file pointer\n"
          "fi <log drv#> - Force initialize the logical drive\n"
          "fl [<path>] - Directory listing\n"
          "fo <mode> <file> - Open a file\n"
    	  "fp -  (to be added by you) \n"
          "fr <len> - Read file\n"
          "fs [<path>] - Show logical drive status\n"
          "fz [<len>] - Get/Set transfer unit for fr/fw commands\n"
          "h view help (this)\n");
}

/***************************************************************************/
/*  main                                                                   */
/***************************************************************************/
int main(void)
{
	int fifospace;
    char *ptr, *ptr2;
    long p1, p2, p3;
    uint8_t res, b1, drv = 0;
    uint16_t w1;
    uint32_t s1, s2, cnt, blen = sizeof(Buff);
    static const uint8_t ft[] = { 0, 12, 16, 32 };
    uint32_t ofs = 0, sect = 0, blk[2];
    FATFS *fs;                  /* Pointer to file system object */

    alt_up_audio_dev * audio_dev;
    /* used for audio record/playback */
    unsigned int l_buf;
    unsigned int r_buf;
    // open the Audio port
    audio_dev = alt_up_audio_open_dev ("/dev/Audio");
    if ( audio_dev == NULL)
    alt_printf ("Error: could not open audio device \n");
    else
    alt_printf ("Opened audio device \n");

    IoInit();

    IOWR(SEVEN_SEG_PIO_BASE,1,0x0007);

    xputs(PSTR("FatFs module test monitor\n"));
    xputs(_USE_LFN ? "LFN Enabled" : "LFN Disabled");
    xprintf(", Code page: %u\n", _CODE_PAGE);

    display_help();


#if _USE_LFN
    Finfo.lfname = Lfname;
    Finfo.lfsize = sizeof(Lfname);
#endif

    for (;;) {

        get_line(Line, sizeof(Line));

        ptr = Line;
        switch (*ptr++) {

        case 'm':              /* System memroy/register controls */
            switch (*ptr++) {
            case 'd':          /* md <address> [<count>] - Dump memory */
                if (!xatoi(&ptr, &p1))
                    break;
                if (!xatoi(&ptr, &p2))
                    p2 = 128;
                for (ptr = (char *) p1; p2 >= 16; ptr += 16, p2 -= 16)
                    put_dump((uint8_t *) ptr, (uint32_t) ptr, 16);
                if (p2)
                    put_dump((uint8_t *) ptr, (uint32_t) ptr, p2);
                break;
            }
            break;

        case 'd':              /* Disk I/O layer controls */
            switch (*ptr++)
            {
            case 'd':          /* dd [<drv> [<lba>]] - Dump secrtor */
                if (!xatoi(&ptr, &p1))
                {
                    p1 = drv;
                }
                else
                {
                    if (!xatoi(&ptr, &p2))
                        p2 = sect;
                }
                drv = (uint8_t) p1;
                sect = p2 + 1;
                res = disk_read((uint8_t) p1, Buff, p2, 1);
                if (res)
                {
                    xprintf("rc=%d\n", (uint16_t) res);
                    break;
                }
                xprintf("D:%lu S:%lu\n", p1, p2);
                for (ptr = (char *) Buff, ofs = 0; ofs < 0x200; ptr += 16, ofs += 16)
                    put_dump((uint8_t *) ptr, ofs, 16);
                break;

            case 'i':          /* di <drv> - Initialize disk */
                if (!xatoi(&ptr, &p1))
                    break;
                xprintf("rc=%d\n", (uint16_t) disk_initialize((uint8_t) p1));
                break;

            case 's':          /* ds <drv> - Show disk status */
                if (!xatoi(&ptr, &p1))
                    break;
                if (disk_ioctl((uint8_t) p1, GET_SECTOR_COUNT, &p2) == RES_OK) {
                    xprintf("Drive size: %lu sectors\n", p2);
                }
                if (disk_ioctl((uint8_t) p1, GET_SECTOR_SIZE, &w1) == RES_OK) {
                    xprintf("Sector size: %u bytes\n", w1);
                }
                if (disk_ioctl((uint8_t) p1, GET_BLOCK_SIZE, &p2) == RES_OK) {
                    xprintf("Block size: %lu sectors\n", p2);
                }
                if (disk_ioctl((uint8_t) p1, MMC_GET_TYPE, &b1) == RES_OK) {
                    xprintf("MMC/SDC type: %u\n", b1);
                }
                if (disk_ioctl((uint8_t) p1, MMC_GET_CSD, Buff) == RES_OK) {
                    xputs("CSD:\n");
                    put_dump(Buff, 0, 16);
                }
                if (disk_ioctl((uint8_t) p1, MMC_GET_CID, Buff) == RES_OK) {
                    xputs("CID:\n");
                    put_dump(Buff, 0, 16);
                }
                if (disk_ioctl((uint8_t) p1, MMC_GET_OCR, Buff) == RES_OK) {
                    xputs("OCR:\n");
                    put_dump(Buff, 0, 4);
                }
                if (disk_ioctl((uint8_t) p1, MMC_GET_SDSTAT, Buff) == RES_OK) {
                    xputs("SD Status:\n");
                    for (s1 = 0; s1 < 64; s1 += 16)
                        put_dump(Buff + s1, s1, 16);
                }
                break;

            case 'c':          /* Disk ioctl */
                switch (*ptr++) {
                case 's':      /* dcs <drv> - CTRL_SYNC */
                    if (!xatoi(&ptr, &p1))
                        break;
                    xprintf("rc=%d\n", disk_ioctl((uint8_t) p1, CTRL_SYNC, 0));
                    break;
                case 'e':      /* dce <drv> <start> <end> - CTRL_ERASE_SECTOR */
                    if (!xatoi(&ptr, &p1) || !xatoi(&ptr, (long *) &blk[0]) || !xatoi(&ptr, (long *) &blk[1]))
                        break;
                    xprintf("rc=%d\n", disk_ioctl((uint8_t) p1, CTRL_ERASE_SECTOR, blk));
                    break;
                }
                break;
            }
            break; // end of Disk Controls //

        case 'b':              /* Buffer controls */
            switch (*ptr++)
            {
            case 'd':          /* bd <addr> - Dump R/W buffer */
                if (!xatoi(&ptr, &p1))
                    break;
                for (ptr = (char *) &Buff[p1], ofs = p1, cnt = 32; cnt; cnt--, ptr += 16, ofs += 16)
                    put_dump((uint8_t *) ptr, ofs, 16);
                break;


            case 'r':          /* br <drv> <lba> [<num>] - Read disk into R/W buffer */
                if (!xatoi(&ptr, &p1))
                    break;
                if (!xatoi(&ptr, &p2))
                    break;
                if (!xatoi(&ptr, &p3))
                    p3 = 1;
                xprintf("rc=%u\n", (uint16_t) disk_read((uint8_t) p1, Buff, p2, p3));
                break;


            case 'f':          /* bf <val> - Fill working buffer */
                if (!xatoi(&ptr, &p1))
                    break;
                memset(Buff, (uint8_t) p1, sizeof(Buff));
                break;

            }
            break; // end of Buffer Controls //

        case 'f':              /* FatFS API controls */
            switch (*ptr++)
            {

            case 'c':          /* fc - Close a file */
                put_rc(f_close(&File1));
                break;

            case 'd':          /* fd <len> - read and dump file from current fp */
                if (!xatoi(&ptr, &p1))
                    break;
                ofs = File1.fptr;
                while (p1)
                {
                    if ((uint32_t) p1 >= 16)
                    {
                        cnt = 16;
                        p1 -= 16;
                    }
                    else
                    {
                        cnt = p1;
                        p1 = 0;
                    }
                    res = f_read(&File1, Buff, cnt, &cnt);
                    if (res != FR_OK)
                    {
                        put_rc(res);
                        break;
                    }
                    if (!cnt)
                        break;

                    put_dump(Buff, ofs, cnt);
                    ofs += 16;
                }
                break;

            case 'e':          /* fe - Seek file pointer */
                if (!xatoi(&ptr, &p1))
                    break;
                res = f_lseek(&File1, p1);
                put_rc(res);
                if (res == FR_OK)
                    xprintf("fptr=%lu(0x%lX)\n", File1.fptr, File1.fptr);
                break;

            case 'i':          /* fi <vol> - Force initialized the logical drive */
                if (!xatoi(&ptr, &p1))
                    break;
                put_rc(f_mount((uint8_t) p1, &Fatfs[p1]));
                break;

            case 'l':          /* fl [<path>] - Directory listing */
                while (*ptr == ' ')
                    ptr++;
                res = f_opendir(&Dir, ptr);
                if (res) // if res in non-zero there is an error; print the error.
                {
                    put_rc(res);
                    break;
                }
                p1 = s1 = s2 = 0; // otherwise initialize the pointers and proceed.
                for (;;)
                {
                    res = f_readdir(&Dir, &Finfo);
                    if ((res != FR_OK) || !Finfo.fname[0])
                        break;
                    if (Finfo.fattrib & AM_DIR)
                    {
                        s2++;
                    }
                    else
                    {
                        s1++;
                        p1 += Finfo.fsize;
                    }
                    xprintf("%c%c%c%c%c %u/%02u/%02u %02u:%02u %9lu  %s",
                            (Finfo.fattrib & AM_DIR) ? 'D' : '-',
                            (Finfo.fattrib & AM_RDO) ? 'R' : '-',
                            (Finfo.fattrib & AM_HID) ? 'H' : '-',
                            (Finfo.fattrib & AM_SYS) ? 'S' : '-',
                            (Finfo.fattrib & AM_ARC) ? 'A' : '-',
                            (Finfo.fdate >> 9) + 1980, (Finfo.fdate >> 5) & 15, Finfo.fdate & 31,
                            (Finfo.ftime >> 11), (Finfo.ftime >> 5) & 63, Finfo.fsize, &(Finfo.fname[0]));
#if _USE_LFN
                    for (p2 = strlen(Finfo.fname); p2 < 14; p2++)
                        xputc(' ');
                    xprintf("%s\n", Lfname);
#else
                    xputc('\n');
#endif
                }
                xprintf("%4u File(s),%10lu bytes total\n%4u Dir(s)", s1, p1, s2);
                res = f_getfree(ptr, (uint32_t *) & p1, &fs);
                if (res == FR_OK)
                    xprintf(", %10lu bytes free\n", p1 * fs->csize * 512);
                else
                    put_rc(res);
                break;

            case 'o':          /* fo <mode> <file> - Open a file */
                if (!xatoi(&ptr, &p1))
                    break;
                while (*ptr == ' ')
                    ptr++;
                put_rc(f_open(&File1, ptr, (uint8_t) p1));
                break;


            case 'p':          /* fp <len> - read and play file from current fp */
                if (!xatoi(&ptr, &p1))
                    break;
                ofs = File1.fptr;
                int i = 0;
                while (p1)
		{
/*
		<<<<<<<<<<<<<<<<<<<<<<<<< YOUR fp CODE GOES IN HERE >>>>>>>>>>>>>>>>>>>>>>
*/
		}
		xprintf("done\n");
		break;
            case 'r':          /* fr <len> - read file */
                if (!xatoi(&ptr, &p1))
                    break;
                p2 = 0;
                Timer = 0;
                while (p1)
                {
                    if ((uint32_t) p1 >= blen)
                    {
                        cnt = blen;
                        p1 -= blen;
                    }
                    else
                    {
                        cnt = p1;
                        p1 = 0;
                    }
                    res = f_read(&File1, Buff, cnt, &s2);
                    if (res != FR_OK)
                    {
                        put_rc(res); // output a read error if a read error occurs
                        break;
                    }
                    p2 += s2; // increment p2 by the s2 referenced value
                    if (cnt != s2) //error if cnt does not equal s2 referenced value ???
                        break;
                }
                xprintf("%lu bytes read with %lu kB/sec.\n", p2, Timer ? (p2 / Timer) : 0);
                break;

            case 's':          /* fs [<path>] - Show volume status */
                res = f_getfree(ptr, (uint32_t *) & p2, &fs);
                if (res)
                {
                    put_rc(res);
                    break;
                }
                xprintf("FAT type = FAT%u\nBytes/Cluster = %lu\nNumber of FATs = %u\n"
                        "Root DIR entries = %u\nSectors/FAT = %lu\nNumber of clusters = %lu\n"
                        "FAT start (lba) = %lu\nDIR start (lba,clustor) = %lu\nData start (lba) = %lu\n\n...",
                        ft[fs->fs_type & 3], (uint32_t) fs->csize * 512, fs->n_fats,
                        fs->n_rootdir, fs->fsize, (uint32_t) fs->n_fatent - 2, fs->fatbase, fs->dirbase, fs->database);
                acc_size = acc_files = acc_dirs = 0;
                res = scan_files(ptr);
                if (res)
                {
                    put_rc(res);
                    break;
                }
                xprintf("\r%u files, %lu bytes.\n%u folders.\n"
                        "%lu KB total disk space.\n%lu KB available.\n",
                        acc_files, acc_size, acc_dirs, (fs->n_fatent - 2) * (fs->csize / 2), p2 * (fs->csize / 2));
                break;

            case 'z':          /* fz [<rw size>] - Change R/W length for fr/fw/fx command */
                if (xatoi(&ptr, &p1) && p1 >= 1 && p1 <= sizeof(Buff))
                    blen = p1;
                xprintf("blen=%u\n", blen);
                break;
            }
            break; // end of FatFS API controls //

        case 'h':
            display_help();
            break;

        }
    }

    /*
     * This return here make no sense.
     * But to prevent the compiler warning:
     * "return type of 'main' is not 'int'
     * we use an int as return :-)
     */
    return (0);
	}
