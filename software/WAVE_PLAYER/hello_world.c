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

#include "altera_avalon_timer_regs.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"

#define PSTR(_a)  _a

static alt_alarm alarm;
static unsigned long Systick = 0;
static volatile unsigned short Timer;   /* 1000Hz increment timer */

/*Global Variables*/

enum speed { normal = 4, half = 2, doub = 8 };
enum channel { stereo, mono };

struct play_info {
	int p1;
	int i;
	int bitsRead;
	int bitsToRead;
	int bufferLength;
	enum speed speed;
	enum channel channel;
};

struct play_info pi;

enum state {
	stopped,
	paused,
	playing,
	stopped_to_switch,
};

enum state global_state;

char fileInfo[20][20];
int fileSize[20];
int fileIndex[20];
int fileNumber = 0;
int cur_file = 0;

volatile int needPrint;

uint16_t lbuf;
uint16_t rbuf;

uint32_t acc_size;                 /* Work register for fs command */
uint16_t acc_files, acc_dirs;
FILINFO Finfo;

char Line[256];                 /* Console input buffer */

FATFS Fatfs[_VOLUMES];          /* File system object for each logical drive */
FIL File1, File2;               /* File objects */
DIR Dir;                        /* Directory object */

uint8_t Buff[512] __attribute__ ((aligned(4)));  /* Working buffer */

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
}

static void IoInit(void)
{
   uart0_init(115200);
   ffs_DiskIOInit();
   alt_alarm_start(&alarm, 1, &TimerFunction, NULL);

}

static void put_rc(FRESULT rc)
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

int isWav(const char *filename) {
	char *s = filename;
	while (*s) {
		if (*s == '.') {
			return !strcmp(s, ".WAV") || !strcmp(s, ".wav");
		}
		s++;
	}
	return 0;
}

void setup(FILE *lcd, struct play_info *pi, const char *name, int size) {
	f_close(&File1);
	volatile int sw = IORD(SWITCH_PIO_BASE, 0) & 3;
	switch (sw) {
		case 3:
			pi->speed = normal;
			pi->channel = stereo;
			pi->bufferLength = 512;
			break;
		case 2:
			pi->speed = half;
			pi->channel = stereo;
			pi->bufferLength = 512;
			break;
		case 1:
			pi->speed = doub;
			pi->channel = stereo;
			pi->bufferLength = 512;
			break;
		case 0:
			pi->speed = normal;
			pi->channel = mono;
			pi->bufferLength = 512;
			break;
	}

	int res = f_open(&File1, name, 1);
	if (res != FR_OK) {
		xprintf("failed to open %s", name);
		return;
	}

	pi->p1 = size;
	pi->i = 1;
	pi->bitsRead = 0;
	pi->bitsToRead = 0;

#define WAV_HEADER_LENGTH 44

    char header[WAV_HEADER_LENGTH];
    int numRead;
    res = f_read(&File1, header, WAV_HEADER_LENGTH, &numRead);
    if (numRead != WAV_HEADER_LENGTH || res != FR_OK) {
    	xprintf("%s:%d only read %d/%d bytes", __FILE__, __LINE__, numRead, WAV_HEADER_LENGTH);
    	return;
    }

    pi->p1 -= WAV_HEADER_LENGTH;
}

void do_play(alt_up_audio_dev *audio_dev, struct play_info *pi) {
	while (pi->p1 > 0) {

		pi->bitsToRead = pi->bufferLength;

		if ((uint32_t) pi->p1 >= pi->bufferLength)
		{
			pi->bitsToRead = pi->bufferLength;
			pi->p1 -= pi->bufferLength;
		}
		else
		{
			pi->bitsToRead = pi->p1;
			pi->p1 = 0;
		}

		int res = f_read(&File1, Buff, pi->bitsToRead, &pi->bitsRead);
		if (res != FR_OK || pi->bitsRead != pi->bitsToRead) {
			xprintf("only got %d/%d\n", pi->bitsRead, pi->bitsToRead);
			return;
		}
		int i;
		for(pi->i = 0; pi->i < pi->bitsRead; pi->i+=pi->speed) {
			if (global_state != playing) {
				return;
			}
			i = pi->i;
			lbuf = ((Buff[i+1] << 8) | Buff[i]);
			rbuf = ((Buff[i+3] << 8) | Buff[i+2]);

			if(pi->channel == mono) {
				rbuf = lbuf;
			}

			while(alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT) == 0);
			alt_up_audio_write_fifo (audio_dev, &(rbuf), 1, ALT_UP_AUDIO_RIGHT);
        	while (alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_LEFT) == 0);
			alt_up_audio_write_fifo (audio_dev, &(lbuf), 1, ALT_UP_AUDIO_LEFT);
		}
	}
}

#define ESC 27
#define CLEAR_LCD_STRING "[2J"

void print_playing_helper(FILE *lcd, const char *file, enum state st, enum speed spd, enum channel ch) {
    fprintf(lcd, "%c%s", ESC, CLEAR_LCD_STRING);
    if (st == playing) {
    	if (spd == normal && ch == mono) {
    		fprintf(lcd, "%d.%s\n%s\n", fileIndex[cur_file], file, "PLAYBACK-MONO-LEFT_Audio");
    	} else if (spd == normal) {
    		fprintf(lcd, "%d.%s\n%s\n", fileIndex[cur_file], file, "PLAYBACK-NORMAL SPD");
    	} else if (spd == doub) {
    		fprintf(lcd, "%d.%s\n%s\n", fileIndex[cur_file], file, "PLAYBACK-DBL SPD");
    	} else if (spd == half) {
    		fprintf(lcd, "%d.%s\n%s\n", fileIndex[cur_file], file, "PLAYBACK-HALF SPD");
    	}
    } else if (st == paused) {
		fprintf(lcd, "%d.%s\n%s\n", fileIndex[cur_file], file, "PAUSED");
    } else if (st == stopped) {
		fprintf(lcd, "%d.%s\n%s\n", fileIndex[cur_file], file, "STOPPED");
    }
}

static void timer_handler (void* context, alt_u32 id) {
	IOWR(BUTTON_PIO_BASE, 3, 0x0);
	IOWR(BUTTON_PIO_BASE, 2, 0xf);

	IOWR_ALTERA_AVALON_TIMER_STATUS(SYSTEM_TIMER_BASE, 0);
	IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0x0);

 	volatile int pb = IORD(BUTTON_PIO_BASE, 0);
 	switch (global_state) {
 		case playing:
 			switch (pb) {
 				case 0b1110:
    				++cur_file;
    				if (cur_file >= fileNumber) {
    					cur_file = 0;
    				}
    				global_state = stopped_to_switch;
    				needPrint = 1;
 					break;
 				case 0b1101:
 					global_state = paused;
 					needPrint = 1;
 					break;
 				case 0b1011:
 					global_state = stopped;
 					needPrint = 1;
 					break;
 				case 0b0111:
    				--cur_file;
    				if (cur_file < 0) {
    					cur_file = fileNumber-1;
    				}
    				global_state = stopped_to_switch;
 					needPrint = 1;
 					break;
 				default:
 					break;
 			}
 			break;
 		case paused:
 			switch (pb) {
 				case 0b1110:
    				++cur_file;
    				if (cur_file >= fileNumber) {
    					cur_file = 0;
    				}
    				global_state = stopped;
 					needPrint = 1;
 					break;
 				case 0b1101:
 					global_state = playing;
 					break;
 				case 0b1011:
 					global_state = stopped;
 					needPrint = 1;
 					break;
 				case 0b0111:
    				--cur_file;
    				if (cur_file < 0) {
    					cur_file = fileNumber-1;
    				}
    				global_state = stopped;
 					needPrint = 1;
 					break;
 				default:
 					break;
 			}
 			break;
 		case stopped:
 			switch (pb) {
 				case 0b1110:
    				++cur_file;
    				if (cur_file >= fileNumber) {
    					cur_file = 0;
    				}
 					needPrint = 1;
 					break;
 				case 0b1101:
 					global_state = playing;
 					break;
 				case 0b1011:
 					break;
 				case 0b0111:
    				--cur_file;
    				if (cur_file < 0) {
    					cur_file = fileNumber-1;
    				}
 					needPrint = 1;
 					break;
 				default:
 					break;
 			}
 			break;
 	}
	//IOWR(BUTTON_PIO_BASE, 3, 0x0);
}

void button_handler(void* context, alt_u32 id) {
	IOWR(BUTTON_PIO_BASE, 2, 0x0);

	IOWR_ALTERA_AVALON_TIMER_STATUS(SYSTEM_TIMER_BASE, 0);
	alt_irq_register(SYSTEM_TIMER_IRQ, (void*) 0, timer_handler);

	IOWR_ALTERA_AVALON_TIMER_PERIODH(SYSTEM_TIMER_BASE, 0x10);
	IOWR_ALTERA_AVALON_TIMER_PERIODL(SYSTEM_TIMER_BASE, 0xDA);

	IOWR_ALTERA_AVALON_TIMER_CONTROL(SYSTEM_TIMER_BASE, 0x5);
}

int main()
{
	int fifospace;
    char *ptr = "", *ptr2 = "";
    long p1, p2, p3;
    uint8_t res, b1, drv = 0;
    uint16_t w1;
    uint32_t s1, s2, cnt, blen = sizeof(Buff);
    static const uint8_t ft[] = { 0, 12, 16, 32 };
    uint32_t ofs = 0, sect = 0, blk[2];
    FATFS *fs;

    alt_up_audio_dev * audio_dev;
    // open the Audio port
    audio_dev = alt_up_audio_open_dev ("/dev/Audio");
    if ( audio_dev == NULL) {
    	alt_printf ("Error: could not open audio device \n");
    	return 0;
    }
    IoInit();

    IOWR(SEVEN_SEG_PIO_BASE,1,0x0007);

    disk_initialize((uint8_t) 0);
    f_mount((uint8_t) 0, &Fatfs[0]);

    res = f_opendir(&Dir, ptr);
    if (res) // if res in non-zero there is an error; print the error.
    {
        put_rc(res);
        return 0;
    }
    p1 = s1 = s2 = 0; // otherwise initialize the pointers and proceed.

    int k = 1;
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
        if (isWav(&(Finfo.fname[0]))) {
        	strcpy(fileInfo[fileNumber], &(Finfo.fname[0]));
        	fileSize[fileNumber] = Finfo.fsize;
        	fileIndex[fileNumber] = k;
        	fileNumber++;
        }
        ++k;
    }

    res = f_getfree(ptr, (uint32_t *) & p1, &fs);
    if (res != FR_OK)
        put_rc(res);

    FILE *lcd = fopen("/dev/lcd_display", "w");

    cur_file = 0;
	global_state = stopped;
    setup(lcd, &pi, fileInfo[cur_file], fileSize[cur_file]);
	print_playing_helper(lcd, fileInfo[cur_file], global_state, pi.speed, pi.channel);

	IOWR(BUTTON_PIO_BASE, 2, 0xf);
	IOWR(BUTTON_PIO_BASE, 3, 0x0);
	alt_irq_register(BUTTON_PIO_IRQ, 0, button_handler);

    for (;;) {
    	switch (global_state) {
    		case playing:
    			print_playing_helper(lcd, fileInfo[cur_file], global_state, pi.speed, pi.channel);
    			do_play(audio_dev, &pi);
    			if (pi.p1 <= 0) {
    				global_state = stopped;
    				print_playing_helper(lcd, fileInfo[cur_file], global_state, pi.speed, pi.channel);
    			}
    			break;
    		case paused:
    			if(needPrint == 1) {
        			print_playing_helper(lcd, fileInfo[cur_file], global_state, pi.speed, pi.channel);
        			needPrint = 0;
    			}
    			break;
    		case stopped:
				f_close(&File1);
				setup(lcd, &pi, fileInfo[cur_file], fileSize[cur_file]);
    			if(needPrint == 1) {
        			print_playing_helper(lcd, fileInfo[cur_file], global_state, pi.speed, pi.channel);
        			needPrint = 0;
    			}
				break;
    		case stopped_to_switch:
				f_close(&File1);
				setup(lcd, &pi, fileInfo[cur_file], fileSize[cur_file]);
				print_playing_helper(lcd, fileInfo[cur_file], global_state, pi.speed, pi.channel);
				global_state = playing;
				break;
    		default:
    			break;
    	}
    }

	return 0;
}
