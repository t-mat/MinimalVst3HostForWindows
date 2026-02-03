// A minimalist x64 VST 3.x host application for Windows 11, implemented in native C++20.
//
// clang-format off
//
// SPDX-FileCopyrightText: Copyright (c) Takayuki Matsuoka
// SPDX-License-Identifier: CC0-1.0
// Note: Copyright notice ensures CC0 fallback license validity where waiver is not applicable.
//
// clang-format on

#ifndef UNICODE
#define UNICODE 1
#endif
#ifndef _UNICODE // NOLINT(*-reserved-identifier)
#define _UNICODE 1
#endif
#ifndef _WIN32_WINNT        // NOLINT(*-reserved-identifier)
#define _WIN32_WINNT 0x0A00 // Windows 10
#endif
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN 1
#include <Audioclient.h>
#include <Windows.h>
#include <avrt.h>
#include <mmdeviceapi.h>

#if defined(_MSC_VER) // cl, clang-cl
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "avrt.lib")
#endif

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

// VST 3 SDK 3.8.x
#if defined(__clang__) && defined(_MSC_VER) // clang-cl
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpragma-pack"
#endif
#if defined(_MSC_VER) && !defined(__clang__) // cl
#pragma warning(push)
#pragma warning(disable : 28251)
#endif

#if !defined(DEVELOPMENT) && !defined(RELEASE) && !defined(_DEBUG) && !defined(NDEBUG)
#define NDEBUG 1
#endif

#define INIT_CLASS_IID
#include "base/source/fobject.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"

#if defined(_MSC_VER) && !defined(__clang__) // cl
#pragma warning(pop)
#endif
#if defined(__clang__) && defined(_MSC_VER) // clang-cl
#pragma clang diagnostic pop
#endif

const std::filesystem::path localVst3Dir  = L"./third_party/vst3plugins/";
const std::filesystem::path commonVst3Dir = L"C:/Program Files/Common Files/VST3/";

const std::vector<std::filesystem::path> global_pluginPaths = {
};

enum class Color : int { Normal = 0, Red = 91, Green = 92 };

// Logging function
void lpr(const Color c, const wchar_t *type, const char *file, const int line, const wchar_t *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fwprintf(stderr, L"\x1b[%dm%-5s: %hs(%d): ", static_cast<int>(c), type, file, line);
    vfwprintf(stderr, fmt, args);
    fwprintf(stderr, L"\x1b[0m"); // 0 = reset
    va_end(args);
}

#define MY_ERROR(...) lpr(Color::Red, L"ERROR", __FILE__, __LINE__, __VA_ARGS__)
#define MY_TRACE(...) lpr(Color::Green, L"TRACE", __FILE__, __LINE__, __VA_ARGS__)

// WASAPI Control Class
class Wasapi final {
  public:
    using RefillFunc = std::function<void(std::span<float> wasapiInterleavedBuf, unsigned nChannels, unsigned nSamples,
                                          double sampleRate)>;

    explicit Wasapi(const int hnsBufferDuration = 100000) { init(hnsBufferDuration); }

    ~Wasapi() { cleanup(); }

    [[nodiscard]] bool good() const { return initialized_; }
    [[nodiscard]] unsigned getBufferSize() const { return bufferSize_; }
    [[nodiscard]] unsigned getNumChannels() const { return pFormat_ ? pFormat_->nChannels : 2; }
    [[nodiscard]] double getSampleRate() const { return pFormat_ ? pFormat_->nSamplesPerSec : 0; }
    void setAudioThreadRefillCallback(const RefillFunc &refillFunc) { refillFunc_ = refillFunc; }
    // ReSharper disable once CppMemberFunctionMayBeConst
    void stop() {
        if (hCloseAudioThreadEvent) {
            SetEvent(hCloseAudioThreadEvent);
        }
    }

    // Called from the host's audio thread. Loops while waiting for WASAPI events or host termination requests. Writes
    // to the audio buffer when a WASAPI event is received.
    // ReSharper disable once CppMemberFunctionMayBeConst
    void audioThreadProc() {
        if (!initialized_) {
            return MY_ERROR(L"!initialized_\n");
        }
        const HANDLE events[] = {hRefillEvent_, hCloseAudioThreadEvent};
        const unsigned nChannels = getNumChannels();
        const auto combinedBuffer = std::make_unique<float[]>(bufferSize_ * nChannels);
        DWORD taskIndex = 0;
        HANDLE hTask = nullptr;
        HRESULT hrCoInit = E_UNEXPECTED;
        if (FAILED(hrCoInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            MY_ERROR(L"FAILED(0x%08x), CoInitializeEx\n", hrCoInit);
            goto end;
        }
        // Ask MMCSS to temporarily boost the thread priority to reduce glitches while the low-latency stream plays.
        if (hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex); !hTask) {
            MY_ERROR(L"hTask=%p, AvSetMmThreadCharacteristicsW\n", hTask);
            goto end;
        }
        // Start playing.
        if (HRESULT hr; FAILED(hr = audioClient_->Start())) {
            MY_ERROR(L"FAILED(0x%08x), audioClient_->Start()\n", hr);
            goto end;
        }
        // If hCloseAudioThreadEvent is signaled, WaitForMultipleObjects returns (WAIT_OBJECT_0 + 1).
        while (WaitForMultipleObjects(std::size(events), events, FALSE, INFINITE) == WAIT_OBJECT_0) {
            uint32_t pad = 0;
            if (HRESULT hr; FAILED(hr = audioClient_->GetCurrentPadding(&pad))) {
                MY_ERROR(L"FAILED(0x%08x), audioClient_->GetCurrentPadding()\n", hr);
                break;
            }
            const uint32_t nFrame = bufferSize_ - pad;
            float *o;
            if (HRESULT hr; FAILED(hr = audioRenderClient_->GetBuffer(nFrame, reinterpret_cast<BYTE **>(&o)))) {
                MY_ERROR(L"FAILED(0x%08x), audioRenderClient_->GetBuffer()\n", hr);
                break;
            }
            if (refillFunc_) {
                const std::span wasapiInterleavedBuf(o, nFrame * nChannels);
                refillFunc_(wasapiInterleavedBuf, nChannels, nFrame, pFormat_->nSamplesPerSec);
            } else {
                memset(o, 0, nFrame * nChannels * sizeof(*o));
            }
            if (HRESULT hr; FAILED(hr = audioRenderClient_->ReleaseBuffer(nFrame, 0))) {
                MY_ERROR(L"FAILED(0x%08x), audioRenderClient_->ReleaseBuffer()\n", hr);
                break;
            }
        }
    end:
        if (hTask) {
            AvRevertMmThreadCharacteristics(hTask);
        }
        if (HRESULT hr; FAILED(hr = audioClient_->Stop())) {
            MY_ERROR(L"FAILED(0x%08x), audioClient_->Stop()\n", hr);
        }
        if (SUCCEEDED(hrCoInit)) {
            CoUninitialize();
        }
    }

  private:
    void init(const int hnsBufferDuration) {
        hCloseAudioThreadEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        hRefillEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (HRESULT hr; FAILED(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                                     __uuidof(IMMDeviceEnumerator),
                                                     reinterpret_cast<void **>(&mmDeviceEnumerator_)))) {
            return MY_ERROR(L"FAILED(0x%08x), CoCreateInstance()\n", hr);
        }

        // eRener and eConsole are declared in mmdeviceapi.h
        if (HRESULT hr; FAILED(
                hr = mmDeviceEnumerator_->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eConsole, &mmDevice_))) {
            return MY_ERROR(L"FAILED(0x%08x), mmDeviceEnumerator_->GetDefaultAudioEndpoint()\n", hr);
        }

        if (HRESULT hr; FAILED(hr = mmDevice_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                                        reinterpret_cast<void **>(&audioClient_)))) {
            return MY_ERROR(L"FAILED(0x%08x), mmDevice_->Activate()\n", hr);
        }

        if (HRESULT hr; FAILED(hr = audioClient_->GetMixFormat(&pFormat_))) {
            return MY_ERROR(L"FAILED(0x%08x), audioClient_->GetMixFormat()\n", hr);
        }

        if (HRESULT hr;
            FAILED(hr = audioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                                 hnsBufferDuration, 0, pFormat_, nullptr))) {
            return MY_ERROR(L"FAILED(0x%08x), audioClient_->Initialize()\n", hr);
        }

        if (HRESULT hr; FAILED(hr = audioClient_->SetEventHandle(hRefillEvent_))) {
            return MY_ERROR(L"FAILED(0x%08x), audioClient_->SetEventHandle()\n", hr);
        }

        if (HRESULT hr; FAILED(hr = audioClient_->GetService(__uuidof(IAudioRenderClient),
                                                             reinterpret_cast<void **>(&audioRenderClient_)))) {
            return MY_ERROR(L"FAILED(0x%08x), audioClient_->GetService()\n", hr);
        }

        if (HRESULT hr; FAILED(hr = audioClient_->GetBufferSize(&bufferSize_))) {
            return MY_ERROR(L"FAILED(0x%08x), audioClient_->GetBufferSize()\n", hr);
        }
        initialized_ = true;
    }

    void cleanup() {
        stop();
        if (pFormat_) {
            CoTaskMemFree(std::exchange(pFormat_, nullptr));
        }
        if (audioRenderClient_) {
            std::exchange(audioRenderClient_, nullptr)->Release();
        }
        if (audioClient_) {
            std::exchange(audioClient_, nullptr)->Release();
        }
        if (mmDevice_) {
            std::exchange(mmDevice_, nullptr)->Release();
        }
        if (mmDeviceEnumerator_) {
            std::exchange(mmDeviceEnumerator_, nullptr)->Release();
        }
        if (hRefillEvent_) {
            CloseHandle(std::exchange(hRefillEvent_, nullptr));
        }
        if (hCloseAudioThreadEvent) {
            CloseHandle(std::exchange(hCloseAudioThreadEvent, nullptr));
        }
    }

    HANDLE hCloseAudioThreadEvent = nullptr;
    HANDLE hRefillEvent_ = nullptr;
    IMMDeviceEnumerator *mmDeviceEnumerator_ = nullptr;
    IMMDevice *mmDevice_ = nullptr;
    IAudioClient *audioClient_ = nullptr;
    IAudioRenderClient *audioRenderClient_ = nullptr;
    WAVEFORMATEX *pFormat_ = nullptr;
    uint32_t bufferSize_ = 0;
    bool initialized_ = false;
    RefillFunc refillFunc_{};
}; // class Wasapi

// Thread-safe SPSC (Single Producer Single Consumer) queue
template <class T, unsigned NumberOfElements> class SpscQueue final {
    static constexpr size_t Capacity = NumberOfElements + 1;
    static constexpr size_t FalseSharingSize = std::hardware_destructive_interference_size;
    static constexpr size_t AlignSize = std::max(alignof(T), FalseSharingSize);
    struct AlignedStorage {
        alignas(AlignSize) std::byte storage[sizeof(T)];
    };
    static constexpr unsigned next(const unsigned i) { return (i + 1) % Capacity; }

  public:
    SpscQueue() = default; // NOLINT(*-pro-type-member-init)
    SpscQueue(const SpscQueue &) = delete;
    SpscQueue &operator=(const SpscQueue &) = delete;
    ~SpscQueue() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            const unsigned end = writeIndex_.load(std::memory_order_relaxed);
            for (unsigned i = readIndex_.load(std::memory_order_relaxed); i != end; i = next(i)) {
                std::destroy_at(std::launder(reinterpret_cast<T *>(items_[i].storage)));
            }
        }
    }

    [[nodiscard]] bool push(const T &t) {
        const unsigned currentWriteIndex = writeIndex_.load(std::memory_order_relaxed);
        const unsigned nextWriteIndex = next(currentWriteIndex);
        // false : queue is full
        if (nextWriteIndex == readIndex_.load(std::memory_order_acquire)) [[unlikely]] {
            return false;
        }
        new (items_[currentWriteIndex].storage) T(t);
        writeIndex_.store(nextWriteIndex, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(T &item) {
        const unsigned currentReadIndex = readIndex_.load(std::memory_order_relaxed);
        // false : queue is empty
        if (currentReadIndex == writeIndex_.load(std::memory_order_acquire)) [[unlikely]] {
            return false;
        }
        T *p = std::launder(reinterpret_cast<T *>(items_[currentReadIndex].storage));
        item = std::move(*p);
        std::destroy_at(p);
        readIndex_.store(next(currentReadIndex), std::memory_order_release);
        return true;
    }

  private:
    AlignedStorage items_[Capacity];
    alignas(FalseSharingSize) std::atomic<unsigned> readIndex_ = 0;
    alignas(FalseSharingSize) std::atomic<unsigned> writeIndex_ = 0;
}; // class SpscQueue

// Host Interface
class MyHost : public Steinberg::Vst::IHostApplication {
  public:
    MyHost() = default;
    virtual ~MyHost() = default;

  private:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID tuid, void **obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(tuid, FUnknown::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(tuid, IHostApplication::iid)) {
            *obj = static_cast<IHostApplication *>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    Steinberg::tresult PLUGIN_API getName(Steinberg::Vst::String128 name) override {
        return _snwprintf_s(reinterpret_cast<wchar_t *>(name), 128, 128, L"Minimal VST3 Host") > 0
                   ? Steinberg::kResultTrue
                   : Steinberg::kInternalError;
    }

    Steinberg::tresult PLUGIN_API createInstance(Steinberg::TUID, Steinberg::TUID, void **) override {
        return Steinberg::kResultFalse;
    }

    uint32_t PLUGIN_API addRef() override { return 1; }
    uint32_t PLUGIN_API release() override { return 1; }
}; // class MyHost

// Component Handler Interface
class MyComponentHandler : public Steinberg::Vst::IComponentHandler {
  public:
    MyComponentHandler() = default;
    virtual ~MyComponentHandler() = default;

  private:
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(int32_t) override { return Steinberg::kResultOk; }

    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) override {
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID tuid, void **obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(tuid, IComponentHandler::iid)) {
            *obj = static_cast<IComponentHandler *>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    uint32_t PLUGIN_API addRef() override { return 1; }
    uint32_t PLUGIN_API release() override { return 1; }
}; // class MyComponentHandler

// Plugin GUI Frame Interface
class MyPlugFrame : public Steinberg::IPlugFrame {
  public:
    MyPlugFrame() = default;
    virtual ~MyPlugFrame() = default;
    std::function<Steinberg::tresult(Steinberg::IPlugView *view, Steinberg::ViewRect *newSize)> resizeViewCallback_;

  private:
    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView *view, Steinberg::ViewRect *newSize) override {
        return resizeViewCallback_ ? resizeViewCallback_(view, newSize) : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID tuid, void **obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(tuid, IPlugFrame::iid)) {
            *obj = static_cast<IPlugFrame *>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    uint32_t PLUGIN_API addRef() override { return 1; }
    uint32_t PLUGIN_API release() override { return 1; }
}; // class MyPlugFrame

// Wrapper for the Plugin DLL
class Vst3Dll final {
  public:
    Vst3Dll() = default;
    Vst3Dll(const Vst3Dll &) = delete;
    Vst3Dll &operator=(const Vst3Dll &) = delete;
    ~Vst3Dll() { free(); }

    Steinberg::IPluginFactory *load(const std::filesystem::path &dllPath) {
        free();
        if (hModule_ = LoadLibraryW(dllPath.c_str()); !hModule_) {
            MY_ERROR(L"LoadLibraryW(%s)\n", dllPath.c_str());
            return nullptr;
        } else if (const auto p = GetProcAddress(hModule_, "GetPluginFactory"); !p) {
            MY_ERROR(L"GetProcAddress('GetPluginFactory'), %s\n", dllPath.c_str());
            return nullptr;
        } else {
            using GetPluginFactoryProc = Steinberg::IPluginFactory *(PLUGIN_API *)();
            const auto getPluginFactory = reinterpret_cast<GetPluginFactoryProc>(p);
            return getPluginFactory();
        }
    }

  private:
    void free() {
        if (hModule_) {
            FreeLibrary(std::exchange(hModule_, nullptr));
        }
    }
    HMODULE hModule_ = nullptr;
}; // class Vst3Dll

// Class that holds the plugin and manages audio processing and GUI
class Vst3Plugin final {
  public:
    using EventQueue = SpscQueue<Steinberg::Vst::Event, 4096>;
    EventQueue &getEventQueue() { return eventQueue_; }
    [[nodiscard]] bool hasEventOutput() const { return hasEventOutput_; }
    [[nodiscard]] bool good() const { return initialized_; }
    [[nodiscard]] bool isEffect() const { return isEffect_; }

    Vst3Plugin(const unsigned index, const std::filesystem::path &pluginPath,
               Steinberg::Vst::IHostApplication *hostApplication, const int bufferSize, const double sampleRate) {
        init(index, pluginPath, hostApplication, bufferSize, sampleRate);
    }

    ~Vst3Plugin() { cleanup(); }

    // Callback when the plugin side requests a GUI resize
    Steinberg::tresult resizeView(const Steinberg::ViewRect *newSize) const {
        HWND const hWnd = hWnd_;
        if (!hWnd) {
            return Steinberg::kResultFalse;
        }
        RECT rc = {0, 0, newSize->right - newSize->left, newSize->bottom - newSize->top};
        const auto style = static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_STYLE));
        const auto exStyle = static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
        AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, GetDpiForWindow(hWnd));
        constexpr auto uFlags = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;
        SetWindowPos(hWnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, uFlags);
        return Steinberg::kResultOk;
    }

    void audioThreadVstRefill(                      //
        const std::span<float *> vstInChannelPtrs,  //
        const std::span<float *> vstOutChannelPtrs, //
        const unsigned nSamples,                    //
        const double sampleRate,                    //
        const double tempo,                         //
        Steinberg::Vst::IEventList *inputEvents,    //
        Steinberg::Vst::IEventList *outputEvents,   //
        const double ppqPosition                    //
    ) const {
        // Input Bus Setup
        Steinberg::Vst::AudioBusBuffers inBus = {};
        inBus.numChannels = isEffect_ ? static_cast<int32_t>(vstInChannelPtrs.size()) : 0;
        inBus.channelBuffers32 = isEffect_ ? std::data(vstInChannelPtrs) : nullptr;

        // Output Bus Setup
        Steinberg::Vst::AudioBusBuffers outBus = {};
        outBus.numChannels = static_cast<int32_t>(vstOutChannelPtrs.size());
        outBus.channelBuffers32 = std::data(vstOutChannelPtrs);

        Steinberg::Vst::ProcessContext context = {
            .state = Steinberg::Vst::ProcessContext::kPlaying | Steinberg::Vst::ProcessContext::kTempoValid |
                     Steinberg::Vst::ProcessContext::kProjectTimeMusicValid,
            .sampleRate = sampleRate,
            .projectTimeMusic = ppqPosition,
            .tempo = tempo,
        };

        Steinberg::Vst::ProcessData vstProcessData = {};
        vstProcessData.processMode = Steinberg::Vst::kRealtime;
        vstProcessData.symbolicSampleSize = Steinberg::Vst::kSample32;
        vstProcessData.numInputs = inBus.numChannels > 0 ? 1 : 0;
        vstProcessData.inputs = inBus.numChannels > 0 ? &inBus : nullptr;
        vstProcessData.numOutputs = 1;
        vstProcessData.outputs = &outBus;
        vstProcessData.inputEvents = inputEvents;
        vstProcessData.outputEvents = outputEvents;
        vstProcessData.processContext = &context;
        vstProcessData.numSamples = static_cast<int>(nSamples);
        vstAudioProcessor_->process(vstProcessData);
    }

  private:
    // Check if pointers refer to the same object (retrieve and compare IUnknown pointers)
    static bool isSameObject(const auto &p0, const auto &p1) {
        Steinberg::IPtr<Steinberg::FUnknown> u0;
        Steinberg::IPtr<Steinberg::FUnknown> u1;
        p0->queryInterface(Steinberg::FUnknown::iid, reinterpret_cast<void **>(&u0));
        p1->queryInterface(Steinberg::FUnknown::iid, reinterpret_cast<void **>(&u1));
        return u0 == u1;
    }

    void init(const unsigned index, const std::filesystem::path &pluginPath,
              Steinberg::Vst::IHostApplication *hostApplication, const int bufferSize, const double samplesPerSec) {
        pluginIndex_ = index;
        vst3DllPath_ = pluginPath;

        // The sequence for initialization and setup is complex.
        // Refer to the left side (downward arrows) of: Audio Processor Call Sequence
        // https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Workflow+Diagrams/Audio+Processor+Call+Sequence.html
        {
            auto *const pluginFactory = vst3Dll_.load(pluginPath);
            if (!pluginFactory) {
                return MY_ERROR(L"pluginPath=%s, vst3Dll_.load()\n", pluginPath.c_str());
            }

            // Create Component (Audio Engine / Processor)
            for (int iClass = 0, nClass = pluginFactory->countClasses(); iClass < nClass; ++iClass) {
                Steinberg::PClassInfo c;
                pluginFactory->getClassInfo(iClass, &c);
                if (strcmp(c.category, kVstAudioEffectClass) == 0) {
                    std::string str(c.name);
                    name_ = std::wstring(str.begin(), str.end());
                    pluginFactory->createInstance(c.cid, Steinberg::Vst::IComponent::iid,
                                                  reinterpret_cast<void **>(&vstComponent_));
                    break;
                }
            }
            if (!vstComponent_) {
                return MY_ERROR(L"pluginPath=%s, vstComponent_ == %p\n", pluginPath.c_str(), vstComponent_.get());
            }

            // Initialize Component. IComponent::initialize must be called first
            vstComponent_->initialize(hostApplication);
            isEffect_ = vstComponent_->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput) > 0;
            hasEventOutput_ = vstComponent_->getBusCount(Steinberg::Vst::kEvent, Steinberg::Vst::kOutput) > 0;

            // Create GUI Controller (Edit Controller)
            if (Steinberg::TUID id; vstComponent_->getControllerClassId(id) == Steinberg::kResultOk) {
                pluginFactory->createInstance(id, Steinberg::Vst::IEditController::iid,
                                              reinterpret_cast<void **>(&vstEditController_));
            }
            if (!vstEditController_) {
                // If the controller isn't created yet, check if the component itself
                // implements it
                vstComponent_->queryInterface(Steinberg::Vst::IEditController::iid,
                                              reinterpret_cast<void **>(&vstEditController_));
            }
            if (!vstEditController_) {
                return MY_ERROR(L"pluginPath=%s, vstEditController_=%p\n", pluginPath.c_str(),
                                vstEditController_.get());
            }
            vstEditController_->initialize(hostApplication);
            vstEditController_->setComponentHandler(&myComponentHandler_);

            // Connection for parameter synchronization between the processing side and UI side Connection is not needed
            // if they are the same object (Single Component)
            if (!isSameObject(vstComponent_, vstEditController_)) {
                Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> cp1;
                vstComponent_->queryInterface(Steinberg::Vst::IConnectionPoint::iid, reinterpret_cast<void **>(&cp1));
                if (!cp1) {
                    return MY_ERROR(L"pluginPath=%s, cp1=%p\n", pluginPath.c_str(), cp1.get());
                }

                Steinberg::IPtr<Steinberg::Vst::IConnectionPoint> cp2;
                vstEditController_->queryInterface(Steinberg::Vst::IConnectionPoint::iid,
                                                   reinterpret_cast<void **>(&cp2));
                if (!cp2) {
                    return MY_ERROR(L"pluginPath=%s, cp2=%p\n", pluginPath.c_str(), cp2.get());
                }

                cp1->connect(cp2);
                cp2->connect(cp1);
            }
        }

        vstComponent_->queryInterface(Steinberg::Vst::IAudioProcessor::iid,
                                      reinterpret_cast<void **>(&vstAudioProcessor_));
        if (!vstAudioProcessor_) {
            return MY_ERROR(L"pluginPath=%s, vstComponent_->queryInterface()\n", pluginPath.c_str());
        }

        {
            constexpr Steinberg::Vst::SpeakerArrangement speakerArr = Steinberg::Vst::SpeakerArr::kStereo;
            Steinberg::Vst::SpeakerArrangement speakerIn = isEffect_ ? speakerArr : 0;
            Steinberg::Vst::SpeakerArrangement speakerOut = speakerArr;
            vstAudioProcessor_->setBusArrangements(&speakerIn, speakerIn != 0, &speakerOut, speakerOut != 0);
            Steinberg::Vst::ProcessSetup setup = {Steinberg::Vst::kRealtime, Steinberg::Vst::kSample32, bufferSize,
                                                  samplesPerSec};
            vstAudioProcessor_->setupProcessing(setup);
        }

        // Activate Buses
        vstComponent_->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, 0, true);
        vstComponent_->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);
        vstComponent_->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kInput, 0, true);
        vstComponent_->activateBus(Steinberg::Vst::kEvent, Steinberg::Vst::kOutput, 0, true);
        vstComponent_->setActive(true);
        vstAudioProcessor_->setProcessing(true);

        // Create Editor Window
        plugView_ = vstEditController_->createView(Steinberg::Vst::ViewType::kEditor);
        if (!plugView_) {
            return MY_ERROR(L"pluginPath=%s, vstEditController_->createView()\n", pluginPath.c_str());
        }
        plugView_->setFrame(&myPlugFrame_);

        myPlugFrame_.resizeViewCallback_ = [&](auto *, const Steinberg::ViewRect *vr) { return resizeView(vr); };

        {
            const WNDCLASSW wc = {
                .lpfnWndProc = s_wndProc,
                .hInstance = GetModuleHandle(nullptr),
                .hCursor = LoadCursor(nullptr, IDC_ARROW),
                .lpszClassName = L"MinimalVST3HostWindow",
            };
            RegisterClassW(&wc);

            Steinberg::ViewRect viewRect;
            plugView_->getSize(&viewRect);

            RECT rc = {0, 0, viewRect.right - viewRect.left, viewRect.bottom - viewRect.top};
            constexpr DWORD style = WS_OVERLAPPEDWINDOW;
            AdjustWindowRectExForDpi(&rc, style, FALSE, 0, GetDpiForSystem());

            const std::wstring caption = std::wstring(L"[#") + std::to_wstring(pluginIndex_) + L"] " + name_;

            hWnd_ =
                CreateWindowExW(0, wc.lpszClassName, caption.c_str(), style | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, this);
        }

        if (plugView_->attached(hWnd_, Steinberg::kPlatformTypeHWND) != Steinberg::kResultOk) {
            return MY_ERROR(L"pluginPath=%s, plugView_->attached()\n", pluginPath.c_str());
        }

        initialized_ = true;
        MY_TRACE(L"\"%s\" (%s) is loaded from \"%s\"\n", name_.c_str(), isEffect() ? L"effect" : L"instrument",
                 pluginPath.c_str());
    }

    void cleanup() const {
        // Regarding release order, refer to the right side (upward arrows) of:
        // Audio Processor Call Sequence
        // https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Workflow+Diagrams/Audio+Processor+Call+Sequence.html
        if (plugView_) {
            plugView_->removed();
        }
        if (vstAudioProcessor_) {
            vstAudioProcessor_->setProcessing(false);
        }
        if (vstComponent_) {
            vstComponent_->setActive(false);
        }
        if (vstEditController_) {
            vstEditController_->terminate();
        }
        if (vstComponent_) {
            vstComponent_->terminate();
        }
        if (hWnd_) {
            DestroyWindow(hWnd_);
        }
    }

    void keyScan() {
        if (GetKeyState(VK_ESCAPE) & 0x8000) {
            PostQuitMessage(0);
        }
        for (auto &[vk, key] : keyMap) {
            if (const bool newStatus = (GetKeyState(vk) & 0x8000) != 0; key.status_ != newStatus) {
                key.status_ = newStatus;
                Steinberg::Vst::Event e = {
                    .busIndex = 0,
                    .sampleOffset = 0,
                    .ppqPosition = 0,
                    .flags = Steinberg::Vst::Event::kIsLive,
                };
                if (key.status_) {
                    e.type = Steinberg::Vst::Event::kNoteOnEvent;
                    e.noteOn.channel = 0;
                    e.noteOn.pitch = key.midiNote_;
                    e.noteOn.velocity = 1.0f;
                    e.noteOn.noteId = key.midiNote_;
                } else {
                    e.type = Steinberg::Vst::Event::kNoteOffEvent;
                    e.noteOff.channel = 0;
                    e.noteOff.pitch = key.midiNote_;
                    e.noteOff.velocity = 0.0f;
                    e.noteOff.noteId = key.midiNote_;
                }
                MY_TRACE(L"Note %-3s:  %3d\n", key.status_ ? L"On" : L"Off", key.midiNote_);
                if (!eventQueue_.push(e)) {
                    MY_ERROR(L"  eventQueue_ is full\n");
                }
            }
        }
    }

    // ReSharper disable once CppParameterMayBeConst
    static LRESULT CALLBACK s_wndProc(HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) {
        if (msg == WM_NCCREATE) {
            if (const auto cs = reinterpret_cast<CREATESTRUCT *>(lParam)) {
                const auto lp = reinterpret_cast<LONG_PTR>(cs->lpCreateParams);
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, lp);
            }
        }
        auto *p = reinterpret_cast<Vst3Plugin *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        return p ? p->wndProc(hWnd, msg, wParam, lParam) : DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    // ReSharper disable once CppParameterMayBeConst
    LRESULT wndProc(HWND hWnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) {
        switch (msg) {
        case WM_SIZE:
            if (Steinberg::IPlugView *plugView = plugView_.get()) {
                RECT rc;
                GetClientRect(hWnd, &rc);
                Steinberg::ViewRect viewRect = {0, 0, static_cast<int>(rc.right - rc.left),
                                                static_cast<int>(rc.bottom - rc.top)};
                plugView->onSize(&viewRect);
            }
            break;
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP:
            keyScan();
            break;
        default:
            break;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    struct Key {
        const int16_t midiNote_{};
        bool status_{false};
    };

    EventQueue eventQueue_;
    Steinberg::IPtr<Steinberg::Vst::IComponent> vstComponent_;
    Steinberg::IPtr<Steinberg::Vst::IEditController> vstEditController_;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> vstAudioProcessor_;
    Steinberg::IPtr<Steinberg::IPlugView> plugView_;
    MyComponentHandler myComponentHandler_;
    Vst3Dll vst3Dll_;
    HWND hWnd_ = nullptr;
    // clang-format off
    std::map<int, Key> keyMap{
            {'2',{61}},{'3',{63}},           {'5',{66}},{'6',{68}},{'7',{70}},
        {'Q',{60}},{'W',{62}},{'E',{64}},{'R',{65}},{'T',{67}},{'Y',{69}},{'U',{71}}, {'I',{72}},

            {'S',{49}},{'D',{51}},           {'G',{54}},{'H',{56}},{'J',{58}},
        {'Z',{48}},{'X',{50}},{'C',{52}},{'V',{53}},{'B',{55}},{'N',{57}},{'M',{59}}, {VK_OEM_COMMA,{60}},
    };
    // clang-format on
    std::filesystem::path vst3DllPath_;
    std::wstring name_;
    MyPlugFrame myPlugFrame_;
    unsigned pluginIndex_ = 0;
    bool isEffect_ = false;
    bool hasEventOutput_ = false;
    bool initialized_ = false;
}; // class Vst3Plugin

// Simple event list for passing events within AppMain::audioThreadAppRefill
class MySimpleEventList : public Steinberg::Vst::IEventList {
  public:
    MySimpleEventList() = default;
    virtual ~MySimpleEventList() = default;
    void clear() { eventCount_ = 0; }

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event &e) override {
        if (eventCount_ >= MaxEvents) {
            return Steinberg::kResultFalse;
        }
        events_[eventCount_++] = e;
        return Steinberg::kResultOk;
    }

  private:
    int32_t PLUGIN_API getEventCount() override { return eventCount_; }

    Steinberg::tresult PLUGIN_API getEvent(const Steinberg::int32 index, Steinberg::Vst::Event &e) override {
        if (index < 0 || index >= eventCount_) {
            return Steinberg::kResultFalse;
        }
        e = events_[index];
        return Steinberg::kResultOk;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID tuid, void **obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(tuid, Steinberg::Vst::IEventList::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(tuid, FUnknown::iid)) {
            *obj = this;
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    uint32_t PLUGIN_API addRef() override { return 1; }
    uint32_t PLUGIN_API release() override { return 1; }

    static constexpr int32_t MaxEvents = 1024;
    int32_t eventCount_ = 0;
    std::array<Steinberg::Vst::Event, MaxEvents> events_ = {};
}; // class MySimpleEventList

// Main Application
class AppMain final {
  public:
    AppMain() = default;
    ~AppMain() = default;

    int mainLoop() {
        Wasapi wasapi{};
        if (!wasapi.good()) {
            MY_ERROR(L"! wasapi.good()\n");
            return EXIT_FAILURE;
        }

        for (const auto &pluginPath : global_pluginPaths) {
            if (auto p = std::make_unique<Vst3Plugin>(static_cast<unsigned>(vst3Plugins_.size()),
                                                      std::filesystem::absolute(pluginPath), &myHost_,
                                                      wasapi.getBufferSize(), wasapi.getSampleRate());
                p->good()) {
                vst3Plugins_.push_back(std::move(p));
            }
        }
        if (vst3Plugins_.empty()) {
            MY_ERROR(L"vst3Plugins_.empty()\n");
            return EXIT_FAILURE;
        }

        tempo_ = 120.0;
        currentPpq_ = 0.0;
        maxSamples_ = wasapi.getBufferSize();
        maxChannels_ = wasapi.getNumChannels();
        inpPtrs_.resize(maxChannels_);
        outPtrs_.resize(maxChannels_);
        pingPongAudioBuffers_[0].resize(maxSamples_ * maxChannels_);
        pingPongAudioBuffers_[1].resize(maxSamples_ * maxChannels_);

        // Callback from the audio thread during WASAPI updates. Calls the process methods of each plugin.
        wasapi.setAudioThreadRefillCallback([&](const std::span<float> wasapiInterleavedBuf, const unsigned nChannels,
                                                const unsigned nSamples, const double sampleRate) {
            return audioThreadAppRefill(wasapiInterleavedBuf, nChannels, nSamples, sampleRate);
        });

        {
            // audioThread handles WASAPI updates. Triggers audioThreadRefill via the refill callback above.
            std::thread audioThread([&] { wasapi.audioThreadProc(); });
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            // wasapi.audioThreadProc() also terminates within wasapi.stop()
            wasapi.stop();
            audioThread.join();
        }
        return EXIT_SUCCESS;
    }

  private:
    double tempo_ = 120.0;
    double currentPpq_ = 0.0;
    unsigned maxSamples_ = 0;
    unsigned maxChannels_ = 0;
    MyHost myHost_;
    std::vector<std::unique_ptr<Vst3Plugin>> vst3Plugins_;
    std::array<std::vector<float>, 2> pingPongAudioBuffers_;
    std::vector<float *> inpPtrs_;
    std::vector<float *> outPtrs_;
    std::array<MySimpleEventList, 2> pingPongEventLists_;

    void audioThreadAppRefill(const std::span<float> wasapiInterleavedBuf, const unsigned nChannels,
                              const unsigned nSamples, const double sampleRate) {
        pingPongEventLists_[0].clear();
        pingPongEventLists_[1].clear();
        MySimpleEventList *inpEvents = &pingPongEventLists_[0];
        MySimpleEventList *outEvents = &pingPongEventLists_[1];

        // Retrieve events from UI
        for (const std::unique_ptr<Vst3Plugin> &vst3Plugin : vst3Plugins_) {
            Steinberg::Vst::Event e = {};
            while (vst3Plugin->getEventQueue().pop(e)) {
                inpEvents->addEvent(e);
            }
        }

        // Prepare two sets of I/O buffers
        float *inpPtr = pingPongAudioBuffers_[0].data();
        float *outPtr = pingPongAudioBuffers_[1].data();

        // Zero-clear the initial input buffer
        const unsigned bufSize = nSamples * nChannels;
        memset(inpPtr, 0, sizeof(inpPtr[0]) * bufSize);

        // Process plugins in series
        for (const std::unique_ptr<Vst3Plugin> &vst3Plugin : vst3Plugins_) {
            // Set I/O buffer addresses for each channel. inpPtr points to the output of the previous plugin.
            for (unsigned iChannel = 0; iChannel < nChannels; ++iChannel) {
                inpPtrs_[iChannel] = inpPtr + iChannel * nSamples;
                outPtrs_[iChannel] = outPtr + iChannel * nSamples;
            }

            vst3Plugin->audioThreadVstRefill(std::span(inpPtrs_), std::span(outPtrs_), nSamples, sampleRate, tempo_,
                                             inpEvents, outEvents, currentPpq_);

            // If the plugin outputs events, swap the event lists
            if (vst3Plugin->hasEventOutput()) {
                // Clear the processed event list
                inpEvents->clear();
                // Swap event lists
                std::swap(inpEvents, outEvents);
                // At this point, inpEvents contains the event output from the previous plugin
            }

            // If the plugin is not an effect (e.g., an instrument), add its output to the input (summing)
            if (!vst3Plugin->isEffect()) {
                for (unsigned i = 0; i < bufSize; ++i) {
                    outPtr[i] += inpPtr[i];
                }
            }

            // Buffer swapping
            std::swap(inpPtr, outPtr);
            // Now inpPtr points to the output of the plugin just processed
        }

        // Write the final result into the WASAPI interleaved buffer
        for (unsigned iSample = 0; iSample < nSamples; ++iSample) {
            for (unsigned iChannel = 0; iChannel < nChannels; ++iChannel) {
                wasapiInterleavedBuf[iSample * nChannels + iChannel] = inpPtr[iChannel * nSamples + iSample];
            }
        }

        // PPQ per second is (tempo / 60). PPQ per sample is that multiplied by (1 / sampleRate).
        currentPpq_ += nSamples * tempo_ / 60.0 / sampleRate;
    }
}; // class AppMain

int main() {
    MY_TRACE(L"Start\n");
    int result = EXIT_FAILURE;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (HRESULT hr; FAILED(hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        MY_ERROR(L"FAILED(0x%08x), CoInitializeEx()", hr);
    } else {
        try {
            AppMain appMain;
            result = appMain.mainLoop();
        } catch (std::exception &e) {
            printf("Exception: %s\n", e.what());
        }
        CoUninitialize();
    }
    MY_TRACE(L"End\n");
    return result;
}
