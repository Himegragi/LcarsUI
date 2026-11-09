#pragma once
#include <windows.h>
#include <msctf.h>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
struct Blocked : std::runtime_error { using std::runtime_error::runtime_error; };
GUID guid(unsigned long a, unsigned short b, unsigned short c, std::initializer_list<unsigned char> d) {
    GUID g{a, b, c, {}};
    std::copy(d.begin(), d.end(), g.Data4);
    return g;
}
// For this process only: Microsoft Bopomofo, then back to the US keyboard.
struct Profiles {
    ITfInputProcessorProfileMgr* manager = nullptr;
    const CLSID bopomofo = guid(0xB115690A, 0xEA02, 0x48D5, {0xA2, 0x31, 0xE3, 0x57, 0x8D, 0x2F, 0xDF, 0x80});
    const GUID profile = guid(0xB2F9C502, 0x1742, 0x11D4, {0x97, 0x90, 0x00, 0x80, 0xC8, 0x82, 0x68, 0x7E});
    Profiles() {
        if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr,
                                    reinterpret_cast<void**>(&manager))))
            throw Blocked("TSF profile manager unavailable");
    }
    ~Profiles() { us(); if (manager) manager->Release(); }
    bool chinese() {
        return SUCCEEDED(manager->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0404, bopomofo, profile, nullptr,
                                                  TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE));
    }
    void us() {
        manager->ActivateProfile(TF_PROFILETYPE_KEYBOARDLAYOUT, 0x0409, CLSID_NULL, GUID_NULL,
                                 reinterpret_cast<HKL>(static_cast<std::uintptr_t>(0x04090409)), TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
    }
    std::string active() {
        TF_INPUTPROCESSORPROFILE p{};
        if (FAILED(manager->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &p))) return "unknown";
        bool is_bopomofo = IsEqualGUID(p.clsid, bopomofo) && IsEqualGUID(p.guidProfile, profile);
        return "langid=0x" + [&] { char b[8]; std::snprintf(b, sizeof b, "%04x", p.langid); return std::string(b); }() +
               (is_bopomofo ? " Microsoft Bopomofo TIP" : p.dwProfileType == TF_PROFILETYPE_KEYBOARDLAYOUT ? " keyboard layout" : " other TIP");
    }
};