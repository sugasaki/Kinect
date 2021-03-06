//------------------------------------------------------------------------------
// <copyright file="MicArrayEchoCancellation.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

// This module provides sample code used to demonstrate Kinect DMO processing audio in AEC mode and
// simultaneously performing beam tracking.

// Note: 
//   To run the demo properly for AEC enabled modes (mode 0 and 4), 
//   users must play some audio signals through the SAME speaker  
//   device specified for the DMO. These audio signals simulate far-end
//   voices in a two-way chatting scenario. Users may use any player to
//   play any audio signals. If there is no active render stream on the
//   selected speaker device, the Kinect AEC DMO will fail to process.

#include <windows.h>

// For configuring DMO properties
#include <wmcodecdsp.h>

// For discovering microphone array device
#include <MMDeviceApi.h>
#include <devicetopology.h>

// For functions and definitions used to create output file
#include <dmo.h> // Mo*MediaType
#include <uuids.h> // FORMAT_WaveFormatEx and such
#include <mfapi.h> // FCC

// For string input,output and manipulation
#include <tchar.h>
#include <strsafe.h>
#include <conio.h>

// For CLSID_KinectAudio GUID
#include "NuiApi.h"


#define SAFE_ARRAYDELETE(p) {if (p) delete[] (p); (p) = NULL;}
#define SAFE_RELEASE(p) {if (NULL != p) {(p)->Release(); (p) = NULL;}}

#define CHECK_RET(hr, message) if (FAILED(hr)) { printf("%s: %08X\n", message, hr); goto exit;}
#define CHECKHR(x) hr = x; if (FAILED(hr)) {printf("%d: %08X\n", __LINE__, hr); goto exit;}
#define CHECK_ALLOC(pb, message) if (NULL == pb) { puts(message); goto exit;}
#define CHECK_BOOL(b, message) if (!b) { hr = E_FAIL; puts(message); goto exit;}

class CStaticMediaBuffer : public IMediaBuffer {
public:
   CStaticMediaBuffer() {}
   CStaticMediaBuffer(BYTE *pData, ULONG ulSize, ULONG ulData) :
      m_pData(pData), m_ulSize(ulSize), m_ulData(ulData), m_cRef(1) {}
   STDMETHODIMP_(ULONG) AddRef() { return 2; }
   STDMETHODIMP_(ULONG) Release() { return 1; }
   STDMETHODIMP QueryInterface(REFIID riid, void **ppv) {
      if (riid == IID_IUnknown) {
         AddRef();
         *ppv = (IUnknown*)this;
         return NOERROR;
      }
      else if (riid == IID_IMediaBuffer) {
         AddRef();
         *ppv = (IMediaBuffer*)this;
         return NOERROR;
      }
      else
         return E_NOINTERFACE;
   }
   STDMETHODIMP SetLength(DWORD ulLength) {m_ulData = ulLength; return NOERROR;}
   STDMETHODIMP GetMaxLength(DWORD *pcbMaxLength) {*pcbMaxLength = m_ulSize; return NOERROR;}
   STDMETHODIMP GetBufferAndLength(BYTE **ppBuffer, DWORD *pcbLength) {
      if (ppBuffer) *ppBuffer = m_pData;
      if (pcbLength) *pcbLength = m_ulData;
      return NOERROR;
   }
   void Init(BYTE *pData, ULONG ulSize, ULONG ulData) {
        m_pData = pData;
        m_ulSize = ulSize;
        m_ulData = ulData;
    }
protected:
   BYTE *m_pData;
   ULONG m_ulSize;
   ULONG m_ulData;
   ULONG m_cRef;
};

// Helper functions used to generate output file
HRESULT DShowRecord(IMediaObject* pDMO, IPropertyStore* pPS, const TCHAR* outFile, int  iDuration);
HRESULT WriteToFile(HANDLE hFile, void* p, DWORD cb);
HRESULT WriteWaveHeader(HANDLE hFile, WAVEFORMATEX *pWav, DWORD *pcbWritten);
HRESULT FixUpChunkSizes(HANDLE hFile, DWORD cbHeader, DWORD cbAudioData);

///////////////////////////////////////////////////////////////////////////
// Main function
//
// Initializes COM, creates and configures a DMO object, captures sound
// from microphone array and records it to a WAV file.
//
///////////////////////////////////////////////////////////////////////////
int __cdecl _tmain(int argc, const TCHAR ** argv)
{
    HRESULT hr = S_OK;
    CoInitialize(NULL);
    IMediaObject* pDMO = NULL;  
    IPropertyStore* pPS = NULL;
    HANDLE mmHandle = NULL;
    DWORD mmTaskIndex = 0;
    LPCTSTR szOutputFile = _T("AECout.wav");
    TCHAR szOutfileFullName[MAX_PATH];
    int ch;

    // control how long the Demo runs
    int  iDuration = 20;   // seconds

    // Set high priority to avoid getting preempted while capturing sound
    mmHandle = AvSetMmThreadCharacteristics(L"Audio", &mmTaskIndex);
    CHECK_BOOL(mmHandle != NULL, "failed to set thread priority\n");

    // DMO initialization
    INuiAudioBeam* pAudio = NULL;
    CHECKHR(NuiInitialize(NUI_INITIALIZE_FLAG_USES_AUDIO));
    CHECKHR(NuiGetAudioSource(&pAudio));
    CHECKHR(pAudio->QueryInterface(IID_IMediaObject, (void**)&pDMO));
    CHECKHR(pAudio->QueryInterface(IID_IPropertyStore, (void**)&pPS));
    SAFE_RELEASE(pAudio);

    // Set AEC-MicArray DMO system mode.
    // This must be set for the DMO to work properly
    PROPVARIANT pvSysMode;
    PropVariantInit(&pvSysMode);
    pvSysMode.vt = VT_I4;
    //   SINGLE_CHANNEL_AEC = 0
    //   OPTIBEAM_ARRAY_ONLY = 2
    //   OPTIBEAM_ARRAY_AND_AEC = 4
    //   SINGLE_CHANNEL_NSAGC = 5
    pvSysMode.lVal = (LONG)(4);
    CHECKHR(pPS->SetValue(MFPKEY_WMAAECMA_SYSTEM_MODE, pvSysMode));
    PropVariantClear(&pvSysMode);

    puts("Play a song, e.g. in Windows Media Player, to perform echo cancellation using\nsound from speakers.\n");
    DWORD dwRet = GetFullPathName(szOutputFile, (DWORD)ARRAYSIZE(szOutfileFullName), szOutfileFullName,NULL);
    CHECK_BOOL(dwRet != 0, "Sound output could not be written");

    _tprintf(_T("Sound output will be written to file: \n%s\n"),szOutfileFullName);

    // NOTE: Need to wait 4 seconds for device to be ready right after initialization
    DWORD dwWait = 4;
    while (dwWait > 0)
    {
        _tprintf(_T("Device will be ready for recording in %d second(s).\r"), dwWait--);
        Sleep(1000);
    }
    _tprintf(_T("Device is ready. Press any key to start recording.\n"));
    ch = _getch();

    // Capture sound in microphone array while performing beam angle detection and echo cancellation
    DShowRecord(pDMO, pPS, szOutputFile, iDuration);

exit:
    SAFE_RELEASE(pDMO);
    SAFE_RELEASE(pPS);

    AvRevertMmThreadCharacteristics(mmHandle);
    NuiShutdown();
    CoUninitialize();
}


///////////////////////////////////////////////////////////////////////////
// DShowRecord
//
// Uses the DMO in source mode to retrieve clean audio samples and record
// them to a .wav file.
//
///////////////////////////////////////////////////////////////////////////
HRESULT DShowRecord(IMediaObject* pDMO, IPropertyStore* pPS, const TCHAR* outFile, int  iDuration)
{
    _tprintf(_T("\nRecording using DMO\n"));

    INuiAudioBeam* pSC = NULL;
    HRESULT hr;
    int  cTtlToGo = 0;

    DWORD cOutputBufLen = 0;
    BYTE *pbOutputBuffer = NULL;

    WAVEFORMATEX wfxOut = {WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0};
    CStaticMediaBuffer outputBuffer;
    DMO_OUTPUT_DATA_BUFFER OutputBufferStruct = {0};
    OutputBufferStruct.pBuffer = &outputBuffer;
    DMO_MEDIA_TYPE mt = {0};

    ULONG cbProduced = 0;
    DWORD dwStatus;

    // Set DMO output format
    hr = MoInitMediaType(&mt, sizeof(WAVEFORMATEX));
    CHECK_RET(hr, "MoInitMediaType failed");
    
    mt.majortype = MEDIATYPE_Audio;
    mt.subtype = MEDIASUBTYPE_PCM;
    mt.lSampleSize = 0;
    mt.bFixedSizeSamples = TRUE;
    mt.bTemporalCompression = FALSE;
    mt.formattype = FORMAT_WaveFormatEx;	
    memcpy(mt.pbFormat, &wfxOut, sizeof(WAVEFORMATEX));
    
    hr = pDMO->SetOutputType(0, &mt, 0); 
    CHECK_RET(hr, "SetOutputType failed");
    MoFreeMediaType(&mt);

    // Allocate streaming resources. This step is optional. If it is not called here, it
    // will be called when first time ProcessInput() is called. However, if you want to 
    // get the actual frame size being used, it should be called explicitly here.
    hr = pDMO->AllocateStreamingResources();
    CHECK_RET(hr, "AllocateStreamingResources failed");
    
    // Get actually frame size being used in the DMO. (optional, do as you need)
    int iFrameSize;
    PROPVARIANT pvFrameSize;
    PropVariantInit(&pvFrameSize);
    CHECKHR(pPS->GetValue(MFPKEY_WMAAECMA_FEATR_FRAME_SIZE, &pvFrameSize));
    iFrameSize = pvFrameSize.lVal;
    PropVariantClear(&pvFrameSize);

    // allocate output buffer
    cOutputBufLen = wfxOut.nSamplesPerSec * wfxOut.nBlockAlign;
    pbOutputBuffer = new BYTE[cOutputBufLen];
    CHECK_ALLOC (pbOutputBuffer, "out of memory.\n");

   // number of frames to record
    cTtlToGo = iDuration * 100;

    HANDLE hFile = CreateFile(
            outFile,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            CREATE_ALWAYS,
            0,
            NULL
            );
    if(hFile == INVALID_HANDLE_VALUE)
    {		
        _tprintf(_T("Could not open the output file: %s\n"), outFile);
        goto exit;
    }

    DWORD written = 0;
    WriteWaveHeader(hFile, &wfxOut, &written);
    int totalBytes = 0;
    
    hr = pDMO->QueryInterface(IID_INuiAudioBeam, (void**)&pSC);
    CHECK_RET (hr, "QueryInterface for IID_INuiAudioBeam failed");

    double dBeamAngle, dAngle;	

    // main loop to get mic output from the DMO
    puts("\nAEC-MicArray is running ... Press \"s\" to stop\n");

#pragma warning(push)
#pragma warning(disable: 4127) // conditional expression is constant

    while (1)
    {
        Sleep(10); //sleep 10ms

        if (cTtlToGo--<=0)
            break;

        do{
            outputBuffer.Init((byte*)pbOutputBuffer, cOutputBufLen, 0);
            OutputBufferStruct.dwStatus = 0;
            hr = pDMO->ProcessOutput(0, 1, &OutputBufferStruct, &dwStatus);
            CHECK_RET (hr, "ProcessOutput failed.");

            if (hr == S_FALSE) {
                cbProduced = 0;
            } else {
                hr = outputBuffer.GetBufferAndLength(NULL, &cbProduced);
                CHECK_RET (hr, "GetBufferAndLength failed");
            }
            
            WriteToFile(hFile, pbOutputBuffer, cbProduced);
            totalBytes += cbProduced;

            // Obtain beam angle from INuiAudioBeam afforded by microphone array
            hr = pSC->GetBeam(&dBeamAngle);
            double dConf;
            hr = pSC->GetPosition(&dAngle, &dConf);
            if(SUCCEEDED(hr))
            {								
                
                //Use a moving average to smooth this out
                if(dConf>0.9)
                {					
                    _tprintf(_T("Position: %f\t\tConfidence: %f\t\tBeam Angle = %f\r"), dAngle, dConf, dBeamAngle);					
                }
            }

        } while (OutputBufferStruct.dwStatus & DMO_OUTPUT_DATA_BUFFERF_INCOMPLETE);

        // check keyboard input to stop
        if (_kbhit())
        {
            int ch = _getch();
            if (ch == 's' || ch == 'S')
                break;
        }
    }

#pragma warning(pop)

    FixUpChunkSizes(hFile, written, totalBytes);

exit:
    SAFE_ARRAYDELETE(pbOutputBuffer);    
    SAFE_RELEASE(pSC);

    return hr;
}


///////////////////////////////////////////////////////////////////////////
// WriteWaveHeader
//
// Writes a WAVE file header placeholder that will be updated by
// FixUpChunkSizes after recording is complete.
//
///////////////////////////////////////////////////////////////////////////
HRESULT WriteWaveHeader(
    HANDLE hFile,               // Output file.
    WAVEFORMATEX *pWav,
    DWORD *pcbWritten           // Receives the size of the header.    
    )
{
    HRESULT hr = S_OK;
    UINT32 cbFormat = sizeof(WAVEFORMATEX);
    *pcbWritten = 0;       

    // Write the 'RIFF' header and the start of the 'fmt ' chunk.

    if (SUCCEEDED(hr))
    {
        DWORD header[] = { 
            // RIFF header
            FCC('RIFF'), 
            0, 
            FCC('WAVE'),  
            // Start of 'fmt ' chunk
            FCC('fmt '), 
            cbFormat 
        };

        DWORD dataHeader[] = { FCC('data'), 0 };

        hr = WriteToFile(hFile, header, sizeof(header));

        // Write the WAVEFORMATEX structure.
        if (SUCCEEDED(hr))
        {
            hr = WriteToFile(hFile, pWav, cbFormat);
        }

        // Write the start of the 'data' chunk

        if (SUCCEEDED(hr))
        {
            hr = WriteToFile(hFile, dataHeader, sizeof(dataHeader));
        }

        if (SUCCEEDED(hr))
        {
            *pcbWritten = sizeof(header) + cbFormat + sizeof(dataHeader);
        }
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////////
// FixUpChunkSizes
//
// Writes the file-size information into the WAVE file header.
// Note that WAVE files use the RIFF file format. Each RIFF chunk has a
// data size, and the RIFF header has a total file size.
//
///////////////////////////////////////////////////////////////////////////
HRESULT FixUpChunkSizes(
    HANDLE hFile,           // Output file.
    DWORD cbHeader,         // Size of the 'fmt ' chuck.
    DWORD cbAudioData       // Size of the 'data' chunk.
    )
{
    HRESULT hr = S_OK;

    LARGE_INTEGER ll;
    ll.QuadPart = cbHeader - sizeof(DWORD);

    if (0 == SetFilePointerEx(hFile, ll, NULL, FILE_BEGIN))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
    }

    // Write the data size.

    if (SUCCEEDED(hr))
    {
        hr = WriteToFile(hFile, &cbAudioData, sizeof(cbAudioData));
    }

    if (SUCCEEDED(hr))
    {
        // Write the file size.
        ll.QuadPart = sizeof(FOURCC);

        if (0 == SetFilePointerEx(hFile, ll, NULL, FILE_BEGIN))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }

    if (SUCCEEDED(hr))
    {
        DWORD cbRiffFileSize = cbHeader + cbAudioData - 8;

        // NOTE: The "size" field in the RIFF header does not include
        // the first 8 bytes of the file. i.e., it is the size of the
        // data that appears _after_ the size field.

        hr = WriteToFile(hFile, &cbRiffFileSize, sizeof(cbRiffFileSize));
    }

    return hr;
}

HRESULT WriteToFile(HANDLE hFile, void* p, DWORD cb)
{
    DWORD cbWritten = 0;
    HRESULT hr = S_OK;

    BOOL bResult = WriteFile(hFile, p, cb, &cbWritten, NULL);
    if (!bResult)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
    }
    return hr;
}
