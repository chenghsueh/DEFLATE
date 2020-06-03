#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define INPUT_BLOCK_SIZE 32768
#define HASH_TABLE_BITS 15
#define HASH_TABLE_SIZE (1 << HASH_TABLE_BITS)
#define HASH_TABLE_MAX_LIST_LEN 20
#define LZ_MIN_MATCH_LEN 3
#define STATIC_HUFFMAN_TYPE 1


int read_bytes = 0;
typedef struct
{
    int width;
    int height;
    int stride;
    unsigned char *pdata;
}BMP ;

typedef struct {
    int bit_pos, stream_size;
    unsigned char bytebuf;
    unsigned char* databuf;
    FILE *f;
} Stream;

typedef struct {
    int* prev;
    int size;
    int max_size;
    int start_pos;
    int next_pos;
}Hash_chain;

typedef struct {
    int distance;
    int length;
    int literal;
}LZ_Value;

typedef struct __lz_queue_node{
    LZ_Value value;
    struct __lz_queue_node *next;
}LZ_Queue_Node;

typedef struct {
    LZ_Queue_Node *head;
    LZ_Queue_Node *tail;
}LZ_Queue;

typedef struct {
    int freq;
    int code;
    int code_len;
    int dad;
}Huffman_Table;

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

static const int huffman_lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                     35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};

static const int huffman_len_bits[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 
                                         3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

static const int huffman_dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257,
                                      385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
                                      16385, 24577};

static const int huffman_dist_bits[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 
                                          7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static const int static_huffman_init_values[4] = {48, 400, 0, 192};

static const unsigned char static_huffman_length[4] = {8, 9, 7, 8};

static const int bl_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2 ,4, 1 ,15};

void init_Hash_Table(Hash_chain* table, int max_size)
{
    int i;
    for(i = 0; i < HASH_TABLE_SIZE; i ++)
    {
        table[i].prev = NULL;
        table[i].max_size = max_size;
    }
}

void init_Hash_Chain(Hash_chain *entry)
{
    entry->prev = (int *)malloc(sizeof(int)*entry->max_size);
    entry->size = 0;
    entry->start_pos = 0;
    entry->next_pos = 0;
}

void init_Huffman_Table(Huffman_Table* lit_table, Huffman_Table* dis_table, Huffman_Table* bl_table)
{
    int i = 0;
    for(i = 0; i < 256+1+29; i ++)
        lit_table[i].freq = 0;
    for(i = 0; i < 30; i ++)
        dis_table[i].freq = 0;
    for(i = 0; i < 19; i ++)
        bl_table[i].freq = 0;

    lit_table[256].freq = 1;
}

void Huffman_Table_reset(Huffman_Table* lit_table, Huffman_Table* dis_table)
{
    int i = 0;
    for(i = 0; i < 256+1+29; i ++)
    {
        lit_table[i].freq = 0;
    }
    for(i = 0; i < 30; i ++)
    {
        dis_table[i].freq = 0;
    }
    
}

void Heap_Insert(int *heap, int *depth, int heap_len, Huffman_Table* table, int n)
{
    int v = heap[n];
    int j = n << 1;
    //printf("%d %d", j, heap_len);
    while(j <= heap_len)
    {
        //printf("%d %d %d\n", table[heap[j]].freq, table[heap[j+1]].freq, table[v].freq);
        if(j < heap_len)
        {
            if(table[heap[j+1]].freq < table[heap[j]].freq || 
            (table[heap[j+1]].freq == table[heap[j]].freq && depth[heap[j+1]] <= depth[heap[j]]))
            {
                j ++;
                //printf("123");
            }
        }
        if(table[v].freq < table[heap[j]].freq || 
        (table[v].freq == table[heap[j]].freq && depth[v] <= depth[heap[j]]))
        {
            //printf("456");
            break;
        }

        heap[n] = heap[j];
        n = j;

        j = j << 1;
        //printf("%d \n", j);
    }
    //printf("123");
    heap[n] = v;
}

void init_LZ_Queue(LZ_Queue *q)
{
    q->head = NULL;
    q->tail = NULL;
}

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

bool Get_bit(Stream *stream)
{
    if(stream->bit_pos == 0)
    {
        //stream->bytebuf = fgetc(stream->f);
        int read_error = fread(&stream->bytebuf, sizeof(unsigned char), 1, stream->f);
        read_bytes ++;
        //if(read_error == 0)
            //printf("No Data to read\n");
        /*if(stream->bytebuf == 0xff)
        {
            unsigned char check = fgetc(f);
            if(check != 0x00)
            {
                printf("error :%x \n", check);
                return 0;
            }
        }*/
    }
    bool bit = stream->bytebuf & (1 << (7 - stream->bit_pos));
    stream->bit_pos = (stream->bit_pos == 7 ? 0 : stream->bit_pos + 1);

    return bit;
}

int Get_Stream(Stream *stream, int size)
{
    int i;
    int buf = 0;
    for(i = 0; i < size; i ++)
    {
        buf = buf << 1;
        buf += (int)Get_bit(stream);
    }

    return buf;
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
}

int bmp_load(BMP *pb, char *file)
{
    BMPFILEHEADER header = {0};
    FILE *fp = NULL;
    int i;
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
    //pb->pdata = (unsigned char*)malloc(pb->stride * pb->height);
    pb->stride = header.Width * 3;
    pb->pdata = (unsigned char *)malloc(pb->stride * pb->height);

    printf("%d %d\n", header.DataOffset, header.BitsPerPixel);

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

void File_Save(Stream *stream)
{
    FILE *fp = fopen("encode", "wb");
    fwrite(stream->databuf, stream->stream_size, 1, fp);
    fclose(fp);
}

int Get_Hash_key(unsigned char* str)
{
    int hash_mask = (1 << HASH_TABLE_BITS) - 1;
    int hash_shift = 5;
    int i, key;

    for(i = 0; i < 3; i ++)
    {
        unsigned char tmp = (str[i] >> 8 - hash_shift);
        key = ((key << hash_shift) ^ tmp) & hash_mask;
    }

    return key;
}

void Put_Hash_Table(Hash_chain* entry, int value)
{
    if(entry->prev == NULL)
        init_Hash_Chain(entry);
    //printf("init hash chain sucess\n");
    
    entry->prev[entry->next_pos] = value;
    //printf("%d\n", entry->max_size);
    entry->next_pos = (entry->next_pos + 1) % entry->max_size;
    //printf("wtf2\n");

    if(entry->size < entry->max_size)
        entry->size ++;
    else
    {
        //printf("wtf3\n");
        entry->start_pos = (entry->start_pos + 1) % entry->max_size;
    }
    //printf("wtf\n");
}

void Hash_Table_reset(Hash_chain* table)
{
    for(int i = 0; i < HASH_TABLE_SIZE; i ++)
    {
        if(table[i].prev)
        {
            free(table[i].prev);
            table[i].prev = NULL;
        }
    }
}

void LZ_enqueue(LZ_Queue *q, unsigned char literal, int length, int distance)
{
    LZ_Queue_Node *new_node = (LZ_Queue_Node*)malloc(sizeof(LZ_Queue_Node));
    
    new_node->value.literal = literal;
    new_node->value.length = length;
    new_node->value.distance = distance;

    if(q->head == NULL)
    {
        q->head = new_node;
        q->tail = q->head;
        new_node->next = NULL;
    }
    else
    {
        new_node->next = q->tail->next;
        q->tail->next = new_node;
        q->tail = new_node;
    }
}

LZ_Value LZ_dequeue(LZ_Queue *q)
{
    LZ_Value node;

    if(q->head == q->tail)
    {
        if(q->head != NULL){
            node = q->head->value;
            free(q->head);
            q->head = NULL;
            q->tail = NULL;
        }
    }
    else
    {
        node = q->head->value;
        LZ_Queue_Node *tmp = q->head;
        q->head = q->head->next;
        free(tmp);
    }

    return node;
}

void huffman_get_litieral_code(int literal,int *size, int *code)
{
    if(literal <= 143)
    {
        *size = static_huffman_length[0];
        *code = static_huffman_init_values[0] + literal;
    }
    else if(literal <= 255)
    {
        *size = static_huffman_length[1];
        *code = static_huffman_init_values[1] + literal - 144;
    }
    else if(literal <= 279)
    {
        *size = static_huffman_length[2];
        *code = static_huffman_init_values[2] + literal - 256;
    }
    else
    {
        *size = static_huffman_length[3];
        *code = static_huffman_init_values[3] + literal - 280;
    }
}

int Get_length_code(int length)
{
    int i = 0;
    while(huffman_lens[i] < length && i < 29)
        i ++;
    if(huffman_lens[i] != length)
        i --;

    return i;
}

int Get_dist_code(int distance)
{
    int i = 0;
    while(huffman_lens[i] < distance && i < 30)
        i ++;
    if(huffman_lens[i] != distance)
        i --;

    return i;
}

void gen_bitlen(Huffman_Table* table, int *bl_count, int *heap, int heap_max, int max_code)
{
    int i, j, k;
    int bits;
    int overflow = 0;
    int heap_size = 2 * (256+1+29) + 1;
    for(i = 0; i < 15; i ++)
    {
        bl_count[i] = 0;
    }

    table[heap[heap_max]].code_len = 0;

    for(i = heap_max + 1; i < heap_size; i ++)
    {
        bits = table[table[heap[i]].dad].code_len + 1;
        if(bits > 15)
        {
            bits = 15;
            overflow ++;
        }
        table[heap[i]].code_len = bits;

        if(heap[i] > max_code) continue;

        bl_count[bits] ++;
    }
    if(overflow == 0){
        printf("no overflow\n");
        return;
    }
    
    do
    {
        bits = 15 - 1;
        while (bl_count[bits] == 0) bits --;
        bl_count[bits] --;
        bl_count[bits+1] += 2;
        bl_count[15] --;

        overflow -= 2;
    } while(overflow > 0);
    printf("123\n");

    k = heap_size;
    for(bits = 15; bits != 0; bits --)
    {
        i = bl_count[bits];
        while(i != 0)
        {
            j = heap[--k];
            if(j > max_code) continue;
            if((unsigned) table[j].code_len != (unsigned) bits)
            {
                table[j].code_len = bits;
            }
            i --;
        }
    }
}

void gen_code(Huffman_Table* table, int *bl_count, int max_code)
{
    unsigned char next_code[16];
    unsigned char code = 0;
    int bits;
    int n;

    for(bits = 1; bits <= 15; bits ++)
    {
        code = (code + bl_count[bits-1]) << 1;
        next_code[bits] = code;
    }

    for(n = 0; n <= max_code; n ++)
    {
        int len = table[n].code_len;
        if(len == 0) continue;
        table[n].code = next_code[len]++;
    }
}

void Build_Huffman_tree(Huffman_Table* table, int size, int *huf_max_code)
{
    int heap_len = 0;
    int heap[2*size+1];
    int depth[2*size+1];
    int bl_count[16];
    int max = 2*size+1;
    int max_code;
    int i;

    for(i = 0; i < size; i ++)
    {
        if(table[i].freq != 0)
        {
            heap[++(heap_len)] = i;
            max_code = i;
            depth[i] = 0;
            //printf("%d %d\n", i, table[i].freq);
        }
        else
        {
            table[i].code_len = 0;
        }
    }
    printf("maxcode= %d, heap_len= %d\n", max_code, heap_len);
    *huf_max_code = max_code;

    for(i = heap_len/2; i >= 1; i --)
    {
        //printf("%d times:\n", i);
        Heap_Insert(heap, depth, heap_len, table, i);
        /*for(int j = 1; j <= 10; j ++)
        {
            printf("%d %d\n", heap[j], table[heap[j]].freq);
        }*/
    }

    int node = size;
    while(heap_len >= 2)
    {
        int n, m;
        n = heap[1];
        heap[1] = heap[heap_len --];
        Heap_Insert(heap, depth, heap_len, table, 1);
        m = heap[1];

        heap[--max] = n;
        heap[--max] = m;

        //printf("choose : %d %d\n", table[n].freq, table[m].freq);

        table[node].freq = table[n].freq + table[m].freq;
        depth[node] = (depth[n] >= depth[m] ? depth[n] : depth[m]) + 1;
        table[n].dad = table[m].dad = node;

        heap[1] = node;
        node ++;
        Heap_Insert(heap, depth, heap_len, table, 1);

    }
    
    heap[--max] = heap[1];
    /*printf("max= %d\n", max);
    for(i = max; i < 2*size+1; i ++)
    {
        printf("%d \n", table[heap[i]].freq);
    }*/

    gen_bitlen(table, bl_count, heap, max, max_code);
    for(i = 0; i < 16; i ++)
    {
        printf("%d ", bl_count[i]);
    }
    printf("\n");
    gen_code(table, bl_count, max_code);
    /*for(i = 0; i < 10; i ++)
    {
        printf("%d ", table[i].code);
    }*/
}

void Scan_Huffman_tree(Huffman_Table* table, Huffman_Table* bl_tree, int max_code)
{
    int i;
    int prevlen = -1;
    int curlen;
    int nextlen = table[0].code_len;
    int count = 0;
    int min_count = 4;
    int max_count = 7;
    
    if(nextlen == 0)
    {
        max_count = 138;
        min_count = 3;
    }
    table[max_code+1].code_len = (unsigned char)0xffff;

    for(i = 0; i <= max_code; i ++)
    {
        curlen=nextlen;
        nextlen = table[i+1].code_len;
        count ++;
        if(curlen == nextlen && count < max_count)
            continue;
        else if(count < min_count)
            bl_tree[curlen].freq += count;
        else if(curlen != 0)
        {
            if(curlen != prevlen)
                bl_tree[curlen].freq ++;
            bl_tree[16].freq ++;
        }
        else if(count <= 10)
            bl_tree[17].freq ++;
        else
            bl_tree[18].freq ++;
        
        count = 0;
        prevlen = curlen;
        if(nextlen == 0)
        {
            max_count = 138;
            min_count = 3;
        }
        else if(curlen == nextlen)
        {
            max_count = 6;
            min_count = 3;
        }
        else
        {
            max_count = 7;
            min_count = 4;
        }
    }
}

int Build_Huffman_bl_tree
(Huffman_Table* lit_huf_tree, Huffman_Table* dis_huf_tree, Huffman_Table* bl_tree, int lit_max_code, int dis_max_code)
{
    int *bl_max_code;
    int i;
    Scan_Huffman_tree(lit_huf_tree, bl_tree, lit_max_code);
    Scan_Huffman_tree(dis_huf_tree, bl_tree, dis_max_code);

    Build_Huffman_tree(bl_tree, 19, bl_max_code);

    for(i = 18; i >= 3; i --)
    {
        if(bl_tree[bl_order[i]].code_len != 0)
            break;
    }

    return i;
}

void Deflate_process_huffman_tree(Stream *OutStream, Huffman_Table* table, Huffman_Table* bl_table, int max_code)
{
    int i, j;
    int prevlen = -1;
    int curlen;
    int nextlen = table[0].code_len;
    int count = 0;
    int min_count = 4;
    int max_count = 7;
    
    if(nextlen == 0)
    {
        max_count = 138;
        min_count = 3;
    }
    table[max_code+1].code_len = (unsigned char)0xffff;

    for(i = 0; i <= max_code; i ++)
    {
        curlen=nextlen;
        nextlen = table[i+1].code_len;
        count ++;
        if(curlen == nextlen && count < max_count)
            continue;
        else if(count < min_count)
        {
            while(count > 1)
            {
                Put_Stream(OutStream, bl_table[curlen].code, bl_table[curlen].code_len);
                count--;
            }
            count --;
        }
        else if(curlen != 0)
        {
            if(curlen != prevlen)
            {
                Put_Stream(OutStream, bl_table[curlen].code, bl_table[curlen].code_len);
            }
            Put_Stream(OutStream, bl_table[16].code, bl_table[16].code_len);
            Put_Stream(OutStream, count-3, 2);
        }
        else if(count <= 10)
        {
            Put_Stream(OutStream, bl_table[17].code, bl_table[17].code_len);
            Put_Stream(OutStream, count-3, 3);
        }
        else
        {
            Put_Stream(OutStream, bl_table[18].code, bl_table[18].code_len);
            Put_Stream(OutStream, count-11, 7);
        }
        
        count = 0;
        prevlen = curlen;
        if(nextlen == 0)
        {
            max_count = 138;
            min_count = 3;
        }
        else if(curlen == nextlen)
        {
            max_count = 6;
            min_count = 3;
        }
        else
        {
            max_count = 7;
            min_count = 4;
        }
    }
}

void Deflate_process_queue(LZ_Queue *queue, Stream *OutStream, bool last_block, bool StaticOrDynamic,
Huffman_Table* l_table, Huffman_Table* d_table, Huffman_Table* bl_table, int l_max_code, int d_max_code, int bl_max_code)
{
    int i = 0;
    if(last_block == true)
        Put_Stream(OutStream, 1, 1);
    else
    {
        Put_Stream(OutStream, 0, 1);
    }
    
    if(StaticOrDynamic == false)
        Put_Stream(OutStream, 1, 2);
    else
        Put_Stream(OutStream, 2, 2);
    printf("last block = %d \n", last_block);
    //printf("123\n");
    //printf("%d, %d, %d\n", queue->head->value.literal, queue->head->value.length, queue->head->value.distance);
    
    if(StaticOrDynamic)
    {
        Put_Stream(OutStream, l_max_code-257, 5);
        Put_Stream(OutStream, d_max_code-1, 5);
        Put_Stream(OutStream, bl_max_code-4, 4);
        
        for(i = 0; i < bl_max_code; i ++)
        {
            Put_Stream(OutStream, bl_table[bl_order[i]].code_len, 3);
        }

        Deflate_process_huffman_tree(OutStream, l_table, bl_table, l_max_code-1);
        Deflate_process_huffman_tree(OutStream, d_table, bl_table, d_max_code-1);
    }

    while(queue->head != NULL)
    {
        i ++;
        LZ_Value value = LZ_dequeue(queue);
        int size, code = 0;
        //if(i == 1)
            //printf("%d %d %d\n", value.literal, value.length, value.distance);
        if(value.length == 0)
        {
            if(StaticOrDynamic == false)
                huffman_get_litieral_code((int)value.literal, &size, &code);
            else
            {
                code = l_table[value.literal].code;
                size = l_table[value.literal].code_len;
            }
            Put_Stream(OutStream, code, size);
            //if(i == 1)
                //printf("%d %d, %x\n",value.literal, size, code);
        }
        else
        {
            int i = 0;
            while(huffman_lens[i] < value.length && i < 29)
                i ++;
            if(huffman_lens[i] != value.length)
                i --;
            if(StaticOrDynamic == false)
                huffman_get_litieral_code(257+i, &size, &code);
            else
            {
                code = l_table[257+i].code;
                size = l_table[257+i].code_len;
            }
            Put_Stream(OutStream, code, size);
            code = value.length - huffman_lens[i];
            size = huffman_len_bits[i];
            Put_Stream(OutStream, code, size);
            
            i = 0;
            while(huffman_dists[i] < value.distance && i < 30)
                i ++;
            if(huffman_dists[i] != value.distance)
                i --;
            if(StaticOrDynamic == false)
                Put_Stream(OutStream, i, 5);
            else
            {
                code = d_table[i].code;
                size = d_table[i].code_len;
            }
            code = value.distance - huffman_dists[i];
            size = huffman_dist_bits[i];
            Put_Stream(OutStream, code, size);
        }
        //printf("123");
    }

    Put_Stream(OutStream, 0, 7);
} 

void Inflate_process_queue(LZ_Queue *queue, FILE *f)
{
    unsigned char buf[INPUT_BLOCK_SIZE];
    int buf_size = 0;

    while(queue->head != NULL)
    {
        //printf("123\n");
        LZ_Value value = LZ_dequeue(queue);

        if(value.length == 0)
        {
            buf[buf_size] = value.literal;
            buf_size ++;
        }
        else
        {
            int pos = buf_size - value.distance;
            for(int i = 0; i < value.length; i ++)
            {
                buf[buf_size] = buf[pos+i];
                buf_size ++;
            }
        }
    }
    fwrite(buf, sizeof(unsigned char), buf_size, f);
}

void deflate(BMP *pb)
{
    int data_tag = 0;
    Stream out;
    init_Stream(&out);
    out.databuf = (unsigned char* )malloc(sizeof(unsigned char)*pb->stride*pb->height);
    unsigned char *block = (unsigned char*)malloc(INPUT_BLOCK_SIZE*sizeof(unsigned char));
    Hash_chain* lookup_table = (Hash_chain*)malloc(HASH_TABLE_SIZE*sizeof(Hash_chain));
    Huffman_Table* lit_huf_tab = (Huffman_Table*)malloc(((256+29+1)*2+1)*sizeof(Huffman_Table));
    Huffman_Table* dis_huf_tab = (Huffman_Table*)malloc(((30*2)+1)*sizeof(Huffman_Table));
    Huffman_Table* bl_tab = (Huffman_Table*)malloc(((19*2)+1)*sizeof(Huffman_Table));
    LZ_Queue lz_queue;

    init_Huffman_Table(lit_huf_tab, dis_huf_tab, bl_tab);
    init_Hash_Table(lookup_table, HASH_TABLE_MAX_LIST_LEN);
    init_LZ_Queue(&lz_queue);

    int data_size = pb->stride*pb->height;
    bool last_block = false;
    bool StaticOrDynamic = true;
    int block_size = 0;
    unsigned char cur_buf[3];

    if(data_size - data_tag >= INPUT_BLOCK_SIZE){
        block_size = INPUT_BLOCK_SIZE;
        memcpy(block, pb->pdata + data_tag, block_size);
        data_tag += INPUT_BLOCK_SIZE;
    }
    else
    {
        block_size = data_size - data_tag;
        memcpy(block, pb->pdata + data_tag, block_size);
        data_tag = data_size;
    }
    printf("read start %d %d\n", data_size, pb->stride);
    int block_num = 0;
    while (data_tag <= data_size && !last_block)
    {
        block_num ++;
        if(data_tag == data_size)
            last_block = true;

        int lab_start = 0;
        while(lab_start < block_size)
        {
            int i = 0;
            while(lab_start + i < block_size && i < 3)
            {
                cur_buf[i] = block[lab_start+i];
                i ++;
            }

            if(i < 3)
            {
                //printf("123");
                int j = 0;
                for(j = 0; j < i; j ++){
                    LZ_enqueue(&lz_queue, cur_buf[j], 0, 0);
                    lit_huf_tab[(int)cur_buf[j]].freq ++;
                }
                
                break;
            }
            else
            {
                Hash_chain *chain = &lookup_table[Get_Hash_key(cur_buf)];

                if(chain->prev == NULL)
                {
                    Put_Hash_Table(&lookup_table[Get_Hash_key(cur_buf)], lab_start);
                    LZ_enqueue(&lz_queue, cur_buf[0], 0, 0);
                    lit_huf_tab[(int)cur_buf[0]].freq ++;
                    lab_start ++;
                }
                else
                {
                    //printf("123");
                    int j;
                    int longest_match_len = 0;
                    int longest_match_pos = -1;

                    for(j = 0; j < chain->size; j ++)
                    {
                        int match_pos = chain->prev[(chain->start_pos + j) % chain->max_size];
                        int len_tmp = 0;
                        while(block[match_pos+len_tmp] == block[lab_start+len_tmp] &&
                              lab_start+len_tmp < block_size && len_tmp < 258)
                            len_tmp ++;
                        
                        if(len_tmp > longest_match_len)
                        {
                            longest_match_len = len_tmp;
                            longest_match_pos = match_pos;
                        }
                    }

                    if(longest_match_len < LZ_MIN_MATCH_LEN)
                    {
                        LZ_enqueue(&lz_queue, cur_buf[0], 0, 0);
                        Put_Hash_Table(&lookup_table[Get_Hash_key(cur_buf)], lab_start);
                        lit_huf_tab[(int)cur_buf[0]].freq ++;
                        lab_start ++;
                    }
                    else
                    {
                        int distance = lab_start - longest_match_pos;
                        //if(lab_start > 1000 && lab_start <= 2000 && data_tag <= INPUT_BLOCK_SIZE)
                            //printf("%x, %x, %d, %d\n",lab_start, longest_match_pos, longest_match_len, lab_start - longest_match_pos);
                        LZ_enqueue(&lz_queue, 0, longest_match_len, lab_start - longest_match_pos);
                        lit_huf_tab[Get_length_code(longest_match_len)].freq ++;
                        dis_huf_tab[Get_dist_code(distance)].freq ++;

                        int final_pos = lab_start + longest_match_len;
                        while(lab_start < final_pos - 2){
                            for(j = 0; j < 3; j ++)
                                cur_buf[j] = block[lab_start+j];

                            Put_Hash_Table(&lookup_table[Get_Hash_key(cur_buf)], lab_start);
                            lab_start ++;
                        }
                        lab_start += 2;
                        if(lab_start > 1000 && lab_start <= 2000 && data_tag <= INPUT_BLOCK_SIZE)
                            printf("%x, %x\n",final_pos, lab_start);
                    }
                    
                }
                
            }
            int j;
            LZ_Queue_Node *tmp = lz_queue.head;
            /*while(lab_start == 100 && data_tag == INPUT_BLOCK_SIZE)
            {
                printf("%d, %d, %d\n", tmp->value.literal, tmp->value.length, tmp->value.distance);
                if(tmp == lz_queue.tail)
                    break;
                else
                    tmp = tmp->next;
            }*/
        }
        int lit_max_code, dis_max_code;
        Build_Huffman_tree(lit_huf_tab, 256+1+29, &lit_max_code);
        Build_Huffman_tree(dis_huf_tab, 30, &dis_max_code);
        int bl_max_code = Build_Huffman_bl_tree(lit_huf_tab, dis_huf_tab, bl_tab, lit_max_code, dis_max_code);
        Deflate_process_queue(&lz_queue, &out, last_block, StaticOrDynamic, lit_huf_tab, dis_huf_tab, bl_tab, lit_max_code+1, dis_max_code+1, bl_max_code+1);
        Hash_Table_reset(lookup_table);
        Huffman_Table_reset(lit_huf_tab, dis_huf_tab);

        if(data_size - data_tag >= INPUT_BLOCK_SIZE){
            block_size = INPUT_BLOCK_SIZE;
            memcpy(block, pb->pdata + data_tag, block_size);
            data_tag += INPUT_BLOCK_SIZE;
        }
        else
        {
            block_size = data_size - data_tag;
            memcpy(block, pb->pdata, block_size);
            printf("%d %d %d \n",last_block, data_tag, block_size);
            data_tag = data_size;
        }
        //printf("%d ", data_tag);
    }
    printf("%d \n", block_num);
    End_Stream(&out);
    for(int i = 0; i < 10; i ++)
    {
        //printf("%x ", out.databuf[i]);
    }
    File_Save(&out);
}

void inflate(char *filename)
{
    Stream in;
    FILE *f = fopen(filename, "rb");
    if(f == NULL)
    {
        printf("decode read fail");
    }
    FILE *f_out = fopen("decode","wb");
    LZ_Queue lz_queue;
    init_LZ_Queue(&lz_queue);
    init_Stream(&in);
    in.bit_pos = 0;
    in.f = f;
    /*char i;
    fread(&i, sizeof(i), 1, in.f);
    printf("%x ", i);*/

    bool last_block = false;
    
    while(!last_block)
    {
        last_block = Get_bit(&in);
        unsigned char block_type = Get_Stream(&in, 2);
        /*unsigned char block_type = 0;
        block_type = Get_bit(&in);
        block_type *= 2;
        block_type += Get_bit(&in);*/
        
        printf("%d %d\n", last_block, block_type);
        if(block_type = STATIC_HUFFMAN_TYPE)
        {
            bool block_finished = false;

            printf("123\n");
            while(!block_finished)
            {
                int i;
                int cur_code = 0;
                cur_code = Get_Stream(&in, 7);
                //printf("%d", cur_code);

                int extra_bits_len;
                if(cur_code <= 23) extra_bits_len = 0;
                else if(cur_code <= 95) extra_bits_len = 1;
                else if(cur_code <= 99) extra_bits_len = 1;
                else extra_bits_len = 2;

                for(i = 0; i < extra_bits_len; i ++)
                {
                    cur_code <<= 1;
                    cur_code += Get_bit(&in);
                }

                if(cur_code <= 23) cur_code += 256;
                else if(cur_code <= 191) cur_code -= 48;
                else if(cur_code <= 199) cur_code += 88;
                else cur_code -= 256;

                if(cur_code == 256)
                {
                    block_finished = true;
                }
                else if(cur_code <= 255)
                {
                    LZ_enqueue(&lz_queue, cur_code, 0, 0);
                    //printf("%d ", cur_code);
                }
                else
                {
                    int len_pos = cur_code - 257;
                    unsigned char len_extra_bits = Get_Stream(&in, huffman_len_bits[len_pos]);

                    unsigned char dist_pos = Get_Stream(&in, 5);

                    int dist_extra_bits = Get_Stream(&in, huffman_dist_bits[dist_pos]);

                    LZ_enqueue(&lz_queue, 0, huffman_lens[len_pos] + len_extra_bits
                                           , huffman_dists[dist_pos] + dist_extra_bits);
                }
            }
        }
        printf("inflate process queue\n");
        Inflate_process_queue(&lz_queue, f_out);
    }

    printf("total read bytes : %d\n", read_bytes);
}

int main()
{
    char *input_file = "lena256.bmp";
    BMP bmp={0};
    bmp_load(&bmp, input_file);
    //FILE *tmp = fopen("tmp", "wb");
    //fwrite(bmp.pdata, bmp.height*bmp.stride, 1, tmp);
    for(int i = 0; i < 10; i ++)
    {
        //printf("%d ", bmp.pdata[i]);
    }
    printf("\n");
    deflate(&bmp);
    //inflate("encode");
    /*int size = (2*(256+1+29)+1);
    Huffman_Table *table = (Huffman_Table*)malloc(size*sizeof(Huffman_Table));
    for(int i = 0; i < size; i ++)
    {
        table[i].freq = 0;
    }
    for(int i = 0; i < 10; i ++)
    {
        table[i].freq += (10-i);
        printf("%d ", table[i].freq);
    }
    printf("\n");
    Build_Huffman_tree(table, 256+1+29);
    for(int i = 0; i < 10; i ++)
    {
        //printf("%d %d\n", table[i].code, table[i].code_len);
    }*/
}

//zlib use heap to construct huffman tree, combine two smallest node and add to heap