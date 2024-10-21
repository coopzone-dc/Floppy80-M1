#include <stdio.h>
#include <string.h>
#include "tusb.h"

#include "defines.h"
#include "video.h"

#define VIDEO_BUFFER_SIZE 0x800
#define VIDEO_NUM_COLS 64
#define VIDEO_NUM_ROWS 16

byte g_byVideoMemory[VIDEO_BUFFER_SIZE];
word g_wVideoLinesModified[VIDEO_NUM_ROWS];
word g_wPrevVideoLinesModified[VIDEO_NUM_ROWS];

byte g_byLineBuffer[128];

void InitVideo(void)
{
	memset(g_byVideoMemory, 0x20, sizeof(g_byVideoMemory));
    memset(g_wVideoLinesModified, 0, sizeof(g_wVideoLinesModified));
    memset(g_wPrevVideoLinesModified, 0, sizeof(g_wPrevVideoLinesModified));
}

void VideoWrite(word addr, byte ch)
{
    if ((addr < 0x3C00) || (addr > 0x3FFF))
    {
        return;
    }

    addr -= 0x3C00;
    g_byVideoMemory[addr] = ch;
    ++g_wVideoLinesModified[addr/VIDEO_NUM_COLS];
}

byte TranslateVideoChar(byte by)
{
    if ((by & 0xA0) == 0)
    {
        by |= 0x40;
    }

	switch (by)
	{
		case 128:
		case 130:
		case 160:
			by = ' ';
			break;

		case 138:
			by = 'X';
			break;

		case 133:
		case 149:
			by = 'X';
			break;

		case 131:
		case 135:
		case 143:
		case 159:
		case 175:
			by = 'X';
			break;

		case 136:
		case 140:
		case 180:
			by = 'X';
			break;

		case 189:
		case 190:
		case 191:
			by = 'X';
			break;

		case 188:
			by = 'X';
			break;

		default:
			if (by > 128)
			{
				by = 'X';
			}

			break;
	}

	return by;
}

void GetVideoLine(int nLine, byte* pby, int nMaxLen)
{
    byte* pbyVid = g_byVideoMemory+(nLine * VIDEO_NUM_COLS);
    int   i;

    for (i = 0; i < VIDEO_NUM_COLS; ++i)
    {
        *pby = TranslateVideoChar(*pbyVid);
        ++pby;
        ++pbyVid;
    }

    *pby = 0;
}

void ServiceVideo(void)
{
    static byte buf[VIDEO_NUM_COLS+1];
    static int  nPreviousVideoModified = 0;
    static int  nStartLine = 0;
    static int  nCurrentLine = 0;
    static int  state = 0;

    if (tud_cdc_n_write_available(CDC_ITF) < 80)
    {
        return;
    }

    switch (state)
    {
        case 0:
            ++nCurrentLine;

            if (nCurrentLine >= VIDEO_NUM_ROWS)
            {
                nCurrentLine = 0;
            }

            ++state;
            break;

        case 1:
            if (g_wVideoLinesModified[nCurrentLine] == g_wPrevVideoLinesModified[nCurrentLine])
            {
                state = 0;
                break;
            }

            g_wPrevVideoLinesModified[nCurrentLine] = g_wVideoLinesModified[nCurrentLine];
            sprintf(buf, "\033[%dH", nCurrentLine+1);
            tud_cdc_n_write(CDC_ITF, buf, strlen(buf));
            ++state;
            break;

        case 2:
            GetVideoLine(nCurrentLine, g_byLineBuffer, sizeof(g_byLineBuffer));
            ++state;
            break;

        case 3:
            tud_cdc_n_write(CDC_ITF, g_byLineBuffer, VIDEO_NUM_COLS);
            ++state;
            break;

        default:
            state = 0;
            break;
    }
}

void PrintVideo(void)
{
    byte buf[VIDEO_NUM_COLS+1];
    int  i;

    for (i = 0; i < VIDEO_NUM_ROWS; ++i)
    {
        GetVideoLine(i, buf, sizeof(buf));
        puts(buf);
    }
}
