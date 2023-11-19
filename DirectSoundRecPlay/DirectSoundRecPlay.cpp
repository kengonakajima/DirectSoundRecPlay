#include <windows.h>
#include <dsound.h>
#include <iostream>

#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")




/*
 SampleBuffer
 サンプルデータを格納しておく構造体

 */
typedef struct
{
#define SAMPLE_MAX (24000)
    short samples[SAMPLE_MAX];
    int used;
    int totalPushed;
    int totalShifted;
    LONG64 initialMs;
} SampleBuffer;

SampleBuffer* g_recbuf; // 録音したサンプルデータ
SampleBuffer* g_playbuf; // 再生予定のサンプルデータ

// 必要なSampleBufferを初期化する
void ensureSampleBuffers() {
    if (!g_recbuf) {
        g_recbuf = (SampleBuffer*)malloc(sizeof(SampleBuffer));
        memset(g_recbuf, 0, sizeof(SampleBuffer));
        g_playbuf = (SampleBuffer*)malloc(sizeof(SampleBuffer));
        memset(g_playbuf, 0, sizeof(SampleBuffer));
    }
}

static int shiftSamples(SampleBuffer* buf, short* output, int num) {
    int to_output = num;
    if (to_output > buf->used) to_output = buf->used;
    // output
    if (output) for (int i = 0; i < to_output; i++) output[i] = buf->samples[i];
    // shift
    int to_shift = buf->used - to_output;
    for (int i = to_output; i < buf->used; i++) buf->samples[i - to_output] = buf->samples[i];
    buf->used -= to_output;
    buf->totalShifted += to_output;
    //printf("shiftSamples: buf used: %d\n",buf->used);
    return to_output;
}
static void pushSamples(SampleBuffer* buf, short* append, int num) {
    if (buf->used + num > SAMPLE_MAX) shiftSamples(buf, NULL, num);
    for (int i = 0; i < num; i++) {
        buf->samples[i + buf->used] = append[i];
    }
    buf->used += num;
    buf->totalPushed += num;
    if (!buf->initialMs) buf->initialMs = GetTickCount64();
    //printf("pushSamples: g_samples_used: %d\n",buf->used);
}
static int getRoom(SampleBuffer* buf) {
    return SAMPLE_MAX - buf->used;
}
// マイクから受け取ったサンプルの保存されている数を返す
int getRecordedSampleCount() {
    return g_recbuf->used;
}
void discardRecordedSamples(int num) {
    shiftSamples(g_recbuf, NULL, num);
    //printf("discardRecordedSamples: g_samples_used: %d\n",g_recbuf->used);
}
// マイクから受け取って保存されているサンプルを1個取得する
short getRecordedSample(int index) {
    return g_recbuf->samples[index];
}
// 再生するサンプルを送る。
void pushSamplesForPlay(short* samples, int num) {
    pushSamples(g_playbuf, samples, num);
}

///////////////////////////


#define SAMPLE_RATE 24000
#define NUM_CHANNELS 1
#define BITS_PER_SAMPLE 16
#define BUFFER_SIZE (SAMPLE_RATE * NUM_CHANNELS * BITS_PER_SAMPLE / 8)

LPDIRECTSOUNDCAPTURE8 pDSCapture = NULL;
LPDIRECTSOUNDCAPTUREBUFFER pDSCaptureBuffer = NULL;
LPDIRECTSOUND8 pDS = NULL;
LPDIRECTSOUNDBUFFER pDSBuffer = NULL;

bool InitializeDirectSound(HWND hwnd) {
    if (FAILED(DirectSoundCaptureCreate8(NULL, &pDSCapture, NULL))) {
        return false;
    }

    WAVEFORMATEX wfx;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = BITS_PER_SAMPLE;
    wfx.nChannels = NUM_CHANNELS;
    wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    DSCBUFFERDESC dscbd;
    ZeroMemory(&dscbd, sizeof(dscbd));
    dscbd.dwSize = sizeof(dscbd);
    dscbd.dwBufferBytes = BUFFER_SIZE;
    dscbd.lpwfxFormat = &wfx;

    if (FAILED(pDSCapture->CreateCaptureBuffer(&dscbd, &pDSCaptureBuffer, NULL))) {
        return false;
    }

    if (FAILED(DirectSoundCreate8(NULL, &pDS, NULL))) {
        return false;
    }

    if (FAILED(pDS->SetCooperativeLevel(hwnd, DSSCL_PRIORITY))) {
        return false;
    }

    DSBUFFERDESC dsbd;
    ZeroMemory(&dsbd, sizeof(dsbd));
    dsbd.dwSize = sizeof(dsbd);
    dsbd.dwFlags = DSBCAPS_GLOBALFOCUS;
    dsbd.dwBufferBytes = BUFFER_SIZE;
    dsbd.lpwfxFormat = &wfx;

    if (FAILED(pDS->CreateSoundBuffer(&dsbd, &pDSBuffer, NULL))) {
        return false;
    }

    return true;
}

void CaptureAndPlay() {
    LPVOID readPtr1, readPtr2;
    DWORD readBytes1, readBytes2;

    LPVOID writePtr1, writePtr2;
    DWORD writeBytes1, writeBytes2;

    pDSCaptureBuffer->Start(DSCBSTART_LOOPING);

    while (true) {
        pDSCaptureBuffer->Lock(0, 0, &readPtr1, &readBytes1, &readPtr2, &readBytes2, DSCBLOCK_ENTIREBUFFER);

        pDSBuffer->Lock(0, 0, &writePtr1, &writeBytes1, &writePtr2, &writeBytes2, DSBLOCK_ENTIREBUFFER);

        memcpy(writePtr1, readPtr1, readBytes1);
        if (readPtr2) {
            memcpy((BYTE*)writePtr1 + readBytes1, readPtr2, readBytes2);
        }

        pDSBuffer->Unlock(writePtr1, writeBytes1, writePtr2, writeBytes2);

        pDSCaptureBuffer->Unlock(readPtr1, readBytes1, readPtr2, readBytes2);

        pDSBuffer->Play(0, 0, DSBPLAY_LOOPING);

        Sleep(10);  // Adjust sleep time as needed
    }

    pDSCaptureBuffer->Stop();
    pDSBuffer->Stop();
}


void updatePlaySynth()
{
    static DWORD lastOfs = 0;
    short tmp[512];
    for (int i = 0; i < 512; i++) {
        short v = -1024 + (i % 64) * 32;
        v *= 4;
        tmp[i] = v;
    }
    LPVOID writePtr1, writePtr2;
    DWORD writeBytes1, writeBytes2;
    DWORD sz = 512 * 2;
    HRESULT hr= pDSBuffer->Lock(lastOfs, sz, &writePtr1, &writeBytes1, &writePtr2, &writeBytes2, 0);

    printf("lastOfs:%d wp1:%p wb1:%d wp2:%p wb2:%d\n", lastOfs,writePtr1,writeBytes1,writePtr2,writeBytes2);
    memcpy(writePtr1, (void*)tmp, writeBytes1);
    if (writePtr2) {
        memcpy(writePtr2, (char*)(tmp) + writeBytes1, writeBytes2);

    }
    lastOfs += sz;
    lastOfs %= BUFFER_SIZE;
    pDSBuffer->Unlock(writePtr1, writeBytes1, writePtr2, writeBytes2);
    pDSBuffer->Play(0, 0, DSBPLAY_LOOPING);

}
void updatePlay()
{
    ensureSampleBuffers();
    if(g_playbuf->used<512 || g_playbuf->totalPushed<8192) {
        printf("skip loop!!\n");
        return;
    }
    static DWORD lastOfs = 0;
    short tmp[512];
    shiftSamples(g_playbuf, tmp, 512);
    LPVOID writePtr1, writePtr2;
    DWORD writeBytes1, writeBytes2;
    DWORD sz = 512 * 2;
    HRESULT hr = pDSBuffer->Lock(lastOfs, sz, &writePtr1, &writeBytes1, &writePtr2, &writeBytes2, 0);

    printf("lastOfs:%d wp1:%p wb1:%d wp2:%p wb2:%d  used:%d tot:%d\n", lastOfs, writePtr1, writeBytes1, writePtr2, writeBytes2, g_recbuf->used, g_recbuf->totalPushed);
    memcpy(writePtr1, (void*)tmp, writeBytes1);
    if (writePtr2) {
        memcpy(writePtr2, (char*)(tmp)+writeBytes1, writeBytes2);

    }
    lastOfs += sz;
    lastOfs %= BUFFER_SIZE;
    pDSBuffer->Unlock(writePtr1, writeBytes1, writePtr2, writeBytes2);
    pDSBuffer->Play(0, 0, DSBPLAY_LOOPING);

}

DWORD lastCapturePos = 0;  // 最後に取得したキャプチャ位置を保持する変数

void updateCapture()
{
    DWORD currentCapturePos;
    DWORD bytesToCapture;

    // 現在のキャプチャ位置を取得
    pDSCaptureBuffer->GetCurrentPosition(&currentCapturePos, NULL);

    if (currentCapturePos < lastCapturePos)
    {
        // バッファがラップアラウンドした場合
        bytesToCapture = (BUFFER_SIZE - lastCapturePos) + currentCapturePos;
    }
    else
    {
        // ラップアラウンドしなかった場合
        bytesToCapture = currentCapturePos - lastCapturePos;
    }

    if (bytesToCapture > 0)
    {
        void* ptr1;
        DWORD size1;
        void* ptr2;
        DWORD size2;

        pDSCaptureBuffer->Lock(lastCapturePos, bytesToCapture, &ptr1, &size1, &ptr2, &size2, 0);

        // 最初の部分のデータをコピー
        ensureSampleBuffers();
        pushSamples(g_recbuf, (short*)ptr1, size1 / 2);
        short s0 = ((short*)ptr1)[0];
        printf("s0:%d used:%d size1:%d\n", s0,getRecordedSampleCount(),size1);
        if (ptr2) {
            printf("push ptr2, bytes:%d\n", size2);
            pushSamples(g_recbuf, (short*)ptr2, size2 / 2);
        }

        
        pDSCaptureBuffer->Unlock(ptr1, size1, ptr2, size2);
    }

    lastCapturePos = currentCapturePos;
}

void updateCapture__()
{
    LPVOID readPtr1, readPtr2;
    DWORD readBytes1, readBytes2;

    pDSCaptureBuffer->Lock(0, 0, &readPtr1, &readBytes1, &readPtr2, &readBytes2, DSCBLOCK_ENTIREBUFFER);
    pDSCaptureBuffer->Unlock(readPtr1, readBytes1, readPtr2, readBytes2);

}
void copyToPlay()
{
    ensureSampleBuffers();
    const LONG64 ms=GetTickCount64();
    double dt = (double)(ms - g_recbuf->initialMs) / 1000.0;
    float avgPushPerSecond = (g_recbuf->totalPushed) / dt;
    printf("copytoplay: used:%d tot:%d avgPush:%f\n", g_recbuf->used, g_recbuf->totalPushed,avgPushPerSecond);
 #if 1
    const int N = 1024;
    if (g_recbuf->used >= N && getRoom(g_playbuf) >= N) {
        short samples[N];
        shiftSamples(g_recbuf, samples, N);
        pushSamples(g_playbuf, samples, N);
    }
 #endif

}
void CaptureAndPlay2() {
    pDSCaptureBuffer->Start(DSCBSTART_LOOPING);
    while (1) {
        updateCapture();
        copyToPlay();
        updatePlay();
        Sleep(10);  // Adjust sleep time as needed
    }

    pDSCaptureBuffer->Stop();
    pDSBuffer->Stop();
}

int main() {
    HWND hwnd = GetConsoleWindow();
    if (!InitializeDirectSound(hwnd)) {
        std::cout << "Failed to initialize DirectSound." << std::endl;
        return 1;
    }

    CaptureAndPlay2();

    return 0;
}
