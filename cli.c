#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "stdio.h"

#include "defines.h"
#include "system.h"
#include "file.h"
#include "fdc.h"

void ServiceFdcLog(void);

extern FdcDriveType g_dtDives[MAX_DRIVES];
extern TrackType    g_tdTrack;

static uint64_t g_nCdcPrevTime;
static uint32_t g_nCdcConnectDuration;
static bool     g_bCdcConnected;
static bool     g_bCdcPromptSent;

static char     g_szCommandLine[64];
static int      g_nCommandLineIndex;

static DIR     dj;				// Directory object
static FILINFO fno;				// File information

static bool    g_bOutputLog = false;

static char szHelpText[] = {
                        "\n"
                        "help   - returns this message\n"
                        "status - returns the current FDC status\n"
                        "dir    - returns a directory listing of the root folder of the SD-Card\n"
                        "         optionally include a filter.  For example dir .ini\n"
                        "boot   - selects an ini file to be specified in the boot.cfg\n"
                        "logon  - enable output of FDC interface logging output\n"
                        "logoff - disable output of FDC interface logging output\n"
                        "disks  - returns the stats to the mounted diskettes\n"
                        "dump   - returns sectors of each track on the indicate drive\n"
                    };

void InitCli(void)
{
    g_bCdcPromptSent = false;
    g_bCdcConnected  = false;
    g_nCdcPrevTime   = time_us_64();
}

void ListFiles(char* pszFilter)
{
    FRESULT fr;  // Return value
    int nCol = 0;

    memset(&dj, 0, sizeof(dj));
    memset(&fno, 0, sizeof(fno));
    fr = f_findfirst(&dj, &fno, "0:", "*");

	while ((fr == FR_OK) && (fno.fname[0] != 0))
	{
		if ((fno.fattrib & AM_DIR) || (fno.fattrib & AM_SYS))
		{
			// pcAttrib = pcDirectory;
		}
		else
		{
			if ((pszFilter[0] == 0) || (stristr(fno.fname, pszFilter) != NULL))
			{
                ++nCol;

    			if (nCol < 5)
				{
                    printf("%30s %7d", fno.fname, fno.fsize);
                }
                else
                {
                    printf("%30s %7d\r\n", fno.fname, fno.fsize);
                    nCol = 0;
                }
            }
		}

		if (fno.fname[0] != 0)
		{
			fr = f_findnext(&dj, &fno); /* Search for next item */
		}
	}
}

void ProcessDisksRequest(void)
{
    int i;
    char* pszDensity[] = {"SD", "DD"};

    for (i = 0; i < MAX_DRIVES; ++i)
    {
        printf("File name  : %s\r\n", g_dtDives[i].szFileName);
        printf("Density    : %s\r\n", pszDensity[g_dtDives[i].dmk.byDensity]);
        printf("Num sides  : %d\r\n", g_dtDives[i].dmk.byNumSides);
        printf("Track size : %d\r\n", g_dtDives[i].dmk.wTrackLength);
        printf("Sector size: %d\r\n", g_dtDives[i].dmk.nSectorSize);
    }
}

void DumpSector(int nDrive, int nTrack, int nSector)
{
    int nOffset = g_tdTrack.nDataOffset[nSector];
    int nSectorSize = 256; //g_dtDives[nDrive].dmk.byDmkDiskHeader[1];
    int i;

    if (nOffset < 0)
    {
        return;
    }

    printf("Drive %d, Track %d, Sector %d\r\n", nDrive, nTrack, nSector);
    sleep_ms(1);

    BYTE* pby = g_tdTrack.byTrackData + nOffset - 3;

    // dump to the 0xFE byte
    for (i = 1; i <= 256; ++i)
    {
        printf("%02X ", *pby);

        if ((i % 16) == 0)
        {
            printf("\r\n");
            sleep_ms(5);
        }

        if (*pby == 0xFE)
        {
            i = 256;
        }
        else
        {
            ++pby;
        }
    }

    printf("\r\n");

    if (*pby != 0xFE)
    {
        return;
    }

    ++pby;
    printf("Track %d, Side %d, Sector %d, Length %d\r\n",
            *pby, *(pby+1), *(pby+2), *(pby+3));
    sleep_ms(5);

    nSectorSize = 128 << *(pby+3);
    
    pby += 4;

    // dump to the 0xFB byte
    for (i = 1; i <= 256; ++i)
    {
        printf("%02X ", *pby);

        if ((i % 16) == 0)
        {
            printf("\r\n");
            sleep_ms(5);
        }

        if (*pby == 0xFB)
        {
            i = 256;
        }
        else
        {
            ++pby;
        }
    }

    printf("\r\n");

    if (*pby != 0xFB)
    {
        return;
    }

    ++pby;

    for (i = 1; i <= nSectorSize; ++i)
    {
        printf("%02X ", *pby);
        ++pby;

        if ((i % 16) == 0)
        {
            printf("\r\n");
            sleep_ms(5);
        }
    }

    printf("\r\n");
    sleep_ms(5);
}

void ProcessDumpRequest(void)
{
    int nDrive  = 2;
    int nTracks = g_dtDives[nDrive].dmk.byDmkDiskHeader[1];
    int i, j;

    for (i = 0; i < nTracks; ++i)
    {
    	FdcReadTrack(nDrive, 0, i);

        printf("Track %d\r\n", i);

        for (j = 0; j < 64; ++j)
        {
            DumpSector(nDrive, i, j);
        }
    }
}

void ProcessCommand(char* psz)
{
    char szParm1[16] = {""};
    char szCmd[16] = {""};

    psz = GetWord(psz, szCmd, sizeof(szCmd)-2);

    if (stricmp(szCmd, "HELP") == 0)
    {
        puts(szHelpText);
        return;
    }

    if (stricmp(szCmd, "DIR") == 0)
    {
        psz = GetWord(psz, szParm1, sizeof(szParm1)-2);
        ListFiles(szParm1);
        return;
    }

    if (stricmp(szCmd, "BOOT") == 0)
    {
        psz = GetWord(psz, szParm1, sizeof(szParm1)-2);
		FdcSaveBootCfg(szParm1);
        FdcProcessStatusRequest(true);
        return;
    }

    if (stricmp(szCmd, "STATUS") == 0)
    {
        FdcProcessStatusRequest(true);
        return;
    }

    if (stricmp(szCmd, "LOGON") == 0)
    {
        g_bOutputLog = true;
        return;
    }

    if (stricmp(szCmd, "LOGOFF") == 0)
    {
        g_bOutputLog = false;
        return;
    }
    
    if (stricmp(szCmd, "DISKS") == 0)
    {
        ProcessDisksRequest();
        return;
    }

    if (stricmp(szCmd, "DUMP") == 0)
    {
        ProcessDumpRequest();
        return;
    }

    puts("Unknown command");
    puts(szHelpText);
}

void ServiceCli(void)
{
    uint64_t nTimeNow;
    char*    prompt = {"\nCMD>"};
    int      c;

    if (!tud_cdc_connected())
    {
        g_bCdcConnected = false;
        return;
    }

    if (g_bCdcConnected == false)
    {
        g_bCdcConnected = true;
       	g_nCdcPrevTime  = time_us_64();
        g_nCdcConnectDuration = 0;
        g_szCommandLine[0] = 0;
        g_nCommandLineIndex = 0;
        return;
    }

    if (g_bCdcPromptSent == false)
    {
        nTimeNow = time_us_64();
        g_nCdcConnectDuration += (g_nCdcPrevTime - nTimeNow);
        g_nCdcPrevTime = nTimeNow;

        if (g_nCdcConnectDuration < 2000000)
        {
            return;
        }

        printf("\nCMD> ");
        g_bCdcPromptSent = true;
    }

    if (g_bOutputLog)
    {
        ServiceFdcLog();
    }
    else
    {
        log_tail = log_head;
    }

    c = getchar_timeout_us(0);

    if (c == PICO_ERROR_TIMEOUT) // no new characters
    {
        return;
    }

    if (c == '\r')
    {
        puts("");
        ProcessCommand(g_szCommandLine);
        printf("\nCMD> ");
        g_nCommandLineIndex = 0;
        g_szCommandLine[0] = 0;
    }
    else if (c == '\b') // backspace
    {
        if (g_nCommandLineIndex > 0)
        {
            --g_nCommandLineIndex;
            g_szCommandLine[g_nCommandLineIndex] = 0;
            printf("\b \b");
        }
    }
    else if (c < 32)
    {
        while (c != PICO_ERROR_TIMEOUT)
        {
            c = getchar_timeout_us(100);
        }
    }
    else if (g_nCommandLineIndex < sizeof(g_szCommandLine)-2)
    {
        if ((c != '\n') && (c != '\r'))
        {
            printf("%c", c);
            g_szCommandLine[g_nCommandLineIndex] = c;
            ++g_nCommandLineIndex;
            g_szCommandLine[g_nCommandLineIndex] = 0;
        }
    }
}
