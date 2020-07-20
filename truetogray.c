#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct
{
    int width;
    int height;
    int stride;
    unsigned char *pdata;
}BMP ;

typedef struct {
    uint16_t Type;
    uint32_t FileSize;
    int Reserved;
    int DataOffset;
    int HeaderSize;
    int Width;
    int Height;
    uint16_t Planes;
    uint16_t BitsPerPixel;
    int Compression;
    int ImageSize;
    int XPelsPerMeter;
    int YPelsPerMeter;
    int ColorUsed;
    int ColorImportant;
} BMPFILEHEADER;

int bmp_load(BMP *pb, char *file)
{
    BMPFILEHEADER header = {0};
    FILE *fp = NULL;
    int i, j;
    fp = fopen(file, "rb");
    if(!fp)
    {
        printf("Read file error");
        return -1;
    }
    printf("loading file\n");
    // read bmp header
    fread(&header.Type, 1, 2, fp);
    fread(&header.FileSize, 1, 4, fp);
    fread(&header.Reserved, 1, 4, fp);
    fread(&header.DataOffset, 1, 4, fp);
    fread(&header.HeaderSize, 1, 4, fp);
    fread(&header.Width, 1, 4, fp);
    fread(&header.Height, 1, 4, fp);
    fread(&header.Planes, 1, 2, fp);
    fread(&header.BitsPerPixel, 1, 2, fp);
    fread(&header.Compression, 1, 4, fp);
    fread(&header.ImageSize, 1, 4, fp);
    fread(&header.XPelsPerMeter, 1, 4, fp);
    fread(&header.YPelsPerMeter, 1, 4, fp);
    fread(&header.ColorUsed, 1, 4, fp);
    fread(&header.ColorImportant, 1, 4, fp);

    // save the imformation taht we need
    pb->width = header.Width;
    pb->height = header.Height;
    pb->stride = (header.Width * 3 + 4 - 1)& ~(4-1); //pixel size * width
    //pb->pdata = (unsigned char*)malloc(pb->stride * pb->height);
    //pb->stride = header.Width * 3;
    pb->pdata = (unsigned char *)malloc(pb->stride * pb->height);

    printf("%d %d\n", header.DataOffset, header.BitsPerPixel);
    printf("%d %d\n", pb->stride, pb->height);

    int pos = 0;
    fseek(fp, header.DataOffset, SEEK_SET);
    pb->pdata += pb->stride*pb->height;
    for(i = 0; i < pb->height; i ++)
    {
        pb->pdata -= pb->stride;
        fread(pb->pdata, pb->stride, 1, fp);
    }

    fclose(fp);

    return pb->pdata?0:-1;
}

int bmp_save(BMP *bp, char *filename)
{
    int i, j;
    BMPFILEHEADER header;
    FILE* fp = fopen(filename, "wb");

    header.Type = 0x4D42;
    header.FileSize = 54 + 1024 + bp->width*bp->height;
    header.Reserved = 0;
    header.DataOffset = 54 + 1024;
    header.HeaderSize = 0x28;
    header.Width = bp->width;
    header.Height = bp->height;
    header.Planes = 1;
    header.BitsPerPixel = 8;
    header.Compression = 0;
    header.ImageSize = bp->width * bp->height;
    header.XPelsPerMeter = 0;
    header.YPelsPerMeter = 0;
    header.ColorUsed = 256;
    header.ColorImportant = 0;

    //write header
    fwrite(&header.Type, 1, 2, fp);
    fwrite(&header.FileSize, 1, 4, fp);
    fwrite(&header.Reserved, 1, 4, fp);
    fwrite(&header.DataOffset, 1, 4, fp);
    fwrite(&header.HeaderSize, 1, 4, fp);
    fwrite(&header.Width, 1, 4, fp);
    fwrite(&header.Height, 1, 4, fp);
    fwrite(&header.Planes, 1, 2, fp);
    fwrite(&header.BitsPerPixel, 1, 2, fp);
    fwrite(&header.Compression, 1, 4, fp);
    fwrite(&header.ImageSize, 1, 4, fp);
    fwrite(&header.XPelsPerMeter, 1, 4, fp);
    fwrite(&header.YPelsPerMeter, 1, 4, fp);
    fwrite(&header.ColorUsed, 1, 4, fp);
    fwrite(&header.ColorImportant, 1, 4, fp);

    for(int i = 0; i < 256; i ++)
    {
        fwrite(&i, sizeof(unsigned char), 1, fp);
        fwrite(&i, sizeof(unsigned char), 1, fp);
        fwrite(&i, sizeof(unsigned char), 1, fp);
        fwrite("", sizeof(unsigned char), 1, fp);
    }

    int alig = ((bp->width)*3) % 4;
    for(i = bp->height - 1; i >= 0; i --)
    {
        for(j = 0; j < bp->width; j ++)
        {
            unsigned char tmp = 0.114 * bp->pdata[i*bp->stride + j*3] + 0.587 * bp->pdata[i*bp->stride + j*3+1] + 0.299*bp->pdata[i*bp->stride + j*3+2];
            if(i == 0 && j == 0)
            printf("%d %d %d %d", tmp,  bp->pdata[i*bp->stride + j*3+2],  bp->pdata[i*bp->stride + j*3+1],  bp->pdata[i*bp->stride + j*3]);
            fputc(tmp, fp);
        }
        for(j =0; j < alig; j ++)
        {
            fwrite("", sizeof(unsigned char), 1, fp);
        }
    }

    fclose(fp);
}

int main(int argc, char *argv[])
{
    char filename[20], output[20];
    //printf("%s\n", argv[1]);
    /*if(argc == 1)
    {
        strcpy(filename, argv[1]);
        printf("%s\n", filename);
    }*/
    BMP bmp = {0};
    int handle = bmp_load(&bmp, argv[1]);

    bmp_save(&bmp, argv[2]);

    if(handle == 1)
        printf("It's already 24 bit\n");
    else if(handle == 0)
        printf("success\n");
    else
        printf("failed\n");

}