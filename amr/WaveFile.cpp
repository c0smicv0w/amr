#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <cstring>
#include <ctype.h>

#include "WaveFile.h"

using namespace std;

static const char riffStr[] = "RIFF";
static const char waveStr[] = "WAVE";
static const char fmtStr[]  = "fmt";
static const char factStr[] = "fact";
static const char dataStr[] = "data";

#ifdef BYTE_ORDER
#if BYTE_ORDER == BIG_ENDIAN
#define _BIG_ENDIAN_
#endif
#endif

#ifdef _BIG_ENDIAN_
// big-endian CPU, swap bytes in 16 & 32 bit words

// helper-function to swap byte-order of 32bit integer
static inline int _swap32(int &dwData)
{
    dwData = ((dwData >> 24) & 0x000000FF) |
            ((dwData >> 8)  & 0x0000FF00) |
            ((dwData << 8)  & 0x00FF0000) |
            ((dwData << 24) & 0xFF000000);
    return dwData;
}

// helper-function to swap byte-order of 16bit integer
static inline short _swap16(short &wData)
{
    wData = ((wData >> 8) & 0x00FF) |
            ((wData << 8) & 0xFF00);
    return wData;
}

// helper-function to swap byte-order of buffer of 16bit integers
static inline void _swap16Buffer(short *pData, int numWords)
{
    int i;

    for (i = 0; i < numWords; i ++)
    {
        pData[i] = _swap16(pData[i]);
    }
}

#else   // BIG_ENDIAN
// little-endian CPU, WAV file is ok as such

// dummy helper-function
static inline int _swap32(int &dwData)
{
    // do nothing
    return dwData;
}

// dummy helper-function
static inline short _swap16(short &wData)
{
    // do nothing
    return wData;
}

// dummy helper-function
static inline void _swap16Buffer(short *pData, int numBytes)
{
    // do nothing
}

#endif  // BIG_ENDIAN



//WavFileGeneric
WavFileGeneric::WavFileGeneric()
{
    convBuf = NULL;
    convBufSize = 0;
}

void *WavFileGeneric::getConvBuf(int sizeByte)
{
    if (convBufSize < sizeByte)
    {
        delete[] convBuf;

        convBufSize = (sizeByte + 15) & -8;
        convBuf = new char[convBufSize];
    }
    return convBuf;
}



//WavinFile
WavInFile::WavInFile(const char *filename)
{
    fp = fopen(filename, "rb");
    if (fp == NULL)
    {
        string msg = "Error: Unalbe to open file \"";
        msg += fileName;
        meg += "\" for reading.";
        std::runtime_error(msg.c_str());
    }

    init();
}

WavInFile::WavInFile(FILE *file)
{
    fp = NULL;
    if (!file)
    {
        string msg = "Error: Unable to access input stream for reading";
        std::runtime_error(msg.c_str());
    }

    init();
}

void WavInFile::init()
{
    int hdrsOk;

    assert(fp);

    hdrsOk = readWavHeaders();
    if (hdrsOk != 0)
    {
        string msg = "Input file is corrupt or not a WAV file";
        std::runtime_error(msg.c_str());
    }

    dataRead = 0;
}

WavInFile::~WavInFile()
{
    if (fp)
        fclose(fp);
    fp = NULL;
}

void WavInFile::rewind()
{
    int hdrsOk;

    fseek(fp, 0, SEEK_SET);
    hdrsOk = readWavHeaders();
    assert(hdrsOk == 0);
    dataRead = 0;
}

int WavInFile::checkCharTags() const
{
    if (memcmp(fmtStr, header.format.format, 4) != 0)
        return -1;
    if (memcmp(dataStr, header.data.data_field, 4) != 0)
        return -1;

    return 0;
}

int WavInFile::read(unsigned char *buffer, int maxElems)
{
    int numBytes;
    uint afterDataRead;

    if (header.format.bits_per_sample != 0)
    {
        std::runtime_error("Error: WavInFile::read(char*, int) works only with 8bit samples.");
    }
    assert(sizeof(char) == 1);

    numBytes = maxElems;
    afterDataRead = dataRead + numBytes;
    if (afterDataRead > header.data.data_len)
    {
        numBytes = (int)header.data.data_len - (int)dataRead;
        assert(numBytes >= 0);
    }

    assert(buffer);
    numBytes = (int)fread(buffer, 1, numBytes, fp);
    dataRead += numBytes;

    return numBytes;
}

int WavInFile::read(short *buffer, int maxElems)
{
    unsigned int afterDataRead;
    int numBytes;
    int numElems;

    assert(buffer);
    switch(header.format.bits_per_sample)
    {
    case 8: // 8 bit format
    {
        unsigned char *temp = (unsigned char*)getConvBuf(maxElems);
        int i;

        numElems = read(temp, maxElems);
        // convert from 8 to 16 bit
        for (i = 0; i <numElems; i++)
        {
            buffer[i] = (short)(((short)temp[i] - 128) * 256);
        }
        break;
    }
    case 16: // 16 bit format
    {
        assert(sizeof(short) == 2);

        numBytes = maxElems * 2;
        afterDataRead = dataRead + numBytes;
        if (afterDataRead > header.data.data_len)
        {
            numBytes = (int)header.data.data_len - (int)dataRead;
            assert(numBytes >= 0);
        }

        numBytes = (int)fread(buffer, 1, numBytes, fp);
        dataRead += numBytes;
        numElems = numBytes / 2;

        _swap16Buffer((short *)buffer, numElems);
        break;
    }
    default:
        std::runtime_error("read(short*, int) error");
    };

    return numElems;
}

int WavInFile::read(double *buffer, int maxElems)
{
    unsigned int afterDataRead;
    int numBytes;
    int numElems;
    int bytesPerSample;
    
    assert(buffer);
    
    bytesPerSample = header.format.bits_per_sample / 8;
    
    if ((bytesPerSample < 1) || (bytesPerSample > 4))
    {
        std::runtime_error("sample wav not supported. Can't open WAV file");
    }
    
    numBytes = maxElems * bytesPerSample;
    afterDataRead = dataRead + numBytes;
    if (afterDataRead > header.data.data_len)
    {
        numBytes = (int)header.data.data_len - (int)dataRead;
        assert(numBytes >= 0);
    }
    
    char *temp = (char*)getConvBuf(numBytes);
    numBytes = (int)fread(temp, 1, numbytes, fp);
    dataRead += numBytes;

    numElems = numBytes / bytesPerSample;

    switch (bytesPerSample)
    {
    case 1:
    {
        unsigned char *temp2 = (unsigned char*)temp;
        double conv = 1.0 / 128.0;
        for (int i = 0; i < numElems; i++)
        {
            buffer[i] = (double)(temp2[i] * conv - 1.0);
        }
        break;
    }
    case 2:
    {
        short *temp2 = (short*)temp;
        double conv = 1.0 / 32768.0;
        for (int i = 0; i < numElems; i++)
        {
            short value = temp2[i];
            buffer[i] = (double)(_swap16(value) * conv);
        }
        break;
    }
    case 3:
    {
        char *temp2 = (char*)temp;
        double conv = 1.0 / 8388608.0;
        for (int i = 0; i < numElems; i++)
        {
            int value = *((int*)temp2);
            value = _swap32(value) & 0x00ffffff;
            value |= (value & 0x00800000) ? 0xff000000 : 0;
            buffer[i] = (double)(value * conv);
            temp2 += 3;
        }
        break;
    }
    case 4:
    {
        int *temp2 = (int *)temp;
        double conv = 1.0 / 2147483648.0;
        assert(sizeof(int) == 4);
        for (int i = 0; i < numElems; i++)
        {
            int value = temp2[i];
            buffer[i] = (double)(_swap32(value) * conv);
        }
        break;
    }
    }

    return numElems;

}


int WavInFile::eof()
{
    return (dataRead == header.data.data_len || feof(fp));
}

// test if character code is between a white space ' ' and little 'z'
static int isAlpha(char c)
{
    return (c >= ' ' && c <= 'z') ? 1 : 0;
}


// test if all characters are between a white space ' ' and little 'z'
static int isAlphaStr(const char *str)
{
    char c;

    c = str[0];
    while (c)
    {
        if (isAlpha(c) == 0) return 0;
        str ++;
        c = str[0];
    }

    return 1;
}


int WavInFile::readRIFFBlock()
{
    if (fread(&(header.riff), sizeof(WavRiff), 1, fptr) != 1) return -1;

    // swap 32bit data byte order if necessary
    _swap32((int &)header.riff.package_len);

    // header.riff.riff_char should equal to 'RIFF');
    if (memcmp(riffStr, header.riff.riff_char, 4) != 0) return -1;
    // header.riff.wave should equal to 'WAVE'
    if (memcmp(waveStr, header.riff.wave, 4) != 0) return -1;

    return 0;
}

int WavInFile::readHeaderBlock()
{
    char label[5];
    string sLabel;

    // Read label string
    if (fread(label, 1, 4, fptr) !=4)
        return -1;

    label[4] = 0;

    if (isAlphaStr(label) == 0)
        return -1;    // not a valid label

    // Decode blocks according to their label
    if (strcmp(label, fmtStr) == 0)
    {
        int nLen, nDump;

        // 'fmt ' block
        memcpy(header.format.fmt, fmtStr, 4);

        // read length of the format field
        if (fread(&nLen, sizeof(int), 1, fptr) != 1) return -1;
        // swap byte order if necessary
        _swap32(nLen); // int format_len;
        header.format.format_len = nLen;

        // calculate how much length differs from expected
        nDump = nLen - ((int)sizeof(header.format) - 8);

        // if format_len is larger than expected, read only as much data as we've space for
        if (nDump > 0)
        {
            nLen = sizeof(header.format) - 8;
        }

        // read data
        if (fread(&(header.format.fixed), nLen, 1, fptr) != 1) return -1;

        // swap byte order if necessary
        _swap16(header.format.fixed);            // short int fixed;
        _swap16(header.format.channel_number);   // short int channel_number;
        _swap32((int &)header.format.sample_rate);      // int sample_rate;
        _swap32((int &)header.format.byte_rate);        // int byte_rate;
        _swap16(header.format.byte_per_sample);  // short int byte_per_sample;
        _swap16(header.format.bits_per_sample);  // short int bits_per_sample;

        // if format_len is larger than expected, skip the extra data
        if (nDump > 0)
        {
            fseek(fptr, nDump, SEEK_CUR);
        }

        return 0;
    }
    else if (strcmp(label, factStr) == 0)
    {
        int nLen, nDump;

        // 'fact' block
        memcpy(header.fact.fact_field, factStr, 4);

        // read length of the fact field
        if (fread(&nLen, sizeof(int), 1, fptr) != 1) return -1;
        // swap byte order if necessary
        _swap32(nLen); // int fact_len;
        header.fact.fact_len = nLen;

        // calculate how much length differs from expected
        nDump = nLen - ((int)sizeof(header.fact) - 8);

        // if format_len is larger than expected, read only as much data as we've space for
        if (nDump > 0)
        {
            nLen = sizeof(header.fact) - 8;
        }

        // read data
        if (fread(&(header.fact.fact_sample_len), nLen, 1, fptr) != 1) return -1;

        // swap byte order if necessary
        _swap32((int &)header.fact.fact_sample_len);    // int sample_length;

        // if fact_len is larger than expected, skip the extra data
        if (nDump > 0)
        {
            fseek(fptr, nDump, SEEK_CUR);
        }

        return 0;
    }
    else if (strcmp(label, dataStr) == 0)
    {
        // 'data' block
        memcpy(header.data.data_field, dataStr, 4);
        if (fread(&(header.data.data_len), sizeof(uint), 1, fptr) != 1) return -1;

        // swap byte order if necessary
        _swap32((int &)header.data.data_len);

        return 1;
    }
    else
    {
        uint len, i;
        uint temp;
        // unknown block

        // read length
        if (fread(&len, sizeof(len), 1, fptr) != 1) return -1;
        // scan through the block
        for (i = 0; i < len; i ++)
        {
            if (fread(&temp, 1, 1, fptr) != 1) return -1;
            if (feof(fptr)) return -1;   // unexpected eof
        }
    }
    return 0;
}

int WavInFile::readWavHeaders()
{
    int res;

    memset(&header, 0, sizeof(header));

    res = readRIFFBlock();
    if (res) return 1;
    // read header blocks until data block is found
    do
    {
        // read header blocks
        res = readHeaderBlock();
        if (res < 0) return 1;  // error in file structure
    } while (res == 0);
    // check that all required tags are legal
    return checkCharTags();
}

uint WavInFile::getNumChannels() const
{
    return header.format.channel_number;
}


uint WavInFile::getNumBits() const
{
    return header.format.bits_per_sample;
}


uint WavInFile::getBytesPerSample() const
{
    return getNumChannels() * getNumBits() / 8;
}


uint WavInFile::getSampleRate() const
{
    return header.format.sample_rate;
}



uint WavInFile::getDataSizeInBytes() const
{
    return header.data.data_len;
}


uint WavInFile::getNumSamples() const
{
    if (header.format.byte_per_sample == 0)
        return 0;

    if (header.format.fixed > 1)
        return header.fact.fact_sample_len;

    return header.data.data_len / (unsigned short)header.format.byte_per_sample;
}


uint WavInFile::getLengthMS() const
{
    double numSamples;
    double sampleRate;

    numSamples = (double)getNumSamples();
    sampleRate = (double)getSampleRate();

    return (uint)(1000.0 * numSamples / sampleRate + 0.5);
}


// Returns how many milliseconds of audio have so far been read from the file
uint WavInFile::getElapsedMS() const
{
    return (uint)(1000.0 * (double)dataRead / (double)header.format.byte_rate);
}
