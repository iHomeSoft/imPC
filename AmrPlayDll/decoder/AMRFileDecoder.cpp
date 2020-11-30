#include "../StdAfx.h"
#include <stdio.h>
#include <string>
extern "C"
{
#include "../AMR/typedef.h"
#include "../AMR/n_proc.h"
#include "../AMR/cnst.h"
#include "../AMR/mode.h"
#include "../AMR/frame.h"
#include "../AMR/strfunc.h"
#include "../AMR/sp_dec.h"
#include "../AMR/d_homing.h"
}

#include "AMRFileDecoder.h"

/*
@	amr�ļ��ļ�ͷ�Ľ�β
@	amr�ļ��в�ͬ���ļ�ͷ���������е�ͷ�ļ���һ����ͬ�ص�
@	���Ƕ�����{0x23,0x21,0x41,0x4D,0x52,0x0A}(#!AMR\n or 0x2321414d520a)
@	������ֻ����Ե�������amr��Ƶ���ԣ�����Ƕ��������ļ�ͷ�Ľ�βΪ(#!AMR-WB\n or 0x2321414d522d57420a)
*/
#ifndef AMR_MAGIC_NUMBER
#define AMR_MAGIC_NUMBER "#!AMR\n"
#endif
#ifndef GP4_MAGIC_NUMBER
#define GP4_MAGIC_NUMBER "ftyp3gp4"
#endif


#define MAX_PACKED_SIZE (MAX_SERIAL_SIZE / 8 + 2)
/* frame size in serial bitstream file (frame type + serial stream + flags) */
#define SERIAL_FRAMESIZE (1+MAX_SERIAL_SIZE+5)

/*
@	ȫ�ֵ�amr����
@	֡��С
@	����С
@	֡ʱ�������룩
*/
static const int amrFramesSize[16] = {13, 14, 16, 18, 20, 21, 27, 32, 6, 0, 0, 0, 0, 0, 0, 0}; // amrÿ֡��С,byte
static const int amrPackedSize[16] = {12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0}; // amrÿ֡�������ݴ�С,byte
static const int amrFrameTime = 20;                                                            // ÿ֡����ʱ�䣬��λms

AMRFileDecoder::AMRFileDecoder(void)
: m_pBaseAddress(NULL)
, m_pCurAddress(NULL)
, m_liFileSize(0)
, m_dwFrameCount(0)
, m_pvDecoderState(NULL)
{
}

AMRFileDecoder::AMRFileDecoder(LPCTSTR lpszFile)
: m_pBaseAddress(NULL)
, m_pCurAddress(NULL)
, m_liFileSize(0)
, m_dwFrameCount(0)
, m_pvDecoderState(NULL)
, m_sFilePathName(lpszFile)
{
    // ����ʱ�����ļ�ӳ�䵽�ڴ�

    // ���ļ�
	/*	CreateFile params
	@	param1 The name of the object to be created or opened.
	@	param2 The access to the object, which can be summarized as read, write, both or neither (zero).
	@	param3 The sharing mode of an object, which can be read, write, both, delete, all of these, or none (refer to the following table).
	@	param4 A pointer to a SECURITY_ATTRIBUTES
	@	param5 An action to take on files that exist and do not exist.
	@	param6 The file attributes and flags
	@	param7 A valid handle to a template file with the GENERIC_READ access right
	*/
    HANDLE hFileAmr = CreateFile(lpszFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ATLASSERT(hFileAmr);
    if(hFileAmr == NULL)    return ;

    // ȡ�ļ���С
    LARGE_INTEGER liFileSize;
    memset(&liFileSize, 0, sizeof(LARGE_INTEGER));
    if(!GetFileSizeEx(hFileAmr, &liFileSize))
    {
        CloseHandle(hFileAmr);
        return;
    }
    m_liFileSize = liFileSize.QuadPart;

    // �����ļ�ӳ�����
    HANDLE hFileMapping = CreateFileMapping(hFileAmr, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(hFileAmr);
    ATLASSERT(hFileMapping);
    if(hFileMapping == NULL) return ;

    // ӳ��
    m_pBaseAddress = (LPSTR)MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hFileMapping);
    ATLASSERT(m_pBaseAddress);

    // �����α�
    m_pCurAddress = m_pBaseAddress;
}

AMRFileDecoder::~AMRFileDecoder(void)
{
    if(m_pBaseAddress)
    {
        UnmapViewOfFile(m_pBaseAddress);
        m_pBaseAddress = NULL;
    }

    EndDecode();
}

/*
@	�ж�amr��Ƶ�ļ���һ����Ƶ֡�Ƿ�Ϊ����֡
*/
bool AMRFileDecoder::IsVaild()
{
    if(m_pBaseAddress == NULL || m_liFileSize == 0)
        return false;

    // ���amr�ļ�ͷ
	char magic[8]={0};
	int n = strlen(AMR_MAGIC_NUMBER);
    memcpy(magic, m_pBaseAddress, strlen(AMR_MAGIC_NUMBER));
	if (memcmp(magic, AMR_MAGIC_NUMBER, strlen(AMR_MAGIC_NUMBER)) == 0)
	{
		//zmr֡ͷ����Ϊ"#!AMR\n" ��һ���ɽ�֡(����)
		return true;
	}

	//amr�ļ���֡�Ĵ���������ô���Ķ���
	else
	{
		byte bmagic[1024]={0};
		byte buff[] = {0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70, 0x33, 0x67, 0x70, 0x34, 0x00, 0x00, 0x00};
		memcpy(bmagic, m_pBaseAddress, sizeof(buff));
		return memcmp(bmagic, buff, sizeof(buff)) == 0;
	}
    //return strncmp(magic, AMR_MAGIC_NUMBER, strlen(AMR_MAGIC_NUMBER)) == 0;
}


/*
@	һ��amr��Ƶ����Ƶ֡ʱ��Ϊ20����
@	GetFrameCount()��ȡ������Ƶ֡������
@	GetTimeLength��ȡ��Ƶ�ļ��Ĳ���ʱ��(��ȷ������)
*/
ULONGLONG AMRFileDecoder::GetTimeLength()
{
    if(!IsVaild())  return 0;
   
    return GetFrameCount() * amrFrameTime;
}

/*
@	��ȡ����֡�ĸ�������
*/
WAVEFORMATEX AMRFileDecoder::GetWaveFromatX()
{
    WAVEFORMATEX wfmtx;
    memset(&wfmtx, 0, sizeof(WAVEFORMATEX));
    wfmtx.wFormatTag = WAVE_FORMAT_PCM;
    wfmtx.nChannels = 1; // ������
    wfmtx.nSamplesPerSec = 8000; // 8khz
    wfmtx.nAvgBytesPerSec = 16000;
    wfmtx.nBlockAlign = 2;
    wfmtx.wBitsPerSample = 16; // 16λ
    wfmtx.cbSize = 0;

    return wfmtx;
}

/*
@	��ʼ���룺
@	1�����ж���Ƶ֡�Ƿ����
@	2����ʼ��������
@	3�����ÿ�ʼ����λ��(�α�λ��+֡ͷǰ8λ)
*/
BOOL AMRFileDecoder::BeginDecode()
{
    if(!IsVaild())  return FALSE;

    Speech_Decode_FrameState *& speech_decoder_state = (Speech_Decode_FrameState*&)m_pvDecoderState;
	/* init decoder */
    if(Speech_Decode_Frame_init(&speech_decoder_state, "Decode"))
    {
        if(speech_decoder_state)
        {
            Speech_Decode_Frame_exit(&speech_decoder_state);
        }

        return FALSE;
    }

    ATLASSERT(m_pvDecoderState);

    m_pCurAddress = m_pBaseAddress + strlen(AMR_MAGIC_NUMBER);

    return TRUE;
}

/*
@	��������
*/
void AMRFileDecoder::EndDecode()
{
    if(m_pvDecoderState)
    {
        Speech_Decode_Frame_exit((Speech_Decode_FrameState**)&m_pvDecoderState);
        m_pvDecoderState = NULL;
    }
}

/*
@	�����α���ڵ��ڳ�ʼ�α�λ��+�ļ���С���Ѿ����ļ�ĩβ��
*/
bool AMRFileDecoder::IsEOF()
{
    return m_pCurAddress >= m_pBaseAddress + m_liFileSize;
}

/*
@	������Ƶ֡�е���Ƶ���ݿ�
*/
DWORD AMRFileDecoder::Decode(LPSTR& pData)
{
    // �����ַ�Ѿ�������Χ���������ݴ�СΪ0
    if(m_pCurAddress >= m_pBaseAddress + m_liFileSize)
        return 0;

    Word16 serial[SERIAL_FRAMESIZE];   /* coded bits                    */
    Word16 pcmFrame[L_FRAME];          /* Synthesis                     */

    UWord8 toc, q, ft;
    UWord8 packed_bits[MAX_PACKED_SIZE];

    RXFrameType rx_type = (RXFrameType)0;
    Mode mode = (Mode)0;

    Word16 reset_flag = 0;
    Word16 reset_flag_old = 1;

    Speech_Decode_FrameState *speech_decoder_state = (Speech_Decode_FrameState*)m_pvDecoderState;

    // ����ĺ��Ĵ���
    {
        // tocΪÿһ��Amr֡�����ֽ�����
        toc = *m_pCurAddress++;
        /* read rest of the frame based on ToC byte */
        q  = (toc >> 2) & 0x01;
        ft = (toc >> 3) & 0x0F;
        memcpy(packed_bits, m_pCurAddress, amrPackedSize[ft]);
        m_pCurAddress += amrPackedSize[ft];

        rx_type = UnpackBits(q, ft, packed_bits, &mode, &serial[1]);

        if (rx_type == RX_NO_DATA) 
        {
            mode = speech_decoder_state->prev_mode;
        }
        else 
        {
            speech_decoder_state->prev_mode = mode;
        }

        /* if homed: check if this frame is another homing frame */
        if (reset_flag_old == 1)
        {
            /* only check until end of first subframe */
            reset_flag = decoder_homing_frame_test_first(&serial[1], mode);
        }
        /* produce encoder homing frame if homed & input=decoder homing frame */
        if ((reset_flag != 0) && (reset_flag_old != 0))
        {
            for (int i = 0; i < L_FRAME; i++)
            {
                pcmFrame[i] = EHF_MASK;
            }
        }
        else
        {     
            /* decode frame */
            Speech_Decode_Frame(speech_decoder_state, mode, &serial[1], rx_type, pcmFrame);
        }

        /* if not homed: check whether current frame is a homing frame */
        if (reset_flag_old == 0)
        {
            /* check whole frame */
            reset_flag = decoder_homing_frame_test(&serial[1], mode);
        }
        /* reset decoder if current frame is a homing frame */
        if (reset_flag != 0)
        {
            Speech_Decode_Frame_reset(speech_decoder_state);
        }
        reset_flag_old = reset_flag;
    }
    
    // ���
    if(pData)
    {
        memcpy(pData, pcmFrame, sizeof(Word16) * L_FRAME);
    }

    // ���������С��bytes
    return L_FRAME * sizeof(Word16);
}

DWORD AMRFileDecoder::GetDecodedFrameMaxSize()
{
    return L_FRAME * sizeof(Word16);
}

DWORD AMRFileDecoder::GetDecodedMaxSize()
{
    return GetFrameCount() * GetDecodedFrameMaxSize();
}

void AMRFileDecoder::SetFilePathName(LPCTSTR lpszFile)
{
    if(m_sFilePathName.CompareNoCase(lpszFile) == 0)
        return;

    // �ر���ǰ��ӳ�����
    if(m_pBaseAddress)
    {
        UnmapViewOfFile(m_pBaseAddress);
        m_pBaseAddress = NULL;
    }

    // �رս�����
    EndDecode();

    // �ָ���ʼֵ
    m_pCurAddress = NULL;
    m_liFileSize = 0;
    m_dwFrameCount = 0;
    m_pvDecoderState = NULL;

    // ���ļ�
    HANDLE hFileAmr = CreateFile(lpszFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ATLASSERT(hFileAmr);
    if(hFileAmr == NULL)    return ;

    // ȡ�ļ���С
    LARGE_INTEGER liFileSize;
    memset(&liFileSize, 0, sizeof(LARGE_INTEGER));
    if(!GetFileSizeEx(hFileAmr, &liFileSize))
    {
        CloseHandle(hFileAmr);
        return;
    }
    m_liFileSize = liFileSize.QuadPart;

    // �����ļ�ӳ�����
    HANDLE hFileMapping = CreateFileMapping(hFileAmr, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(hFileAmr);
    ATLASSERT(hFileMapping);
    if(hFileMapping == NULL) return ;

    // ӳ��
    m_pBaseAddress = (LPSTR)MapViewOfFile(hFileMapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hFileMapping);
    ATLASSERT(m_pBaseAddress);

    // �����α�
    m_pCurAddress = m_pBaseAddress;
}

DWORD AMRFileDecoder::GetFrameCount()
{
    ATLASSERT(IsVaild());

    if(m_dwFrameCount <= 0)
    {
        m_dwFrameCount = 0;

        LPSTR pCur = m_pBaseAddress + strlen(AMR_MAGIC_NUMBER);

        unsigned char toc = 0, ft = 0;
        while (pCur < m_pBaseAddress + m_liFileSize)
        {
            // tocΪÿһ��Amr֡�����ֽ�����
            toc = *pCur++;
            ft = (toc >> 3) & 0x0F;
            pCur += amrPackedSize[ft];
            ++m_dwFrameCount;
        }
    }

    return m_dwFrameCount;
}