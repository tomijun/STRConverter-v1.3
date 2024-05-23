#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <Windows.h>
#include "SFmpqapi.h"
#include "SFmpq_static.h"
#include "SFmpqapi_no-lib.h"
#include "resource.h"

using namespace std;
typedef unsigned long long QWORD;
MPQHANDLE* Save1[1024];
FILE* Save2[1024];
int Save1ptr = 0;
int Save2ptr = 0;

void f_ScloseAll(MPQHANDLE hMPQ)
{
    for (int i = 0; i < Save2ptr; i++)
        fclose(Save2[i]);
    for (int i = 0; i < Save1ptr; i++)
        SFileCloseFile(*Save1[i]);
    SFileCloseArchive(hMPQ); // MPQ 닫기
    Save1ptr = 0;
    Save2ptr = 0;
    return;
}

int f_Sopen(MPQHANDLE hMPQ, LPCSTR lpFileName, MPQHANDLE* hFile)
{
    // SFileOpenFileEx([MPQ핸들], [파일명], 0, &[리턴받을 파일핸들]);
    if (!SFileOpenFileEx(hMPQ, lpFileName, 0, hFile)) {
        printf("%s이(가) 존재하지 않습니다. [%d]\n", lpFileName, GetLastError());
        return -1;
    }
    Save1[Save1ptr++] = hFile;
}

int f_Scopy(MPQHANDLE hMPQ, MPQHANDLE* hFile, LPCSTR foutName, FILE** fout)
{
    fopen_s(fout, foutName, "w+b");
    if (*fout == NULL) {
        f_ScloseAll(hMPQ);
        printf("%s을 열 수 없습니다.\n", foutName);
        return -1;
    }
    Save2[Save2ptr++] = *fout;
    DWORD fsize = SFileGetFileSize(*hFile, NULL);
    char buffer[4096] = { 0 };
    while (fsize > 0) {
        DWORD transfersize = min(4096, fsize);
        DWORD readbyte;
        SFileReadFile(*hFile, buffer, transfersize, &readbyte, NULL); //fread에 해당
        if (readbyte != transfersize) {
            printf("SFileReadFile read %d bytes / %d bytes expected.\n", readbyte, transfersize);
            f_ScloseAll(hMPQ);
            return -1;
        }
        fwrite(buffer, 1, readbyte, *fout);
        fsize -= transfersize;
    }
}

int f_Fcopy(FILE** fin, FILE** fout, DWORD fsize)
{
    FILE* Fin = *fin;
    FILE* Fout = *fout;

    char buffer[4096] = { 0 };

    while (fsize > 0) {
        DWORD transfersize = min(4096, fsize);
        DWORD readbyte;
        readbyte = fread(buffer, transfersize, 1, Fin);
        if (readbyte != 1) {
            printf("ReadFile Failed.\n");
            return -1;
        }
        fwrite(buffer, transfersize, 1, Fout);
        fsize -= transfersize;
    }
}

void f_Swrite(MPQHANDLE hMPQ, LPCSTR finName, LPCSTR MPQName)
{
    MpqAddFileToArchive(hMPQ, finName, MPQName, MAFA_COMPRESS);
}

void f_SwriteWav(MPQHANDLE hMPQ, LPCSTR finName, LPCSTR MPQName)
{
    MpqAddWaveToArchive(hMPQ, finName, MPQName, MAFA_COMPRESS, MAWA_QUALITY_LOW);
}

int getTotalLine(FILE* fp) {
    int line = 0;
    char c;
    while ((c = fgetc(fp)) != EOF)
        if (c == '\n') line++;
    return(line);
}

DWORD STRoff1 = 0;
DWORD STRoff2 = 0;
DWORD STRoff3 = 0;
DWORD Ret1 = 0;
DWORD Ret2 = 0;
void GetChkSection(FILE* fp, const char* Name)
{
    DWORD Key = Name[0] + Name[1] * 256 + Name[2] * 65536 + Name[3] * 16777216;
    int ret = 0, size;
    fseek(fp, 0, 2);
    size = ftell(fp);
    fseek(fp, 0, 0);

    DWORD Section[2];
    DWORD Check = 0;
    for (int i = 0; i < size;)
    {
        fseek(fp, i, 0);
        fread(Section, 4, 2, fp);
        if (Section[0] == Key)
        {
            Ret1 = i;
            Ret2 = i + (Section[1] + 8);
            Check = 1;
            break;
        }
        else
            i += (Section[1] + 8);
    }
    if (Check == 0)
    {
        printf("맵의 %s단락을 찾을 수 없습니다.", Name);
        Ret1 = -1;
    }
    return;
}

HRSRC ETResource, ETResource2;
HGLOBAL ETResourceData, ETResourceData2;
void* ETptr, *ETptr2;
DWORD PatchSTR(FILE* fin, FILE* fout)
{
    ETResource = ::FindResource(NULL, MAKEINTRESOURCE(IDR_ENCODE), RT_RCDATA);
    ETResourceData = ::LoadResource(NULL, ETResource);
    ETptr = ::LockResource(ETResourceData);

    ETResource2 = ::FindResource(NULL, MAKEINTRESOURCE(IDR_ENCODE2), RT_RCDATA);
    ETResourceData2 = ::LoadResource(NULL, ETResource2);
    ETptr2 = ::LockResource(ETResourceData2);
    
    if (ETptr == NULL || ETptr2 == NULL)
    {
        printf("cp949utf8.EncodeTable 로드에 실패했습니다.\n");
        return -1;
    }
    BYTE* ETArr = (BYTE*)ETptr;
    BYTE* ETArr2 = (BYTE*)ETptr2;

    int iSTRxflag = 0, oSTRxflag = 0, Mode, strnum, encflag;
    DWORD Size, Temp, retSize=0;
    WORD wTemp;
    BYTE bTemp, bTemp2[2], bTemp3[3];
    fseek(fin, 0, 2);
    Size = ftell(fin);
    fseek(fin, 0x8, 0);

    char* finArr = (char*)malloc(sizeof(char) * Size);
    fseek(fin, 0, 0);
    fread(finArr, 1, Size, fin);

    BYTE Buffer[4096] = { 0 };
    BYTE* Bptr = (BYTE*)Buffer;

    fseek(fin, 0, 0);
    fread(&Temp, 4, 1, fin);
    if ((Temp & 0xFF000000) >> 24 == 'x')
        iSTRxflag = 1;

    Retry:
    if (iSTRxflag == 0)
        printf("\n스트링단락 형식을 선택하세요 (현재 STR 단락)\n1. STR 단락\n2. STRx 단락\n");
    else
        printf("\n스트링단락 형식을 선택하세요 (현재 STRx 단락)\n1. STR 단락\n2. STRx 단락\n");
    scanf_s("%d", &Mode);
    if (Mode == 1)
        oSTRxflag = 0;
    else if (Mode == 2)
        oSTRxflag = 1;
    else
        goto Retry;
   
    Retry2:
    printf("인코딩 타입을 선택하세요 (미변환시 0을 입력)\n0. 변환하지 않음\n1. cp949 (scm 구버젼)\n2. utf8 (scm 신버젼)\n");
    scanf_s("%d", &Mode);
    if (Mode == 0)
        encflag = 0;
    else if (Mode == 1)
        encflag = 1;
    else if (Mode == 2)
        encflag = 2;
    else
        goto Retry2;

    fseek(fin, 8, 0);
    if (iSTRxflag == 0) // STR 
    {
        fread(&wTemp, 2, 1, fin);
        strnum = wTemp;
    }
    else // STRx
        fread(&strnum, 4, 1, fin);
    int *strloc = (int*)malloc(sizeof(int) * (strnum));
    int* strsize = (int*)malloc(sizeof(int) * (strnum));

    for (int i = 0; i < strnum; i++)
    {
        if (iSTRxflag == 0) // STR 
        {
            fseek(fin, 2 * i + 10, 0);
            fread(&wTemp, 2, 1, fin);
            strloc[i] = wTemp;
            strsize[i] = strlen(finArr + wTemp + 8);
        }
        else // STRx
        {
            fseek(fin, 4 * i + 12, 0);
            fread(&Temp, 4, 1, fin);
            strloc[i] = Temp;
            strsize[i] = strlen(finArr + Temp + 8);
        }
    }

    // write
    fseek(fout, 0, 0);
    if (oSTRxflag == 0) // STR
        Temp = (' ' << 24) + ('R' << 16) + ('T' << 8) + 'S';
    else // STRx
        Temp = ('x' << 24) + ('R' << 16) + ('T' << 8) + 'S';
    fwrite(&Temp, 4, 1, fout);
    fseek(fout, 8, 0);
    if (oSTRxflag == 0) // STR
    {
        if (strnum >= 0x10000)
        {
            printf("스트링 갯수가 65535개를 초과하여 STR형식으로 변환할 수 없습니다.\n");
            return -1;
        }
        wTemp = strnum;
        fwrite(&wTemp, 2, 1, fout);
    }
    else
    {
        Temp = strnum;
        fwrite(&Temp, 4, 1, fout);
    }

    if (oSTRxflag == 0) // STR
    {
        int cur = 11 + 2 * strnum;
        fseek(fin, cur - 1, 0);
        bTemp = 0;
        fwrite(&bTemp, 1, 1, fout);
        for (int i = 0; i < strnum; i++)
        {
            int size = strsize[i];
            if (size <= 0)
            {
                fseek(fout, 10 + 2 * i, 0);
                wTemp = 2 + 2 * strnum;
                fwrite(&wTemp, 2, 1, fout);
            }
            else
            {
                size++;
                fseek(fout, 10 + 2 * i, 0);
                if (cur >= 0x10008)
                {
                    printf("스트링 단락 용량이 65535bytes를 초과하여 STR형식으로 변환할 수 없습니다.\n");
                    return -1;
                }
                wTemp = cur - 0x8;
                fwrite(&wTemp, 2, 1, fout);
                
                fseek(fin, strloc[i] + 8, 0);
                fseek(fout, cur, 0);
                if (encflag == 1) // utf8 -> cp949
                {
                    int remain = size, pos = strloc[i] + 8;
                    while (remain > 0)
                    {
                        fseek(fin, pos, 0);
                        fread(&bTemp, 1, 1, fin);
                        if (bTemp >= 0xE0 && remain >= 3) // 3bytes
                        {
                            fseek(fin, pos, 0);
                            fread(bTemp3, 1, 3, fin);
                            
                            // EABCDF-> ACF0 + (B-8)*4+(D-8) (B,D = 8~B)
                            int loc = ((((bTemp3[0] & 0xF) << 12) + ((bTemp3[1] & 0xF) << 8) + ((bTemp3[2] & 0xF) << 4) + (((bTemp3[1] & 0xF0) - 0x80) >> 2) + (((bTemp3[2] & 0xF0) - 0x80) >> 4)) << 1) +0x2000;
                            if (loc >= 0x22000 || loc < 0x2000)
                            {
                                loc = 0x0;
                                printf("cp949로 변환할 수 없는 문자열이 존재합니다. 해당 문자열은 공백으로 변환됩니다.\n");
                            }
                            wTemp = ETArr2[loc] + (ETArr2[loc + 1] << 8);

                            fseek(fout, cur, 0);
                            fwrite(&wTemp, 2, 1, fout);
                            cur += 2;
                            pos += 3;
                            remain -= 3;
                        }
                        else if (bTemp >= 0x80 && remain >= 2) // 2bytes
                        {
                            fseek(fin, pos, 0);
                            fread(bTemp2, 1, 2, fin);

                            // ABCD - 0xC200
                            int loc = ((bTemp2[0] << 8) + bTemp2[1] - 0xC200) << 1;
                            if (loc >= 0x2000 || loc < 0)
                            {
                                loc = 0x0;
                                printf("cp949로 변환할 수 없는 문자열이 존재합니다. 해당 문자열은 공백으로 변환됩니다.\n");
                            }
                            wTemp = ETArr2[loc] + (ETArr2[loc + 1] << 8);

                            fseek(fout, cur, 0);
                            fwrite(&wTemp, 2, 1, fout);
                            cur += 2;
                            pos += 2;
                            remain -= 2;
                        }
                        else // 1byte
                        {
                            fseek(fout, cur, 0);
                            fwrite(&bTemp, 1, 1, fout);
                            cur++;
                            pos++;
                            remain--;
                        }
                    }
                }
                else if (encflag == 2) // cp949 -> utf8
                {
                    int remain = size, pos = strloc[i] + 8;
                    while (remain > 0)
                    {
                        fseek(fin, pos, 0);
                        fread(&bTemp, 1, 1, fin);
                        if (bTemp >= 0x80 && remain >= 2) // 2bytes
                        {
                            fseek(fin, pos, 0);
                            fread(bTemp2, 1, 2, fin);
                            int loc = ((bTemp2[0] << 8) + bTemp2[1] - 0x8141) << 2;
                            if (loc >= 0x1F2F8 || loc < 0)
                            {
                                loc = 0x9260;
                                printf("utf8로 변환할 수 없는 문자열이 존재합니다. 해당 문자열은 공백으로 변환됩니다.\n\n");
                            }

                            if (ETArr[loc + 2] == 0x0) // 2bytes
                            {
                                bTemp2[0] = ETArr[loc];
                                bTemp2[1] = ETArr[loc + 1];
                                
                                fseek(fout, cur, 0);
                                fwrite(bTemp2, 1, 2, fout);
                                cur += 2;
                            }
                            else // 3bytes
                            {
                                bTemp3[0] = ETArr[loc];
                                bTemp3[1] = ETArr[loc + 1];
                                bTemp3[2] = ETArr[loc + 2];
                                
                                fseek(fout, cur, 0);
                                fwrite(bTemp3, 1, 3, fout);
                                cur += 3;
                            } 
                            pos += 2;
                            remain -= 2;
                        }
                        else // 1byte
                        {
                            fseek(fout, cur, 0);
                            fwrite(&bTemp, 1, 1, fout);
                            cur++;
                            pos++;
                            remain--;
                        }
                    }
                }
                else // no enc
                {
                    f_Fcopy(&fin, &fout, size);
                    cur += size;
                }
            }
        }
        retSize = cur;
    }
    else
    {
        int cur = 13 + 4 * strnum;
        fseek(fin, cur - 1, 0);
        bTemp = 0;
        fwrite(&bTemp, 1, 1, fout);
        for (int i = 0; i < strnum; i++)
        {
            int size = strsize[i];
            if (size <= 0)
            {
                fseek(fout, 12 + 4 * i, 0);
                Temp = 4 + 4 * strnum;
                fwrite(&Temp, 4, 1, fout);
            }
            else
            {
                size++;
                fseek(fout, 12 + 4 * i, 0);
                Temp = cur - 0x8;
                fwrite(&Temp, 4, 1, fout);

                fseek(fin, strloc[i] + 8, 0);
                fseek(fout, cur, 0);
                if (encflag == 1) // utf8 -> cp949
                {
                    int remain = size, pos = strloc[i] + 8;
                    while (remain > 0)
                    {
                        fseek(fin, pos, 0);
                        fread(&bTemp, 1, 1, fin);
                        if (bTemp >= 0xE0 && remain >= 3) // 3bytes
                        {
                            fseek(fin, pos, 0);
                            fread(bTemp3, 1, 3, fin);

                            // EABCDF-> ACF0 + (B-8)*4+(D-8) (B,D = 8~B)
                            int loc = ((((bTemp3[0] & 0xF) << 12) + ((bTemp3[1] & 0xF) << 8) + ((bTemp3[2] & 0xF) << 4) + (((bTemp3[1] & 0xF0) - 0x80) >> 2) + (((bTemp3[2] & 0xF0) - 0x80) >> 4)) << 1) + 0x2000;
                            if (loc >= 0x22000 || loc < 0x2000)
                            {
                                loc = 0x0;
                                printf("cp949로 변환할 수 없는 문자열이 존재합니다. 해당 문자열은 공백으로 변환됩니다.\n");
                            }
                            wTemp = ETArr2[loc] + (ETArr2[loc + 1] << 8);

                            fseek(fout, cur, 0);
                            fwrite(&wTemp, 2, 1, fout);
                            cur += 2;
                            pos += 3;
                            remain -= 3;
                        }
                        else if (bTemp >= 0x80 && remain >= 2) // 2bytes
                        {
                            fseek(fin, pos, 0);
                            fread(bTemp2, 1, 2, fin);

                            // ABCD - 0xC200
                            int loc = ((bTemp2[0] << 8) + bTemp2[1] - 0xC200) << 1;
                            if (loc >= 0x2000 || loc < 0)
                            {
                                loc = 0x0;
                                printf("cp949로 변환할 수 없는 문자열이 존재합니다. 해당 문자열은 공백으로 변환됩니다.\n");
                            }
                            wTemp = ETArr2[loc] + (ETArr2[loc + 1] << 8);

                            fseek(fout, cur, 0);
                            fwrite(&wTemp, 2, 1, fout);
                            cur += 2;
                            pos += 2;
                            remain -= 2;
                        }
                        else // 1byte
                        {
                            fseek(fout, cur, 0);
                            fwrite(&bTemp, 1, 1, fout);
                            cur++;
                            pos++;
                            remain--;
                        }
                    }
                }
                else if (encflag == 2) // cp949 -> utf8
                {
                    int remain = size, pos = strloc[i] + 8;
                    while (remain > 0)
                    {
                        fseek(fin, pos, 0);
                        fread(&bTemp, 1, 1, fin);
                        if (bTemp >= 0x80 && remain >= 2) // 2bytes
                        {
                            fseek(fin, pos, 0);
                            fread(bTemp2, 1, 2, fin);
                            int loc = ((bTemp2[0] << 8) + bTemp2[1] - 0x8141) << 2;
                            if (loc >= 0x1F2F8 || loc < 0)
                            {
                                loc = 0x9260;
                                printf("utf8로 변환할 수 없는 문자열이 존재합니다. 해당 문자열은 공백으로 변환됩니다.\n\n");
                            }

                            if (ETArr[loc + 2] == 0x0) // 2bytes
                            {
                                bTemp2[0] = ETArr[loc];
                                bTemp2[1] = ETArr[loc + 1];

                                fseek(fout, cur, 0);
                                fwrite(bTemp2, 1, 2, fout);
                                cur += 2;
                            }
                            else // 3bytes
                            {
                                bTemp3[0] = ETArr[loc];
                                bTemp3[1] = ETArr[loc + 1];
                                bTemp3[2] = ETArr[loc + 2];

                                fseek(fout, cur, 0);
                                fwrite(bTemp3, 1, 3, fout);
                                cur += 3;
                            }
                            pos += 2;
                            remain -= 2;
                        }
                        else // 1byte
                        {
                            fseek(fout, cur, 0);
                            fwrite(&bTemp, 1, 1, fout);
                            cur++;
                            pos++;
                            remain--;
                        }
                    }
                }
                else // no enc
                {
                    f_Fcopy(&fin, &fout, size);
                    cur += size;
                }
            }
        }
        retSize = cur;
    }
  
    free(strloc);
    free(strsize);
    free(finArr);
    fseek(fout, 4, 0);
    Temp = retSize - 8;
    fwrite(&Temp, 4, 1, fout);
    if (oSTRxflag == 0)
        printf("\nSTR Section : All %d Strings / 0x%X bytes\n\n", strnum, retSize - 0x8);
    else
        printf("\nSTRx Section : All %d Strings / 0x%X bytes\n\n", strnum, retSize - 0x8);
    return retSize;
}

int main(int argc, char* argv[])
{
    FILE* fout, * lout;
    FILE* wout[1024];
    MPQHANDLE hMPQ;
    MPQHANDLE hFile;
    MPQHANDLE hList;
    MPQHANDLE hWav[1024];
    char* WavName[1024];
    int Wavptr = 0;

    // Open MPQ

    printf("--------------------------------------\n     。`+˚CS_STRConverter v1.3 。+.˚\n--------------------------------------\n\t\t\tMade By Ninfia\n");

    char* input = argv[1];

    //Test
    //char input2[] = "1.scx";
    //if (argc == 1)
    //  input = input2;
    //Test


    if (argc == 1) // Selected No file
    {
        printf("선택된 파일이 없습니다.\n");
        system("pause");
        return 0;
    }


    if (!SFileOpenArchive(input, 0, 0, &hMPQ))
    {
        if (argc > 2)
            printf("다수의 파일이 입력되었습니다. 입력된 파일들을 코드파일로 간주합니다.[%d개의 파일 입력됨]\n\n", argc - 1);
        else
            printf("맵파일이 아닙니다. 입력된 파일을 코드파일로 간주합니다.[%d]\n\n", GetLastError());
       
        ETResource = ::FindResource(NULL, MAKEINTRESOURCE(IDR_ENCODE), RT_RCDATA);
        ETResourceData = ::LoadResource(NULL, ETResource);
        ETptr = ::LockResource(ETResourceData);
        ETResource2 = ::FindResource(NULL, MAKEINTRESOURCE(IDR_ENCODE2), RT_RCDATA);
        ETResourceData2 = ::LoadResource(NULL, ETResource2);
        ETptr2 = ::LockResource(ETResourceData2);
        if (ETptr == NULL || ETptr2 == NULL)
        {
            printf("cp949utf8.EncodeTable 로드에 실패했습니다.\n\n");
            return -1;
        }
        BYTE* ETArr = (BYTE*)ETptr;
        BYTE* ETArr2 = (BYTE*)ETptr2;

        printf("인코딩 타입을 선택하세요.");
        int Mode, encflag;
    Retry3:
        printf("\n1. cp949 (scm 구버젼)\n2. utf8 (scm 신버젼)\n\n");
        scanf_s("%d", &Mode);
        if (Mode == 1)
            encflag = 1;
        else if (Mode == 2)
            encflag = 2;
        else
            goto Retry3;

        for (int k = 1; k < argc; k++)
        {
            input = argv[k];

            char iname[600];
            strcpy_s(iname, 512, input);
            int ilength = strlen(iname);

            strcpy_s(iname + ilength - 4, 512 - ilength - 4, "_out.scx");

            std::string instr, outstr;

            strcpy_s(iname, 512, input);
            int ilen = strlen(iname);
            char* rptr = strrchr(iname, '.');
            char ext[10];
            strcpy_s(ext, rptr);
            strcpy_s(rptr, 600 - ilen, "_out");
            strcat_s(iname, ext);

            FILE* fin, * fout;
            fopen_s(&fin, input, "rb");
            fopen_s(&fout, iname, "wb");

            DWORD size, Temp, retSize = 0;
            WORD wTemp;
            BYTE bTemp, bTemp2[2], bTemp3[3];
            fseek(fin, 0, 2);
            size = ftell(fin);
            fseek(fin, 0, 0);

            char buffer[8192] = { 0 };
            int fsize = size;
            while (fsize > 0)
            {
                if (fsize >= 8191)
                {
                    fread(buffer, 1, 8191, fin);
                    buffer[8191] = 0;
                    instr += buffer;
                    fsize -= 8191;
                }
                else
                {
                    fread(buffer, 1, fsize, fin);
                    buffer[fsize] = 0;
                    instr += buffer;
                    fsize = 0;
                }
            }
            printf("%s 로드됨 (%d bytes)\n", input, size);

            int remain = size, pos = 0, cur = 0;
            if (encflag == 1) // utf8 -> cp949
            {
                while (remain > 0)
                {
                    bTemp = instr[pos];
                    if (bTemp >= 0xE0 && remain >= 3) // 3bytes
                    {
                        memcpy(bTemp3, &(instr[pos]), 3);

                        // EABCDF-> ACF0 + (B-8)*4+(D-8) (B,D = 8~B)
                        int loc = ((((bTemp3[0] & 0xF) << 12) + ((bTemp3[1] & 0xF) << 8) + ((bTemp3[2] & 0xF) << 4) + (((bTemp3[1] & 0xF0) - 0x80) >> 2) + (((bTemp3[2] & 0xF0) - 0x80) >> 4)) << 1) + 0x2000;

                        if (loc >= 0x22000 || loc < 0x2000)
                        {
                            loc = 0x0;
                            printf("cp949로 변환할 수 없는 문자열이 존재합니다. 해당 문자열은 공백으로 변환됩니다.\n");
                        }
                        outstr += ETArr2[loc];
                        outstr += ETArr2[loc + 1];

                        cur += 2;
                        pos += 3;
                        remain -= 3;
                    }
                    else if (bTemp >= 0x80 && remain >= 2) // 2bytes
                    {
                        memcpy(bTemp2, &(instr[pos]), 2);

                        // ABCD - 0xC200
                        int loc = ((bTemp2[0] << 8) + bTemp2[1] - 0xC200) << 1;

                        if (loc >= 0x2000 || loc < 0)
                        {
                            loc = 0x0;
                            printf("cp949로 변환할 수 없는 문자열이 존재합니다. 해당 문자열은 공백으로 변환됩니다.\n");
                        }
                        outstr += ETArr2[loc];
                        outstr += ETArr2[loc + 1];

                        cur += 2;
                        pos += 2;
                        remain -= 2;
                    }
                    else // 1byte
                    {
                        outstr += bTemp;

                        cur++;
                        pos++;
                        remain--;
                    }
                }
            }
            else
            {
                while (remain > 0)
                {
                    bTemp = instr[pos];
                    if (bTemp >= 0x80 && remain >= 2) // 2bytes
                    {
                        memcpy(bTemp2, &(instr[pos]), 2);

                        int loc = ((bTemp2[0] << 8) + bTemp2[1] - 0x8141) << 2;
                        if (loc >= 0x1F2F8 || loc < 0)
                        {
                            loc = 0x9260;
                            printf("utf8로 변환할 수 없는 문자열이 존재합니다. 해당 문자열은 공백으로 변환됩니다.\n\n");
                        }

                        if (ETArr[loc + 2] == 0x0) // 2bytes
                        {
                            outstr += ETArr[loc];
                            outstr += ETArr[loc + 1];
                            cur += 2;
                        }
                        else // 3bytes
                        {
                            outstr += ETArr[loc];
                            outstr += ETArr[loc + 1];
                            outstr += ETArr[loc + 2];
                            cur += 3;
                        }
                        pos += 2;
                        remain -= 2;
                    }
                    else // 1byte
                    {
                        outstr += bTemp;

                        cur++;
                        pos++;
                        remain--;
                    }
                }
            }
            int curpos = 0;
            fsize = outstr.size();
            while (curpos < fsize)
            {
                if (fsize - curpos >= 8192)
                {
                    fwrite(outstr.data() + curpos, 1, 8192, fout);
                    curpos += 8192;
                }
                else
                {
                    fwrite(outstr.data() + curpos, 1, fsize - curpos, fout);
                    curpos = fsize;
                }
            }

            fseek(fout, 0, 2);
            retSize = ftell(fout);
            fseek(fout, 0, 0);

            printf("적용후 코드파일 : %s 로 저장됨 (%dbytes)\n\n", iname, retSize);

            fclose(fin);
            fclose(fout);
        }
    }
    else
    {
        char iname[600];
        strcpy_s(iname, 512, input);
        int ilength = strlen(iname);

        strcpy_s(iname + ilength - 4, 512 - ilength - 4, "_out.scx");

        printf("%s 의 MPQ 로드 완료\n", input);
        // Open Files
        f_Sopen(hMPQ, "(listfile)", &hList);
        f_Scopy(hMPQ, &hList, "(listfile).txt", &lout);
        printf("(listfile)을 불러와 맵 내부의 파일 목록을 읽는중\n");
        fseek(lout, 0, 0);
        char strTemp[512] = { 0 };
        int strLength, listline, line;
        listline = getTotalLine(lout);
        line = 0;
        fseek(lout, 0, 0);
        int chksize = 0;
        while (line < listline)
        {
            line++;
            fgets(strTemp, 512, lout);
            strLength = strlen(strTemp);
            strTemp[strLength - 2] = 0;
            if (!strcmp(strTemp, "staredit\\scenario.chk"))
            {
                f_Sopen(hMPQ, "staredit\\scenario.chk", &hFile);
                f_Scopy(hMPQ, &hFile, "scenario.chk", &fout);
                fseek(fout, 0, 2);
                chksize = ftell(fout);
                fseek(fout, 0, 0);
                fclose(fout);
                SFileCloseFile(hFile);
            }
            else
            {
                char* tmpBuffer = (char*)malloc(512);
                tmpnam_s(tmpBuffer, 512);
                WavName[Wavptr] = tmpBuffer;
                f_Sopen(hMPQ, strTemp, &hWav[Wavptr]);
                f_Scopy(hMPQ, &hWav[Wavptr], WavName[Wavptr], &wout[Wavptr]);
                fclose(wout[Wavptr]);
                SFileCloseFile(hWav[Wavptr]);
                Wavptr++;
            }

        }
        fclose(lout);
        SFileCloseFile(hList);
        SFileCloseArchive(hMPQ); // MPQ 닫기

        printf("scenario.chk %d bytes, 사운드 %d개 추출됨\n\n", chksize, Wavptr);

        // Patch STR(X)
        fopen_s(&fout, "scenario.chk", "rb");
        GetChkSection(fout, "STR ");
        if (Ret1 == -1)
            GetChkSection(fout, "STRx");
        STRoff1 = Ret1;
        STRoff2 = Ret2;
        fseek(fout, 0, 2);
        STRoff3 = ftell(fout);
        fseek(fout, 0, 0);
        //printf("%X %X %X\n", STRoff1, STRoff2, STRoff3);
        DWORD STRSizePrev = STRoff2 - STRoff1;

        FILE* fnew;
        fopen_s(&fnew, "scenario_new.chk", "wb");

        fseek(fout, 0, 0);
        f_Fcopy(&fout, &fnew, STRoff1 - 0); // 0 ~ STR prev

        fseek(fout, STRoff2, 0);
        f_Fcopy(&fout, &fnew, STRoff3 - STRoff2); // STR end ~ END

        FILE* STRi, * STRo;
        fopen_s(&STRi, "STRi.chk", "w+b");
        fopen_s(&STRo, "STRo.chk", "w+b");
        fseek(fout, STRoff1, 0);
        f_Fcopy(&fout, &STRi, STRoff2 - STRoff1);

        DWORD STRSize = PatchSTR(STRi, STRo);

        fseek(STRo, 0, 0);
        fseek(fnew, 0, 2);
        f_Fcopy(&STRo, &fnew, STRSize); // Copy STR
        chksize = ftell(fnew);

        fclose(fout);
        fclose(fnew);
        fclose(STRi);
        fclose(STRo);

        // Write MPQ
        char* out = iname;
        hMPQ = MpqOpenArchiveForUpdate(out, MOAU_CREATE_ALWAYS, 1024);
        if (hMPQ == INVALID_HANDLE_VALUE) { DeleteFileA(out);  return false; }

        // Write Files & Delete Temp
        f_Swrite(hMPQ, "(listfile).txt", "(listfile)");
        fopen_s(&lout, "(listfile).txt", "rb");

        Wavptr = 0;
        line = 0;
        while (line < listline)
        {
            line++;
            fgets(strTemp, 512, lout);
            strLength = strlen(strTemp);
            strTemp[strLength - 2] = 0;
            if (!strcmp(strTemp, "staredit\\scenario.chk"))
            {
                f_Swrite(hMPQ, "scenario_new.chk", "staredit\\scenario.chk");
                DeleteFileA("scenario.chk");
                DeleteFileA("scenario_new.chk");
                DeleteFileA("STRi.chk");
                DeleteFileA("STRo.chk");
            }
            else
            {
                f_SwriteWav(hMPQ, WavName[Wavptr], strTemp);
                DeleteFileA(WavName[Wavptr]);
                free(WavName[Wavptr]);
                Wavptr++;
            }
        }
        fclose(lout);
        DeleteFileA("(listfile).txt");
        MpqCloseUpdatedArchive(hMPQ, 0);

        printf("적용후 scenario.chk 의 크기 : %dbytes\n%s 로 저장됨\a\n", chksize, iname);
    }
    system("pause");
    return 0;
}