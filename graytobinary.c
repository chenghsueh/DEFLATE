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

typedef struct {
    int bit_pos, stream_size;
    unsigned char bytebuf;
    unsigned char* databuf;
    FILE *f;
} Stream;

void init_Stream(Stream *stream)
{
    stream->bit_pos = 8;
    stream->bytebuf = 0;
    stream->stream_size = 0;
}

void Put_Stream(Stream *stream, int value, int size)
{
    int  shift;
    unsigned char tmp;
    //printf("initial : %d, %d\n", value, size);
    while(size > 0)
    {
        shift = size - stream->bit_pos;
        //printf("%d\n", shift);
        if(shift >= 0)
        {
            tmp = value >> shift;
            size -= stream->bit_pos;
            stream->bit_pos = 8;
            //printf("%x, %d\n", tmp, stream->bit_pos);
        }
        else
        {
            tmp = value << (-1*shift);
            stream->bit_pos -= size;
            size = 0;
            //printf("%x, %d\n", tmp, stream->bit_pos);
        }
        stream->bytebuf += tmp;
        //printf("%x, %d\n", stream->bytebuf, stream->bit_pos);
        if(stream->bit_pos == 8)
        {
            stream->databuf[stream->stream_size] = stream->bytebuf;
            //printf("stream output : %x, %d\n", stream->bytebuf, stream->stream_size);
            stream->stream_size ++;
            /*if(stream->bytebuf == 0xff)
            {
                stream->databuf[stream->stream_size] = 0x00;
                stream->stream_size ++;
            }*/
            stream->bytebuf = 0;
        }
    }
}

void End_Stream(Stream *stream)
{
    if(stream->bit_pos == 8)
        return;

    unsigned char tmp = (1 << stream->bit_pos) - 1;
    stream->bytebuf += tmp;
    stream->databuf[stream->stream_size] = stream->bytebuf;
    stream->stream_size ++;
    if(stream->bytebuf == 0xff)
    {
        stream->databuf[stream->stream_size] = 0x00;
        stream->stream_size ++;
    }
    stream->bytebuf = 0;
    stream->bit_pos = 8;
}

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
    //pb->stride = (header.Width * 3 + 4 - 1)& ~(4-1); //pixel size * width
    int align = ((pb->width*header.BitsPerPixel/8)*3) % 4;
    pb->stride = pb->width + align;
    //pb->pdata = (unsigned char*)malloc(pb->stride * pb->height);
    //pb->stride = header.Width * 3;
    pb->pdata = (unsigned char *)malloc(pb->stride * pb->height);

    printf("%d %d\n", header.DataOffset, header.BitsPerPixel);
    printf("%d %d\n", pb->stride, pb->height);
    printf("%d\n", pb->stride * pb->height);

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
    if(!fp)
    {
        printf("Read file error");
        return -1;
    }
    Stream out;

    init_Stream(&out);
    out.databuf = (unsigned char*)malloc(bp->stride*bp->height);
    printf("init stream\n");


    header.Type = 0x4D42;
    header.FileSize = 54 + 8 + bp->width*bp->height/8;
    header.Reserved = 0;
    header.DataOffset = 54 + 8;
    header.HeaderSize = 0x28;
    header.Width = bp->width;
    header.Height = bp->height;
    header.Planes = 1;
    header.BitsPerPixel = 1;
    header.Compression = 0;
    header.ImageSize = bp->width * bp->height / 8;
    header.XPelsPerMeter = 0;
    header.YPelsPerMeter = 0;
    header.ColorUsed = 2;
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

    i = 0;
    fwrite(&i, sizeof(unsigned char), 1, fp);
    fwrite(&i, sizeof(unsigned char), 1, fp);
    fwrite(&i, sizeof(unsigned char), 1, fp);
    fwrite("", sizeof(unsigned char), 1, fp);
    i = 255;
    fwrite(&i, sizeof(unsigned char), 1, fp);
    fwrite(&i, sizeof(unsigned char), 1, fp);
    fwrite(&i, sizeof(unsigned char), 1, fp);
    fwrite("", sizeof(unsigned char), 1, fp);

    int alig = ((bp->width*header.BitsPerPixel/8)*3) % 4;
    printf("%d %d %d %d\n", alig, bp->height, bp->stride, bp->width);
    //printf("%d\n", bp->pdata[(bp->height-1)*bp->stride+223]);
    for(i = bp->height - 1; i >= 0; i --)
    {
        for(int k = 0; k < bp->width; k ++)
        {
            //printf("%d %d %d %d",i, k, bp->pdata[i*bp->stride + k], i*bp->stride + k);
            if(bp->pdata[i*bp->stride + k] == 255){
                Put_Stream(&out, 1, 1);
                //printf("%d ", k);
            }
            else{
                //printf("123");
                Put_Stream(&out, 0, 1);
                //printf("%d ", k);
            }
            //if(i == bp->height -2)
            //printf("%d ", k);
        }
        //End_Stream(&out);
        for(j =0; j < 1; j ++)
        {
            Put_Stream(&out, 0, 12);
        }
        //printf("%d ", i);
        //End_Stream(&out);
    }

    fwrite(out.databuf, out.stream_size, 1, fp);
    //printf("123");

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

    printf("%d\n", handle);

    bmp_save(&bmp, argv[2]);

    if(handle == 1)
        printf("It's already 24 bit\n");
    else if(handle == 0)
        printf("success\n");
    else
        printf("failed\n");

}