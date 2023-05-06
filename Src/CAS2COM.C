/*****************************************************************************
 * CAS file converter to CP/M executable COM file for ПК8000 (PK8000).       *
 * Copyright (C) 2019 Andrey Hlus                                            *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>

#include <conio.h>

/*
  подключаем хидеры с кодом загрузки сконвертированных программ
*/
#include "bas.h"
#include "hex.h"
#include "hexa.h"
#include "hexs.h"


/*
  тип данных CAS-файла
*/
#define CAS_BINARY      0   // бинарник
#define CAS_BINPROT     1   // бинарник с защитой от AMATASOFT
#define CAS_BASIC       2   // бейсик-программа, сохранённая по CSAVE
#define CAS_TEXT        3   // бейсик-программа, сохранённая по SAVE (простой текст)

#define MAX_BSIZE  0x7F00   // макс. размер кода бейсик программ 0xBF00-0x4000

/*
  смещение относительно конца хидеров
*/
#define CSTART    -6        // начало блока
#define CSIZE     -2        // размер блока
#define CENTRY    -4        // точка входа

/*
  коды ошибок
*/
#define ERR_BAD_FORMAT      1
#define ERR_PROTECTED       2
#define ERR_UNEXPECTED_EOF  3
#define ERR_CORRUPT_DATA    4
#define ERR_UNKNOWN_FRM     5
#define ERR_CREATE_FILE     6
#define ERR_WRITE_FILE      7
#define ERR_NOT_MEM         8
#define ERR_TOO_LARGE       9
#define ERR_LAST            10

/*
  структура CAS-файла
*/
typedef struct
{
    unsigned char  type;                // тип файла: CAS_BINARY|CAS_BASIC|CAS_TEXT
    unsigned char  name[16];            // имя файла на ленте
    unsigned char  fname[16];           // имя для записи на диск
    unsigned long  size;                // размер файла
    unsigned int   start;               // начальный адрес загрузки
    unsigned int   stop;                // конечный адрес загрузки
    unsigned int   entry;               // адрес точки входа
    char          *data;
} CAS;

char *ext[] = {".COM", ".COM", ".COM", ".BAS"};
char *err[] = {"bad file format",
               "file is protect",
               "unexpected end of file",
               "corrupt data",
               "unknown data format",
               "can`t create file",
               "can`t write data",
               "not enought memory",
               "file too large"};


/*
  сигнатуры для разных типов CAS-файлов
*/
char HEADER[8] = { 0x1F,0xA6,0xDE,0xBA,0xCC,0x13,0x7D,0x74 };
char BIN[10]   = { 0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0 };
char ASCII[10] = { 0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA };
char BASIC[10] = { 0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3 };



char KOI2DOS[64]= {'j','a','b','c','d','e','f','g','h','i','i','k','l','m','n','o',
                   'p','y','r','s','t','u','w','v','-','-','z','j','q','j','x','-',
                   'J','A','B','C','D','E','F','G','H','I','I','K','L','M','N','O',
                   'P','Y','R','S','T','U','W','V','-','-','Z','J','Q','J','X','-' };

char KOI2RUS[64]= {'ю','а','б','ц','д','е','ф','г','х','и','й','к','л','м','н','о',
                   'п','я','р','с','т','у','ж','в','ь','ы','з','ш','э','щ','ч','ъ',
                   'Ю','А','Б','Ц','Д','Е','Ф','Г','Х','И','Й','К','Л','М','Н','О',
                   'П','Я','Р','С','Т','У','Ж','В','Ь','Ы','З','Ш','Э','Щ','Ч','Ъ'};


char bSafeCode;
char sFileName[256];


char fpresent(char *name)
{
    FILE *f;
    if ((f = fopen(name, "rb")) == NULL)
    {
        return 0;
    }
    fclose(f);
    return 0xFF;
}

long fsize(FILE *f)
{
    long oldpos, size;

    oldpos = ftell(f);
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, oldpos, SEEK_SET);
    return size;
}


void cas_Warning(char *msg)
{
    printf("WARNING: %s\n", msg);
}

void cas_Error(int n)
{
    if (n < ERR_LAST && n > 0)
        printf("ERROR:  %s!\n\n", err[n-1]);
}


void cas_ToName(CAS *cas)
{
    int i;
    unsigned char c;

    for (i = 0; i < 6; i++)
    {
        c = cas->name[i];
        if (c <= ' ')
            break;
        if (c >= 0xC0)
        {
            cas->fname[i] = KOI2DOS[c-0xC0];
            cas->name[i] = KOI2RUS[c-0xC0];
        } else {
            cas->fname[i] = c;
        }
    }
    cas->name[i] = 0x00;
    cas->fname[i] = 0x00;
}

// ищет очередной дескриптор
int cas_ReadDesc(FILE *f)
{
    char buff[8];
    long pos;

    pos = ftell(f);
    while (fread(buff, 1, 8, f) == 8)
    {
        if (!memcmp(buff, HEADER, 8))
        {
            return -1;                      // нашли дескриптор
        }
        fseek(f, ++pos, SEEK_SET);
    }
    return 0;
}

unsigned int fgetw(FILE *f)
{
    unsigned int h, l;
    l = fgetc(f);
    h = fgetc(f);
    return h*256+l;
}

void fseteof(FILE *f)
{
    while (!feof(f))
        fgetc(f);
}


// считывает блок данных бинарного формата
int cas_ReadBData(FILE *f, CAS *cas)
{
    unsigned int size;
    unsigned int size2;
    unsigned char ch;

    // считываем дескриптор данных
    if (!cas_ReadDesc(f))
        return ERR_BAD_FORMAT;
    // считываем данные о размещении в памяти
    cas->start = fgetw(f);
    cas->stop  = fgetw(f);
    cas->entry = fgetw(f);

    if (cas->start < 0x4000 || cas->start >= cas->stop)
        return ERR_BAD_FORMAT;

    if (cas->start >= 0xF635)
    {
        // загрузка в рабочие области биос и бейсика, похоже на защиту от копирования
        if (cas->start == 0xFB85)
        {
            // защита Назарова Михаила
            size = cas->stop-cas->start;
            if (size < 200)
                return ERR_PROTECTED;   // неизвестный формат
            cas_Warning("programm is protect by Nazarov Mihail");
            // считываем данные о размещении в памяти
            cas->start = fgetw(f);
            cas->stop  = fgetw(f);
            cas->entry = fgetw(f);
            size -= 6;
            // пропускаем код защиты
            while (size)
            {
                fgetc(f);
                size--;
            }
            if (!cas_ReadDesc(f))
                return ERR_CORRUPT_DATA;
        } else if (cas->start == 0xF6D0) {
            // похоже на защиту AMATASOFT
            cas->type = CAS_BINPROT;
            // пропускаем код защиты
            size = 80;
            while (size)
            {
                fgetc(f);
                size--;
            }
            // ищем настоящие данные
            size = 100;
            while (size)
            {
                ch = fgetc(f);
                if (ch == 0)
                    break;
            }
            if (!size)
                return ERR_PROTECTED;   // неизвестный формат
            cas_Warning("programm is protect by AMATASOFT");
            // считываем данные о размещении в памяти
            cas->start = fgetw(f);
            cas->stop  = cas->start + fgetw(f);
            cas->entry = fgetw(f);
            if (cas->start < 0x4000 || cas->start >= cas->stop)
                return ERR_BAD_FORMAT;
        } else if (cas->start == 0xF6D6) {
            // похоже на зашиту Protection v2.0 1.03.93 + Alior
            size = 14;
            while (size)
            {
                fgetc(f);
                size--;
            }
            if (fgetc(f) != 0x21)
                return ERR_PROTECTED;
            cas->stop = fgetw(f);
            fgetc(f);fgetc(f);fgetc(f);
            if (fgetc(f) != 0x21)
                return ERR_PROTECTED;
            cas->start = fgetw(f);
            fgetc(f);fgetc(f);fgetc(f);
            if (fgetc(f) != 0x01)
                return ERR_PROTECTED;
            size = fgetw(f);
            if ((cas->start + size) > cas->stop)
                return ERR_PROTECTED;
            cas_Warning("programm is protect by Alior");
            cas->stop = cas->start + size;
            // пропускаем код загрузчика
            size = 48;
            while (size)
            {
                fgetc(f);
                size--;
            }
            // позиционируем на начало реального кода
            size = 100;
            while (size)
            {
                ch = fgetc(f);
                if (ch == 0)
                    break;
            }
            if (!size)
                return ERR_CORRUPT_DATA;
        } else {
            return ERR_PROTECTED;
        }
    }
    // загрузка в свободные области памяти
    if (cas->entry < cas->start || cas->entry >= cas->stop)
        return ERR_CORRUPT_DATA;
    // выделяем память и считываем
    size = cas->stop-cas->start;
    cas->data = malloc(size);
    if (cas->data == NULL)
        return ERR_NOT_MEM;
    if ((size2 = fread(cas->data, 1, size, f)) != size)
    {
        printf("WARNING: expected: %u bytes, read: %u bytes\n", size, size2);
//        free(cas->data);
//        return ERR_UNEXPECTED_EOF;
    }
    return 0;
}



int cas_ReadCData(FILE *f, CAS *cas)
{
    unsigned int size;
    char        *b;

    if (!cas_ReadDesc(f))
        return ERR_BAD_FORMAT;

    // подгружаем в буфер
    cas->data = malloc(MAX_BSIZE+0x100);
    if (cas->data == NULL)
        return ERR_NOT_MEM;
    memset(cas->data, 0x00, MAX_BSIZE+0x100);
    size = 0;
    if (fread(cas->data+1, 1, MAX_BSIZE, f) > 0)
    {
        // высчитываем размер
        b = cas->data;
        while (size < MAX_BSIZE)
        {
            if (*b == 0x00)
            {
                b++;
                size++;
                if (*b == 0x00)
                {
                    b++;
                    size++;
                    if (*b == 0x00)
                        break;              // нашли конец данных
                }
            }
            b++;
            size++;
        } // while
    } else {
        cas_Warning("no data!");
    }
    if (*b != 0x00)
    {
        cas_Warning("file too long, cut data!");
        *b = 0x00;
    }
    // выравниваем размер на границу 256 байт
    size = ((size+10) & 0xFF00) + 0x100;
    cas->start = 0x0000;
    cas->entry = size+0x4000;
    cas->stop  = size;
    return 0;
}

int cas_ReadTData(FILE *f, CAS *cas)
{
    unsigned int rd, size;
    char *b;

    cas->data = malloc(MAX_BSIZE+0x100);    // максимальный размер данных
    if (cas->data == NULL)
        return ERR_NOT_MEM;
    b = cas->data;
    size = 0;
    while (!feof(f))
    {
        // переходим на очередной блок
        if (!cas_ReadDesc(f))
            return ERR_CORRUPT_DATA;
        // считываем блок в 256 байт
        rd = fread(b, 1, 256, f);
        while (rd)
        {
            if (*b == 0x1A)
            {
                size++;
                fseteof(f);
                break;
            } else {
                b++;
                size++;
                rd--;
            }
            if (size > MAX_BSIZE)
            {
                cas_Warning("file too long, cut data!");
                fseteof(f);
                break;
            }
        }
    }
    cas->stop = size;
    return 0;
}


CAS *cas_Open(char *name)
{
    FILE *f;
    char buff[10];
    CAS *cas;
    int  code;

    if ((f = fopen(name, "rb")) == NULL)
    {
        printf("\nERROR: can`t open '%s'\n", name);
        return NULL;
    }
    cas = (CAS*) malloc(sizeof(CAS));
    if (cas)
    {
        cas->size = fsize(f);
        printf("  (%li bytes)\n", cas->size);
        if (cas->size >= 64000)
        {
            cas_Error(ERR_TOO_LARGE);
            free(cas);
            return NULL;
        }

        if (cas_ReadDesc(f))
        {
            if (fread(buff, 1, 10, f) == 10)    // считываем тип данных
            {
                if (!memcmp(buff, BIN, 10))
                {
                    // это бинарные данные, считываем имя файла
                    cas->type = CAS_BINARY;
                    fread(cas->name, 1, 6, f);
                    cas_ToName(cas);
                    printf("FOUND:  %s (BINARY)\n",cas->name);
                    code = cas_ReadBData(f, cas);
                    printf("  -start: 0x%X\n  -stop : 0x%X\n  -entry: 0x%X\n", cas->start, cas->stop, cas->entry);

                } else if (!memcmp(buff, BASIC, 10))
                {
                    // это бейсик-программа, считываем имя файла
                    cas->type = CAS_BASIC;
                    fread(cas->name, 1, 6, f);
                    cas_ToName(cas);
                    printf("FOUND:  %s (BASIC)\n",cas->name);
                    code = cas_ReadCData(f, cas);
                    printf("  -start: 0x%X\n  -stop : 0x%X\n  -size : %u bytes\n", 0x4000, cas->entry, cas->stop);

                } else if (!memcmp(buff, ASCII, 10))
                {
                    cas->type = CAS_TEXT;
                    fread(cas->name, 1, 6, f);
                    cas_ToName(cas);
                    printf("FOUND:  %s (TEXT)\n",cas->name);
                    // считываем данные
                    code = cas_ReadTData(f, cas);
                    if (!code)
                        printf("  -size: %u\n", cas->stop);

                } else {
                    fread(cas->name, 1, 6, f);
                    cas_ToName(cas);
                    printf("FOUND:  %s (UNKNOWN)\n",cas->name);
                    code = ERR_UNKNOWN_FRM;
                }
            } else {
               code = ERR_UNEXPECTED_EOF;
            }
        } else {
            code = ERR_BAD_FORMAT;
        }
    } else {
        printf("\n");
        code = ERR_NOT_MEM;
    }
    fclose(f);
    if (!code)
        return cas;
    if (cas)
        free(cas);
    cas_Error(code);
    return NULL;
}


int com_Create(CAS *cas)
{
    FILE *f;
    char *head;
    char fill[256];
    char fname[16];
    unsigned int size, hsize;          // размер блока
    int          code;
    int          i;

    strcpy(fname, cas->fname);
    strcat(fname, ext[cas->type]);
    printf("CREATE: %s\n", fname);
    memset(fill, 0x00, sizeof(fill));
    code = 0;

    if (fpresent(fname))
    {
        printf("WARNING: file is already present!\n");
        // подбираем другое имя для файла
        for (i = 1; i <= 99; i++)
        {
            memset(fname, 0x00,sizeof(fname));
            sprintf(fname,"%s%i%s",cas->fname,i,ext[cas->type]);
            if (!fpresent(fname))
                break;
        }
        if (fpresent(fname))
        {
            cas_Error(ERR_CREATE_FILE);
            return 0;
        }
        printf("CREATE: %s\n", fname);
    }

    if ((f = fopen(fname, "wb")) != NULL)
    {
        // выбираем и настраиваем заголовок
        switch (cas->type)
        {
            case CAS_BINARY:
                {
                    if (bSafeCode)
                    {
                        hsize = sizeof(BLOADS);
                        size = cas->stop-cas->start;
                        head = &BLOADS[0];
                    } else {
                        hsize = sizeof(BLOAD);
                        size = cas->stop-cas->start;
                        head = &BLOAD[0];
                    }
                    break;
                }
            case CAS_BINPROT:
                {
                    hsize = sizeof(BPROT);
                    size = cas->stop-cas->start;
                    head = &BPROT[0];
                    break;
                }
            case CAS_BASIC:
                {
                    hsize = sizeof(CLOAD);
                    size = cas->stop;
                    head = &CLOAD[0];
                    break;
                }
            case CAS_TEXT:
                {
                    size = cas->stop;
                    hsize = 128 - (size % 128); // защита от выравнивания
                    head = NULL;
               }
        } // switch

        if (head)
        {
            head[hsize+CSTART]   = cas->start & 0xFF;
            head[hsize+CSTART+1] = cas->start >> 8;
            head[hsize+CENTRY]   = cas->entry & 0xFF;
            head[hsize+CENTRY+1] = cas->entry >> 8;
            head[hsize+CSIZE]    = size & 0xFF;
            head[hsize+CSIZE+1]  = size >> 8;
            // и сохраняем его
            if (fwrite(head, 1, hsize, f) != hsize)
                code = ERR_WRITE_FILE;
        }
        // сохраняем данные
        printf("WRITE:  %u Bytes data\n", size);
        if (!code)
            if (fwrite(cas->data, 1, size, f) != size)
                code = ERR_WRITE_FILE;
        if (!code)
        {
            size += hsize;
            if (size % 128)
                fwrite(fill, 1, 128 - (size % 128), f);
        }
        fclose(f);
    } else {
        code = ERR_CREATE_FILE;
    }

    if (!code)
        return -1;

    cas_Error(code);
    return 0;
}



void do_usage(void)
{
    printf("Usage: CAS2COM <filename.cas> [-options]\n");
    printf("  options:\n");
    printf("    h|? - this help\n");
    printf("    s   - use safety running\n");
}

char do_argv(int argc, char *argv[])
{
    int     i;

    if (argc < 2)
    {
        do_usage();
        return 0;
    }

    bSafeCode = 0;
    sFileName[0] = 0;
    i = 1;
    while (i < argc)
    {
        if ((argv[i][0] == '-') || (argv[i][0] == '/'))
        {
            switch (argv[i][1])
            {
                case 'S':
                case 's': {
                    bSafeCode = 1;
                    break;
                }
                case 'H':
                case 'h':
                case '?': {
                    do_usage();
                    return 0;
                }

            } // switch
        } else {
            // предполагаем, что это имя файла
            if (strlen(sFileName) == 0)
                strncpy(sFileName, &argv[i][0], sizeof(sFileName));
        }
        i++;
    }
    fflush(stdout);
    return -1;
}


int main(int argc, char* argv[])
{
    CAS *cas;

    printf("CAS to COM converter  v1.5\n");
    while (kbhit()) getch();

    if (!do_argv(argc, argv))
        return 0;

    printf("SOURCE: %s", sFileName);
    if ((cas = cas_Open(sFileName)) != NULL)
    {
        // создаем COM-файл
        if (com_Create(cas))
            printf("Done!\n\n");
    }
    return -1;
}
